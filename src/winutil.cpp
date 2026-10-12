#include "winutil.h"
#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <uxtheme.h>

#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif

namespace win {

void ExcludeFromCapture(HWND hwnd, bool exclude) {
    // Developer switch: SCREENREC_SHOW_UI=1 keeps our own windows visible to capture (for screenshots of the UI).
    static const bool showUi = GetEnvironmentVariableW(L"SCREENREC_SHOW_UI", nullptr, 0) > 0;
    if (showUi) exclude = false;
    if (hwnd) SetWindowDisplayAffinity(hwnd, exclude ? WDA_EXCLUDEFROMCAPTURE : WDA_NONE);
}

void SetClickThrough(HWND hwnd, bool clickThrough) {
    // Mouse pass-through needs LAYERED|TRANSPARENT; LAYERED is dropped again when not needed
    // because it changes how DirectComposition content is blended.
    LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    LONG_PTR next = clickThrough ? (ex | WS_EX_TRANSPARENT | WS_EX_LAYERED) : (ex & ~(WS_EX_TRANSPARENT | WS_EX_LAYERED));
    if (next != ex) {
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, next);
        if (next & WS_EX_LAYERED) SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
    }
}

void EnableDarkMenus() {
    // Undocumented but stable uxtheme exports (used by Explorer/Notepad) that switch
    // popup menus to the dark theme.
    HMODULE ux = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!ux) return;
    using SetPreferredAppModeFn = int(WINAPI*)(int);
    using FlushMenuThemesFn = void(WINAPI*)();
    auto setMode = reinterpret_cast<SetPreferredAppModeFn>(GetProcAddress(ux, MAKEINTRESOURCEA(135)));
    auto flush = reinterpret_cast<FlushMenuThemesFn>(GetProcAddress(ux, MAKEINTRESOURCEA(136)));
    if (setMode) setMode(2 /* ForceDark */);
    if (flush) flush();
}

void StyleFrame(HWND hwnd) {
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
    int corner = 2 /* DWMWCP_ROUND */;
    DwmSetWindowAttribute(hwnd, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &corner, sizeof(corner));
    COLORREF border = RGB(0x4a, 0x4a, 0x4e);
    DwmSetWindowAttribute(hwnd, 34 /* DWMWA_BORDER_COLOR */, &border, sizeof(border));
    MARGINS m{0, 0, 0, 1};
    DwmExtendFrameIntoClientArea(hwnd, &m);
}

RECT PrimaryMonitorRect() {
    HMONITOR mon = MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(mon, &mi);
    return mi.rcMonitor;
}

RECT VirtualScreenRect() {
    int x = GetSystemMetrics(SM_XVIRTUALSCREEN), y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    return {x, y, x + GetSystemMetrics(SM_CXVIRTUALSCREEN), y + GetSystemMetrics(SM_CYVIRTUALSCREEN)};
}

std::vector<Monitor> Monitors() {
    std::vector<Monitor> out;
    std::vector<HMONITOR> seen;
    ComPtr<IDXGIFactory1> factory;
    if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        ComPtr<IDXGIAdapter1> adapter;
        for (UINT a = 0; factory->EnumAdapters1(a, &adapter) != DXGI_ERROR_NOT_FOUND; ++a, adapter.Reset()) {
            ComPtr<IDXGIOutput> output;
            for (UINT o = 0; adapter->EnumOutputs(o, &output) != DXGI_ERROR_NOT_FOUND; ++o, output.Reset()) {
                DXGI_OUTPUT_DESC d{};
                if (FAILED(output->GetDesc(&d)) || !d.AttachedToDesktop) continue;
                if (std::find(seen.begin(), seen.end(), d.Monitor) != seen.end()) continue;
                seen.push_back(d.Monitor);
                MONITORINFO mi{sizeof(mi)};
                GetMonitorInfoW(d.Monitor, &mi);
                out.push_back({mi.rcMonitor, mi.rcWork, (int)a, (int)o, (mi.dwFlags & MONITORINFOF_PRIMARY) != 0});
            }
        }
    }
    if (out.empty()) {
        HMONITOR m = MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO mi{sizeof(mi)};
        GetMonitorInfoW(m, &mi);
        out.push_back({mi.rcMonitor, mi.rcWork, 0, 0, true});
    }
    return out;
}

static Monitor MonitorFromHandle(HMONITOR h) {
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(h, &mi);
    for (auto& m : Monitors())
        if (EqualRect(&m.rect, &mi.rcMonitor)) return m;
    return Monitors().front();
}

Monitor MonitorAt(POINT pt) { return MonitorFromHandle(MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST)); }
Monitor MonitorFor(const RECT& r) { return MonitorFromHandle(MonitorFromRect(&r, MONITOR_DEFAULTTONEAREST)); }

UINT DpiOf(HWND hwnd) {
    UINT d = hwnd ? GetDpiForWindow(hwnd) : 0;
    return d ? d : GetDpiForSystem();
}

RECT WindowBounds(HWND hwnd) {
    RECT r{};
    if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof(r)))) GetWindowRect(hwnd, &r);
    return r;
}

HWND TopLevelWindowAt(POINT pt) {
    struct Ctx {
        POINT pt;
        HWND found = nullptr;
    } ctx{pt};
    EnumWindows(
        [](HWND h, LPARAM lp) -> BOOL {
            auto* c = reinterpret_cast<Ctx*>(lp);
            if (!IsWindowVisible(h) || IsIconic(h)) return TRUE;
            DWORD pid = 0;
            GetWindowThreadProcessId(h, &pid);
            if (pid == GetCurrentProcessId()) return TRUE;
            BOOL cloaked = FALSE;
            DwmGetWindowAttribute(h, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
            if (cloaked) return TRUE;
            if (GetWindowLongPtrW(h, GWL_EXSTYLE) & WS_EX_TRANSPARENT) return TRUE;
            wchar_t cls[64];
            GetClassNameW(h, cls, 64);
            if (!wcscmp(cls, L"Progman") || !wcscmp(cls, L"WorkerW") || !wcscmp(cls, L"Shell_TrayWnd")) return TRUE;
            RECT r = WindowBounds(h);
            if (r.right - r.left < 40 || r.bottom - r.top < 40) return TRUE;
            if (!PtInRect(&r, c->pt)) return TRUE;
            c->found = h;
            return FALSE;
        },
        reinterpret_cast<LPARAM>(&ctx));
    return ctx.found;
}

int PopupMenu(HWND owner, POINT pt, const std::vector<MenuItem>& items) {
    HMENU m = CreatePopupMenu();
    for (auto& it : items) {
        if (it.id == 0) {
            AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
            continue;
        }
        UINT flags = MF_STRING | (it.checked ? MF_CHECKED : 0) | (it.enabled ? 0 : MF_GRAYED);
        AppendMenuW(m, flags, it.id, it.text.c_str());
    }
    SetForegroundWindow(owner);
    int id = TrackPopupMenuEx(m, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, pt.x, pt.y, owner, nullptr);
    DestroyMenu(m);
    return id;
}

std::optional<std::wstring> PickFolder(HWND owner, const std::wstring& initial) {
    ComPtr<IFileOpenDialog> dlg;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return {};
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    ComPtr<IShellItem> start;
    if (!initial.empty() && SUCCEEDED(SHCreateItemFromParsingName(initial.c_str(), nullptr, IID_PPV_ARGS(&start))))
        dlg->SetFolder(start.Get());
    if (FAILED(dlg->Show(owner))) return {};
    ComPtr<IShellItem> item;
    if (FAILED(dlg->GetResult(&item))) return {};
    PWSTR p = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &p))) return {};
    std::wstring s = p;
    CoTaskMemFree(p);
    return s;
}

// ---------------------------------------------------------------------------------------------
// Small modal text prompt (used for Rename).

namespace {
struct Prompt {
    std::wstring title, text;
    bool done = false, ok = false;
    HWND edit = nullptr;
    HFONT font = nullptr;
    HBRUSH bg = nullptr, field = nullptr;
};

LRESULT CALLBACK PromptProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    auto* st = reinterpret_cast<Prompt*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    switch (msg) {
    case WM_NCCREATE:
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCTW*)lp)->lpCreateParams);
        break;
    case WM_CREATE: {
        float k = DpiOf(h) / 96.f;
        auto S = [k](int v) { return (int)(v * k); };
        st->font = CreateFontW(-S(14), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        st->edit = CreateWindowExW(0, L"EDIT", st->text.c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                   S(16), S(18), S(368), S(26), h, (HMENU)100, nullptr, nullptr);
        HWND ok = CreateWindowExW(0, L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, S(208),
                                  S(60), S(84), S(28), h, (HMENU)IDOK, nullptr, nullptr);
        HWND cancel = CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP, S(300), S(60), S(84),
                                      S(28), h, (HMENU)IDCANCEL, nullptr, nullptr);
        for (HWND c : {st->edit, ok, cancel}) SendMessageW(c, WM_SETFONT, (WPARAM)st->font, TRUE);
        SetWindowTheme(st->edit, L"DarkMode_CFD", nullptr);
        SetWindowTheme(ok, L"DarkMode_Explorer", nullptr);
        SetWindowTheme(cancel, L"DarkMode_Explorer", nullptr);
        SetFocus(st->edit);
        // Select the name without the extension, like Explorer.
        size_t dot = st->text.find_last_of(L'.');
        SendMessageW(st->edit, EM_SETSEL, 0, dot == std::wstring::npos ? -1 : (LPARAM)dot);
        return 0;
    }
    case WM_CTLCOLOREDIT:
        SetTextColor((HDC)wp, RGB(0xf2, 0xf2, 0xf7));
        SetBkColor((HDC)wp, RGB(0x1c, 0x1c, 0x1e));
        return (LRESULT)st->field;
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORSTATIC:
        SetBkColor((HDC)wp, RGB(0x2c, 0x2c, 0x2e));
        return (LRESULT)st->bg;
    case WM_ERASEBKGND: {
        RECT r;
        GetClientRect(h, &r);
        FillRect((HDC)wp, &r, st->bg);
        return 1;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK) {
            int n = GetWindowTextLengthW(st->edit);
            std::wstring s(n, L'\0');
            GetWindowTextW(st->edit, s.data(), n + 1);
            st->text = s;
            st->ok = true;
            st->done = true;
        } else if (LOWORD(wp) == IDCANCEL) {
            st->done = true;
        }
        return 0;
    case WM_CLOSE:
        st->done = true;
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}
}  // namespace

std::optional<std::wstring> PromptText(HWND owner, const std::wstring& title, const std::wstring& initial) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = PromptProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = L"ScreenRecPrompt";
        RegisterClassW(&wc);
        registered = true;
    }
    Prompt st{title, initial};
    st.bg = CreateSolidBrush(RGB(0x2c, 0x2c, 0x2e));
    st.field = CreateSolidBrush(RGB(0x1c, 0x1c, 0x1e));
    float k = DpiOf(owner) / 96.f;
    RECT orc{};
    GetWindowRect(owner, &orc);
    RECT r{0, 0, (LONG)(400 * k), (LONG)(104 * k)};
    AdjustWindowRectExForDpi(&r, WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME, DpiOf(owner));
    int w = r.right - r.left, hgt = r.bottom - r.top;
    int x = (orc.left + orc.right - w) / 2, y = (orc.top + orc.bottom - hgt) / 2;
    HWND h = CreateWindowExW(WS_EX_DLGMODALFRAME, L"ScreenRecPrompt", title.c_str(), WS_CAPTION | WS_SYSMENU, x, y, w, hgt,
                             owner, nullptr, GetModuleHandleW(nullptr), &st);
    BOOL dark = TRUE;
    DwmSetWindowAttribute(h, 20, &dark, sizeof(dark));
    EnableWindow(owner, FALSE);
    ShowWindow(h, SW_SHOW);
    MSG msg;
    while (!st.done && GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(h, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    EnableWindow(owner, TRUE);
    DestroyWindow(h);
    SetForegroundWindow(owner);
    DeleteObject(st.font);
    DeleteObject(st.bg);
    DeleteObject(st.field);
    if (!st.ok) return {};
    return st.text;
}

bool Confirm(HWND owner, const std::wstring& title, const std::wstring& text) {
    return MessageBoxW(owner, text.c_str(), title.c_str(), MB_YESNO | MB_ICONQUESTION) == IDYES;
}

void Error(HWND owner, const std::wstring& text) { MessageBoxW(owner, text.c_str(), L"ScreenRec", MB_OK | MB_ICONERROR); }

// ---------------------------------------------------------------------------------------------

bool RecycleFile(const std::wstring& path) {
    std::wstring from = path + L'\0';   // double-null terminated
    SHFILEOPSTRUCTW op{};
    op.wFunc = FO_DELETE;
    op.pFrom = from.c_str();
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
    return SHFileOperationW(&op) == 0 && !op.fAnyOperationsAborted;
}

void ShowInFolder(const std::wstring& path) {
    std::wstring args = L"/select," + Quote(path);
    ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
}

void OpenWithShell(const std::wstring& path) { ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL); }

bool CopyFileToClipboard(HWND owner, const std::wstring& path) {
    size_t bytes = sizeof(DROPFILES) + (path.size() + 2) * sizeof(wchar_t);
    HGLOBAL mem = GlobalAlloc(GHND, bytes);
    if (!mem) return false;
    auto* df = static_cast<DROPFILES*>(GlobalLock(mem));
    df->pFiles = sizeof(DROPFILES);
    df->fWide = TRUE;
    memcpy(reinterpret_cast<BYTE*>(df) + sizeof(DROPFILES), path.c_str(), path.size() * sizeof(wchar_t));
    GlobalUnlock(mem);
    if (!OpenClipboard(owner)) {
        GlobalFree(mem);
        return false;
    }
    EmptyClipboard();
    bool ok = SetClipboardData(CF_HDROP, mem) != nullptr;
    CloseClipboard();
    if (!ok) GlobalFree(mem);
    return ok;
}

bool CopyTextToClipboard(HWND owner, const std::wstring& text) {
    HGLOBAL mem = GlobalAlloc(GHND, (text.size() + 1) * sizeof(wchar_t));
    if (!mem) return false;
    memcpy(GlobalLock(mem), text.c_str(), (text.size() + 1) * sizeof(wchar_t));
    GlobalUnlock(mem);
    if (!OpenClipboard(owner)) {
        GlobalFree(mem);
        return false;
    }
    EmptyClipboard();
    bool ok = SetClipboardData(CF_UNICODETEXT, mem) != nullptr;
    CloseClipboard();
    if (!ok) GlobalFree(mem);
    return ok;
}

void SetRunAtLogin(bool enable) {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &key))
        return;
    if (enable) {
        wchar_t exe[MAX_PATH];
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::wstring v = Quote(exe);
        RegSetValueExW(key, L"ScreenRec", 0, REG_SZ, (const BYTE*)v.c_str(), (DWORD)((v.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(key, L"ScreenRec");
    }
    RegCloseKey(key);
}

bool F12ReservedByDebugger() {
    const wchar_t* key = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\AeDebug";
    wchar_t debugger[MAX_PATH] = {};
    DWORD size = sizeof(debugger);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, key, L"Debugger", RRF_RT_REG_SZ, nullptr, debugger, &size) || !debugger[0])
        return false;
    DWORD vk = 0;   // 0 (the default) means VK_F12
    size = sizeof(vk);
    RegGetValueW(HKEY_LOCAL_MACHINE, key, L"UserDebuggerHotKey", RRF_RT_REG_DWORD, nullptr, &vk, &size);
    return vk == 0 || vk == VK_F12;
}

int RunHidden(const std::wstring& cmdline, std::string* output, DWORD timeoutMs) {
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return -1;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);

    STARTUPINFOW si{sizeof(si)};
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = nul;
    si.hStdOutput = wr;
    si.hStdError = wr;
    PROCESS_INFORMATION pi{};
    std::wstring cmd = cmdline;
    BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(wr);
    CloseHandle(nul);
    if (!ok) {
        CloseHandle(rd);
        return -1;
    }
    std::string out;
    std::thread reader([&] {
        char buf[4096];
        DWORD n;
        while (ReadFile(rd, buf, sizeof(buf), &n, nullptr) && n) out.append(buf, n);
    });
    int code = -1;
    if (WaitForSingleObject(pi.hProcess, timeoutMs) == WAIT_OBJECT_0) {
        DWORD c = 0;
        GetExitCodeProcess(pi.hProcess, &c);
        code = (int)c;
    } else {
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 2000);
    }
    reader.join();
    CloseHandle(rd);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    if (output) *output = std::move(out);
    return code;
}

std::wstring Quote(const std::wstring& s) {
    if (!s.empty() && s.find_first_of(L" \t\"") == std::wstring::npos) return s;
    std::wstring r = L"\"";
    size_t backslashes = 0;
    for (wchar_t c : s) {
        if (c == L'\\') {
            backslashes++;
            continue;
        }
        if (c == L'"') r.append(backslashes * 2 + 1, L'\\');
        else r.append(backslashes, L'\\');
        backslashes = 0;
        r += c;
    }
    r.append(backslashes * 2, L'\\');
    r += L'"';
    return r;
}

}  // namespace win
