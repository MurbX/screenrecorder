#include "ui.h"
#include "winutil.h"
#include <commctrl.h>

static const wchar_t* kClass = L"ScreenRecWindow";

Window::~Window() {
    if (hwnd_) {
        SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    surface_.Destroy();
}

bool Window::Create(const wchar_t* title, DWORD style, DWORD exStyle, RECT rc, HWND owner, bool composition) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{sizeof(wc)};
        wc.style = CS_DBLCLKS;
        wc.lpfnWndProc = Proc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kClass;
        LoadIconWithScaleDown(wc.hInstance, MAKEINTRESOURCEW(101), GetSystemMetrics(SM_CXICON),
                              GetSystemMetrics(SM_CYICON), &wc.hIcon);
        LoadIconWithScaleDown(wc.hInstance, MAKEINTRESOURCEW(101), GetSystemMetrics(SM_CXSMICON),
                              GetSystemMetrics(SM_CYSMICON), &wc.hIconSm);
        RegisterClassExW(&wc);
        registered = true;
    }
    if (composition) exStyle |= WS_EX_NOREDIRECTIONBITMAP;
    hwnd_ = CreateWindowExW(exStyle, kClass, title, style, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, owner,
                            nullptr, GetModuleHandleW(nullptr), this);
    if (!hwnd_) return false;
    if (exStyle & WS_EX_LAYERED) SetLayeredWindowAttributes(hwnd_, 0, 255, LWA_ALPHA);
    dpi_ = (float)win::DpiOf(hwnd_);
    surface_.SetDpi(dpi_);
    if (!surface_.Create(hwnd_, composition)) return false;
    if (!composition && frameless_) {
        win::StyleFrame(hwnd_);
        // Re-run WM_NCCALCSIZE so the standard frame disappears immediately.
        SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    return true;
}

void Window::Show(bool show) {
    if (!hwnd_) return;
    if (show) {
        bool noActivate = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE) & WS_EX_NOACTIVATE;
        ShowWindow(hwnd_, noActivate ? SW_SHOWNOACTIVATE : SW_SHOW);
        Invalidate();
    } else {
        ShowWindow(hwnd_, SW_HIDE);
    }
}

void Window::Close() {
    if (hwnd_) DestroyWindow(hwnd_);
}

float Window::W() const {
    RECT r{};
    if (hwnd_) GetClientRect(hwnd_, &r);
    return (r.right - r.left) / Scale();
}

float Window::H() const {
    RECT r{};
    if (hwnd_) GetClientRect(hwnd_, &r);
    return (r.bottom - r.top) / Scale();
}

POINT Window::ToScreen(float x, float y) const {
    POINT pt{(LONG)std::lround(x * Scale()), (LONG)std::lround(y * Scale())};
    ClientToScreen(hwnd_, &pt);
    return pt;
}

Window::Control& Window::Add(D2D1_RECT_F rc, std::function<void(gfx::Painter&, const Control&)> paint,
                             std::function<void()> click) {
    Control c;
    c.rc = rc;
    c.paint = std::move(paint);
    c.click = std::move(click);
    controls_.push_back(std::move(c));
    return controls_.back();
}

void Window::EnsureLayout() {
    if (!needLayout_) return;
    needLayout_ = false;
    controls_.clear();
    hot_ = down_ = -1;
    Layout();
}

void Window::Render() {
    ID2D1DeviceContext* dc = surface_.Begin();
    if (!dc) return;
    EnsureLayout();
    gfx::Painter p{dc, surface_.Brush()};
    Paint(p);
    surface_.End();
}

void Window::PaintControls(gfx::Painter& p) {
    for (auto& c : controls_)
        if (c.paint) c.paint(p, c);
}

int Window::ControlAt(float x, float y) const {
    for (int i = (int)controls_.size() - 1; i >= 0; --i) {
        const auto& c = controls_[i];
        if (c.enabled && (c.click || c.drag || c.rclick) && InRect(c.rc, x, y)) return i;
    }
    return -1;
}

LRESULT Window::HitTest(float x, float y) {
    if (y < captionH_ && ControlAt(x, y) < 0) return HTCAPTION;
    return HTCLIENT;
}

LRESULT CALLBACK Window::Proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* self = static_cast<Window*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        self->hwnd_ = h;
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    auto* self = reinterpret_cast<Window*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (!self) return DefWindowProcW(h, msg, wp, lp);
    if (msg == WM_NCDESTROY) {
        SetWindowLongPtrW(h, GWLP_USERDATA, 0);
        self->surface_.Destroy();
        self->hwnd_ = nullptr;
        self->controls_.clear();
        self->needLayout_ = true;
        self->OnClosed();
        return DefWindowProcW(h, msg, wp, lp);
    }
    return self->OnMessage(msg, wp, lp);
}

LRESULT Window::OnMessage(UINT msg, WPARAM wp, LPARAM lp) {
    HWND h = hwnd_;
    switch (msg) {
    case WM_NCCALCSIZE:
        if (frameless_ && wp) return 0;
        break;
    case WM_NCHITTEST:
        if (frameless_) {
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(h, &pt);
            EnsureLayout();
            return HitTest(pt.x / Scale(), pt.y / Scale());
        }
        break;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(h, &ps);
        EndPaint(h, &ps);
        Render();
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_MOUSEACTIVATE:
        if (GetWindowLongPtrW(h, GWL_EXSTYLE) & WS_EX_NOACTIVATE) return MA_NOACTIVATE;
        break;
    case WM_SIZE:
        needLayout_ = true;
        Invalidate();
        return 0;
    case WM_DPICHANGED: {
        dpi_ = (float)HIWORD(wp);
        surface_.SetDpi(dpi_);
        auto* r = reinterpret_cast<RECT*>(lp);
        SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        Relayout();
        return 0;
    }
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            SetCursor(LoadCursorW(nullptr, IDC_ARROW));
            return TRUE;
        }
        break;
    case WM_MOUSEMOVE: {
        float x = GET_X_LPARAM(lp) / Scale(), y = GET_Y_LPARAM(lp) / Scale();
        EnsureLayout();
        if (!tracking_) {
            TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, h, 0};
            TrackMouseEvent(&t);
            tracking_ = true;
        }
        if (down_ >= 0 && down_ < (int)controls_.size() && controls_[down_].drag) {
            auto drag = controls_[down_].drag;
            drag(x, y, 1);
        }
        int idx = ControlAt(x, y);
        if (idx != hot_) {
            if (hot_ >= 0 && hot_ < (int)controls_.size()) controls_[hot_].hot = false;
            hot_ = idx;
            if (hot_ >= 0) controls_[hot_].hot = true;
            Invalidate();
        }
        OnMouse(msg, x, y, wp);
        return 0;
    }
    case WM_MOUSELEAVE:
        tracking_ = false;
        if (hot_ >= 0 && hot_ < (int)controls_.size()) controls_[hot_].hot = false;
        hot_ = -1;
        Invalidate();
        OnMouse(msg, -1, -1, 0);
        return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK: {
        float x = GET_X_LPARAM(lp) / Scale(), y = GET_Y_LPARAM(lp) / Scale();
        EnsureLayout();
        int idx = ControlAt(x, y);
        if (idx >= 0) {
            down_ = idx;
            controls_[idx].down = true;
            SetCapture(h);
            if (controls_[idx].drag) {
                auto drag = controls_[idx].drag;
                drag(x, y, 0);
            }
            Invalidate();
        }
        OnMouse(msg, x, y, wp);
        return 0;
    }
    case WM_LBUTTONUP: {
        float x = GET_X_LPARAM(lp) / Scale(), y = GET_Y_LPARAM(lp) / Scale();
        std::function<void()> click;
        if (down_ >= 0 && down_ < (int)controls_.size()) {
            auto& c = controls_[down_];
            c.down = false;
            if (c.drag) {
                auto drag = c.drag;
                drag(x, y, 2);
            } else if (InRect(c.rc, x, y)) {
                click = c.click;
            }
        }
        down_ = -1;
        if (GetCapture() == h) ReleaseCapture();
        Invalidate();
        OnMouse(msg, x, y, wp);
        if (click) click();   // may relayout or even close this window
        return 0;
    }
    case WM_RBUTTONUP: {
        float x = GET_X_LPARAM(lp) / Scale(), y = GET_Y_LPARAM(lp) / Scale();
        int idx = ControlAt(x, y);
        if (idx >= 0 && controls_[idx].rclick) {
            auto rc = controls_[idx].rclick;
            rc();
            return 0;
        }
        OnMouse(msg, x, y, wp);
        return 0;
    }
    case WM_CAPTURECHANGED:
        if (down_ >= 0 && down_ < (int)controls_.size()) controls_[down_].down = false;
        down_ = -1;
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

// ---------------------------------------------------------------------------------------------

namespace ui {

static D2D1_COLOR_F C(uint32_t rgb, bool enabled, float a = 1.f) { return Rgb(rgb, enabled ? a : a * 0.35f); }

void Tile(gfx::Painter& p, const Window::Control& c, Icon icon, const std::wstring& label) {
    uint32_t bg = c.down ? th::tilePress : c.hot ? th::tileHover : th::tile;
    p.FillRound(c.rc, 8, Rgb(bg));
    float iconSize = 26;
    D2D1_RECT_F ib = RectXYWH(Center(c.rc).x - iconSize / 2, c.rc.top + RH(c.rc) * 0.22f, iconSize, iconSize);
    DrawIcon(p, icon, ib, Rgb(th::text), 1.6f);
    p.Text(label, RectF(c.rc.left, ib.bottom + 4, c.rc.right, c.rc.bottom - 4), 11.5f, Rgb(th::text),
           DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
}

void RoundToggle(gfx::Painter& p, const Window::Control& c, Icon icon, bool on) {
    float d = std::min(RW(c.rc), RH(c.rc));
    auto ctr = Center(c.rc);
    uint32_t fill = on ? (c.hot ? th::toggleOnHover : th::toggleOn) : (c.hot ? th::tilePress : th::toggleOff);
    p.Circle(ctr, d / 2, C(fill, c.enabled));
    float is = d * 0.56f;
    DrawIcon(p, icon, RectXYWH(ctr.x - is / 2, ctr.y - is / 2, is, is), C(on ? th::bg : th::text2, c.enabled), 1.9f);
}

void IconButton(gfx::Painter& p, const Window::Control& c, Icon icon, uint32_t color, float iconSize, bool background) {
    if (background || c.hot || c.down)
        p.FillRound(c.rc, 6, Rgb(c.down ? th::tilePress : c.hot ? th::tileHover : th::tile));
    auto ctr = Center(c.rc);
    uint32_t col = (c.hot && color == th::text2) ? th::text : color;
    DrawIcon(p, icon, RectXYWH(ctr.x - iconSize / 2, ctr.y - iconSize / 2, iconSize, iconSize), C(col, c.enabled));
}

void Pill(gfx::Painter& p, const Window::Control& c, const std::wstring& text) {
    p.FillRound(c.rc, RH(c.rc) / 2, Rgb(c.down ? th::tilePress : c.hot ? th::tileHover : th::tile));
    float h = RH(c.rc);
    D2D1_RECT_F ib = RectXYWH(c.rc.left + 10, c.rc.top + (h - 12) / 2, 12, 12);
    p.FillRound(ib, 2, Rgb(th::text2));
    DrawIcon(p, Icon::Play, Inflate(ib, -3), Rgb(th::tile));
    p.Text(text, RectF(ib.right + 7, c.rc.top, c.rc.right - 8, c.rc.bottom), 12.5f, C(th::text, c.enabled),
           DWRITE_FONT_WEIGHT_MEDIUM);
}

void Button(gfx::Painter& p, const Window::Control& c, const std::wstring& text, uint32_t bg, uint32_t bgHover, Icon icon,
            bool withIcon) {
    p.FillRound(c.rc, 7, C(c.hot ? bgHover : bg, c.enabled));
    if (withIcon) {
        float tw = gfx::MeasureText(text, 13, DWRITE_FONT_WEIGHT_SEMI_BOLD);
        float total = 16 + 8 + tw;
        float x = Center(c.rc).x - total / 2;
        DrawIcon(p, icon, RectXYWH(x, Center(c.rc).y - 8, 16, 16), Rgb(0xffffff));
        p.Text(text, RectF(x + 24, c.rc.top, c.rc.right, c.rc.bottom), 13, Rgb(0xffffff), DWRITE_FONT_WEIGHT_SEMI_BOLD);
    } else {
        p.Text(text, c.rc, 13, C(0xffffff, c.enabled), DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER);
    }
}

void Checkbox(gfx::Painter& p, const Window::Control& c, const std::wstring& label, bool checked) {
    float y = Center(c.rc).y;
    D2D1_RECT_F box = RectXYWH(c.rc.left, y - 8, 16, 16);
    if (checked) {
        p.FillRound(box, 4, C(c.hot ? th::blueHover : th::blue, c.enabled));
        DrawIcon(p, Icon::Check, Inflate(box, -2), C(0xffffff, c.enabled), 2.6f);
    } else {
        p.FillRound(box, 4, Rgb(th::bgDeep));
        p.StrokeRound(Inflate(box, -0.5f), 4, C(c.hot ? th::text2 : th::border, c.enabled));
    }
    p.Text(label, RectF(box.right + 8, c.rc.top, c.rc.right, c.rc.bottom), 13, C(th::text, c.enabled));
}

void Dropdown(gfx::Painter& p, const Window::Control& c, const std::wstring& value) {
    p.FillRound(c.rc, 6, C(c.hot ? th::tileHover : th::tile, c.enabled));
    p.Text(value, RectF(c.rc.left + 10, c.rc.top, c.rc.right - 26, c.rc.bottom), 13, C(th::text, c.enabled));
    DrawIcon(p, Icon::Chevron, RectXYWH(c.rc.right - 22, Center(c.rc).y - 7, 14, 14), C(th::text2, c.enabled), 2);
}

void Slider(gfx::Painter& p, const Window::Control& c, float t) {
    t = std::clamp(t, 0.f, 1.f);
    float y = Center(c.rc).y, x0 = c.rc.left + 7, x1 = c.rc.right - 7;
    p.FillRound(RectF(x0, y - 2, x1, y + 2), 2, Rgb(th::tilePress));
    float x = x0 + (x1 - x0) * t;
    p.FillRound(RectF(x0, y - 2, x, y + 2), 2, C(th::blue, c.enabled));
    p.Circle({x, y}, c.hot || c.down ? 8.f : 7.f, C(0xffffff, c.enabled));
}

void Field(gfx::Painter& p, const Window::Control& c, const std::wstring& text, bool active) {
    p.FillRound(c.rc, 6, Rgb(th::bgDeep));
    p.StrokeRound(Inflate(c.rc, -0.5f), 6, Rgb(active ? th::blue : c.hot ? th::text3 : th::border), active ? 1.5f : 1.f);
    p.Text(text, RectF(c.rc.left + 10, c.rc.top, c.rc.right - 10, c.rc.bottom), 13,
           Rgb(active ? th::blue : th::text), DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER);
}

void Separator(gfx::Painter& p, float x1, float y1, float x2, float y2) { p.Line({x1, y1}, {x2, y2}, Rgb(th::sep), 1); }

}  // namespace ui
