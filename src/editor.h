#pragma once
#include "ui.h"

// Screenshot editor ("Markup"): annotate, redact and crop an image, then copy, save or drag it
// into another app. Annotations stay editable (select, move, delete) until saved.
class ImageEditor : public Window {
public:
    enum class Tool { Select, Arrow, Line, Rect, Ellipse, Pen, Highlighter, Text, Step, Blur, Pixelate, Spotlight, Magnify, Callout, Crop };

    bool Open(const std::wstring& path);

protected:
    LRESULT OnMessage(UINT msg, WPARAM wp, LPARAM lp) override;
    void Layout() override;
    void Paint(gfx::Painter& p) override;
    void OnMouse(UINT msg, float x, float y, WPARAM wp) override;
    LRESULT HitTest(float x, float y) override;

private:
    struct Shape {
        Tool tool = Tool::Arrow;
        uint32_t color = 0;
        float width = 4;   // image px
        bool filled = false;
        std::vector<D2D1_POINT_2F> pts;   // image px
        std::wstring text;
        int number = 0;
        D2D1_RECT_F bounds{};   // last drawn extent (image px), for hit testing
        // Cached blur/pixelate patch for the current device context.
        ComPtr<ID2D1Bitmap> patch;
        ID2D1DeviceContext* patchDc = nullptr;
        D2D1_RECT_F patchFor{};
    };
    struct Snapshot {
        std::vector<Shape> shapes;
        RECT crop;
        int nextStep;
    };

    // Document
    bool Load(const std::wstring& path);
    float Unit() const;
    RECT CropOrFull() const;
    void DrawDoc(gfx::Painter& p, ID2D1Bitmap* img, bool exporting);
    void DrawShape(gfx::Painter& p, Shape& s, ID2D1Bitmap* img, bool exporting);
    void EnsurePatch(Shape& s, ID2D1DeviceContext* dc);
    bool Export(std::vector<uint32_t>& out, int& w, int& h);   // final pixels (cropped)
    bool Save(const std::wstring& path);
    bool SaveInPlace();
    void Copy();
    void CopyPath();
    void DragOut();
    bool ConfirmClose();

    // Editing
    void Commit();
    void Undo();
    void Redo();
    void SetTool(Tool t);
    void CommitText();
    int HitShape(D2D1_POINT_2F p) const;
    D2D1_POINT_2F ToImage(float x, float y) const;
    D2D1_RECT_F CanvasRect() const;
    void UpdateView();
    void Status(const std::wstring& s);

    std::wstring path_;
    std::vector<uint32_t> px_;
    int iw_ = 0, ih_ = 0;
    ComPtr<ID2D1Bitmap> img_;
    ID2D1DeviceContext* imgDc_ = nullptr;
    std::vector<Shape> shapes_;
    RECT crop_{};   // image px; empty = no crop
    int nextStep_ = 1;
    std::vector<Snapshot> undo_, redo_;
    bool dirty_ = false;

    Tool tool_ = Tool::Arrow;
    uint32_t color_ = 0xff453a;
    int widthLevel_ = 1;   // 0 thin, 1 medium, 2 thick
    bool filled_ = false;

    int sel_ = -1;
    int editing_ = -1;        // text/callout being typed
    bool drawing_ = false;    // creating a new shape (it is shapes_.back())
    bool moving_ = false;
    bool cropping_ = false;
    D2D1_POINT_2F dragFrom_{};
    std::vector<D2D1_POINT_2F> moveOrig_;
    RECT cropOrig_{};

    float k_ = 1;              // image px -> DIP
    D2D1_POINT_2F off_{};      // DIP offset of the image's top-left
    std::wstring hover_, status_;
    ULONGLONG statusUntil_ = 0;
    std::vector<std::wstring> tips_;   // tooltip per control index
};
