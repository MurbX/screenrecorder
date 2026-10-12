#include "app.h"
#include "gfx.h"
#include <mfapi.h>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    // One instance only: bring the running one to the front instead.
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\ScreenRec.SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND other = FindWindowW(L"ScreenRecWindow", L"ScreenRec")) {
            ShowWindow(other, SW_RESTORE);
            SetForegroundWindow(other);
        }
        return 0;
    }

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    OleInitialize(nullptr);   // COM (STA) + OLE drag-and-drop for dragging screenshots out
    MFStartup(MF_VERSION);

    int code = 1;
    if (!gfx::Init()) {
        MessageBoxW(nullptr, L"Could not initialise Direct2D / Direct3D.", L"ScreenRec", MB_ICONERROR);
    } else {
        App app;
        if (app.Init()) code = app.Run();
    }

    MFShutdown();
    OleUninitialize();
    if (mutex) CloseHandle(mutex);
    return code;
}
