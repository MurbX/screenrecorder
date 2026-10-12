#include "app.h"
#include "overlays.h"
#include "winutil.h"

namespace {
constexpr float kDim = 0.5f;

RECT Normalize(RECT r) {
    if (r.left > r.right) std::swap(r.left, r.right);
    if (r.top > r.bottom) std::swap(r.top, r.bottom);
    return r;
}

RECT ClampTo(RECT r, RECT m) {
    r.left = std::clamp(r.left, m.left, m.right);
    r.right = std::clamp(r.right, m.left, m.right);
    r.top = std::clamp(r.top, m.top, m.bottom);
    r.bottom = std::clamp(r.bottom, m.top, m.bottom);
    return r;
}

RECT CursorMonitor() {
    POINT pt;
    GetCursorPos(&pt);
    return win::MonitorAt(pt).rect;
}
}  // namespace

// The overlay spans every monitor (the virtual screen); a selection always stays on one monitor
// because each display is captured separately.
bool AreaOverlay::Create() {
    virt_ = win::VirtualScreenRect();
    if (!Window::Create(L"ScreenRec Area", WS_POPUP, WS_EX_TOPMOST | WS_EX_TOOLWINDOW, virt_, nullptr, true)) return false;
    win::ExcludeFromCapture(hwnd_, true);
    return true;
}

D2D1_RECT_F AreaOverlay::ToDip(RECT r) const {
    float s = Scale();
    return RectF((r.left - virt_.left) / s, (r.top - virt_.top) / s, (r.right - virt_.left) / s, (r.bottom - virt_.top) / s);
}

void AreaOverlay::Hint(gfx::Painter& p, const std::wstring& text) {
    D2D1_RECT_F m = ToDip(CursorMonitor());
    float w = gfx::MeasureText(text, 14) + 36;
    D2D1_RECT_F r = RectXYWH((m.left + m.right) / 2 - w / 2, m.top + 40, w, 36);
    p.FillRound(r, 18, Rgb(0x1c1c1e, 0.92f));
    p.Text(text, r, 14, Rgb(th::text), DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER);
}

void AreaOverlay::SetKind(Kind k) {
    kind_ = k;
    drag_ = Drag::None;
    if (k == Kind::Hidden) {
        Show(false);
        return;
    }
    // Always re-assert full coverage: monitors may have changed, and Windows rescales windows
    // that cross monitors with different DPI.
    virt_ = win::VirtualScreenRect();
    SetWindowPos(hwnd_, nullptr, virt_.left, virt_.top, virt_.right - virt_.left, virt_.bottom - virt_.top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    bool interactive = k != Kind::Frame;
    win::SetClickThrough(hwnd_, !interactive);
    ShowWindow(hwnd_, interactive ? SW_SHOW : SW_SHOWNOACTIVATE);
    if (interactive) SetForegroundWindow(hwnd_);
    hoverMon_ = CursorMonitor();
    Invalidate();
}

void AreaOverlay::BeginSelect(bool shot) {
    shot_ = shot;
    countdown_ = 0;
    SetKind(Kind::Select);
}

void AreaOverlay::BeginAdjust(RECT r) {
    shot_ = false;
    region_ = ClampTo(r, win::MonitorFor(r).rect);
    countdown_ = 0;
    SetKind(Kind::Adjust);
}

void AreaOverlay::BeginPick(bool shot) {
    shot_ = shot;
    hoverWin_ = nullptr;
    countdown_ = 0;
    SetKind(Kind::PickWindow);
}

void AreaOverlay::BeginPickMonitor(bool shot) {
    shot_ = shot;
    countdown_ = 0;
    SetKind(Kind::PickMonitor);
}

void AreaOverlay::ShowFrame(RECT r, bool border) {
    region_ = r;
    border_ = border;
    SetKind(Kind::Frame);
}

void AreaOverlay::Hide() {
    countdown_ = 0;
    SetKind(Kind::Hidden);
}

void AreaOverlay::SetRegion(RECT r) {
    region_ = r;
    Invalidate();
}

void AreaOverlay::SetCountdown(int n) {
    countdown_ = n;
    Invalidate();
}

int AreaOverlay::HandleAt(POINT px) const {
    const RECT& r = region_;
    LONG cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
    POINT hs[8] = {{r.left, r.top}, {cx, r.top},       {r.right, r.top},   {r.right, cy},
                   {r.right, r.bottom}, {cx, r.bottom}, {r.left, r.bottom}, {r.left, cy}};
    LONG tol = (LONG)(9 * Scale());
    for (int i = 0; i < 8; ++i)
        if (std::abs(px.x - hs[i].x) <= tol && std::abs(px.y - hs[i].y) <= tol) return i;
    return -1;
}

LRESULT AreaOverlay::OnMessage(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_SETCURSOR: {
        if (LOWORD(lp) != HTCLIENT) break;
        POINT pt;
        GetCursorPos(&pt);
        LPCWSTR cur = IDC_ARROW;
        if (kind_ == Kind::Select) cur = IDC_CROSS;
        else if (kind_ == Kind::Adjust) {
            int h = drag_ == Drag::Resize ? handle_ : HandleAt(pt);
            static const LPCWSTR sizes[8] = {IDC_SIZENWSE, IDC_SIZENS, IDC_SIZENESW, IDC_SIZEWE,
                                             IDC_SIZENWSE, IDC_SIZENS, IDC_SIZENESW, IDC_SIZEWE};
            if (h >= 0) cur = sizes[h];
            else if (drag_ == Drag::Move || PtInRect(&region_, pt)) cur = IDC_SIZEALL;
            else cur = IDC_CROSS;
        } else if (kind_ == Kind::PickWindow || kind_ == Kind::PickMonitor) {
            cur = IDC_HAND;
        }
        SetCursor(LoadCursorW(nullptr, cur));
        return TRUE;
    }
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) {
            app().Cancel();
            return 0;
        }
        if (wp == VK_RETURN && kind_ == Kind::Adjust) {
            app().Record();
            return 0;
        }
        break;
    case WM_WINDOWPOSCHANGING: {
        // Spanning monitors with different scaling makes Windows shrink/grow this window; any
        // move or resize is forced back to the whole virtual screen.
        auto* pos = reinterpret_cast<WINDOWPOS*>(lp);
        if (!(pos->flags & SWP_NOSIZE) || !(pos->flags & SWP_NOMOVE)) {
            pos->x = virt_.left;
            pos->y = virt_.top;
            pos->cx = virt_.right - virt_.left;
            pos->cy = virt_.bottom - virt_.top;
            pos->flags &= ~(SWP_NOSIZE | SWP_NOMOVE);
        }
        break;
    }
    case WM_DPICHANGED:
        // Spanning several monitors: keep covering the virtual screen instead of the suggested rect.
        dpi_ = (float)HIWORD(wp);
        surface_.SetDpi(dpi_);
        SetWindowPos(hwnd_, nullptr, virt_.left, virt_.top, virt_.right - virt_.left, virt_.bottom - virt_.top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        Relayout();
        return 0;
    }
    return Window::OnMessage(msg, wp, lp);
}

void AreaOverlay::OnMouse(UINT msg, float x, float y, WPARAM) {
    if (x < 0) return;   // mouse leave
    POINT pt{(LONG)std::lround(x * Scale()) + virt_.left, (LONG)std::lround(y * Scale()) + virt_.top};
    switch (msg) {
    case WM_LBUTTONDOWN:
        if (kind_ == Kind::PickWindow) {
            if (hoverWin_) app().OnWindowPicked(hoverWin_);
            return;
        }
        if (kind_ == Kind::PickMonitor) {
            app().OnMonitorPicked(win::MonitorAt(pt).rect);
            return;
        }
        if (kind_ != Kind::Select && kind_ != Kind::Adjust) return;
        dragStart_ = pt;
        dragOrig_ = region_;
        if (kind_ == Kind::Adjust && (handle_ = HandleAt(pt)) >= 0) drag_ = Drag::Resize;
        else if (kind_ == Kind::Adjust && PtInRect(&region_, pt)) drag_ = Drag::Move;
        else drag_ = Drag::New;
        // New selections stay on the monitor where the drag started; edits on the current one.
        dragMon_ = drag_ == Drag::New ? win::MonitorAt(pt).rect : win::MonitorFor(region_).rect;
        SetCapture(hwnd_);
        break;
    case WM_LBUTTONDBLCLK:
        if (kind_ == Kind::Adjust && PtInRect(&region_, pt)) app().Record();
        break;
    case WM_MOUSEMOVE: {
        RECT mon = win::MonitorAt(pt).rect;
        bool monChanged = !EqualRect(&mon, &hoverMon_);
        hoverMon_ = mon;
        if (kind_ == Kind::PickWindow) {
            HWND h = win::TopLevelWindowAt(pt);
            RECT r{};
            if (h) {
                RECT b = win::WindowBounds(h), wm = win::MonitorFor(b).rect;
                IntersectRect(&r, &b, &wm);
            }
            if (h != hoverWin_ || !EqualRect(&r, &hoverRect_) || monChanged) {
                hoverWin_ = h;
                hoverRect_ = r;
                Invalidate();
            }
            return;
        }
        if (drag_ == Drag::None) {
            if (monChanged) Invalidate();   // move the hint / monitor highlight along
            return;
        }
        LONG dx = pt.x - dragStart_.x, dy = pt.y - dragStart_.y;
        RECT r = dragOrig_;
        const RECT& m = dragMon_;
        if (drag_ == Drag::New) {
            r = Normalize({dragStart_.x, dragStart_.y, pt.x, pt.y});
        } else if (drag_ == Drag::Move) {
            LONG w = r.right - r.left, h = r.bottom - r.top;
            r.left = std::clamp(r.left + dx, m.left, m.right - w);
            r.top = std::clamp(r.top + dy, m.top, m.bottom - h);
            r.right = r.left + w;
            r.bottom = r.top + h;
        } else {
            int h = handle_;
            if (h == 0 || h == 6 || h == 7) r.left += dx;
            if (h == 2 || h == 3 || h == 4) r.right += dx;
            if (h == 0 || h == 1 || h == 2) r.top += dy;
            if (h == 4 || h == 5 || h == 6) r.bottom += dy;
            r = Normalize(r);
        }
        region_ = ClampTo(r, m);
        if (drag_ == Drag::New && kind_ == Kind::Select) kind_ = Kind::Adjust;
        Render();
        if (!shot_) app().OnRegionChanged(region_);
        break;
    }
    case WM_LBUTTONUP:
        if (drag_ == Drag::None) return;
        OnMouse(WM_MOUSEMOVE, x, y, 0);   // apply the release point (the last move may not have arrived)
        drag_ = Drag::None;
        if (GetCapture() == hwnd_) ReleaseCapture();
        if (region_.right - region_.left < 16 || region_.bottom - region_.top < 16) {
            // A click without a real drag: go back to selecting.
            kind_ = Kind::Select;
            Invalidate();
            return;
        }
        if (shot_) {   // screenshot: capture as soon as the drag ends
            app().OnShotArea(region_);
            return;
        }
        app().OnRegionChanged(region_);
        Invalidate();
        break;
    }
}

void AreaOverlay::Paint(gfx::Painter& p) {
    p.Clear(D2D1::ColorF(0, 0, 0, 0));
    float W = this->W(), H = this->H();
    auto dimAround = [&](D2D1_RECT_F r, float a) {
        auto c = D2D1::ColorF(0, 0, 0, a);
        p.Fill(RectF(0, 0, W, r.top), c);
        p.Fill(RectF(0, r.bottom, W, H), c);
        p.Fill(RectF(0, r.top, r.left, r.bottom), c);
        p.Fill(RectF(r.right, r.top, W, r.bottom), c);
    };

    switch (kind_) {
    case Kind::Select:
        p.Fill(RectF(0, 0, W, H), D2D1::ColorF(0, 0, 0, kDim));
        Hint(p, shot_ ? L"Drag to capture an area  ·  Esc to cancel" : L"Drag to select the area to record  ·  Esc to cancel");
        break;
    case Kind::Adjust: {
        D2D1_RECT_F r = ToDip(region_);
        dimAround(r, kDim);
        p.Stroke(r, D2D1::ColorF(0, 0, 0, 0.5f), 1);
        p.Stroke(r, Rgb(0xffffff), 1, gfx::DashStyle());
        float cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
        D2D1_POINT_2F hs[8] = {{r.left, r.top}, {cx, r.top},       {r.right, r.top},   {r.right, cy},
                               {r.right, r.bottom}, {cx, r.bottom}, {r.left, r.bottom}, {r.left, cy}};
        for (auto& h : hs) {
            p.Circle(h, 4.5f, Rgb(0xffffff));
            p.StrokeCircle(h, 4.5f, Rgb(0x8e8e93), 1);
        }
        if (drag_ == Drag::None && !shot_) Hint(p, L"Drag the handles to adjust  ·  Enter or double-click to record");
        if (shot_) {   // live size while dragging a screenshot area
            std::wstring size = Fmt(L"%d x %d", region_.right - region_.left, region_.bottom - region_.top);
            float tw = gfx::MeasureText(size, 12.5f) + 16;
            D2D1_RECT_F tag = RectXYWH(r.left, r.top - 28 > 0 ? r.top - 28 : r.top + 6, tw, 22);
            p.FillRound(tag, 6, Rgb(0x1c1c1e, 0.9f));
            p.Text(size, tag, 12.5f, Rgb(th::text), DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER);
        }
        break;
    }
    case Kind::PickWindow:
        if (hoverWin_) {
            D2D1_RECT_F r = ToDip(hoverRect_);
            dimAround(r, 0.25f);
            p.Fill(r, Rgb(th::blue, 0.18f));
            p.Stroke(Inflate(r, -1.5f), Rgb(th::blue), 3);
        } else {
            p.Fill(RectF(0, 0, W, H), D2D1::ColorF(0, 0, 0, 0.25f));
        }
        Hint(p, shot_ ? L"Click a window to capture it  ·  Esc to cancel" : L"Click a window to record it  ·  Esc to cancel");
        break;
    case Kind::PickMonitor: {
        D2D1_RECT_F r = ToDip(hoverMon_);
        dimAround(r, kDim);
        p.Stroke(Inflate(r, -2), Rgb(th::blue), 4);
        std::wstring label = Fmt(shot_ ? L"Click to capture this screen  ·  %d x %d" : L"Click to record this screen  ·  %d x %d", hoverMon_.right - hoverMon_.left,
                                 hoverMon_.bottom - hoverMon_.top);
        float w = gfx::MeasureText(label, 16, DWRITE_FONT_WEIGHT_SEMI_BOLD) + 48;
        D2D1_RECT_F pill = RectXYWH(Center(r).x - w / 2, Center(r).y - 26, w, 52);
        p.FillRound(pill, 26, Rgb(th::blue));
        p.Text(label, pill, 16, Rgb(0xffffff), DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER);
        break;
    }
    case Kind::Frame:
        if (border_) {
            D2D1_RECT_F r = Inflate(ToDip(region_), 1.5f);
            p.Stroke(r, D2D1::ColorF(0, 0, 0, 0.6f), 1);
            p.Stroke(r, Rgb(0xffffff), 1, gfx::DashStyle());
        }
        break;
    case Kind::Hidden:
        break;
    }

    if (countdown_ > 0) {
        D2D1_RECT_F r = (region_.right > region_.left) ? ToDip(region_) : ToDip(CursorMonitor());
        auto c = Center(r);
        p.Circle(c, 70, D2D1::ColorF(0, 0, 0, 0.6f));
        p.Text(std::to_wstring(countdown_), RectXYWH(c.x - 70, c.y - 70, 140, 140), 72, Rgb(0xffffff),
               DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER);
    }
}
