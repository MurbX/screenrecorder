#include "editor.h"
#include "app.h"
#include "dragdrop.h"
#include "screenshot.h"
#include "settings.h"
#include "winutil.h"

namespace {
constexpr float kBar = 52, kStatus = 26, kBtn = 34;

using Tool = ImageEditor::Tool;

D2D1_RECT_F Norm(D2D1_POINT_2F a, D2D1_POINT_2F b) {
    return RectF(std::min(a.x, b.x), std::min(a.y, b.y), std::max(a.x, b.x), std::max(a.y, b.y));
}
float Dist(D2D1_POINT_2F a, D2D1_POINT_2F b) { return std::hypot(b.x - a.x, b.y - a.y); }
D2D1_RECT_F Union(D2D1_RECT_F a, D2D1_RECT_F b) {
    return RectF(std::min(a.left, b.left), std::min(a.top, b.top), std::max(a.right, b.right), std::max(a.bottom, b.bottom));
}
bool IsLight(uint32_t rgb) {
    float l = 0.299f * ((rgb >> 16) & 0xff) + 0.587f * ((rgb >> 8) & 0xff) + 0.114f * (rgb & 0xff);
    return l > 165;
}

struct ColorDef {
    const wchar_t* name;
    uint32_t rgb;
};
const ColorDef kColors[] = {{L"Red", th::red},      {L"Orange", 0xff9f0a}, {L"Yellow", th::yellow}, {L"Green", th::green},
                            {L"Blue", th::blue},    {L"Purple", 0xbf5af2}, {L"Pink", 0xff375f},    {L"White", 0xffffff},
                            {L"Black", 0x1c1c1e}};

struct ToolDef {
    Tool tool;
    Icon icon;
    const wchar_t* tip;
    wchar_t key;
};
const ToolDef kTools[] = {
    {Tool::Select, Icon::Pointer, L"Select & move  (V)", 'V'},  {Tool::Arrow, Icon::Arrow, L"Arrow  (A)", 'A'},
    {Tool::Line, Icon::Line, L"Line  (L)", 'L'},                {Tool::Rect, Icon::Rect, L"Rectangle  (R)", 'R'},
    {Tool::Ellipse, Icon::Ellipse, L"Ellipse  (E)", 'E'},       {Tool::Pen, Icon::Pen, L"Pen  (P)", 'P'},
    {Tool::Highlighter, Icon::Highlighter, L"Highlighter  (H)", 'H'}, {Tool::Text, Icon::Text, L"Text  (T)", 'T'},
    {Tool::Step, Icon::Step, L"Numbered step  (N)", 'N'},       {Tool::Callout, Icon::Callout, L"Callout bubble  (K)", 'K'},
    {Tool::Spotlight, Icon::Spotlight, L"Spotlight  (S)", 'S'}, {Tool::Magnify, Icon::Magnify, L"Magnify  (M)", 'M'},
    {Tool::Blur, Icon::Blur, L"Blur  (B)", 'B'},                {Tool::Pixelate, Icon::Pixelate, L"Pixelate  (X)", 'X'},
    {Tool::Crop, Icon::Crop, L"Crop  (C)", 'C'},
};

const wchar_t* Hint(Tool t) {
    switch (t) {
    case Tool::Select: return L"Click a mark to select it · drag to move · Delete removes it";
    case Tool::Arrow: return L"Drag to draw an arrow · hold Shift for straight angles";
    case Tool::Line: return L"Drag to draw a line · hold Shift for straight angles";
    case Tool::Rect: return L"Drag to draw a rectangle · Shift for a square";
    case Tool::Ellipse: return L"Drag to draw an ellipse · Shift for a circle";
    case Tool::Pen: return L"Draw freehand";
    case Tool::Highlighter: return L"Drag over text to highlight it";
    case Tool::Text: return L"Click where the text should go, then type · Enter to finish";
    case Tool::Step: return L"Click to place the next number";
    case Tool::Callout: return L"Drag from the point you mean to where the bubble should go, then type";
    case Tool::Spotlight: return L"Drag around what matters — everything else dims";
    case Tool::Magnify: return L"Drag from a detail outward to zoom in on it";
    case Tool::Blur: return L"Drag over anything private to blur it";
    case Tool::Pixelate: return L"Drag over anything private to pixelate it";
    case Tool::Crop: return L"Drag to choose the part to keep · Esc removes the crop";
    }
    return L"";
}

// Separable box blur (3 passes ~ gaussian) on BGRA pixels.
void BoxBlur(std::vector<uint32_t>& px, int w, int h, int r) {
    std::vector<uint32_t> tmp(px.size());
    auto pass = [&](std::vector<uint32_t>& src, std::vector<uint32_t>& dst, bool horizontal) {
        int n = horizontal ? w : h, lines = horizontal ? h : w;
        for (int line = 0; line < lines; ++line) {
            auto at = [&](int i) -> uint32_t& {
                i = std::clamp(i, 0, n - 1);
                return horizontal ? src[(size_t)line * w + i] : src[(size_t)i * w + line];
            };
            int sum[3] = {0, 0, 0};
            for (int i = -r; i <= r; ++i)
                for (int c = 0; c < 3; ++c) sum[c] += (at(i) >> (c * 8)) & 0xff;
            int win = 2 * r + 1;
            for (int i = 0; i < n; ++i) {
                uint32_t v = 0xff000000;
                for (int c = 0; c < 3; ++c) v |= (uint32_t)(sum[c] / win) << (c * 8);
                (horizontal ? dst[(size_t)line * w + i] : dst[(size_t)i * w + line]) = v;
                uint32_t out = at(i - r), in = at(i + r + 1);
                for (int c = 0; c < 3; ++c) sum[c] += (int)((in >> (c * 8)) & 0xff) - (int)((out >> (c * 8)) & 0xff);
            }
        }
    };
    for (int k = 0; k < 3; ++k) {
        pass(px, tmp, true);
        pass(tmp, px, false);
    }
}
}  // namespace

// =============================================================================================
// Window

bool ImageEditor::Open(const std::wstring& path) {
    if (hwnd_ && Visible() && dirty_ && path != path_ && !ConfirmClose()) return false;
    if (!Load(path)) {
        win::Error(nullptr, L"Could not open the image:\n" + path);
        return false;
    }
    if (!hwnd_) {
        frameless_ = true;
        if (!Window::Create(L"ScreenRec Editor",
                            WS_POPUP | WS_THICKFRAME | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX,
                            WS_EX_APPWINDOW, {0, 0, 1200, 800}, nullptr, false))
            return false;
    }
    // Size to the image (at most 100% of its pixels), within 88% of the screen.
    POINT pt;
    GetCursorPos(&pt);
    RECT work = win::MonitorAt(pt).work;
    float s = (float)win::DpiOf(hwnd_) / 96.f;
    float ww = (work.right - work.left) * 0.88f, wh = (work.bottom - work.top) * 0.88f;
    int w = (int)std::clamp(iw_ + 48 * s, 960 * s, ww);
    int h = (int)std::clamp(ih_ + (kBar + kStatus + 48) * s, 600 * s, wh);
    if (IsZoomed(hwnd_)) ShowWindow(hwnd_, SW_RESTORE);
    SetWindowPos(hwnd_, nullptr, (work.left + work.right - w) / 2, (work.top + work.bottom - h) / 2, w, h,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowTextW(hwnd_, (FileName(path) + L" — ScreenRec").c_str());
    win::ExcludeFromCapture(hwnd_, S().excludeSelf);
    Show(true);
    SetForegroundWindow(hwnd_);
    SetFocus(hwnd_);
    Status(L"Pick a tool above to mark up the screenshot");
    Relayout();
    return true;
}

bool ImageEditor::Load(const std::wstring& path) {
    std::vector<uint32_t> px;
    int w = 0, h = 0;
    if (!LoadImageFile(path, px, w, h)) return false;
    path_ = path;
    px_ = std::move(px);
    iw_ = w;
    ih_ = h;
    img_.Reset();
    imgDc_ = nullptr;
    shapes_.clear();
    crop_ = {};
    nextStep_ = 1;
    undo_.clear();
    redo_.clear();
    dirty_ = false;
    sel_ = editing_ = -1;
    drawing_ = moving_ = cropping_ = false;
    return true;
}

float ImageEditor::Unit() const { return std::max(1.f, std::max(iw_, ih_) / 1100.f); }

RECT ImageEditor::CropOrFull() const {
    if (crop_.right - crop_.left >= 4 && crop_.bottom - crop_.top >= 4) return crop_;
    return {0, 0, iw_, ih_};
}

D2D1_RECT_F ImageEditor::CanvasRect() const { return RectF(0, kBar, W(), H() - kStatus); }

void ImageEditor::UpdateView() {
    D2D1_RECT_F c = Inflate(CanvasRect(), -24);
    if (iw_ <= 0 || ih_ <= 0 || RW(c) <= 0 || RH(c) <= 0) return;
    k_ = std::min({RW(c) / iw_, RH(c) / ih_, 1.f / Scale()});   // never upscale past 100%
    off_ = {Center(c).x - iw_ * k_ / 2, Center(c).y - ih_ * k_ / 2};
}

D2D1_POINT_2F ImageEditor::ToImage(float x, float y) const { return {(x - off_.x) / k_, (y - off_.y) / k_}; }

void ImageEditor::Status(const std::wstring& s) {
    status_ = s;
    statusUntil_ = GetTickCount64() + 2500;
    Invalidate();
}

LRESULT ImageEditor::HitTest(float x, float y) {
    if (!IsZoomed(hwnd_)) {   // invisible resize borders
        const float e = 6;
        bool l = x < e, r = x > W() - e, t = y < e, b = y > H() - e;
        if (t && l) return HTTOPLEFT;
        if (t && r) return HTTOPRIGHT;
        if (b && l) return HTBOTTOMLEFT;
        if (b && r) return HTBOTTOMRIGHT;
        if (l) return HTLEFT;
        if (r) return HTRIGHT;
        if (t) return HTTOP;
        if (b) return HTBOTTOM;
    }
    if (y < kBar) {
        for (auto& c : controls_)
            if ((c.click || c.drag) && InRect(c.rc, x, y)) return HTCLIENT;
        return HTCAPTION;   // drag the window by the toolbar background
    }
    return HTCLIENT;
}

// =============================================================================================
// Toolbar

void ImageEditor::Layout() {
    tips_.clear();
    auto add = [&](D2D1_RECT_F rc, std::function<void(gfx::Painter&, const Control&)> paint, std::function<void()> click,
                   const std::wstring& tip) -> Control& {
        tips_.push_back(tip);
        return Add(rc, std::move(paint), std::move(click));
    };
    float x = 12, y = (kBar - kBtn) / 2;
    for (auto& t : kTools) {
        Tool tool = t.tool;
        Icon icon = t.icon;
        add(RectXYWH(x, y, kBtn, kBtn), [this, tool, icon](gfx::Painter& p, const Control& c) {
            bool on = tool_ == tool;
            if (on) p.FillRound(c.rc, 8, Rgb(th::blue));
            ui::IconButton(p, c, icon, on ? 0xffffff : th::text2, 18);
        }, [this, tool] { SetTool(tool); }, t.tip);
        x += kBtn + 2;
    }
    x += 10;
    // Color
    add(RectXYWH(x, y, kBtn, kBtn), [this](gfx::Painter& p, const Control& c) {
        if (c.hot) p.FillRound(c.rc, 8, Rgb(th::tileHover));
        auto ctr = Center(c.rc);
        p.Circle(ctr, 9, Rgb(color_));
        p.StrokeCircle(ctr, 9, Rgb(0xffffff, 0.85f), 1.5f);
    }, [this, x] {
        std::vector<win::MenuItem> items;
        for (int i = 0; i < (int)std::size(kColors); ++i) items.push_back({i + 1, kColors[i].name, color_ == kColors[i].rgb});
        int id = win::PopupMenu(hwnd_, ToScreen(x, kBar), items);
        if (id <= 0) return;
        color_ = kColors[id - 1].rgb;
        if (sel_ >= 0) {
            Commit();
            shapes_[sel_].color = color_;
        }
        Invalidate();
    }, L"Color");
    x += kBtn + 2;
    // Thickness
    add(RectXYWH(x, y, kBtn, kBtn), [](gfx::Painter& p, const Control& c) { ui::IconButton(p, c, Icon::Width, th::text2, 18); },
        [this, x] {
            int id = win::PopupMenu(hwnd_, ToScreen(x, kBar),
                                    {{1, L"Thin", widthLevel_ == 0}, {2, L"Medium", widthLevel_ == 1}, {3, L"Thick", widthLevel_ == 2}});
            if (id <= 0) return;
            widthLevel_ = id - 1;
            if (sel_ >= 0) {
                Commit();
                shapes_[sel_].width = (widthLevel_ == 0 ? 1.5f : widthLevel_ == 1 ? 3.f : 5.5f) * Unit();
            }
            Invalidate();
        }, L"Thickness / size");
    x += kBtn + 2;
    // Fill (rectangles, ellipses)
    add(RectXYWH(x, y, kBtn, kBtn), [this](gfx::Painter& p, const Control& c) {
        if (filled_) p.FillRound(c.rc, 8, Rgb(th::tileHover));
        ui::IconButton(p, c, Icon::Fill, filled_ ? 0xffffff : th::text2, 18);
    }, [this] {
        filled_ = !filled_;
        if (sel_ >= 0 && (shapes_[sel_].tool == Tool::Rect || shapes_[sel_].tool == Tool::Ellipse)) {
            Commit();
            shapes_[sel_].filled = filled_;
        }
        Invalidate();
    }, L"Filled shapes");
    x += kBtn + 12;
    auto& u = add(RectXYWH(x, y, kBtn, kBtn), [](gfx::Painter& p, const Control& c) { ui::IconButton(p, c, Icon::Undo, th::text2, 18); },
                  [this] { Undo(); }, L"Undo  (Ctrl+Z)");
    u.enabled = !undo_.empty();
    x += kBtn + 2;
    auto& r = add(RectXYWH(x, y, kBtn, kBtn), [](gfx::Painter& p, const Control& c) { ui::IconButton(p, c, Icon::Redo, th::text2, 18); },
                  [this] { Redo(); }, L"Redo  (Ctrl+Y)");
    r.enabled = !redo_.empty();

    // Right side: copy, copy path, save, drag out, close.
    float rx = W() - 12 - kBtn;
    add(RectXYWH(rx, y, kBtn, kBtn), [](gfx::Painter& p, const Control& c) { ui::IconButton(p, c, Icon::Close, th::text2, 14); },
        [this] { SendMessageW(hwnd_, WM_CLOSE, 0, 0); }, L"Close");
    rx -= kBtn + 14;
    auto& grip = add(RectXYWH(rx, y, kBtn, kBtn), [](gfx::Painter& p, const Control& c) {
        p.FillRound(c.rc, 8, Rgb(c.hot || c.down ? th::tilePress : th::tile));
        DrawIcon(p, Icon::Grip, RectXYWH(Center(c.rc).x - 9, Center(c.rc).y - 9, 18, 18), Rgb(th::text));
    }, nullptr, L"Drag into any app or folder to share");
    grip.drag = [this, start = POINT{}, dragged = false](float, float, int phase) mutable {
        POINT pt;
        GetCursorPos(&pt);
        if (phase == 0) {
            start = pt;
            dragged = false;
        } else if (phase == 1 && !dragged && PastDragThreshold(start, pt)) {
            dragged = true;
            if (GetCapture() == hwnd_) ReleaseCapture();
            DragOut();
        } else if (phase == 2 && !dragged) {
            Status(L"Drag this handle into a chat, a browser or a folder");
        }
    };
    rx -= kBtn + 2;
    add(RectXYWH(rx, y, kBtn, kBtn), [](gfx::Painter& p, const Control& c) { ui::IconButton(p, c, Icon::Save, th::text2, 18); },
        [this, rx] {
            int id = win::PopupMenu(hwnd_, ToScreen(rx, kBar),
                                    {{1, L"Save  (Ctrl+S)"}, {2, L"Save as a copy"}, {0}, {3, L"Show in folder"}, {4, L"Open in default app"}});
            if (id == 1) SaveInPlace();
            else if (id == 2) {
                std::wstring base = StripExt(path_) + L"_edited", ext = L"." + FileExt(path_), dst = base + ext;
                for (int n = 2; FileExists(dst); ++n) dst = base + std::to_wstring(n) + ext;
                if (Save(dst)) Status(L"Saved a copy: " + FileName(dst));
            } else if (id == 3) win::ShowInFolder(path_);
            else if (id == 4) {
                if (dirty_) SaveInPlace();
                win::OpenWithShell(path_);
            }
        }, L"Save");
    rx -= kBtn + 2;
    add(RectXYWH(rx, y, kBtn, kBtn), [](gfx::Painter& p, const Control& c) { ui::IconButton(p, c, Icon::Link, th::text2, 18); },
        [this] { CopyPath(); }, L"Copy file path");
    rx -= kBtn + 2;
    add(RectXYWH(rx, y, kBtn, kBtn), [](gfx::Painter& p, const Control& c) { ui::IconButton(p, c, Icon::Copy, th::text2, 18); },
        [this] { Copy(); }, L"Copy image  (Ctrl+C)");
}

// =============================================================================================
// Painting

void ImageEditor::EnsurePatch(Shape& s, ID2D1DeviceContext* dc) {
    D2D1_RECT_F r = Norm(s.pts[0], s.pts[1]);
    int x0 = std::clamp((int)r.left, 0, iw_), y0 = std::clamp((int)r.top, 0, ih_);
    int x1 = std::clamp((int)std::ceil(r.right), 0, iw_), y1 = std::clamp((int)std::ceil(r.bottom), 0, ih_);
    D2D1_RECT_F key = RectF((float)x0, (float)y0, (float)x1, (float)y1);
    if (s.patch && s.patchDc == dc && memcmp(&key, &s.patchFor, sizeof(key)) == 0) return;
    s.patch.Reset();
    s.patchDc = dc;
    s.patchFor = key;
    int w = x1 - x0, h = y1 - y0;
    if (w <= 0 || h <= 0) return;
    std::vector<uint32_t> px((size_t)w * h);
    for (int y = 0; y < h; ++y) memcpy(&px[(size_t)y * w], &px_[(size_t)(y0 + y) * iw_ + x0], (size_t)w * 4);
    if (s.tool == Tool::Pixelate) {
        int b = std::max((int)(9 * Unit()), std::max(w, h) / 16);
        for (int by = 0; by < h; by += b)
            for (int bx = 0; bx < w; bx += b) {
                int ex = std::min(bx + b, w), ey = std::min(by + b, h);
                uint64_t sum[3] = {0, 0, 0}, n = 0;
                for (int y = by; y < ey; ++y)
                    for (int x = bx; x < ex; ++x, ++n)
                        for (int c = 0; c < 3; ++c) sum[c] += (px[(size_t)y * w + x] >> (c * 8)) & 0xff;
                uint32_t v = 0xff000000;
                for (int c = 0; c < 3; ++c) v |= (uint32_t)(sum[c] / n) << (c * 8);
                for (int y = by; y < ey; ++y)
                    for (int x = bx; x < ex; ++x) px[(size_t)y * w + x] = v;
            }
    } else {
        BoxBlur(px, w, h, std::max(4, (int)(7 * Unit())));
    }
    dc->CreateBitmap({(UINT32)w, (UINT32)h}, px.data(), w * 4,
                     D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE)), &s.patch);
}

void ImageEditor::DrawShape(gfx::Painter& p, Shape& s, ID2D1Bitmap* img, bool exporting) {
    if (s.pts.empty()) return;
    auto col = Rgb(s.color);
    float w = s.width, u = Unit();
    auto* cap = gfx::RoundCapStyle();
    D2D1_POINT_2F a = s.pts[0], b = s.pts.size() > 1 ? s.pts[1] : s.pts[0];
    switch (s.tool) {
    case Tool::Arrow: {
        float len = w * 3.4f + 5 * u, ang = atan2f(b.y - a.y, b.x - a.x);
        D2D1_POINT_2F l{b.x - len * cosf(ang - 0.42f), b.y - len * sinf(ang - 0.42f)};
        D2D1_POINT_2F r{b.x - len * cosf(ang + 0.42f), b.y - len * sinf(ang + 0.42f)};
        D2D1_POINT_2F base{(l.x + r.x) / 2, (l.y + r.y) / 2};
        if (Dist(a, b) > len) p.Line(a, base, col, w, cap);
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
        p.dc->DrawGeometry(g.Get(), p.B(col), w * 0.5f, cap);
        s.bounds = Inflate(Norm(a, b), len);
        break;
    }
    case Tool::Line:
        p.Line(a, b, col, w, cap);
        s.bounds = Inflate(Norm(a, b), w + 4 * u);
        break;
    case Tool::Rect: {
        D2D1_RECT_F r = Norm(a, b);
        if (s.filled) p.Fill(r, col);
        else p.Stroke(r, col, w);   // square corners
        s.bounds = Inflate(r, w);
        break;
    }
    case Tool::Ellipse: {
        D2D1_RECT_F r = Norm(a, b);
        D2D1_ELLIPSE e{Center(r), RW(r) / 2, RH(r) / 2};
        if (s.filled) p.dc->FillEllipse(e, p.B(col));
        else p.dc->DrawEllipse(e, p.B(col), w);
        s.bounds = Inflate(r, w);
        break;
    }
    case Tool::Pen:
    case Tool::Highlighter: {
        ComPtr<ID2D1PathGeometry> g;
        gfx::D2D()->CreatePathGeometry(&g);
        ComPtr<ID2D1GeometrySink> sk;
        g->Open(&sk);
        sk->BeginFigure(s.pts[0], D2D1_FIGURE_BEGIN_HOLLOW);
        if (s.pts.size() == 1) sk->AddLine({s.pts[0].x + 0.1f, s.pts[0].y});
        for (size_t i = 1; i < s.pts.size(); ++i) sk->AddLine(s.pts[i]);
        sk->EndFigure(D2D1_FIGURE_END_OPEN);
        sk->Close();
        bool hl = s.tool == Tool::Highlighter;
        float sw = hl ? w * 4 : w;
        p.dc->DrawGeometry(g.Get(), p.B(Rgb(s.color, hl ? 0.4f : 1.f)), sw, cap);
        D2D1_RECT_F bb{};
        g->GetBounds(nullptr, &bb);
        s.bounds = Inflate(bb, sw / 2 + 2 * u);
        break;
    }
    case Tool::Text: {
        float size = w * 5.5f + 8 * u;
        bool editing = !exporting && editing_ >= 0 && &shapes_[editing_] == &s;
        std::wstring t = s.text;
        float tw = gfx::MeasureText(t.empty() ? L" " : t, size, DWRITE_FONT_WEIGHT_BOLD) + size * 0.3f;
        D2D1_RECT_F r = RectXYWH(a.x, a.y - size * 0.75f, tw, size * 1.5f);
        // Outline in a contrasting color so the text reads on any background.
        auto outline = IsLight(s.color) ? D2D1::ColorF(0, 0, 0, 0.75f) : D2D1::ColorF(1, 1, 1, 0.9f);
        float o = std::max(1.f, size / 18);
        if (!t.empty())
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx)
                    if (dx || dy) p.Text(t, RectXYWH(r.left + dx * o, r.top + dy * o, RW(r) + 4, RH(r)), size, outline, DWRITE_FONT_WEIGHT_BOLD);
        p.Text(t, RectXYWH(r.left, r.top, RW(r) + 4, RH(r)), size, col, DWRITE_FONT_WEIGHT_BOLD);
        if (editing) {   // caret + dashed box
            float cx = r.left + (t.empty() ? 0 : gfx::MeasureText(t, size, DWRITE_FONT_WEIGHT_BOLD)) + 2;
            p.Line({cx, r.top + size * 0.2f}, {cx, r.bottom - size * 0.2f}, col, std::max(1.5f, u * 1.5f));
            p.Stroke(Inflate(r, 4 * u), Rgb(th::blue, 0.8f), 1.5f * u, gfx::DashStyle());
        }
        s.bounds = r;
        break;
    }
    case Tool::Step: {
        float rad = w * 3.4f + 7 * u;
        p.Circle(a, rad, col);
        p.StrokeCircle(a, rad, Rgb(0xffffff, 0.95f), std::max(1.5f, rad * 0.12f));
        p.Text(std::to_wstring(s.number), RectXYWH(a.x - rad, a.y - rad, rad * 2, rad * 2), rad * 1.1f,
               Rgb(IsLight(s.color) ? 0x1c1c1e : 0xffffff), DWRITE_FONT_WEIGHT_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER);
        s.bounds = Inflate(RectXYWH(a.x, a.y, 0, 0), rad);
        break;
    }
    case Tool::Callout: {
        float size = w * 4.5f + 7 * u, pad = size * 0.6f;
        bool editing = !exporting && editing_ >= 0 && &shapes_[editing_] == &s;
        std::wstring t = s.text.empty() && !editing ? L"…" : s.text;
        float tw = std::max(size * 2, gfx::MeasureText(t.empty() ? L" " : t, size, DWRITE_FONT_WEIGHT_SEMI_BOLD));
        D2D1_RECT_F box = RectXYWH(b.x - (tw + 2 * pad) / 2, b.y - (size * 1.4f + 2 * pad) / 2, tw + 2 * pad, size * 1.4f + 2 * pad);
        // Tail from the bubble toward the point being described.
        auto c = Center(box);
        float ang = atan2f(a.y - c.y, a.x - c.x), bw = std::min(RW(box), RH(box)) * 0.22f;
        D2D1_POINT_2F p1{c.x + bw * cosf(ang + 1.5708f), c.y + bw * sinf(ang + 1.5708f)};
        D2D1_POINT_2F p2{c.x + bw * cosf(ang - 1.5708f), c.y + bw * sinf(ang - 1.5708f)};
        ComPtr<ID2D1PathGeometry> g;
        gfx::D2D()->CreatePathGeometry(&g);
        ComPtr<ID2D1GeometrySink> sk;
        g->Open(&sk);
        sk->BeginFigure(p1, D2D1_FIGURE_BEGIN_FILLED);
        sk->AddLine(a);
        sk->AddLine(p2);
        sk->EndFigure(D2D1_FIGURE_END_CLOSED);
        sk->Close();
        p.dc->FillGeometry(g.Get(), p.B(col));
        p.FillRound(box, pad, col);
        uint32_t tc = IsLight(s.color) ? 0x1c1c1e : 0xffffff;
        p.Text(t, Inflate(box, -pad * 0.5f), size, Rgb(tc), DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER);
        if (editing) {
            float tx = Center(box).x + (s.text.empty() ? 0 : gfx::MeasureText(s.text, size, DWRITE_FONT_WEIGHT_SEMI_BOLD) / 2) + 2;
            p.Line({tx, box.top + pad}, {tx, box.bottom - pad}, Rgb(tc), std::max(1.5f, u * 1.5f));
        }
        s.bounds = Union(box, RectXYWH(a.x, a.y, 1, 1));
        break;
    }
    case Tool::Blur:
    case Tool::Pixelate: {
        EnsurePatch(s, p.dc);
        if (s.patch) p.dc->DrawBitmap(s.patch.Get(), s.patchFor, 1.f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        s.bounds = s.patchFor;
        break;
    }
    case Tool::Spotlight: {
        D2D1_RECT_F r = Norm(a, b);
        auto dim = D2D1::ColorF(0, 0, 0, 0.55f);
        p.Fill(RectF(0, 0, (float)iw_, r.top), dim);
        p.Fill(RectF(0, r.bottom, (float)iw_, (float)ih_), dim);
        p.Fill(RectF(0, r.top, r.left, r.bottom), dim);
        p.Fill(RectF(r.right, r.top, (float)iw_, r.bottom), dim);
        s.bounds = r;
        break;
    }
    case Tool::Magnify: {
        float R = std::max(24 * u, Dist(a, b));
        D2D1_RECT_F dst = RectXYWH(a.x - R, a.y - R, 2 * R, 2 * R);
        D2D1_RECT_F src = RectXYWH(a.x - R / 2, a.y - R / 2, R, R);   // 2x zoom
        p.Circle(a, R + 6 * u, D2D1::ColorF(0, 0, 0, 0.25f));
        ComPtr<ID2D1EllipseGeometry> clip;
        gfx::D2D()->CreateEllipseGeometry({a, R, R}, &clip);
        p.dc->PushLayer(D2D1::LayerParameters1(D2D1::InfiniteRect(), clip.Get()), nullptr);
        if (img) p.dc->DrawBitmap(img, dst, 1.f, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC, src);
        p.dc->PopLayer();
        p.StrokeCircle(a, R, Rgb(0xffffff), std::max(2.f, 3 * u));
        s.bounds = Inflate(dst, 6 * u);
        break;
    }
    case Tool::Select:
    case Tool::Crop:
        break;
    }
}

void ImageEditor::DrawDoc(gfx::Painter& p, ID2D1Bitmap* img, bool exporting) {
    if (img) p.dc->DrawBitmap(img, RectF(0, 0, (float)iw_, (float)ih_), 1.f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    for (auto& s : shapes_) DrawShape(p, s, img, exporting);
}

void ImageEditor::Paint(gfx::Painter& p) {
    UpdateView();
    p.Clear(Rgb(0x161618));
    if (!img_ || imgDc_ != p.dc) {
        img_.Reset();
        if (!px_.empty())
            p.dc->CreateBitmap({(UINT32)iw_, (UINT32)ih_}, px_.data(), iw_ * 4,
                               D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE)), &img_);
        imgDc_ = p.dc;
    }
    D2D1_RECT_F ir = RectXYWH(off_.x, off_.y, iw_ * k_, ih_ * k_);
    p.Shadow(ir, 3, 12, 0.5f);
    D2D1_MATRIX_3X2_F old;
    p.dc->GetTransform(&old);
    p.dc->SetTransform(D2D1::Matrix3x2F::Scale(k_, k_) * D2D1::Matrix3x2F::Translation(off_.x, off_.y) * old);
    p.dc->PushAxisAlignedClip(RectF(0, 0, (float)iw_, (float)ih_), D2D1_ANTIALIAS_MODE_ALIASED);
    DrawDoc(p, img_.Get(), false);
    // Crop preview: dim what will be cut away.
    RECT c = CropOrFull();
    bool cropped = c.right - c.left != iw_ || c.bottom - c.top != ih_;
    if (cropped || cropping_) {
        D2D1_RECT_F r = RectF((float)c.left, (float)c.top, (float)c.right, (float)c.bottom);
        auto dim = D2D1::ColorF(0, 0, 0, 0.62f);
        p.Fill(RectF(0, 0, (float)iw_, r.top), dim);
        p.Fill(RectF(0, r.bottom, (float)iw_, (float)ih_), dim);
        p.Fill(RectF(0, r.top, r.left, r.bottom), dim);
        p.Fill(RectF(r.right, r.top, (float)iw_, r.bottom), dim);
        p.Stroke(r, Rgb(0xffffff), 1.5f / k_, gfx::DashStyle());
    }
    if (sel_ >= 0 && sel_ < (int)shapes_.size() && editing_ != sel_)
        p.Stroke(Inflate(shapes_[sel_].bounds, 4 / k_), Rgb(th::blue), 1.5f / k_, gfx::DashStyle());
    p.dc->PopAxisAlignedClip();
    p.dc->SetTransform(old);

    // Toolbar and status bar on top.
    p.Fill(RectF(0, 0, W(), kBar), Rgb(th::bg));
    ui::Separator(p, 0, kBar, W(), kBar);
    p.Fill(RectF(0, H() - kStatus, W(), H()), Rgb(th::bg));
    ui::Separator(p, 0, H() - kStatus, W(), H() - kStatus);
    std::wstring left = Hint(tool_);
    for (size_t i = 0; i < controls_.size() && i < tips_.size(); ++i)
        if (controls_[i].hot) left = tips_[i];
    if (GetTickCount64() < statusUntil_) left = status_;
    p.Text(left, RectF(14, H() - kStatus, W() * 0.65f, H()), 12, Rgb(th::text2));
    std::wstring right = Fmt(L"%d × %d  ·  %d%%  ·  ", c.right - c.left, c.bottom - c.top,
                             (int)std::lround(k_ * Scale() * 100)) +
                         (dirty_ ? L"Unsaved changes" : L"Saved");
    p.Text(right, RectF(W() * 0.5f, H() - kStatus, W() - 14, H()), 12, Rgb(dirty_ ? th::yellow : th::text3),
           DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_TRAILING);
    PaintControls(p);
}

// =============================================================================================
// Export

bool ImageEditor::Export(std::vector<uint32_t>& out, int& w, int& h) {
    CommitText();
    RECT c = CropOrFull();
    w = c.right - c.left;
    h = c.bottom - c.top;
    ComPtr<ID2D1DeviceContext> dc;
    if (FAILED(gfx::D2DDevice()->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &dc))) return false;
    auto fmt = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
    ComPtr<ID2D1Bitmap1> target, cpu;
    if (FAILED(dc->CreateBitmap({(UINT32)w, (UINT32)h}, nullptr, 0, D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET, fmt), &target)) ||
        FAILED(dc->CreateBitmap({(UINT32)w, (UINT32)h}, nullptr, 0,
                                D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW, fmt), &cpu)))
        return false;
    ComPtr<ID2D1Bitmap> img;
    dc->CreateBitmap({(UINT32)iw_, (UINT32)ih_}, px_.data(), iw_ * 4,
                     D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE)), &img);
    ComPtr<ID2D1SolidColorBrush> brush;
    dc->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1), &brush);
    dc->SetTarget(target.Get());
    dc->SetDpi(96, 96);
    dc->BeginDraw();
    dc->Clear(D2D1::ColorF(0, 0, 0, 1));
    dc->SetTransform(D2D1::Matrix3x2F::Translation((float)-c.left, (float)-c.top));
    gfx::Painter p{dc.Get(), brush.Get()};
    DrawDoc(p, img.Get(), true);
    if (FAILED(dc->EndDraw())) return false;
    cpu->CopyFromBitmap(nullptr, target.Get(), nullptr);
    D2D1_MAPPED_RECT m{};
    if (FAILED(cpu->Map(D2D1_MAP_OPTIONS_READ, &m))) return false;
    out.resize((size_t)w * h);
    for (int y = 0; y < h; ++y) memcpy(&out[(size_t)y * w], m.bits + (size_t)y * m.pitch, (size_t)w * 4);
    cpu->Unmap();
    Invalidate();   // patches were rebuilt for the export context
    return true;
}

bool ImageEditor::Save(const std::wstring& path) {
    std::vector<uint32_t> px;
    int w = 0, h = 0;
    if (!Export(px, w, h) || !SaveImageFile(px, w, h, path, S().jpgQuality)) {
        win::Error(hwnd_, L"Could not save the image.");
        return false;
    }
    return true;
}

bool ImageEditor::SaveInPlace() {
    if (!Save(path_)) return false;
    dirty_ = false;
    Status(L"Saved");
    return true;
}

void ImageEditor::Copy() {
    std::vector<uint32_t> px;
    int w = 0, h = 0;
    if (Export(px, w, h) && CopyPixelsToClipboard(px, w, h)) Status(L"Image copied — paste it into any chat or document");
}

void ImageEditor::CopyPath() {
    if (dirty_ && !SaveInPlace()) return;   // the path should point at what you see
    if (win::CopyTextToClipboard(hwnd_, path_)) Status(L"Path copied: " + path_);
}

void ImageEditor::DragOut() {
    if (dirty_ && !SaveInPlace()) return;
    DragFileOut(path_);
}

bool ImageEditor::ConfirmClose() {
    CommitText();
    if (!dirty_) return true;
    int r = MessageBoxW(hwnd_, (L"Save your changes to " + FileName(path_) + L"?").c_str(), L"ScreenRec",
                        MB_YESNOCANCEL | MB_ICONQUESTION);
    if (r == IDCANCEL) return false;
    if (r == IDYES) return SaveInPlace();
    dirty_ = false;
    return true;
}

// =============================================================================================
// Editing

void ImageEditor::Commit() {
    undo_.push_back({shapes_, crop_, nextStep_});
    if (undo_.size() > 200) undo_.erase(undo_.begin());
    redo_.clear();
    dirty_ = true;
    Relayout();
}

void ImageEditor::Undo() {
    CommitText();
    if (undo_.empty()) return;
    redo_.push_back({shapes_, crop_, nextStep_});
    shapes_ = std::move(undo_.back().shapes);
    crop_ = undo_.back().crop;
    nextStep_ = undo_.back().nextStep;
    undo_.pop_back();
    sel_ = -1;
    dirty_ = true;
    Relayout();
}

void ImageEditor::Redo() {
    CommitText();
    if (redo_.empty()) return;
    undo_.push_back({shapes_, crop_, nextStep_});
    shapes_ = std::move(redo_.back().shapes);
    crop_ = redo_.back().crop;
    nextStep_ = redo_.back().nextStep;
    redo_.pop_back();
    sel_ = -1;
    dirty_ = true;
    Relayout();
}

void ImageEditor::SetTool(Tool t) {
    CommitText();
    tool_ = t;
    if (t != Tool::Select) sel_ = -1;
    Relayout();
}

void ImageEditor::CommitText() {
    if (editing_ < 0 || editing_ >= (int)shapes_.size()) {
        editing_ = -1;
        return;
    }
    if (shapes_[editing_].tool == Tool::Text && shapes_[editing_].text.empty()) {
        shapes_.erase(shapes_.begin() + editing_);
        if (sel_ == editing_) sel_ = -1;
    }
    editing_ = -1;
    Invalidate();
}

int ImageEditor::HitShape(D2D1_POINT_2F p) const {
    float tol = 6 / k_;
    for (int i = (int)shapes_.size() - 1; i >= 0; --i)
        if (InRect(Inflate(shapes_[i].bounds, tol), p.x, p.y)) return i;
    return -1;
}

void ImageEditor::OnMouse(UINT msg, float x, float y, WPARAM wp) {
    if (x < 0 || px_.empty()) return;
    D2D1_POINT_2F pt = ToImage(x, y);
    bool inCanvas = y > kBar && y < H() - kStatus;
    bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    float u = Unit();
    switch (msg) {
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK: {
        if (!inCanvas) return;
        for (auto& c : controls_)
            if (c.down) return;   // a toolbar button took this press
        if (editing_ >= 0) {
            CommitText();
            if (tool_ == Tool::Select) return;
        }
        if (tool_ == Tool::Select) {
            sel_ = HitShape(pt);
            if (sel_ >= 0 && msg == WM_LBUTTONDBLCLK &&
                (shapes_[sel_].tool == Tool::Text || shapes_[sel_].tool == Tool::Callout)) {
                editing_ = sel_;   // double-click edits text
            } else if (sel_ >= 0) {
                Commit();
                moving_ = true;
                dragFrom_ = pt;
                moveOrig_ = shapes_[sel_].pts;
                SetCapture(hwnd_);
            }
            Invalidate();
            return;
        }
        Commit();
        if (tool_ == Tool::Crop) {
            cropping_ = true;
            dragFrom_ = pt;
            cropOrig_ = crop_;
            crop_ = {};
            SetCapture(hwnd_);
            return;
        }
        Shape s;
        s.tool = tool_;
        s.color = color_;
        s.width = (widthLevel_ == 0 ? 1.5f : widthLevel_ == 1 ? 3.f : 5.5f) * u;
        s.filled = filled_;
        s.pts = {pt};
        if (tool_ == Tool::Text) {
            shapes_.push_back(s);
            editing_ = sel_ = (int)shapes_.size() - 1;
            SetFocus(hwnd_);
            Invalidate();
            return;
        }
        if (tool_ == Tool::Step) {
            s.number = nextStep_++;
            shapes_.push_back(s);
            Invalidate();
            return;
        }
        if (tool_ != Tool::Pen && tool_ != Tool::Highlighter) s.pts.push_back(pt);
        shapes_.push_back(s);
        drawing_ = true;
        SetCapture(hwnd_);
        Invalidate();
        return;
    }
    case WM_MOUSEMOVE: {
        if (!(wp & MK_LBUTTON)) return;
        if (moving_ && sel_ >= 0) {
            for (size_t i = 0; i < moveOrig_.size(); ++i)
                shapes_[sel_].pts[i] = {moveOrig_[i].x + pt.x - dragFrom_.x, moveOrig_[i].y + pt.y - dragFrom_.y};
            Render();
        } else if (cropping_) {
            D2D1_RECT_F r = Norm(dragFrom_, pt);
            crop_ = {std::clamp((LONG)r.left, 0L, (LONG)iw_), std::clamp((LONG)r.top, 0L, (LONG)ih_),
                     std::clamp((LONG)r.right, 0L, (LONG)iw_), std::clamp((LONG)r.bottom, 0L, (LONG)ih_)};
            Render();
        } else if (drawing_ && !shapes_.empty()) {
            Shape& s = shapes_.back();
            if (s.tool == Tool::Pen || s.tool == Tool::Highlighter) {
                if (Dist(s.pts.back(), pt) * k_ >= 1.5f) s.pts.push_back(pt);
            } else {
                D2D1_POINT_2F q = pt, a = s.pts[0];
                if (shift && (s.tool == Tool::Arrow || s.tool == Tool::Line)) {   // snap to 45 degrees
                    float ang = atan2f(q.y - a.y, q.x - a.x), len = Dist(a, q);
                    ang = std::round(ang / 0.785398f) * 0.785398f;
                    q = {a.x + len * cosf(ang), a.y + len * sinf(ang)};
                } else if (shift && (s.tool == Tool::Rect || s.tool == Tool::Ellipse)) {   // square / circle
                    float d = std::max(std::fabs(q.x - a.x), std::fabs(q.y - a.y));
                    q = {a.x + (q.x >= a.x ? d : -d), a.y + (q.y >= a.y ? d : -d)};
                }
                s.pts[1] = q;
            }
            Render();
        }
        return;
    }
    case WM_LBUTTONUP: {
        if (GetCapture() == hwnd_) ReleaseCapture();
        if (moving_) {
            moving_ = false;
            if (Dist(dragFrom_, pt) < 1) {   // a click without movement: nothing changed
                undo_.pop_back();
                dirty_ = !undo_.empty() || dirty_;
            }
        }
        if (cropping_) {
            cropping_ = false;
            if (crop_.right - crop_.left < 8 || crop_.bottom - crop_.top < 8) crop_ = cropOrig_;
        }
        if (drawing_ && !shapes_.empty()) {
            drawing_ = false;
            Shape& s = shapes_.back();
            bool tiny = s.pts.size() >= 2 && Dist(s.pts[0], s.pts.back()) < 4 * u;
            if (s.tool == Tool::Callout && tiny) {
                s.pts[1] = {s.pts[0].x + 90 * u, s.pts[0].y - 70 * u};   // default bubble up-right
                tiny = false;
            }
            if (tiny && s.tool != Tool::Pen && s.tool != Tool::Highlighter) {
                shapes_.pop_back();
                undo_.pop_back();
            } else if (s.tool == Tool::Callout) {
                editing_ = sel_ = (int)shapes_.size() - 1;
                SetFocus(hwnd_);
            }
        }
        Relayout();
        return;
    }
    }
}

LRESULT ImageEditor::OnMessage(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_NCCALCSIZE:
        if (wp && IsZoomed(hwnd_)) {   // maximized frameless window: keep it inside the screen
            auto* p = reinterpret_cast<NCCALCSIZE_PARAMS*>(lp);
            UINT dpi = win::DpiOf(hwnd_);
            int f = GetSystemMetricsForDpi(SM_CXFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
            InflateRect(&p->rgrc[0], -f, -f);
            return 0;
        }
        break;
    case WM_GETMINMAXINFO: {
        auto* mm = reinterpret_cast<MINMAXINFO*>(lp);
        mm->ptMinTrackSize = {(LONG)(900 * Scale()), (LONG)(480 * Scale())};
        return 0;
    }
    case WM_CLOSE:
        if (ConfirmClose()) {
            Show(false);
            px_.clear();
            px_.shrink_to_fit();
            img_.Reset();
            shapes_.clear();
            undo_.clear();
            redo_.clear();
        }
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hwnd_, &pt);
            float y = pt.y / Scale();
            LPCWSTR cur = IDC_ARROW;
            if (y > kBar && y < H() - kStatus)
                cur = tool_ == Tool::Select ? IDC_ARROW : tool_ == Tool::Text ? IDC_IBEAM : IDC_CROSS;
            SetCursor(LoadCursorW(nullptr, cur));
            return TRUE;
        }
        break;
    case WM_CHAR:
        if (editing_ >= 0 && editing_ < (int)shapes_.size()) {
            auto& t = shapes_[editing_].text;
            if (wp == L'\r' || wp == 27) CommitText();
            else if (wp == L'\b') {
                if (!t.empty()) t.pop_back();
            } else if (wp >= 32) t += (wchar_t)wp;
            dirty_ = true;
            Invalidate();
        }
        return 0;
    case WM_KEYDOWN: {
        if (editing_ >= 0) return 0;   // typing goes through WM_CHAR
        bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0, shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        if (ctrl) {
            if (wp == 'Z' && shift) Redo();
            else if (wp == 'Z') Undo();
            else if (wp == 'Y') Redo();
            else if (wp == 'C') Copy();
            else if (wp == 'S') SaveInPlace();
            return 0;
        }
        if ((wp == VK_DELETE || wp == VK_BACK) && sel_ >= 0) {
            Commit();
            shapes_.erase(shapes_.begin() + sel_);
            sel_ = -1;
            Invalidate();
            return 0;
        }
        if (wp == VK_ESCAPE) {
            if (tool_ == Tool::Crop && (crop_.right > crop_.left)) {
                Commit();
                crop_ = {};
            }
            sel_ = -1;
            Invalidate();
            return 0;
        }
        for (auto& t : kTools)
            if ((WPARAM)t.key == wp) {
                SetTool(t.tool);
                return 0;
            }
        break;
    }
    }
    return Window::OnMessage(msg, wp, lp);
}
