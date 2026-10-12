#pragma once
#include "settings.h"
#include "ui.h"

// Full-screen, never-recorded overlay: area selection with handles, window picking,
// the dashed frame while recording and the countdown.
class AreaOverlay : public Window {
public:
    enum class Kind { Hidden, Select, Adjust, PickWindow, PickMonitor, Frame };
    bool Create();
    // shot = true: the same pickers, but for a screenshot (captures on release/click, no control bar)
    void BeginSelect(bool shot = false);
    void BeginAdjust(RECT r);
    void BeginPick(bool shot = false);
    void BeginPickMonitor(bool shot = false);
    void ShowFrame(RECT r, bool border);
    void Hide();
    void SetRegion(RECT r);     // external change (size presets)
    void SetCountdown(int n);   // 0 hides the number
    Kind GetKind() const { return kind_; }
    RECT Region() const { return region_; }

protected:
    LRESULT OnMessage(UINT msg, WPARAM wp, LPARAM lp) override;
    void Paint(gfx::Painter& p) override;
    void OnMouse(UINT msg, float x, float y, WPARAM wp) override;
    LRESULT HitTest(float, float) override { return HTCLIENT; }

private:
    enum class Drag { None, New, Move, Resize };
    int HandleAt(POINT px) const;
    void SetKind(Kind k);
    D2D1_RECT_F ToDip(RECT r) const;
    void Hint(gfx::Painter& p, const std::wstring& text);   // pill at the top of the cursor's monitor

    Kind kind_ = Kind::Hidden;
    RECT virt_{}, region_{};
    RECT hoverMon_{}, dragMon_{};
    bool border_ = true;
    Drag drag_ = Drag::None;
    int handle_ = -1;
    POINT dragStart_{};
    RECT dragOrig_{};
    HWND hoverWin_ = nullptr;
    RECT hoverRect_{};
    int countdown_ = 0;
    bool shot_ = false;
};

// Floating control bar under the selected area: toggles, size/timer, REC / pause / stop.
class ControlBar : public Window {
public:
    bool Create();
    void Place(RECT anchor, bool inside);   // anchor in screen px
    void Refresh();

protected:
    LRESULT OnMessage(UINT msg, WPARAM wp, LPARAM lp) override;
    void Layout() override;
    void Paint(gfx::Painter& p) override;
    LRESULT HitTest(float x, float y) override;

private:
    float ContentWidth();
    void BuildItems(bool addControls, float& width);
    RECT anchor_{};
    bool inside_ = false;
    bool timer_ = false;
};

enum class Tool { Pointer, Pen, Highlighter, Line, Arrow, Rect, Ellipse, Text };

// Transparent full-screen layer that IS recorded: annotations and mouse effects.
class Canvas : public Window {
public:
    bool Create();
    void SetTool(Tool t);
    Tool GetTool() const { return tool_; }
    void SetColor(uint32_t c) { color_ = c; }
    uint32_t Color() const { return color_; }
    void SetWidth(float w) { width_ = w; }
    float StrokeWidth() const { return width_; }
    void Undo();
    void Clear();
    void SetEffects(bool on);
    void Update();   // shows/hides and toggles click-through as needed

protected:
    LRESULT OnMessage(UINT msg, WPARAM wp, LPARAM lp) override;
    void Paint(gfx::Painter& p) override;
    void OnMouse(UINT msg, float x, float y, WPARAM wp) override;
    LRESULT HitTest(float, float) override { return HTCLIENT; }

private:
    struct Stroke {
        Tool tool;
        uint32_t color;
        float width;
        std::vector<D2D1_POINT_2F> pts;
        std::wstring text;
        ComPtr<ID2D1PathGeometry> geo;   // cached polyline for pens
    };
    struct Ripple {
        D2D1_POINT_2F p;
        ULONGLONG start;
        bool right;
    };
    void DrawStroke(gfx::Painter& p, Stroke& s);
    void TickEffects();   // detect clicks, animate ripples
    void CommitText();

    std::vector<Stroke> strokes_;
    std::optional<Stroke> current_;
    bool typing_ = false;
    Tool tool_ = Tool::Pointer;
    uint32_t color_ = th::red;
    float width_ = 4;
    bool effects_ = false;
    std::vector<Ripple> ripples_;
    bool lDown_ = false, rDown_ = false;
    RECT mon_{};
};

// macOS-style floating thumbnail shown after a screenshot or recording (bottom-right of the
// screen). Drag it into any app or folder; click to open it (editor for images, output window
// for videos). Disappears on its own after a few seconds unless hovered.
class Toast : public Window {
public:
    bool Create();
    void Pop(const std::wstring& path, bool video);

protected:
    LRESULT OnMessage(UINT msg, WPARAM wp, LPARAM lp) override;
    void Layout() override;
    void Paint(gfx::Painter& p) override;
    void OnMouse(UINT msg, float x, float y, WPARAM wp) override;
    LRESULT HitTest(float, float) override { return HTCLIENT; }

private:
    void Present();                 // size to the image and slide in
    D2D1_RECT_F CardRect() const;   // the thumbnail inside the shadow margin
    void Dismiss();

    std::wstring path_, thumbPath_;
    bool video_ = false;
    double duration_ = 0;
    SIZE imgSize_{16, 9};
    ComPtr<ID2D1Bitmap> thumb_;
    ID2D1DeviceContext* thumbDc_ = nullptr;
    bool hovering_ = false, pressed_ = false;
    POINT pressAt_{};
    int slide_ = 0;   // slide-in animation step
    RECT final_{};
    int gen_ = 0;     // ignores late thumbnail jobs from a previous recording
};

class DrawToolbar : public Window {
public:
    bool Create();
    void PlaceNear(RECT region);

protected:
    void Layout() override;
    void Paint(gfx::Painter& p) override;
    LRESULT HitTest(float x, float y) override;
};
