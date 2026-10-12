#include "dragdrop.h"
#include "gfx.h"
#include <shlobj.h>
#include <shobjidl.h>

namespace {
class DropSource : public IDropSource {
public:
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IDropSource)) {
            *ppv = static_cast<IDropSource*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&ref_); }
    STDMETHODIMP_(ULONG) Release() override {
        ULONG r = InterlockedDecrement(&ref_);
        if (!r) delete this;
        return r;
    }
    STDMETHODIMP QueryContinueDrag(BOOL escape, DWORD keys) override {
        if (escape) return DRAGDROP_S_CANCEL;
        if (!(keys & MK_LBUTTON)) return DRAGDROP_S_DROP;
        return S_OK;
    }
    STDMETHODIMP GiveFeedback(DWORD) override { return DRAGDROP_S_USEDEFAULTCURSORS; }

private:
    LONG ref_ = 1;
};

// Opaque preview of the image, scaled to fit `maxW` x `maxW * 3/4`, for the drag cursor.
HBITMAP PreviewBitmap(const std::wstring& path, UINT maxW, SIZE& size) {
    ComPtr<IWICBitmapDecoder> dec;
    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(gfx::WIC()->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec)) ||
        FAILED(dec->GetFrame(0, &frame)))
        return nullptr;
    UINT w = 0, h = 0;
    frame->GetSize(&w, &h);
    if (!w || !h) return nullptr;
    double k = std::min({1.0, (double)maxW / w, maxW * 0.75 / h});
    UINT tw = std::max(1u, (UINT)(w * k)), th = std::max(1u, (UINT)(h * k));
    ComPtr<IWICBitmapScaler> scaler;
    ComPtr<IWICFormatConverter> conv;
    gfx::WIC()->CreateBitmapScaler(&scaler);
    if (FAILED(scaler->Initialize(frame.Get(), tw, th, WICBitmapInterpolationModeHighQualityCubic))) return nullptr;
    gfx::WIC()->CreateFormatConverter(&conv);
    if (FAILED(conv->Initialize(scaler.Get(), GUID_WICPixelFormat32bppBGR, WICBitmapDitherTypeNone, nullptr, 0,
                                WICBitmapPaletteTypeCustom)))
        return nullptr;
    BITMAPINFO bi{};
    bi.bmiHeader = {sizeof(BITMAPINFOHEADER), (LONG)tw, -(LONG)th, 1, 32, BI_RGB};
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp) return nullptr;
    conv->CopyPixels(nullptr, tw * 4, tw * th * 4, static_cast<BYTE*>(bits));
    auto* px = static_cast<uint32_t*>(bits);
    for (UINT i = 0; i < tw * th; ++i) px[i] |= 0xff000000;   // opaque
    size = {(LONG)tw, (LONG)th};
    return bmp;
}
}  // namespace

bool PastDragThreshold(POINT start, POINT now) {
    return std::abs(now.x - start.x) > GetSystemMetrics(SM_CXDRAG) || std::abs(now.y - start.y) > GetSystemMetrics(SM_CYDRAG);
}

bool DragFileOut(const std::wstring& path) {
    // The shell's own data object for the file: exactly what Explorer offers when you drag a file.
    ComPtr<IShellItem> item;
    ComPtr<IDataObject> data;
    if (FAILED(SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&item))) ||
        FAILED(item->BindToHandler(nullptr, BHID_DataObject, IID_PPV_ARGS(&data))))
        return false;

    ComPtr<IDragSourceHelper> helper;
    if (SUCCEEDED(CoCreateInstance(CLSID_DragDropHelper, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&helper)))) {
        SIZE sz{};
        UINT maxW = (UINT)(160 * GetDpiForSystem() / 96);
        if (HBITMAP bmp = PreviewBitmap(path, maxW, sz)) {
            SHDRAGIMAGE di{sz, {sz.cx / 2, sz.cy / 2}, bmp, CLR_NONE};
            if (FAILED(helper->InitializeFromBitmap(&di, data.Get()))) DeleteObject(bmp);   // helper owns it on success
        }
    }
    auto* source = new DropSource;
    DWORD effect = DROPEFFECT_NONE;
    HRESULT hr = DoDragDrop(data.Get(), source, DROPEFFECT_COPY, &effect);
    source->Release();
    return hr == DRAGDROP_S_DROP && effect != DROPEFFECT_NONE;
}
