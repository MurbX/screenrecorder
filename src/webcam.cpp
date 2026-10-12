#include "webcam.h"
#include "settings.h"
#include "winutil.h"
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>

namespace {
constexpr float kMargin = 18;   // room for the drop shadow (DIPs)
constexpr float kRadius = 14;

std::vector<ComPtr<IMFActivate>> EnumCameras() {
    std::vector<ComPtr<IMFActivate>> out;
    ComPtr<IMFAttributes> attr;
    if (FAILED(MFCreateAttributes(&attr, 1))) return out;
    attr->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    IMFActivate** devs = nullptr;
    UINT32 n = 0;
    if (FAILED(MFEnumDeviceSources(attr.Get(), &devs, &n))) return out;
    for (UINT32 i = 0; i < n; ++i) {
        out.emplace_back(devs[i]);
        devs[i]->Release();
    }
    CoTaskMemFree(devs);
    return out;
}

std::wstring CameraName(IMFActivate* a) {
    WCHAR* name = nullptr;
    UINT32 len = 0;
    std::wstring s;
    if (SUCCEEDED(a->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &name, &len))) s = name;
    CoTaskMemFree(name);
    return s;
}
}  // namespace

std::vector<std::wstring> ListWebcams() {
    std::vector<std::wstring> names;
    for (auto& a : EnumCameras()) names.push_back(CameraName(a.Get()));
    return names;
}

WebcamWindow::~WebcamWindow() { StopCapture(); }

bool WebcamWindow::Open(Style style) {
    bool restyle = style != style_;
    style_ = style;
    if (!hwnd_) {
        frameless_ = true;
        if (!Window::Create(L"ScreenRec Webcam", WS_POPUP,
                            WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, {0, 0, 320, 180},
                            nullptr, true))
            return false;
        restyle = true;
    }
    if (restyle || !Visible()) FitWindow(false);
    // In preview mode the camera is recorded directly, so the preview itself stays out of captures.
    win::ExcludeFromCapture(hwnd_, style_ == Style::Preview);
    if (!thread_.joinable()) {
        stop_ = false;
        status_ = L"Starting camera…";
        thread_ = std::thread([this, name = S().webcamName] { CaptureLoop(name); });
    }
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    Invalidate();
    return true;
}

void WebcamWindow::Shut() {
    StopCapture();
    if (hwnd_) Show(false);
}

void WebcamWindow::StopCapture() {
    stop_ = true;
    if (thread_.joinable()) thread_.join();
    std::lock_guard lk(mu_);
    frame_.clear();
    fw_ = fh_ = 0;
    bmp_.Reset();
    bmpDc_ = nullptr;
}

RECT WebcamWindow::ScreenRect() const {
    RECT r{};
    if (hwnd_) GetWindowRect(hwnd_, &r);
    int m = (int)(kMargin * Scale());
    InflateRect(&r, -m, -m);
    return r;
}

void WebcamWindow::ApplySettings() {
    if (hwnd_ && Visible()) {
        FitWindow(true);
        Invalidate();
    }
}

void WebcamWindow::FitWindow(bool keepCenter) {
    float s = (float)win::DpiOf(hwnd_) / 96.f;
    int m = (int)(kMargin * s);
    double aspect = (fw_ > 0 && fh_ > 0) ? (double)fh_ / fw_ : 9.0 / 16.0;
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &mi);
    RECT work = mi.rcWork;
    int cw, ch, x, y;
    if (style_ == Style::Preview) {
        cw = (int)(960 * s);
        ch = (int)(cw * aspect);
        x = (work.left + work.right) / 2 - cw / 2 - m;
        y = work.top + (work.bottom - work.top - ch) / 2 - (int)(40 * s) - m;
    } else {
        cw = S().camW;
        ch = S().camShape == 0 ? cw : (int)(cw * aspect);   // circle: square window
        RECT cur{};
        GetWindowRect(hwnd_, &cur);
        if (keepCenter && cur.right > cur.left) {
            x = (cur.left + cur.right) / 2 - cw / 2 - m;
            y = (cur.top + cur.bottom) / 2 - ch / 2 - m;
        } else if (S().camX >= 0 && S().camY >= 0) {
            x = S().camX;
            y = S().camY;
        } else {
            x = work.right - cw - m - (int)(24 * s);
            y = work.bottom - ch - m - (int)(24 * s);
        }
    }
    SetWindowPos(hwnd_, HWND_TOPMOST, x, y, cw + 2 * m, ch + 2 * m, SWP_NOACTIVATE);
}

void WebcamWindow::CaptureLoop(std::wstring want) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    auto setStatus = [this](const wchar_t* s) {
        {
            std::lock_guard lk(mu_);
            status_ = s;
        }
        PostMessageW(hwnd_, WM_APP_WEBCAM_FRAME, 0, 0);
    };
    {
        auto cams = EnumCameras();
        if (cams.empty()) {
            setStatus(L"No webcam found");
            CoUninitialize();
            return;
        }
        size_t idx = 0;
        for (size_t i = 0; i < cams.size(); ++i)
            if (!want.empty() && CameraName(cams[i].Get()) == want) idx = i;
        ComPtr<IMFMediaSource> source;
        if (FAILED(cams[idx]->ActivateObject(IID_PPV_ARGS(&source)))) {
            setStatus(L"The webcam is in use by another app");
            CoUninitialize();
            return;
        }
        ComPtr<IMFAttributes> ra;
        MFCreateAttributes(&ra, 1);
        ra->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
        ComPtr<IMFSourceReader> reader;
        const DWORD stream = (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM;
        if (FAILED(MFCreateSourceReaderFromMediaSource(source.Get(), ra.Get(), &reader))) {
            setStatus(L"Could not open the webcam");
            source->Shutdown();
            CoUninitialize();
            return;
        }
        // Pick the largest native mode up to 1280 px wide that runs at >= 24 fps.
        ComPtr<IMFMediaType> best;
        UINT32 bestW = 0, bestH = 0;
        for (DWORD i = 0;; ++i) {
            ComPtr<IMFMediaType> t;
            if (FAILED(reader->GetNativeMediaType(stream, i, &t))) break;
            UINT32 w = 0, h = 0, num = 0, den = 1;
            MFGetAttributeSize(t.Get(), MF_MT_FRAME_SIZE, &w, &h);
            MFGetAttributeRatio(t.Get(), MF_MT_FRAME_RATE, &num, &den);
            double fps = den ? (double)num / den : 0;
            if (w <= 1280 && fps >= 24 && w * h > bestW * bestH) {
                best = t;
                bestW = w;
                bestH = h;
            }
        }
        if (best) reader->SetCurrentMediaType(stream, nullptr, best.Get());
        ComPtr<IMFMediaType> out;
        MFCreateMediaType(&out);
        out->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        out->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
        if (bestW) MFSetAttributeSize(out.Get(), MF_MT_FRAME_SIZE, bestW, bestH);
        if (FAILED(reader->SetCurrentMediaType(stream, nullptr, out.Get()))) {
            setStatus(L"Unsupported webcam format");
            source->Shutdown();
            CoUninitialize();
            return;
        }
        ComPtr<IMFMediaType> cur;
        reader->GetCurrentMediaType(stream, &cur);
        UINT32 w = 0, h = 0;
        MFGetAttributeSize(cur.Get(), MF_MT_FRAME_SIZE, &w, &h);
        LONG defStride = (LONG)MFGetAttributeUINT32(cur.Get(), MF_MT_DEFAULT_STRIDE, w * 4);

        std::vector<uint8_t> tmp((size_t)w * h * 4);
        while (!stop_) {
            DWORD flags = 0;
            LONGLONG ts = 0;
            ComPtr<IMFSample> sample;
            if (FAILED(reader->ReadSample(stream, 0, nullptr, &flags, &ts, &sample))) break;
            if (flags & (MF_SOURCE_READERF_ERROR | MF_SOURCE_READERF_ENDOFSTREAM)) break;
            if (!sample) continue;
            ComPtr<IMFMediaBuffer> buf;
            if (FAILED(sample->GetBufferByIndex(0, &buf))) continue;
            BYTE* scan0 = nullptr;
            LONG pitch = 0;
            ComPtr<IMF2DBuffer> b2;
            bool locked2d = SUCCEEDED(buf.As(&b2)) && SUCCEEDED(b2->Lock2D(&scan0, &pitch));
            BYTE* raw = nullptr;
            if (!locked2d) {
                DWORD maxLen, curLen;
                if (FAILED(buf->Lock(&raw, &maxLen, &curLen))) continue;
                pitch = defStride;
                scan0 = pitch < 0 ? raw + (size_t)(-pitch) * (h - 1) : raw;
            }
            for (UINT32 y = 0; y < h; ++y) memcpy(&tmp[(size_t)y * w * 4], scan0 + (ptrdiff_t)y * pitch, (size_t)w * 4);
            if (locked2d) b2->Unlock2D();
            else buf->Unlock();
            {
                std::lock_guard lk(mu_);
                frame_.swap(tmp);
                tmp.resize((size_t)w * h * 4);
                fw_ = (int)w;
                fh_ = (int)h;
                frameSeq_++;
                status_.clear();
            }
            if (!pending_.exchange(true)) PostMessageW(hwnd_, WM_APP_WEBCAM_FRAME, 0, 0);
        }
        source->Shutdown();
    }
    CoUninitialize();
}

LRESULT WebcamWindow::OnMessage(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_APP_WEBCAM_FRAME: {
        pending_ = false;
        bool firstFrame = false;
        {
            std::lock_guard lk(mu_);
            firstFrame = bmpSeq_ == 0 && frameSeq_ > 0;
        }
        if (firstFrame) FitWindow(true);   // adopt the camera's real aspect ratio
        Render();
        return 0;
    }
    case WM_MOUSEWHEEL:
        if (style_ == Style::Bubble) {
            int delta = GET_WHEEL_DELTA_WPARAM(wp);
            S().camW = std::clamp((int)(S().camW * (delta > 0 ? 1.1 : 1 / 1.1)), 160, 1600);
            FitWindow(true);
            S().Save();
        }
        return 0;
    case WM_EXITSIZEMOVE:
        if (style_ == Style::Bubble) {
            RECT r;
            GetWindowRect(hwnd_, &r);
            S().camX = r.left;
            S().camY = r.top;
            S().Save();
        }
        return 0;
    }
    return Window::OnMessage(msg, wp, lp);
}

void WebcamWindow::Paint(gfx::Painter& p) {
    p.Clear(D2D1::ColorF(0, 0, 0, 0));
    D2D1_RECT_F content = RectF(kMargin, kMargin, W() - kMargin, H() - kMargin);
    bool circle = style_ == Style::Bubble && S().camShape == 0;
    float rad = circle ? std::min(RW(content), RH(content)) / 2 : kRadius;
    if (S().shadow) p.Shadow(content, rad, 14, 0.55f);

    std::wstring status;
    {
        std::lock_guard lk(mu_);
        status = status_;
        if (fw_ > 0 && (frameSeq_ != bmpSeq_ || bmpDc_ != p.dc)) {
            D2D1_SIZE_U sz{(UINT32)fw_, (UINT32)fh_};
            if (!bmp_ || bmpDc_ != p.dc || bmp_->GetPixelSize().width != sz.width || bmp_->GetPixelSize().height != sz.height) {
                bmp_.Reset();
                p.dc->CreateBitmap(sz, frame_.data(), fw_ * 4,
                                   D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE)),
                                   &bmp_);
                bmpDc_ = p.dc;
            } else {
                bmp_->CopyFromMemory(nullptr, frame_.data(), fw_ * 4);
            }
            bmpSeq_ = frameSeq_;
        }
    }

    ComPtr<ID2D1RoundedRectangleGeometry> clip;
    gfx::D2D()->CreateRoundedRectangleGeometry({content, rad, rad}, &clip);
    p.dc->PushLayer(D2D1::LayerParameters1(D2D1::InfiniteRect(), clip.Get()), nullptr);
    if (bmp_) {
        // "Cover" crop: fill the bubble, cutting the overflowing edges.
        auto bs = bmp_->GetSize();
        float dstA = RW(content) / RH(content), srcA = bs.width / bs.height;
        D2D1_RECT_F src = RectF(0, 0, bs.width, bs.height);
        if (srcA > dstA) {
            float w = bs.height * dstA;
            src.left = (bs.width - w) / 2;
            src.right = src.left + w;
        } else {
            float h = bs.width / dstA;
            src.top = (bs.height - h) / 2;
            src.bottom = src.top + h;
        }
        D2D1_MATRIX_3X2_F old;
        p.dc->GetTransform(&old);
        auto c = Center(content);
        p.dc->SetTransform(D2D1::Matrix3x2F::Scale(S().mirror ? -1.f : 1.f, S().flip ? -1.f : 1.f, c) * old);
        p.dc->DrawBitmap(bmp_.Get(), content, 1.f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, src);
        p.dc->SetTransform(old);
    } else {
        p.Fill(content, Rgb(th::tile));
        DrawIcon(p, Icon::Webcam, RectXYWH(Center(content).x - 16, Center(content).y - 30, 32, 32), Rgb(th::text2));
        p.Text(status, RectF(content.left, Center(content).y + 6, content.right, Center(content).y + 30), 13,
               Rgb(th::text2), DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER);
    }
    p.dc->PopLayer();
    if (S().borderWidth > 0) {
        float bw = (float)S().borderWidth;
        p.StrokeRound(Inflate(content, -bw / 2), rad - bw / 2, Rgb(S().borderColor), bw);
    }
}
