#include "app.h"
#include "cursorfx.h"
#include "overlays.h"
#include "winutil.h"

namespace {
constexpr float kPi = 3.14159265f;
constexpr ULONGLONG kRippleMs = 550;

ComPtr<ID2D1PathGeometry> Polyline(const std::vector<D2D1_POINT_2F>& pts) {
    ComPtr<ID2D1PathGeometry> g;
    gfx::D2D()->CreatePathGeometry(&g);
    ComPtr<ID2D1GeometrySink> s;
    g->Open(&s);
    s->BeginFigure(pts[0], D2D1_FIGURE_BEGIN_HOLLOW);
    if (pts.size() == 1) s->AddLine({pts[0].x + 0.1f, pts[0].y});
    for (size_t i = 1; i < pts.size(); ++i) s->AddLine(pts[i]);
    s->EndFigure(D2D1_FIGURE_END_OPEN);
    s->Close();
    return g;
}

D2D1_RECT_F Norm(D2D1_POINT_2F a, D2D1_POINT_2F b) {
    return RectF(std::min(a.x, b.x), std::min(a.y, b.y), std::max(a.x, b.x), std::max(a.y, b.y));
}

}  // namespace

bool Canvas::Create() {
    mon_ = win::VirtualScreenRect();   // annotations and mouse effects work on every screen
    return Window::Create(L"ScreenRec Canvas", WS_POPUP,
                          WS_EX_TOPMOST | WS_EX_TOOLWINDOW, mon_, nullptr, true);
}

void Canvas::SetTool(Tool t) {
    if (typing_) CommitText();
    tool_ = t;
    Update();
}

void Canvas::Undo() {
    if (typing_) {
        typing_ = false;
        current_.reset();
    } else if (!strokes_.empty()) {
        strokes_.pop_back();
    }
    Update();
    Render();
}

void Canvas::Clear() {
    strokes_.clear();
    current_.reset();
    typing_ = false;
    Update();
    Render();
}

// Click ripples only: the yellow halo and the dot pointer are baked into the system cursor
// (see cursorfx), which has no lag. Ripples stay where the click happened, so a 16 ms poll is enough.
void Canvas::SetEffects(bool on) {
    if (on == effects_) return;
    effects_ = on;
    ripples_.clear();
    if (hwnd_) {
        if (on) SetTimer(hwnd_, 1, 16, nullptr);
        else KillTimer(hwnd_, 1);
    }
    Update();
}

void Canvas::TickEffects() {
    if (!effects_) return;
    CURSORINFO ci{sizeof(ci)};
    GetCursorInfo(&ci);
    // Ripples are centered on the cursor's visible shape (like the halo), not on the arrow's tip.
    POINT off = (ci.flags & CURSOR_SHOWING) ? cursorfx::VisualOffset(ci.hCursor) : POINT{0, 0};
    D2D1_POINT_2F c{(ci.ptScreenPos.x + off.x - mon_.left) / Scale(), (ci.ptScreenPos.y + off.y - mon_.top) / Scale()};
    bool l = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0, r = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
    ULONGLONG now = GetTickCount64();
    bool added = false;
    if (l && !lDown_) ripples_.push_back({c, now, false}), added = true;
    if (r && !rDown_) ripples_.push_back({c, now, true}), added = true;
    lDown_ = l;
    rDown_ = r;
    bool had = !ripples_.empty();
    std::erase_if(ripples_, [now](const Ripple& rp) { return now - rp.start > kRippleMs; });
    if (added || had) Render();   // one extra frame after the last ripple clears it
}
void Canvas::Update() {
    if (!hwnd_) return;
    mon_ = win::VirtualScreenRect();   // re-assert full coverage (monitor changes, DPI rescaling)
    SetWindowPos(hwnd_, nullptr, mon_.left, mon_.top, mon_.right - mon_.left, mon_.bottom - mon_.top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    bool needed = !strokes_.empty() || current_ || tool_ != Tool::Pointer || effects_;
    win::SetClickThrough(hwnd_, tool_ == Tool::Pointer);
    if (needed && !Visible()) ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    if (!needed && Visible()) Show(false);
    Invalidate();
}

void Canvas::CommitText() {
    if (current_ && current_->tool == Tool::Text && !current_->text.empty()) strokes_.push_back(*current_);
    current_.reset();
    typing_ = false;
}

LRESULT Canvas::OnMessage(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_TIMER:
        TickEffects();
        return 0;
    case WM_WINDOWPOSCHANGING: {   // never let Windows' DPI rescaling shrink the canvas
        auto* pos = reinterpret_cast<WINDOWPOS*>(lp);
        if (!(pos->flags & SWP_NOSIZE) || !(pos->flags & SWP_NOMOVE)) {
            pos->x = mon_.left;
            pos->y = mon_.top;
            pos->cx = mon_.right - mon_.left;
            pos->cy = mon_.bottom - mon_.top;
            pos->flags &= ~(SWP_NOSIZE | SWP_NOMOVE);
        }
        break;
    }
    case WM_DPICHANGED:   // spans monitors: keep covering the virtual screen
        dpi_ = (float)HIWORD(wp);
        surface_.SetDpi(dpi_);
        SetWindowPos(hwnd_, nullptr, mon_.left, mon_.top, mon_.right - mon_.left, mon_.bottom - mon_.top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            SetCursor(LoadCursorW(nullptr, tool_ == Tool::Text ? IDC_IBEAM : IDC_CROSS));
            return TRUE;
        }
        break;
    case WM_CHAR:
        if (typing_ && current_) {
            if (wp == L'\r' || wp == 27) CommitText();
            else if (wp == L'\b') {
                if (!current_->text.empty()) current_->text.pop_back();
            } else if (wp >= 32) current_->text += (wchar_t)wp;
            Render();
        }
        return 0;
    case WM_KEYDOWN:
        if (!typing_) {
            if (wp == VK_ESCAPE) {
                SetTool(Tool::Pointer);
                app().RefreshUi();
            } else if (wp == 'Z' && (GetKeyState(VK_CONTROL) & 0x8000)) {
                Undo();
            }
        }
        return 0;
    }
    return Window::OnMessage(msg, wp, lp);
}

void Canvas::OnMouse(UINT msg, float x, float y, WPARAM) {
    if (tool_ == Tool::Pointer || x < 0) return;
    D2D1_POINT_2F pt{x, y};
    switch (msg) {
    case WM_LBUTTONDOWN:
        if (tool_ == Tool::Text) {
            CommitText();
            current_ = Stroke{Tool::Text, color_, width_, {pt}};
            typing_ = true;
            SetForegroundWindow(hwnd_);
            SetFocus(hwnd_);
            Render();
            return;
        }
        if (typing_) CommitText();
        current_ = Stroke{tool_, color_, width_, {pt}};
        if (tool_ != Tool::Pen && tool_ != Tool::Highlighter) current_->pts.push_back(pt);
        SetCapture(hwnd_);
        Render();
        break;
    case WM_MOUSEMOVE:
        if (!current_ || typing_ || !(GetKeyState(VK_LBUTTON) & 0x8000)) return;
        if (current_->tool == Tool::Pen || current_->tool == Tool::Highlighter) {
            auto& last = current_->pts.back();
            if (std::abs(last.x - x) + std::abs(last.y - y) >= 1.5f) current_->pts.push_back(pt);
        } else {
            current_->pts[1] = pt;
        }
        Render();
        break;
    case WM_LBUTTONUP:
        if (!current_ || typing_) return;
        if (GetCapture() == hwnd_) ReleaseCapture();
        strokes_.push_back(std::move(*current_));
        current_.reset();
        Render();
        break;
    }
}

void Canvas::DrawStroke(gfx::Painter& p, Stroke& s) {
    auto col = Rgb(s.color, s.tool == Tool::Highlighter ? 0.38f : 1.f);
    auto* cap = gfx::RoundCapStyle();
    switch (s.tool) {
    case Tool::Pen:
    case Tool::Highlighter: {
        if (!s.geo || (current_ && &s == &*current_)) s.geo = Polyline(s.pts);
        p.dc->DrawGeometry(s.geo.Get(), p.B(col), s.tool == Tool::Highlighter ? s.width * 4 : s.width, cap);
        break;
    }
    case Tool::Line:
        p.Line(s.pts[0], s.pts[1], col, s.width, cap);
        break;
    case Tool::Arrow: {
        auto a = s.pts[0], b = s.pts[1];
        float ang = atan2f(b.y - a.y, b.x - a.x);
        float len = 10 + s.width * 3;
        D2D1_POINT_2F l{b.x - len * cosf(ang - 0.45f), b.y - len * sinf(ang - 0.45f)};
        D2D1_POINT_2F r{b.x - len * cosf(ang + 0.45f), b.y - len * sinf(ang + 0.45f)};
        D2D1_POINT_2F base{(l.x + r.x) / 2, (l.y + r.y) / 2};
        p.Line(a, base, col, s.width, cap);
        ComPtr<ID2D1PathGeometry> g;
        gfx::D2D()->CreatePathGeometry(&g);
        ComPtr<ID2D1GeometrySink> sk;
        g->Open(&sk);
        sk->BeginFigure(b, D2D1_FIGURE_BEGIN_FILLED);
        sk->AddLine(l);
        sk->AddLine(r);
        sk->EndFigure(D2D1_FIGURE_END_CLOSED);
        sk->Close();
        p.dc->FillGeometry(g.Get(), p.B(col));
        p.dc->DrawGeometry(g.Get(), p.B(col), 1.5f, cap);
        break;
    }
    case Tool::Rect:
        p.Stroke(Norm(s.pts[0], s.pts[1]), col, s.width);
        break;
    case Tool::Ellipse: {
        auto r = Norm(s.pts[0], s.pts[1]);
        p.dc->DrawEllipse({Center(r), RW(r) / 2, RH(r) / 2}, p.B(col), s.width);
        break;
    }
    case Tool::Text: {
        float size = 16 + s.width * 3;
        std::wstring t = s.text;
        bool editing = typing_ && current_ && &s == &*current_;
        if (editing) t += L"|";
        if (t.empty()) break;
        float w = gfx::MeasureText(t, size, DWRITE_FONT_WEIGHT_SEMI_BOLD) + 4;
        D2D1_RECT_F r = RectXYWH(s.pts[0].x, s.pts[0].y - size * 0.7f, w, size * 1.4f);
        if (editing) p.Stroke(Inflate(r, 4), Rgb(s.color, 0.6f), 1, gfx::DashStyle());
        p.Text(t, r, size, col, DWRITE_FONT_WEIGHT_SEMI_BOLD);
        break;
    }
    case Tool::Pointer:
        break;
    }
}

void Canvas::Paint(gfx::Painter& p) {
    p.Clear(D2D1::ColorF(0, 0, 0, 0));
    for (auto& s : strokes_) DrawStroke(p, s);
    if (current_) DrawStroke(p, *current_);

    // Click ripples: left = red, right = blue (distinct from the yellow halo on the cursor).
    if (effects_) {
        ULONGLONG now = GetTickCount64();
        for (auto& r : ripples_) {
            float t = (now - r.start) / (float)kRippleMs;
            float ease = 1 - (1 - t) * (1 - t);
            float rad = 14 + 30 * ease;
            uint32_t col = r.right ? th::blue : th::red;
            p.Circle(r.p, rad, Rgb(col, 0.25f * (1 - t)));
            p.StrokeCircle(r.p, rad, Rgb(col, 1.f - t), 4.f);
        }
    }
}

// =============================================================================================

namespace {
constexpr float kTbW = 46, kItem = 36;
struct ColorDef {
    const wchar_t* name;
    uint32_t rgb;
};
const ColorDef kColors[] = {{L"Red", th::red},      {L"Orange", 0xff9f0a}, {L"Yellow", th::yellow},
                            {L"Green", th::green},  {L"Blue", th::blue},   {L"Purple", 0xbf5af2},
                            {L"White", 0xffffff},   {L"Black", 0x000000}};
}  // namespace

bool DrawToolbar::Create() {
    frameless_ = true;
    if (!Window::Create(L"ScreenRec Drawing", WS_POPUP, WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                        {0, 0, 46, 540}, nullptr, false))
        return false;
    win::ExcludeFromCapture(hwnd_, true);
    return true;
}

void DrawToolbar::PlaceNear(RECT region) {
    float s = (float)win::DpiOf(hwnd_) / 96.f;
    int w = (int)(kTbW * s), h = (int)((kItem * 13 + 3 * 9 + 16) * s);
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(MonitorFromRect(&region, MONITOR_DEFAULTTOPRIMARY), &mi);
    RECT work = mi.rcWork;
    int x = region.left - w - (int)(12 * s);
    if (x < work.left + 8) x = region.left + (int)(12 * s);
    if (region.right - region.left <= 0) x = work.left + (int)(24 * s);
    int y = (region.top + region.bottom) / 2 - h / 2;
    if (region.bottom - region.top <= 0) y = (work.top + work.bottom) / 2 - h / 2;
    y = std::clamp<int>(y, work.top + 8, std::max<int>(work.top + 8, work.bottom - h - 8));
    SetWindowPos(hwnd_, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    Relayout();
}

void DrawToolbar::Layout() {
    Canvas& cv = app().GetCanvas();
    float y = 8, x = (kTbW - 34) / 2;
    auto item = [&](Icon icon, std::function<bool()> selected, std::function<void()> act) {
        Add(RectXYWH(x, y, 34, 34), [icon, selected](gfx::Painter& p, const Control& c) {
            bool sel = selected && selected();
            if (sel) p.FillRound(c.rc, 7, Rgb(th::blue));
            ui::IconButton(p, c, icon, sel ? 0xffffff : th::text2, 18);
        }, act);
        y += kItem;
    };
    auto sep = [&] {
        float yy = y + 4;
        Add(RectF(8, yy, kTbW - 8, yy + 1),
            [](gfx::Painter& p, const Control& c) { ui::Separator(p, c.rc.left, c.rc.top, c.rc.right, c.rc.top); });
        y += 9;
    };
    auto tool = [&](Icon icon, Tool t) {
        item(icon, [&cv, t] { return cv.GetTool() == t; }, [this, &cv, t] {
            cv.SetTool(t);
            Invalidate();
        });
    };
    tool(Icon::Pointer, Tool::Pointer);
    tool(Icon::Pen, Tool::Pen);
    tool(Icon::Highlighter, Tool::Highlighter);
    tool(Icon::Line, Tool::Line);
    tool(Icon::Arrow, Tool::Arrow);
    tool(Icon::Rect, Tool::Rect);
    tool(Icon::Ellipse, Tool::Ellipse);
    tool(Icon::Text, Tool::Text);
    sep();
    item(Icon::Width, nullptr, [this, &cv, yy = y] {
        const float widths[] = {2, 4, 8, 14};
        const wchar_t* names[] = {L"Thin", L"Medium", L"Thick", L"Extra thick"};
        std::vector<win::MenuItem> items;
        for (int i = 0; i < 4; ++i) items.push_back({i + 1, names[i], cv.StrokeWidth() == widths[i]});
        int id = win::PopupMenu(hwnd_, ToScreen(kTbW, yy), items);
        if (id > 0) cv.SetWidth(widths[id - 1]);
    });
    // Color swatch.
    Add(RectXYWH(x, y, 34, 34),
        [&cv](gfx::Painter& p, const Control& c) {
            if (c.hot) p.FillRound(c.rc, 7, Rgb(th::tileHover));
            auto ctr = Center(c.rc);
            p.Circle(ctr, 9, Rgb(cv.Color()));
            p.StrokeCircle(ctr, 9, Rgb(0xffffff, 0.8f), 1.5f);
        },
        [this, &cv, yy = y] {
            std::vector<win::MenuItem> items;
            for (int i = 0; i < (int)std::size(kColors); ++i)
                items.push_back({i + 1, kColors[i].name, cv.Color() == kColors[i].rgb});
            int id = win::PopupMenu(hwnd_, ToScreen(kTbW, yy), items);
            if (id > 0) {
                cv.SetColor(kColors[id - 1].rgb);
                Invalidate();
            }
        });
    y += kItem;
    sep();
    item(Icon::Undo, nullptr, [&cv] { cv.Undo(); });
    item(Icon::Trash, nullptr, [&cv] { cv.Clear(); });
    sep();
    item(Icon::Close, nullptr, [] { app().ToggleDrawing(); });
}

void DrawToolbar::Paint(gfx::Painter& p) {
    p.Clear(Rgb(th::bg));
    PaintControls(p);
}

LRESULT DrawToolbar::HitTest(float x, float y) {
    for (auto& c : controls_)
        if (c.click && InRect(c.rc, x, y)) return HTCLIENT;
    return HTCAPTION;
}
