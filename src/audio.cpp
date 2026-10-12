#include "audio.h"
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <mmreg.h>
// initguid.h must come right before the property-key header so PKEY_* are defined here.
#include <initguid.h>
#include <functiondiscoverykeys_devpkey.h>

std::vector<AudioDevice> ListAudioDevices(bool capture) {
    std::vector<AudioDevice> out;
    ComPtr<IMMDeviceEnumerator> en;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&en)))) return out;
    ComPtr<IMMDeviceCollection> col;
    if (FAILED(en->EnumAudioEndpoints(capture ? eCapture : eRender, DEVICE_STATE_ACTIVE, &col))) return out;
    UINT n = 0;
    col->GetCount(&n);
    for (UINT i = 0; i < n; ++i) {
        ComPtr<IMMDevice> dev;
        if (FAILED(col->Item(i, &dev))) continue;
        LPWSTR id = nullptr;
        dev->GetId(&id);
        AudioDevice d;
        d.id = id ? id : L"";
        CoTaskMemFree(id);
        ComPtr<IPropertyStore> props;
        if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &props))) {
            PROPVARIANT v;
            PropVariantInit(&v);
            if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &v)) && v.vt == VT_LPWSTR) d.name = v.pwszVal;
            PropVariantClear(&v);
        }
        out.push_back(d);
    }
    return out;
}

bool AudioPipe::Open(const std::wstring& deviceId, bool loopback, float gain, std::wstring& err) {
    static std::atomic<int> counter{0};
    deviceId_ = deviceId;
    loopback_ = loopback;
    gain_ = gain;
    pipePath_ = Fmt(L"\\\\.\\pipe\\ScreenRec_%lu_%d", GetCurrentProcessId(), ++counter);
    pipe_ = CreateNamedPipeW(pipePath_.c_str(), PIPE_ACCESS_OUTBOUND, PIPE_TYPE_BYTE | PIPE_WAIT, 1, 1 << 20, 1 << 20, 0,
                             nullptr);
    if (pipe_ == INVALID_HANDLE_VALUE) {
        err = L"Could not create the audio pipe.";
        return false;
    }
    ready_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    stop_ = false;
    thread_ = std::thread([this] { Run(); });
    // The worker initialises WASAPI (in its own MTA apartment) and reports the stream format.
    if (WaitForSingleObject(ready_, 5000) != WAIT_OBJECT_0 || !initOk_) {
        err = initError_.empty() ? L"Audio device did not respond." : initError_;
        Stop();
        return false;
    }
    return true;
}

void AudioPipe::Stop() {
    stop_ = true;
    if (thread_.joinable()) {
        if (!connected_ && pipe_ != INVALID_HANDLE_VALUE) {
            // Unblock ConnectNamedPipe by connecting to ourselves.
            HANDLE h = CreateFileW(pipePath_.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
            if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
        }
        thread_.join();
    }
    if (pipe_ != INVALID_HANDLE_VALUE) {
        DisconnectNamedPipe(pipe_);
        CloseHandle(pipe_);
        pipe_ = INVALID_HANDLE_VALUE;
    }
    if (ready_) {
        CloseHandle(ready_);
        ready_ = nullptr;
    }
    connected_ = false;
}

void AudioPipe::Run() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ComPtr<IAudioClient> client;
    ComPtr<IAudioCaptureClient> capture;
    WAVEFORMATEX* wfx = nullptr;
    bool isFloat = false;
    int bits = 16;

    auto fail = [&](const wchar_t* msg) {
        initError_ = msg;
        initOk_ = false;
        SetEvent(ready_);
    };

    {
        ComPtr<IMMDeviceEnumerator> en;
        ComPtr<IMMDevice> dev;
        if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&en)))) {
            fail(L"Audio system unavailable.");
            CoUninitialize();
            return;
        }
        HRESULT hr = deviceId_.empty() ? en->GetDefaultAudioEndpoint(loopback_ ? eRender : eCapture, eConsole, &dev)
                                       : en->GetDevice(deviceId_.c_str(), &dev);
        if (FAILED(hr) && !deviceId_.empty())   // saved device unplugged -> fall back to default
            hr = en->GetDefaultAudioEndpoint(loopback_ ? eRender : eCapture, eConsole, &dev);
        if (FAILED(hr) || FAILED(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &client)) ||
            FAILED(client->GetMixFormat(&wfx))) {
            fail(loopback_ ? L"No speaker device found for system audio." : L"No microphone found.");
            CoUninitialize();
            return;
        }
        if (wfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) isFloat = true;
        if (wfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
            isFloat = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(wfx)->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
        bits = wfx->wBitsPerSample;
        rate_ = (int)wfx->nSamplesPerSec;
        channels_ = wfx->nChannels;
        hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, loopback_ ? AUDCLNT_STREAMFLAGS_LOOPBACK : 0, 2000000, 0, wfx,
                                nullptr);
        if (FAILED(hr) || FAILED(client->GetService(IID_PPV_ARGS(&capture)))) {
            CoTaskMemFree(wfx);
            fail(L"Could not open the audio device.");
            CoUninitialize();
            return;
        }
        CoTaskMemFree(wfx);
    }
    initOk_ = true;
    SetEvent(ready_);

    // Wait for ffmpeg to open the pipe.
    BOOL ok = ConnectNamedPipe(pipe_, nullptr) || GetLastError() == ERROR_PIPE_CONNECTED;
    connected_ = true;
    if (!ok || stop_) {
        CoUninitialize();
        return;
    }

    const int frameBytes = (bits / 8) * channels_;
    std::vector<float> buf;
    auto write = [&](const float* data, size_t frames) -> bool {
        const char* p = reinterpret_cast<const char*>(data);
        size_t left = frames * channels_ * sizeof(float);
        while (left) {
            DWORD n = 0;
            if (!WriteFile(pipe_, p, (DWORD)std::min<size_t>(left, 1 << 16), &n, nullptr)) return false;
            p += n;
            left -= n;
        }
        return true;
    };

    LARGE_INTEGER freq, t0, now;
    QueryPerformanceFrequency(&freq);
    client->Start();
    QueryPerformanceCounter(&t0);
    uint64_t written = 0;

    while (!stop_) {
        Sleep(10);
        bool got = false;
        UINT32 packet = 0;
        while (SUCCEEDED(capture->GetNextPacketSize(&packet)) && packet) {
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            if (FAILED(capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) break;
            buf.assign((size_t)frames * channels_, 0.f);
            if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT)) {
                size_t n = (size_t)frames * channels_;
                if (isFloat) {
                    memcpy(buf.data(), data, n * sizeof(float));
                } else if (bits == 16) {
                    auto* s = reinterpret_cast<int16_t*>(data);
                    for (size_t i = 0; i < n; ++i) buf[i] = s[i] / 32768.f;
                } else if (bits == 32) {
                    auto* s = reinterpret_cast<int32_t*>(data);
                    for (size_t i = 0; i < n; ++i) buf[i] = s[i] / 2147483648.f;
                } else if (bits == 24) {
                    for (size_t i = 0; i < n; ++i) {
                        const BYTE* b = data + i * 3;
                        int32_t v = (b[0] << 8) | (b[1] << 16) | (b[2] << 24);
                        buf[i] = v / 2147483648.f;
                    }
                }
                if (gain_ != 1.f)
                    for (auto& f : buf) f = std::clamp(f * gain_, -1.f, 1.f);
            }
            capture->ReleaseBuffer(frames);
            float peak = 0;
            for (float v : buf) peak = std::max(peak, std::fabs(v));
            level_ = std::max(peak, level_.load() * 0.9f);
            (void)frameBytes;
            if (!write(buf.data(), frames)) {
                stop_ = true;
                break;
            }
            written += frames;
            got = true;
        }
        if (!got) level_ = level_.load() * 0.8f;
        if (!got && loopback_ && !stop_) {
            // Loopback delivers nothing while nothing is playing; keep the stream continuous
            // with silence so audio stays aligned with the video clock.
            QueryPerformanceCounter(&now);
            uint64_t expected = (uint64_t)((now.QuadPart - t0.QuadPart) * (double)rate_ / freq.QuadPart);
            if (expected > written + (uint64_t)(rate_ * 0.06)) {
                size_t frames = (size_t)(expected - written);
                buf.assign(frames * channels_, 0.f);
                if (!write(buf.data(), frames)) break;
                written += frames;
            }
        }
    }
    client->Stop();
    CoUninitialize();
}
