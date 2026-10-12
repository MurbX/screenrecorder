#pragma once
#include "common.h"

struct ShotOptions {
    RECT region{};                 // screen px
    bool cursor = true;
    bool jpg = false;
    int jpgQuality = 90;
    bool clipboard = true;
    std::wstring path;
};

// Grabs the region right away on the calling thread (GDI, so ScreenRec's own capture-excluded
// windows stay out and the cursor is drawn exactly as Windows draws it), then encodes and saves
// on a worker thread. Posts WM_APP_SCREENSHOT_DONE to `notify` with a heap std::wstring* (empty on failure).
void TakeScreenshot(const ShotOptions& opt, HWND notify);

// Puts an image file on the clipboard both as a picture (paste into chats/documents) and as a
// file (paste into Explorer).
bool CopyImageToClipboard(const std::wstring& path);

// Pixels are 0xAARRGGBB rows, top-down (alpha ignored).
bool CopyPixelsToClipboard(const std::vector<uint32_t>& px, int w, int h);
bool SaveImageFile(const std::vector<uint32_t>& px, int w, int h, const std::wstring& path, int jpgQuality = 92);
bool LoadImageFile(const std::wstring& path, std::vector<uint32_t>& px, int& w, int& h);
