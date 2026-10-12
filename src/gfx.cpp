#include "gfx.h"
#include <d3d11_4.h>
#include <map>
#include <tuple>

namespace gfx {
namespace {
ComPtr<ID2D1Factory1> g_d2d;
ComPtr<IDWriteFactory> g_dw;
ComPtr<IWICImagingFactory> g_wic;
ComPtr<ID3D11Device> g_d3d;
ComPtr<IDXGIDevice> g_dxgiDevice;
ComPtr<IDXGIFactory2> g_dxgi;
ComPtr<ID2D1Device> g_d2dDevice;
ComPtr<IDCompositionDevice> g_dcomp;
ComPtr<ID2D1StrokeStyle> g_dash, g_roundCap;
std::map<std::tuple<int, int, int, int>, ComPtr<IDWriteTextFormat>> g_fonts;
int g_generation = 0;   // bumped when the GPU device is recreated

bool CreateDevice() {
    // VIDEO_SUPPORT lets the built-in player decode on the GPU with this same device.
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, nullptr, 0,
                                   D3D11_SDK_VERSION, &g_d3d, nullptr, nullptr);
    if (FAILED(hr)) {
        flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, nullptr, 0, D3D11_SDK_VERSION, &g_d3d,
                               nullptr, nullptr);
    }
    if (FAILED(hr))
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, nullptr, 0,
                               D3D11_SDK_VERSION, &g_d3d, nullptr, nullptr);
    if (FAILED(hr)) return false;
    // The media engine touches the device from its own threads.
    ComPtr<ID3D11Multithread> mt;
    if (SUCCEEDED(g_d3d.As(&mt))) mt->SetMultithreadProtected(TRUE);
    g_d3d.As(&g_dxgiDevice);
    ComPtr<IDXGIAdapter> adapter;
    g_dxgiDevice->GetAdapter(&adapter);
    adapter->GetParent(IID_PPV_ARGS(&g_dxgi));
    if (FAILED(g_d2d->CreateDevice(g_dxgiDevice.Get(), &g_d2dDevice))) return false;
    if (FAILED(DCompositionCreateDevice(g_dxgiDevice.Get(), IID_PPV_ARGS(&g_dcomp)))) return false;
    g_generation++;
    return true;
}
}  // namespace

bool Init() {
    D2D1_FACTORY_OPTIONS opts{};
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &opts, &g_d2d)))
        return false;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(g_dw.GetAddressOf()))))
        return false;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&g_wic))))
        return false;
    float dashes[] = {4.f, 3.f};
    g_d2d->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT,
                                                         D2D1_LINE_JOIN_MITER, 10.f, D2D1_DASH_STYLE_CUSTOM),
                             dashes, 2, &g_dash);
    g_d2d->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                                                         D2D1_LINE_JOIN_ROUND),
                             nullptr, 0, &g_roundCap);
    return CreateDevice();
}

bool RecoverDevice() {
    g_dcomp.Reset();
    g_d2dDevice.Reset();
    g_dxgi.Reset();
    g_dxgiDevice.Reset();
    g_d3d.Reset();
    return CreateDevice();
}

ID2D1Factory1* D2D() { return g_d2d.Get(); }
IDWriteFactory* DW() { return g_dw.Get(); }
IWICImagingFactory* WIC() { return g_wic.Get(); }
ID2D1Device* D2DDevice() { return g_d2dDevice.Get(); }
ID3D11Device* D3D() { return g_d3d.Get(); }
IDXGIFactory2* DXGI() { return g_dxgi.Get(); }
IDCompositionDevice* DComp() { return g_dcomp.Get(); }
ID2D1StrokeStyle* DashStyle() { return g_dash.Get(); }
ID2D1StrokeStyle* RoundCapStyle() { return g_roundCap.Get(); }

IDWriteTextFormat* Font(float size, DWRITE_FONT_WEIGHT weight, DWRITE_TEXT_ALIGNMENT align,
                        DWRITE_PARAGRAPH_ALIGNMENT valign) {
    auto key = std::make_tuple((int)(size * 10), (int)weight, (int)align, (int)valign);
    auto it = g_fonts.find(key);
    if (it != g_fonts.end()) return it->second.Get();
    ComPtr<IDWriteTextFormat> f;
    g_dw->CreateTextFormat(L"Segoe UI", nullptr, weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size,
                           L"en-us", &f);
    f->SetTextAlignment(align);
    f->SetParagraphAlignment(valign);
    f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    DWRITE_TRIMMING trim{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
    ComPtr<IDWriteInlineObject> ellipsis;
    g_dw->CreateEllipsisTrimmingSign(f.Get(), &ellipsis);
    f->SetTrimming(&trim, ellipsis.Get());
    g_fonts[key] = f;
    return f.Get();
}

float MeasureText(const std::wstring& s, float size, DWRITE_FONT_WEIGHT weight) {
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(g_dw->CreateTextLayout(s.c_str(), (UINT32)s.size(), Font(size, weight), 10000.f, 100.f, &layout)))
        return 0;
    DWRITE_TEXT_METRICS m{};
    layout->GetMetrics(&m);
    return m.widthIncludingTrailingWhitespace;
}

void Painter::Shadow(D2D1_RECT_F r, float rad, float spread, float alpha) {
    const int steps = 8;
    for (int i = steps; i >= 1; --i) {
        float d = spread * i / steps;
        D2D1_RECT_F rr = Inflate(r, d);
        rr.top += d * 0.4f;
        rr.bottom += d * 0.4f;
        FillRound(rr, rad + d, D2D1::ColorF(0, 0, 0, alpha / steps));
    }
}

// ---------------------------------------------------------------------------------------------
// Surface

bool Surface::Create(HWND hwnd, bool composition) {
    hwnd_ = hwnd;
    composition_ = composition;
    return CreateSwapChain();
}

static int g_surfaceGen = 0;

bool Surface::CreateSwapChain() {
    Destroy();
    RECT rc;
    GetClientRect(hwnd_, &rc);
    w_ = std::max<LONG>(1, rc.right - rc.left);
    h_ = std::max<LONG>(1, rc.bottom - rc.top);

    DXGI_SWAP_CHAIN_DESC1 d{};
    d.Width = w_;
    d.Height = h_;
    d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    d.SampleDesc.Count = 1;
    d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    d.BufferCount = 2;
    d.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    d.Scaling = DXGI_SCALING_STRETCH;
    d.AlphaMode = composition_ ? DXGI_ALPHA_MODE_PREMULTIPLIED : DXGI_ALPHA_MODE_IGNORE;

    HRESULT hr;
    if (composition_) {
        hr = g_dxgi->CreateSwapChainForComposition(g_d3d.Get(), &d, nullptr, &swap_);
        if (FAILED(hr)) return false;
        g_dcomp->CreateTargetForHwnd(hwnd_, TRUE, &dcTarget_);
        g_dcomp->CreateVisual(&visual_);
        visual_->SetContent(swap_.Get());
        dcTarget_->SetRoot(visual_.Get());
        g_dcomp->Commit();
    } else {
        hr = g_dxgi->CreateSwapChainForHwnd(g_d3d.Get(), hwnd_, &d, nullptr, nullptr, &swap_);
        if (FAILED(hr)) return false;
    }
    if (FAILED(g_d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &dc_))) return false;
    dc_->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1), &brush_);
    dc_->SetTextAntialiasMode(composition_ ? D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE : D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
    g_surfaceGen = g_generation;
    return true;
}

void Surface::Destroy() {
    if (dc_) dc_->SetTarget(nullptr);
    target_.Reset();
    brush_.Reset();
    dc_.Reset();
    visual_.Reset();
    dcTarget_.Reset();
    swap_.Reset();
}

ID2D1DeviceContext* Surface::Begin() {
    if (!hwnd_) return nullptr;
    RECT rc;
    GetClientRect(hwnd_, &rc);
    UINT w = rc.right - rc.left, h = rc.bottom - rc.top;
    if (w == 0 || h == 0) return nullptr;
    if (!swap_ || g_surfaceGen != g_generation) {
        if (!CreateSwapChain()) return nullptr;
    }
    if (w != w_ || h != h_) {
        dc_->SetTarget(nullptr);
        target_.Reset();
        if (FAILED(swap_->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0))) {
            if (!CreateSwapChain()) return nullptr;
        }
        w_ = w;
        h_ = h;
    }
    if (!target_) {
        ComPtr<IDXGISurface> back;
        swap_->GetBuffer(0, IID_PPV_ARGS(&back));
        D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), dpi_, dpi_);
        if (FAILED(dc_->CreateBitmapFromDxgiSurface(back.Get(), &bp, &target_))) return nullptr;
        dc_->SetTarget(target_.Get());
    }
    dc_->SetDpi(dpi_, dpi_);
    dc_->BeginDraw();
    dc_->SetTransform(D2D1::Matrix3x2F::Identity());
    return dc_.Get();
}

void Surface::End() {
    HRESULT hr = dc_->EndDraw();
    if (SUCCEEDED(hr)) hr = swap_->Present(1, 0);
    if (hr == D2DERR_RECREATE_TARGET || hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
        Destroy();
        if (g_d3d && g_d3d->GetDeviceRemovedReason() != S_OK) RecoverDevice();
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

// ---------------------------------------------------------------------------------------------

ComPtr<ID2D1Bitmap> LoadBitmapFile(ID2D1DeviceContext* dc, const std::wstring& path) {
    ComPtr<IWICBitmapDecoder> dec;
    if (FAILED(g_wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec)))
        return nullptr;
    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(dec->GetFrame(0, &frame))) return nullptr;
    ComPtr<IWICFormatConverter> conv;
    g_wic->CreateFormatConverter(&conv);
    if (FAILED(conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0,
                                WICBitmapPaletteTypeCustom)))
        return nullptr;
    ComPtr<ID2D1Bitmap> bmp;
    dc->CreateBitmapFromWicBitmap(conv.Get(), nullptr, &bmp);
    return bmp;
}

}  // namespace gfx
