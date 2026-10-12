#include "player.h"
#include "gfx.h"
#include <mfapi.h>
#include <mfmediaengine.h>

namespace {
class EngineNotify : public IMFMediaEngineNotify {
public:
    explicit EngineNotify(HWND hwnd) : hwnd_(hwnd) {}
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IMFMediaEngineNotify)) {
            *ppv = static_cast<IMFMediaEngineNotify*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&ref_); }
    STDMETHODIMP_(ULONG) Release() override {
        ULONG r = InterlockedDecrement(&ref_);
        if (!r) delete this;
        return r;
    }
    STDMETHODIMP EventNotify(DWORD event, DWORD_PTR param1, DWORD param2) override {
        if (event == MF_MEDIA_ENGINE_EVENT_NOTIFYSTABLESTATE) {
            SetEvent(reinterpret_cast<HANDLE>(param1));
            return S_OK;
        }
        PostMessageW(hwnd_, WM_APP_PLAYER_EVENT, event, param2);
        return S_OK;
    }

private:
    LONG ref_ = 1;
    HWND hwnd_;
};
}  // namespace

bool VideoPlayer::Open(HWND notify, const std::wstring& path) {
    Close();
    UINT token = 0;
    if (FAILED(MFCreateDXGIDeviceManager(&token, &dxgiMgr_))) return false;
    dxgiMgr_->ResetDevice(gfx::D3D(), token);
    ComPtr<IMFMediaEngineClassFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_MFMediaEngineClassFactory, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&factory)))) return false;
    auto* cb = new EngineNotify(notify);
    notify_.Attach(static_cast<IUnknown*>(cb));
    ComPtr<IMFAttributes> attr;
    MFCreateAttributes(&attr, 3);
    attr->SetUnknown(MF_MEDIA_ENGINE_DXGI_MANAGER, dxgiMgr_.Get());
    attr->SetUnknown(MF_MEDIA_ENGINE_CALLBACK, cb);
    attr->SetUINT32(MF_MEDIA_ENGINE_VIDEO_OUTPUT_FORMAT, DXGI_FORMAT_B8G8R8A8_UNORM);
    if (FAILED(factory->CreateInstance(0, attr.Get(), &engine_))) {
        Close();
        return false;
    }
    BSTR url = SysAllocString(path.c_str());
    HRESULT hr = engine_->SetSource(url);
    SysFreeString(url);
    if (FAILED(hr)) {
        Close();
        return false;
    }
    return true;
}

void VideoPlayer::Close() {
    if (engine_) engine_->Shutdown();
    engine_.Reset();
    bmp_.Reset();
    tex_.Reset();
    bmpDc_ = nullptr;
    haveFrame_ = false;
    notify_.Reset();
    dxgiMgr_.Reset();
}

void VideoPlayer::Play() {
    if (!engine_) return;
    if (engine_->IsEnded()) engine_->SetCurrentTime(0);
    engine_->Play();
}

void VideoPlayer::Pause() {
    if (engine_) engine_->Pause();
}

bool VideoPlayer::Paused() const { return !engine_ || engine_->IsPaused(); }
bool VideoPlayer::Ended() const { return engine_ && engine_->IsEnded(); }

double VideoPlayer::Duration() const {
    if (!engine_) return 0;
    double d = engine_->GetDuration();
    return std::isfinite(d) ? d : 0;
}

double VideoPlayer::Position() const { return engine_ ? engine_->GetCurrentTime() : 0; }

void VideoPlayer::Seek(double s) {
    if (engine_) engine_->SetCurrentTime(std::max(0.0, s));
}

bool VideoPlayer::HasVideo() const { return engine_ && engine_->HasVideo(); }

SIZE VideoPlayer::VideoSize() const {
    DWORD w = 0, h = 0;
    if (engine_) engine_->GetNativeVideoSize(&w, &h);
    return {(LONG)w, (LONG)h};
}

bool VideoPlayer::UpdateFrame() {
    if (!engine_ || !engine_->HasVideo()) return false;
    LONGLONG pts = 0;
    if (engine_->OnVideoStreamTick(&pts) != S_OK) return false;
    SIZE sz = VideoSize();
    if (sz.cx <= 0 || sz.cy <= 0) return false;
    D3D11_TEXTURE2D_DESC have{};
    if (tex_) tex_->GetDesc(&have);
    if (!tex_ || have.Width != (UINT)sz.cx || have.Height != (UINT)sz.cy) {
        D3D11_TEXTURE2D_DESC d{};
        d.Width = sz.cx;
        d.Height = sz.cy;
        d.MipLevels = 1;
        d.ArraySize = 1;
        d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        d.SampleDesc.Count = 1;
        d.Usage = D3D11_USAGE_DEFAULT;
        d.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        tex_.Reset();
        bmp_.Reset();
        if (FAILED(gfx::D3D()->CreateTexture2D(&d, nullptr, &tex_))) return false;
    }
    RECT dst{0, 0, sz.cx, sz.cy};
    MFVideoNormalizedRect src{0, 0, 1, 1};
    MFARGB border{0, 0, 0, 255};
    if (FAILED(engine_->TransferVideoFrame(tex_.Get(), &src, &dst, &border))) return false;
    haveFrame_ = true;
    return true;
}

ID2D1Bitmap1* VideoPlayer::Frame(ID2D1DeviceContext* dc) {
    if (!haveFrame_ || !tex_) return nullptr;
    if (!bmp_ || bmpDc_ != dc) {
        ComPtr<IDXGISurface> surf;
        tex_.As(&surf);
        bmp_.Reset();
        auto props = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE,
                                             D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
        if (FAILED(dc->CreateBitmapFromDxgiSurface(surf.Get(), &props, &bmp_))) return nullptr;
        bmpDc_ = dc;
    }
    return bmp_.Get();
}
