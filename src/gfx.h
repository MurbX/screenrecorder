#pragma once
#include "common.h"

namespace gfx {

bool Init();
ID2D1Factory1* D2D();
IDWriteFactory* DW();
IWICImagingFactory* WIC();
ID2D1Device* D2DDevice();
ID3D11Device* D3D();
IDXGIFactory2* DXGI();
IDCompositionDevice* DComp();
bool RecoverDevice();   // after DXGI_ERROR_DEVICE_REMOVED

IDWriteTextFormat* Font(float size, DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL,
                        DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING,
                        DWRITE_PARAGRAPH_ALIGNMENT valign = DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

// A window's GPU drawing surface: flip-model swap chain + D2D device context.
// composition = true renders through DirectComposition so the window can have
// per-pixel transparency (overlays, webcam bubble).
class Surface {
public:
    bool Create(HWND hwnd, bool composition);
    void Destroy();
    ID2D1DeviceContext* Begin();      // nullptr when the window has zero size
    void End();
    void SetDpi(float dpi) { dpi_ = dpi; }
    float Dpi() const { return dpi_; }
    ID2D1SolidColorBrush* Brush() { return brush_.Get(); }
    bool Valid() const { return dc_ != nullptr; }

private:
    bool CreateSwapChain();
    HWND hwnd_ = nullptr;
    bool composition_ = false;
    float dpi_ = 96.f;
    UINT w_ = 0, h_ = 0;
    ComPtr<IDXGISwapChain1> swap_;
    ComPtr<ID2D1DeviceContext> dc_;
    ComPtr<ID2D1Bitmap1> target_;
    ComPtr<ID2D1SolidColorBrush> brush_;
    ComPtr<IDCompositionTarget> dcTarget_;
    ComPtr<IDCompositionVisual> visual_;
};

// Thin drawing helper bound to a device context and a reusable brush.
struct Painter {
    ID2D1DeviceContext* dc;
    ID2D1SolidColorBrush* br;

    ID2D1SolidColorBrush* B(D2D1_COLOR_F c) { br->SetColor(c); return br; }
    void Clear(D2D1_COLOR_F c) { dc->Clear(c); }
    void Fill(D2D1_RECT_F r, D2D1_COLOR_F c) { dc->FillRectangle(r, B(c)); }
    void FillRound(D2D1_RECT_F r, float rad, D2D1_COLOR_F c) { dc->FillRoundedRectangle({r, rad, rad}, B(c)); }
    void StrokeRound(D2D1_RECT_F r, float rad, D2D1_COLOR_F c, float w = 1.f, ID2D1StrokeStyle* s = nullptr) {
        dc->DrawRoundedRectangle({r, rad, rad}, B(c), w, s);
    }
    void Stroke(D2D1_RECT_F r, D2D1_COLOR_F c, float w = 1.f, ID2D1StrokeStyle* s = nullptr) { dc->DrawRectangle(r, B(c), w, s); }
    void Line(D2D1_POINT_2F a, D2D1_POINT_2F b, D2D1_COLOR_F c, float w = 1.f, ID2D1StrokeStyle* s = nullptr) {
        dc->DrawLine(a, b, B(c), w, s);
    }
    void Circle(D2D1_POINT_2F p, float r, D2D1_COLOR_F c) { dc->FillEllipse({p, r, r}, B(c)); }
    void StrokeCircle(D2D1_POINT_2F p, float r, D2D1_COLOR_F c, float w = 1.f) { dc->DrawEllipse({p, r, r}, B(c), w); }
    void Text(const std::wstring& s, D2D1_RECT_F r, float size, D2D1_COLOR_F c,
              DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL,
              DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING,
              DWRITE_PARAGRAPH_ALIGNMENT valign = DWRITE_PARAGRAPH_ALIGNMENT_CENTER) {
        dc->DrawText(s.c_str(), (UINT32)s.size(), Font(size, weight, align, valign), r, B(c),
                     D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
    // Soft drop shadow made of stacked translucent rounded rects.
    void Shadow(D2D1_RECT_F r, float rad, float spread, float alpha);
};

ID2D1StrokeStyle* DashStyle();      // dashed line for selection frames
ID2D1StrokeStyle* RoundCapStyle();  // round caps/joins for pens and icons
float MeasureText(const std::wstring& s, float size, DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL);

// Load an image file (jpg/png) into a D2D bitmap for the given context.
ComPtr<ID2D1Bitmap> LoadBitmapFile(ID2D1DeviceContext* dc, const std::wstring& path);


}  // namespace gfx
