#pragma once
#include "gfx.h"
#include "icons.h"
#include "theme.h"

// Minimal retained UI: each window owns a list of controls (rect + paint + click/drag),
// rebuilt by Layout() whenever the window size, DPI or state changes.
class Window {
public:
    struct Control {
        D2D1_RECT_F rc{};
        std::function<void(gfx::Painter&, const Control&)> paint;
        std::function<void()> click;
        std::function<void(float x, float y, int phase)> drag;   // phase: 0 begin, 1 move, 2 end
        std::function<void()> rclick;
        bool enabled = true;
        bool hot = false, down = false;
    };

    virtual ~Window();
    HWND Hwnd() const { return hwnd_; }
    bool IsOpen() const { return hwnd_ != nullptr; }
    bool Visible() const { return hwnd_ && IsWindowVisible(hwnd_); }
    void Show(bool show = true);
    void Close();
    void Invalidate() {
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
    }
    void Relayout() {
        needLayout_ = true;
        Invalidate();
    }
    float Scale() const { return dpi_ / 96.f; }
    float W() const;   // client size in DIPs
    float H() const;
    POINT ToScreen(float x, float y) const;   // DIP client coords -> physical screen px

protected:
    bool Create(const wchar_t* title, DWORD style, DWORD exStyle, RECT rcPx, HWND owner, bool composition);
    virtual LRESULT OnMessage(UINT msg, WPARAM wp, LPARAM lp);
    virtual void Layout() {}
    virtual void Paint(gfx::Painter& p) { PaintControls(p); }
    virtual void OnMouse(UINT msg, float x, float y, WPARAM wp) {}
    virtual LRESULT HitTest(float x, float y);   // default: drag by caption strip
    virtual void OnClosed() {}

    void Render();
    void EnsureLayout();
    void PaintControls(gfx::Painter& p);
    Control& Add(D2D1_RECT_F rc, std::function<void(gfx::Painter&, const Control&)> paint,
                 std::function<void()> click = nullptr);

    HWND hwnd_ = nullptr;
    gfx::Surface surface_;
    float dpi_ = 96.f;
    float captionH_ = 0;          // draggable strip height (DIPs) for frameless windows
    bool frameless_ = true;       // removes the standard non-client area
    std::vector<Control> controls_;

private:
    static LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM wp, LPARAM lp);
    int ControlAt(float x, float y) const;
    int hot_ = -1, down_ = -1;
    bool tracking_ = false;
    bool needLayout_ = true;
};

// Paint helpers for the Mac-style widgets.
namespace ui {
void Tile(gfx::Painter& p, const Window::Control& c, Icon icon, const std::wstring& label);
void RoundToggle(gfx::Painter& p, const Window::Control& c, Icon icon, bool on);
void IconButton(gfx::Painter& p, const Window::Control& c, Icon icon, uint32_t color = th::text2, float iconSize = 16,
                bool background = false);
void Pill(gfx::Painter& p, const Window::Control& c, const std::wstring& text);
void Button(gfx::Painter& p, const Window::Control& c, const std::wstring& text, uint32_t bg, uint32_t bgHover,
            Icon icon = Icon::Dot, bool withIcon = false);
void Checkbox(gfx::Painter& p, const Window::Control& c, const std::wstring& label, bool checked);
void Dropdown(gfx::Painter& p, const Window::Control& c, const std::wstring& value);
void Slider(gfx::Painter& p, const Window::Control& c, float t);   // t in [0,1]
void Field(gfx::Painter& p, const Window::Control& c, const std::wstring& text, bool active);
void Separator(gfx::Painter& p, float x1, float y1, float x2, float y2);
}  // namespace ui
