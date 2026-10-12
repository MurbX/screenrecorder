#pragma once
#include <windows.h>
#include <windowsx.h>
#include <d2d1_3.h>
#include <dwrite_3.h>
#include <d3d11.h>
#include <dxgi1_3.h>
#include <dcomp.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

using Microsoft::WRL::ComPtr;

// App-private window messages.
enum : UINT {
    WM_APP_RECORD_DONE = WM_APP + 1,   // lParam: heap RecordResult*
    WM_APP_FFMPEG_DIED,                // ffmpeg exited unexpectedly while recording
    WM_APP_WEBCAM_FRAME,               // a new webcam frame is ready
    WM_APP_MEDIA_INFO,                 // background thumbnail/probe finished
    WM_APP_PLAYER_EVENT,               // MFPlay event forwarded to the UI thread
    WM_APP_ENCODERS_READY,
    WM_APP_SCREENSHOT_DONE,            // lParam: heap std::wstring* (path, empty on failure)
    WM_APP_TRIM_DONE,                  // wParam: success, lParam: heap std::wstring* path
    WM_APP_UPDATE_CHECKED,             // lParam: heap updater::CheckResult*
    WM_APP_UPDATE_DOWNLOADED,          // wParam: success, lParam: heap std::wstring* installer path
};


inline D2D1_COLOR_F Rgb(uint32_t rgb, float a = 1.f) {
    return D2D1::ColorF(((rgb >> 16) & 0xff) / 255.f, ((rgb >> 8) & 0xff) / 255.f, (rgb & 0xff) / 255.f, a);
}
inline D2D1_RECT_F RectF(float l, float t, float r, float b) { return D2D1::RectF(l, t, r, b); }
inline D2D1_RECT_F RectXYWH(float x, float y, float w, float h) { return D2D1::RectF(x, y, x + w, y + h); }
inline bool InRect(const D2D1_RECT_F& r, float x, float y) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; }
inline D2D1_RECT_F Inflate(D2D1_RECT_F r, float d) { return D2D1::RectF(r.left - d, r.top - d, r.right + d, r.bottom + d); }
inline float RW(const D2D1_RECT_F& r) { return r.right - r.left; }
inline float RH(const D2D1_RECT_F& r) { return r.bottom - r.top; }
inline D2D1_POINT_2F Center(const D2D1_RECT_F& r) { return D2D1::Point2F((r.left + r.right) / 2, (r.top + r.bottom) / 2); }

std::wstring Widen(const std::string& s);
std::string Narrow(const std::wstring& s);
std::wstring Fmt(const wchar_t* fmt, ...);
std::wstring ExeDir();
std::wstring AppDataDir();      // %APPDATA%\ScreenRec (created)
std::wstring LocalDataDir();    // %LOCALAPPDATA%\ScreenRec (created)
std::wstring JoinPath(const std::wstring& a, const std::wstring& b);
std::wstring FileName(const std::wstring& path);
std::wstring FileExt(const std::wstring& path);     // lower-case, without dot
std::wstring StripExt(const std::wstring& path);
bool FileExists(const std::wstring& path);
uint64_t FileSize(const std::wstring& path);
std::wstring TimestampName();   // 20261012_143012
std::wstring FormatDuration(double seconds, bool forceHours = false);
std::wstring FormatBytes(uint64_t bytes);
