#include "screenshot.h"
#include "gfx.h"
#include <shlobj.h>

namespace {
bool Encode(const std::vector<uint32_t>& px, int w, int h, const ShotOptions& opt) {
    ComPtr<IWICBitmapEncoder> enc;
    if (FAILED(gfx::WIC()->CreateEncoder(opt.jpg ? GUID_ContainerFormatJpeg : GUID_ContainerFormatPng, nullptr, &enc)))
        return false;
    ComPtr<IWICStream> stream;
    gfx::WIC()->CreateStream(&stream);
    if (FAILED(stream->InitializeFromFilename(opt.path.c_str(), GENERIC_WRITE))) return false;
    enc->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> props;
    enc->CreateNewFrame(&frame, &props);
    if (opt.jpg && props) {
        PROPBAG2 opt2{};
        opt2.pstrName = const_cast<LPOLESTR>(L"ImageQuality");
        VARIANT v;
        VariantInit(&v);
        v.vt = VT_R4;
        v.fltVal = opt.jpgQuality / 100.f;
        props->Write(1, &opt2, &v);
    }
    frame->Initialize(props.Get());
    frame->SetSize(w, h);
    WICPixelFormatGUID fmt = GUID_WICPixelFormat24bppBGR;   // no alpha channel in screenshots
    frame->SetPixelFormat(&fmt);
    ComPtr<IWICBitmap> src;
    if (FAILED(gfx::WIC()->CreateBitmapFromMemory(w, h, GUID_WICPixelFormat32bppBGR, w * 4, (UINT)(px.size() * 4),
                                                  (BYTE*)px.data(), &src)))
        return false;
    if (FAILED(frame->WriteSource(src.Get(), nullptr))) return false;
    frame->Commit();
    return SUCCEEDED(enc->Commit());
}

HGLOBAL MakeDib(const std::vector<uint32_t>& px, int w, int h) {
    HGLOBAL mem = GlobalAlloc(GHND, sizeof(BITMAPINFOHEADER) + px.size() * 4);
    if (!mem) return nullptr;
    auto* hdr = static_cast<BITMAPINFOHEADER*>(GlobalLock(mem));
    *hdr = {sizeof(BITMAPINFOHEADER), w, h, 1, 32, BI_RGB};   // bottom-up DIB
    auto* dst = reinterpret_cast<uint32_t*>(hdr + 1);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) dst[(size_t)(h - 1 - y) * w + x] = px[(size_t)y * w + x] | 0xff000000;
    GlobalUnlock(mem);
    return mem;
}

HGLOBAL MakeDrop(const std::wstring& path) {
    HGLOBAL mem = GlobalAlloc(GHND, sizeof(DROPFILES) + (path.size() + 2) * sizeof(wchar_t));
    if (!mem) return nullptr;
    auto* df = static_cast<DROPFILES*>(GlobalLock(mem));
    df->pFiles = sizeof(DROPFILES);
    df->fWide = TRUE;
    memcpy(reinterpret_cast<BYTE*>(df) + sizeof(DROPFILES), path.c_str(), path.size() * sizeof(wchar_t));
    GlobalUnlock(mem);
    return mem;
}

bool SetClipboard(HGLOBAL dib, HGLOBAL drop) {
    if (!OpenClipboard(nullptr)) {
        if (dib) GlobalFree(dib);
        if (drop) GlobalFree(drop);
        return false;
    }
    EmptyClipboard();
    bool ok = true;
    if (dib && !SetClipboardData(CF_DIB, dib)) GlobalFree(dib), ok = false;
    if (drop && !SetClipboardData(CF_HDROP, drop)) GlobalFree(drop);
    CloseClipboard();
    return ok;
}

void CopyToClipboard(const std::vector<uint32_t>& px, int w, int h) { SetClipboard(MakeDib(px, w, h), nullptr); }
}  // namespace

bool CopyImageToClipboard(const std::wstring& path) {
    ComPtr<IWICBitmapDecoder> dec;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter> conv;
    if (FAILED(gfx::WIC()->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec)) ||
        FAILED(dec->GetFrame(0, &frame)) || FAILED(gfx::WIC()->CreateFormatConverter(&conv)) ||
        FAILED(conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGR, WICBitmapDitherTypeNone, nullptr, 0,
                                WICBitmapPaletteTypeCustom)))
        return false;
    UINT w = 0, h = 0;
    conv->GetSize(&w, &h);
    std::vector<uint32_t> px((size_t)w * h);
    if (FAILED(conv->CopyPixels(nullptr, w * 4, (UINT)(px.size() * 4), (BYTE*)px.data()))) return false;
    return SetClipboard(MakeDib(px, (int)w, (int)h), MakeDrop(path));
}

void TakeScreenshot(const ShotOptions& opt, HWND notify) {
    RECT r = opt.region;
    int w = r.right - r.left, h = r.bottom - r.top;
    std::vector<uint32_t> px;
    if (w > 0 && h > 0) {
        HDC screen = GetDC(nullptr);
        HDC mem = CreateCompatibleDC(screen);
        BITMAPINFO bi{};
        bi.bmiHeader = {sizeof(BITMAPINFOHEADER), w, -h, 1, 32, BI_RGB};
        void* bits = nullptr;
        HBITMAP bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (bmp) {
            HGDIOBJ old = SelectObject(mem, bmp);
            BitBlt(mem, 0, 0, w, h, screen, r.left, r.top, SRCCOPY | CAPTUREBLT);
            if (opt.cursor) {
                CURSORINFO ci{sizeof(ci)};
                ICONINFO ii{};
                if (GetCursorInfo(&ci) && (ci.flags & CURSOR_SHOWING) && GetIconInfo(ci.hCursor, &ii)) {
                    // DrawIconEx applies the cursor's masks like Windows does (incl. inverting I-beams).
                    DrawIconEx(mem, ci.ptScreenPos.x - (int)ii.xHotspot - r.left, ci.ptScreenPos.y - (int)ii.yHotspot - r.top,
                               ci.hCursor, 0, 0, 0, nullptr, DI_NORMAL | DI_DEFAULTSIZE);
                    if (ii.hbmColor) DeleteObject(ii.hbmColor);
                    if (ii.hbmMask) DeleteObject(ii.hbmMask);
                }
            }
            GdiFlush();
            px.assign(static_cast<uint32_t*>(bits), static_cast<uint32_t*>(bits) + (size_t)w * h);
            SelectObject(mem, old);
            DeleteObject(bmp);
        }
        DeleteDC(mem);
        ReleaseDC(nullptr, screen);
    }
    if (!px.empty() && opt.clipboard) CopyToClipboard(px, w, h);

    std::thread([opt, px = std::move(px), w, h, notify] {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        std::wstring dir = opt.path.substr(0, opt.path.find_last_of(L'\\'));
        SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
        bool ok = !px.empty() && Encode(px, w, h, opt);
        CoUninitialize();
        PostMessageW(notify, WM_APP_SCREENSHOT_DONE, 0, (LPARAM) new std::wstring(ok ? opt.path : L""));
    }).detach();
}

bool CopyPixelsToClipboard(const std::vector<uint32_t>& px, int w, int h) { return SetClipboard(MakeDib(px, w, h), nullptr); }

bool SaveImageFile(const std::vector<uint32_t>& px, int w, int h, const std::wstring& path, int jpgQuality) {
    ShotOptions o;
    o.path = path;
    o.jpg = FileExt(path) == L"jpg" || FileExt(path) == L"jpeg";
    o.jpgQuality = jpgQuality;
    return Encode(px, w, h, o);
}

bool LoadImageFile(const std::wstring& path, std::vector<uint32_t>& px, int& w, int& h) {
    ComPtr<IWICBitmapDecoder> dec;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter> conv;
    if (FAILED(gfx::WIC()->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &dec)) ||
        FAILED(dec->GetFrame(0, &frame)) || FAILED(gfx::WIC()->CreateFormatConverter(&conv)) ||
        FAILED(conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGR, WICBitmapDitherTypeNone, nullptr, 0,
                                WICBitmapPaletteTypeCustom)))
        return false;
    UINT uw = 0, uh = 0;
    conv->GetSize(&uw, &uh);
    px.assign((size_t)uw * uh, 0);
    if (FAILED(conv->CopyPixels(nullptr, uw * 4, (UINT)(px.size() * 4), (BYTE*)px.data()))) return false;
    for (auto& v : px) v |= 0xff000000;
    w = (int)uw;
    h = (int)uh;
    return true;
}