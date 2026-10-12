#pragma once
#include "common.h"

namespace win {

void ExcludeFromCapture(HWND hwnd, bool exclude);
void SetClickThrough(HWND hwnd, bool clickThrough);
void EnableDarkMenus();
void StyleFrame(HWND hwnd);   // dark DWM frame, rounded corners, shadow
RECT PrimaryMonitorRect();    // physical px
RECT VirtualScreenRect();     // bounding box of all monitors

// A display plus the DXGI adapter/output pair ffmpeg's ddagrab needs to capture it.
struct Monitor {
    RECT rect{}, work{};
    int adapter = 0, output = 0;
    bool primary = false;
};
std::vector<Monitor> Monitors();
Monitor MonitorAt(POINT pt);          // nearest monitor to a point
Monitor MonitorFor(const RECT& r);    // monitor holding most of the rect
UINT DpiOf(HWND hwnd);

RECT WindowBounds(HWND hwnd);  // visible bounds (without the invisible resize border)
HWND TopLevelWindowAt(POINT pt);  // skips our own process's windows and shell desktop

struct MenuItem {
    int id = 0;               // 0 = separator
    std::wstring text;
    bool checked = false;
    bool enabled = true;
};
// Shows a dark popup menu at `screenPt` (physical px); returns the chosen id or 0.
int PopupMenu(HWND owner, POINT screenPt, const std::vector<MenuItem>& items);

std::optional<std::wstring> PickFolder(HWND owner, const std::wstring& initial);
std::optional<std::wstring> PromptText(HWND owner, const std::wstring& title, const std::wstring& initial);
bool Confirm(HWND owner, const std::wstring& title, const std::wstring& text);
void Error(HWND owner, const std::wstring& text);

bool RecycleFile(const std::wstring& path);
void ShowInFolder(const std::wstring& path);
void OpenWithShell(const std::wstring& path);
bool CopyFileToClipboard(HWND owner, const std::wstring& path);
bool CopyTextToClipboard(HWND owner, const std::wstring& text);
void SetRunAtLogin(bool enable);
// True when a JIT debugger (e.g. Visual Studio) owns the F12 key, so a plain-F12 hotkey never fires.
bool F12ReservedByDebugger();

// Runs a console program hidden and waits for it; stdout+stderr go to `output` when given.
// Returns the process exit code, or -1 if it could not start / timed out.
int RunHidden(const std::wstring& cmdline, std::string* output = nullptr, DWORD timeoutMs = 60000);

std::wstring Quote(const std::wstring& s);   // command-line argument quoting

}  // namespace win
