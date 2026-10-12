#include "cursorfx.h"
#include "theme.h"

namespace cursorfx {
namespace {

bool g_applied = false;

// Standard cursors that get the effect. The animated busy cursors (OCR_WAIT, OCR_APPSTARTING)
// are left alone so they keep animating.
const DWORD kCursorIds[] = {32512 /*NORMAL*/, 32513 /*IBEAM*/,   32515 /*CROSS*/,   32516 /*UP*/,
                            32642 /*SIZENWSE*/, 32643 /*SIZENESW*/, 32644 /*SIZEWE*/, 32645 /*SIZENS*/,
                            32646 /*SIZEALL*/, 32648 /*NO*/,      32649 /*HAND*/,    32651 /*HELP*/};

std::wstring MarkerPath() { return JoinPath(LocalDataDir(), L"cursors_replaced"); }

// True when the user's cursor scheme uses an animated (.ani) file for this cursor; a copy would
// freeze on its first frame, so such cursors are left alone.
bool IsAnimated(DWORD id) {
    const wchar_t* name = nullptr;
    switch (id) {
    case 32512: name = L"Arrow"; break;
    case 32513: name = L"IBeam"; break;
    case 32515: name = L"Crosshair"; break;
    case 32516: name = L"UpArrow"; break;
    case 32642: name = L"SizeNWSE"; break;
    case 32643: name = L"SizeNESW"; break;
    case 32644: name = L"SizeWE"; break;
    case 32645: name = L"SizeNS"; break;
    case 32646: name = L"SizeAll"; break;
    case 32648: name = L"No"; break;
    case 32649: name = L"Hand"; break;
    case 32651: name = L"Help"; break;
    default: return false;
    }
    wchar_t path[MAX_PATH] = {};
    DWORD size = sizeof(path);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Control Panel\\Cursors", name, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND,
                     nullptr, path, &size))
        return false;
    return FileExt(path) == L"ani";
}

// Cursor as straight-alpha 0xAARRGGBB pixels.
struct Pixels {
    int w = 0, h = 0;
    POINT hot{};
    std::vector<uint32_t> argb;
    bool invert = false;   // has screen-inverting pixels (e.g. the default I-beam)
};

std::vector<uint32_t> ReadBits(HBITMAP b, int w, int rows) {
    std::vector<uint32_t> px((size_t)w * rows);
    BITMAPINFO bi{};
    bi.bmiHeader = {sizeof(BITMAPINFOHEADER), w, -rows, 1, 32, BI_RGB};
    HDC dc = GetDC(nullptr);
    GetDIBits(dc, b, 0, rows, px.data(), &bi, DIB_RGB_COLORS);
    ReleaseDC(nullptr, dc);
    return px;
}

bool Read(HCURSOR cursor, Pixels& out) {
    ICONINFO ii{};
    if (!cursor || !GetIconInfo(cursor, &ii)) return false;
    BITMAP bm{};
    GetObjectW(ii.hbmMask, sizeof(bm), &bm);
    int w = bm.bmWidth, h = ii.hbmColor ? bm.bmHeight : bm.bmHeight / 2;   // mono masks stack AND over XOR
    auto mask = ReadBits(ii.hbmMask, w, ii.hbmColor ? h : h * 2);
    auto color = ii.hbmColor ? ReadBits(ii.hbmColor, w, h) : std::vector<uint32_t>{};
    bool alpha = std::any_of(color.begin(), color.end(), [](uint32_t p) { return (p >> 24) != 0; });
    out.w = w;
    out.h = h;
    out.hot = {(LONG)ii.xHotspot, (LONG)ii.yHotspot};
    out.argb.assign((size_t)w * h, 0);
    for (size_t i = 0; i < out.argb.size(); ++i) {
        bool andBit = (mask[i] & 0xffffff) != 0;
        if (ii.hbmColor) {
            if (alpha) out.argb[i] = color[i];
            else if (!andBit) out.argb[i] = 0xff000000 | (color[i] & 0xffffff);
            else if (color[i] & 0xffffff) out.invert = true;
        } else {
            bool xorBit = (mask[i + (size_t)w * h] & 0xffffff) != 0;
            if (!andBit) out.argb[i] = xorBit ? 0xffffffff : 0xff000000;
            else if (xorBit) out.invert = true;
        }
    }
    if (ii.hbmColor) DeleteObject(ii.hbmColor);
    DeleteObject(ii.hbmMask);
    return true;
}

POINT Offset(const Pixels& p) {
    int x0 = p.w, y0 = p.h, x1 = -1, y1 = -1;
    for (int y = 0; y < p.h; ++y)
        for (int x = 0; x < p.w; ++x)
            if (p.argb[(size_t)y * p.w + x] >> 24) {
                x0 = std::min(x0, x), y0 = std::min(y0, y), x1 = std::max(x1, x), y1 = std::max(y1, y);
            }
    if (x1 < 0) return {0, 0};
    return {(x0 + x1) / 2 - p.hot.x, (y0 + y1) / 2 - p.hot.y};
}

// Straight-alpha "over" compositing of one pixel.
uint32_t Over(uint32_t src, uint32_t dst) {
    float sa = (src >> 24) / 255.f, da = (dst >> 24) / 255.f;
    float oa = sa + da * (1 - sa);
    if (oa <= 0) return 0;
    auto ch = [&](int shift) {
        float s = (float)((src >> shift) & 0xff), d = (float)((dst >> shift) & 0xff);
        return (uint32_t)std::lround((s * sa + d * da * (1 - sa)) / oa);
    };
    return ((uint32_t)std::lround(oa * 255) << 24) | (ch(16) << 16) | (ch(8) << 8) | ch(0);
}

// Antialiased filled circle.
void Disc(std::vector<uint32_t>& buf, int size, float cx, float cy, float r, uint32_t rgb, float alpha) {
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            float d = std::hypot(x + 0.5f - cx, y + 0.5f - cy);
            float cov = std::clamp(r + 0.5f - d, 0.f, 1.f);
            if (cov <= 0) continue;
            uint32_t a = (uint32_t)std::lround(alpha * cov * 255);
            size_t i = (size_t)y * size + x;
            buf[i] = Over((a << 24) | (rgb & 0xffffff), buf[i]);
        }
}

HCURSOR Build(const Pixels& src, int haloR, bool halo, bool dot, int dotR) {
    POINT off = Offset(src);
    float vcx = (float)(src.hot.x + off.x), vcy = (float)(src.hot.y + off.y);   // visual center in src
    float ext = std::max({vcx, src.w - vcx, vcy, src.h - vcy});
    float need = std::max({halo ? haloR + 1.f : 0.f, dot ? dotR + 3.f : 0.f, dot ? 0.f : ext});
    int size = std::min(256, 2 * (int)std::ceil(need) + 2);
    float c = size / 2.f;
    std::vector<uint32_t> buf((size_t)size * size, 0);

    if (halo) Disc(buf, size, c, c, (float)haloR, th::yellow, 0.42f);
    POINT hot;
    if (dot) {
        Disc(buf, size, c, c, dotR + 1.5f, 0x000000, 0.6f);
        Disc(buf, size, c, c, (float)dotR, 0xffffff, 0.97f);
        hot = {(LONG)c, (LONG)c};
    } else {
        int ox = (int)std::lround(c - vcx), oy = (int)std::lround(c - vcy);
        for (int y = 0; y < src.h; ++y)
            for (int x = 0; x < src.w; ++x) {
                int tx = ox + x, ty = oy + y;
                if (tx < 0 || ty < 0 || tx >= size || ty >= size) continue;
                size_t i = (size_t)ty * size + tx;
                buf[i] = Over(src.argb[(size_t)y * src.w + x], buf[i]);
            }
        hot = {ox + src.hot.x, oy + src.hot.y};
    }

    BITMAPV5HEADER bi{};
    bi.bV5Size = sizeof(bi);
    bi.bV5Width = size;
    bi.bV5Height = size;   // bottom-up
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask = 0x00ff0000;
    bi.bV5GreenMask = 0x0000ff00;
    bi.bV5BlueMask = 0x000000ff;
    bi.bV5AlphaMask = 0xff000000;
    void* bits = nullptr;
    HDC dc = GetDC(nullptr);
    HBITMAP color = CreateDIBSection(dc, reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, dc);
    if (!color) return nullptr;
    auto* dst = static_cast<uint32_t*>(bits);
    for (int y = 0; y < size; ++y) memcpy(dst + (size_t)(size - 1 - y) * size, &buf[(size_t)y * size], size * 4);
    std::vector<BYTE> zeros(((size + 15) / 16) * 2 * size, 0);
    HBITMAP mask = CreateBitmap(size, size, 1, 1, zeros.data());
    ICONINFO ii{FALSE, (DWORD)hot.x, (DWORD)hot.y, mask, color};
    HCURSOR cursor = CreateIconIndirect(&ii);
    DeleteObject(mask);
    DeleteObject(color);
    return cursor;
}
}  // namespace

POINT VisualOffset(HCURSOR cursor) {
    static std::vector<std::pair<HCURSOR, POINT>> cache;
    for (auto& [h, off] : cache)
        if (h == cursor) return off;
    Pixels p;
    POINT off = Read(cursor, p) ? Offset(p) : POINT{0, 0};
    cache.push_back({cursor, off});
    return off;
}

void Apply(bool halo, bool dot) {
    Restore();
    if (!halo && !dot) return;
    float k = GetDpiForSystem() / 96.f;
    int haloR = (int)std::lround(26 * k), dotR = (int)std::lround(7 * k);
    // Marker first, so a crash mid-way still gets cleaned up on the next start.
    HANDLE f = CreateFileW(MarkerPath().c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
    for (DWORD id : kCursorIds) {
        if (!dot && IsAnimated(id)) continue;   // keep custom animated cursors animating
        Pixels p;
        if (!Read(LoadCursorW(nullptr, MAKEINTRESOURCEW(id)), p)) continue;
        // Inverting cursors can't be reproduced with a translucent halo, so leave them exactly as
        // Windows draws them rather than altering how they look.
        if (p.invert && !dot) continue;
        if (HCURSOR c = Build(p, haloR, halo, dot, dotR)) SetSystemCursor(c, id);   // takes ownership
    }
    g_applied = true;
}

void Restore() {
    if (!g_applied && !FileExists(MarkerPath())) return;
    SystemParametersInfoW(SPI_SETCURSORS, 0, nullptr, 0);   // reload the user's cursor scheme
    DeleteFileW(MarkerPath().c_str());
    g_applied = false;
}

void RecoverIfNeeded() { Restore(); }

}  // namespace cursorfx
