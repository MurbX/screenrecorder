#include "app.h"
#include "dragdrop.h"
#include "overlays.h"
#include "recorder.h"
#include "winutil.h"

namespace {
constexpr float kMargin = 16;              // room for the drop shadow (DIPs)
constexpr float kMaxW = 240, kMaxH = 160;  // thumbnail box
constexpr float kRadius = 2;   // nearly flat corners
constexpr UINT_PTR kHideTimer = 1, kSlideTimer = 2;
constexpr int kSlideSteps = 9;
constexpr UINT kVisibleMs = 6000;

struct ThumbReady {
    int gen;
    std::wstring thumb;
    double duration;
};
}  // namespace

bool Toast::Create() {
    frameless_ = true;
    // Composition window: rounded corners and a soft shadow with real transparency.
    if (!Window::Create(L"ScreenRec Preview", WS_POPUP, WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, {0, 0, 272, 192},
                        nullptr, true))
        return false;
    win::ExcludeFromCapture(hwnd_, true);   // never ends up in the next screenshot or recording
    return true;
}

void Toast::Pop(const std::wstring& path, bool video) {
    path_ = path;
    video_ = video;
    duration_ = 0;
    thumb_.Reset();
    int gen = ++gen_;
    if (!video) {
        thumbPath_ = path;
        Present();
        return;
    }
    // Videos: grab a frame first (ffmpeg, ~0.5 s), then show.
    thumbPath_.clear();
    HWND h = hwnd_;
    std::thread([h, gen, path] {
        std::wstring thumb = JoinPath(LocalDataDir(), L"last_recording_thumb.jpg");
        ff::MediaInfo info;
        ff::Probe(path, info);
        bool ok = ff::Thumbnail(path, thumb);
        auto* msg = new ThumbReady{gen, ok ? thumb : L"", info.duration};
        if (!PostMessageW(h, WM_APP_MEDIA_INFO, 0, (LPARAM)msg)) delete msg;
    }).detach();
}

D2D1_RECT_F Toast::CardRect() const { return RectF(kMargin, kMargin, W() - kMargin, H() - kMargin); }

void Toast::Present() {
    // Fit the image's aspect ratio inside the thumbnail box.
    imgSize_ = {16, 9};
    if (!thumbPath_.empty()) {
        ComPtr<IWICBitmapDecoder> dec;
        ComPtr<IWICBitmapFrameDecode> frame;
        if (SUCCEEDED(gfx::WIC()->CreateDecoderFromFilename(thumbPath_.c_str(), nullptr, GENERIC_READ,
                                                           WICDecodeMetadataCacheOnDemand, &dec)) &&
            SUCCEEDED(dec->GetFrame(0, &frame))) {
            UINT w = 0, h = 0;
            frame->GetSize(&w, &h);
            if (w && h) imgSize_ = {(LONG)w, (LONG)h};
        }
    }
    float k = std::min(kMaxW / imgSize_.cx, kMaxH / imgSize_.cy);
    float cw = std::max(90.f, imgSize_.cx * k), ch = std::max(60.f, imgSize_.cy * k);

    POINT pt;
    GetCursorPos(&pt);
    RECT work = win::MonitorAt(pt).work;
    float s = (float)win::DpiOf(hwnd_) / 96.f;
    int w = (int)((cw + 2 * kMargin) * s), h = (int)((ch + 2 * kMargin) * s), m = (int)(10 * s);
    final_ = {work.right - w - m, work.bottom - h - m, work.right - m, work.bottom - m};
    slide_ = 0;
    hovering_ = pressed_ = false;
    SetWindowPos(hwnd_, HWND_TOPMOST, final_.left + (int)(40 * s), final_.top, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    SetTimer(hwnd_, kSlideTimer, 12, nullptr);
    SetTimer(hwnd_, kHideTimer, kVisibleMs, nullptr);
    Relayout();
}

void Toast::Dismiss() {
    KillTimer(hwnd_, kHideTimer);
    KillTimer(hwnd_, kSlideTimer);
    Show(false);
}

LRESULT Toast::OnMessage(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_APP_MEDIA_INFO: {
        auto* r = reinterpret_cast<ThumbReady*>(lp);
        if (r->gen == gen_) {
            thumbPath_ = r->thumb;
            duration_ = r->duration;
            Present();
        }
        delete r;
        return 0;
    }
    case WM_TIMER:
        if (wp == kSlideTimer) {   // slide in from the right with an ease-out
            float t = (float)++slide_ / kSlideSteps, ease = 1 - (1 - t) * (1 - t) * (1 - t);
            int offset = (int)std::lround((1 - ease) * 40 * Scale());
            SetWindowPos(hwnd_, nullptr, final_.left + offset, final_.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            if (slide_ >= kSlideSteps) KillTimer(hwnd_, kSlideTimer);
        } else if (wp == kHideTimer) {
            KillTimer(hwnd_, kHideTimer);
            if (!hovering_ && !pressed_) Show(false);
        }
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            SetCursor(LoadCursorW(nullptr, IDC_HAND));
            return TRUE;
        }
        break;
    }
    return Window::OnMessage(msg, wp, lp);
}

void Toast::Layout() {
    // The only control: a close button that appears on hover (top-left, like macOS).
    D2D1_RECT_F card = CardRect();
    auto& c = Add(RectXYWH(card.left - 8, card.top - 8, 22, 22), [this](gfx::Painter& p, const Control& c) {
        if (!hovering_) return;
        auto ctr = Center(c.rc);
        p.Circle(ctr, 10, Rgb(c.hot ? 0x5a5a5e : 0x3a3a3c));
        p.StrokeCircle(ctr, 10, Rgb(0xffffff, 0.25f), 1);
        DrawIcon(p, Icon::Close, RectXYWH(ctr.x - 5, ctr.y - 5, 10, 10), Rgb(0xffffff), 2.4f);
    }, [this] { Dismiss(); });
    (void)c;
}

void Toast::OnMouse(UINT msg, float x, float y, WPARAM) {
    POINT pt;
    GetCursorPos(&pt);
    switch (msg) {
    case WM_MOUSEMOVE:
        if (!hovering_) {   // stay up while hovered
            hovering_ = true;
            KillTimer(hwnd_, kHideTimer);
            Invalidate();
        }
        if (pressed_ && PastDragThreshold(pressAt_, pt)) {
            // Drag the file out: drop it into a chat, a browser or a folder.
            pressed_ = false;
            if (GetCapture() == hwnd_) ReleaseCapture();
            bool dropped = DragFileOut(path_);
            if (dropped) Dismiss();
            else SetTimer(hwnd_, kHideTimer, 2500, nullptr);
        }
        break;
    case WM_MOUSELEAVE:
        hovering_ = false;
        Invalidate();
        if (!pressed_) SetTimer(hwnd_, kHideTimer, 2500, nullptr);
        break;
    case WM_LBUTTONDOWN:
        if (!controls_.empty() && controls_[0].hot) return;   // the close button handles itself
        pressed_ = true;
        pressAt_ = pt;
        SetCapture(hwnd_);
        break;
    case WM_LBUTTONUP:
        if (!pressed_) return;
        pressed_ = false;
        if (GetCapture() == hwnd_) ReleaseCapture();
        Dismiss();
        app().OpenCapture(path_, video_);   // click: open in ScreenRec
        break;
    }
}

void Toast::Paint(gfx::Painter& p) {
    p.Clear(D2D1::ColorF(0, 0, 0, 0));
    D2D1_RECT_F card = CardRect();
    p.Shadow(card, kRadius, 12, hovering_ ? 0.6f : 0.45f);

    if (!thumb_ || thumbDc_ != p.dc) {
        thumb_ = thumbPath_.empty() ? nullptr : gfx::LoadBitmapFile(p.dc, thumbPath_);
        thumbDc_ = p.dc;
    }
    ComPtr<ID2D1RoundedRectangleGeometry> clip;
    gfx::D2D()->CreateRoundedRectangleGeometry({card, kRadius, kRadius}, &clip);
    p.dc->PushLayer(D2D1::LayerParameters1(D2D1::InfiniteRect(), clip.Get()), nullptr);
    p.Fill(card, Rgb(0x1c1c1e));
    if (thumb_) p.dc->DrawBitmap(thumb_.Get(), card, 1.f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    else DrawIcon(p, Icon::Camcorder, RectXYWH(Center(card).x - 16, Center(card).y - 16, 32, 32), Rgb(th::text3));
    if (video_) {
        // Play badge + duration, like a video file preview.
        auto c = Center(card);
        p.Circle(c, 20, D2D1::ColorF(0, 0, 0, 0.55f));
        DrawIcon(p, Icon::Play, RectXYWH(c.x - 8, c.y - 9, 18, 18), Rgb(0xffffff));
        if (duration_ > 0) {
            std::wstring d = FormatDuration(duration_);
            float tw = gfx::MeasureText(d, 11, DWRITE_FONT_WEIGHT_SEMI_BOLD) + 12;
            D2D1_RECT_F pill = RectXYWH(card.left + 8, card.bottom - 26, tw, 18);
            p.FillRound(pill, 9, D2D1::ColorF(0, 0, 0, 0.6f));
            p.Text(d, pill, 11, Rgb(0xffffff), DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER);
        }
    }
    p.dc->PopLayer();
    p.StrokeRound(Inflate(card, -0.5f), kRadius, Rgb(0xffffff, hovering_ ? 0.35f : 0.18f), 1);
    PaintControls(p);
}
