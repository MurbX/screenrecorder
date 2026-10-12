#include "main_window.h"
#include "app.h"
#include "winutil.h"

namespace {
constexpr float kW = 520, kH = 162, kCaption = 32;
}

bool MainWindow::Create() {
    frameless_ = true;
    captionH_ = kCaption;
    float k = GetDpiForSystem() / 96.f;
    RECT mon = win::PrimaryMonitorRect();
    int w = (int)(kW * k), h = (int)(kH * k);
    int x = mon.left + (mon.right - mon.left - w) / 2, y = mon.top + (int)((mon.bottom - mon.top) * 0.28f);
    return Window::Create(L"ScreenRec", WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, WS_EX_APPWINDOW,
                          {x, y, x + w, y + h}, nullptr, false);
}

LRESULT MainWindow::OnMessage(UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_HOTKEY || msg == WM_TIMER || (msg > WM_APP && msg < WM_APP + 100)) {
        if (app().HandleAppMessage(msg, wp, lp)) return 0;
    }
    switch (msg) {
    case WM_CLOSE:
        app().Quit();
        return 0;
    case WM_GETMINMAXINFO:
        break;
    }
    return Window::OnMessage(msg, wp, lp);
}

void MainWindow::Layout() {
    float w = W();
    // Title bar buttons (right side).
    float bx = w - 10 - 28;
    Add(RectXYWH(bx, 4, 28, 24), [](gfx::Painter& p, const Control& c) { ui::IconButton(p, c, Icon::Close, th::text2, 13); },
        [] { app().Quit(); });
    bx -= 30;
    Add(RectXYWH(bx, 4, 28, 24), [](gfx::Painter& p, const Control& c) { ui::IconButton(p, c, Icon::Minimize, th::text2, 13); },
        [this] { ShowWindow(hwnd_, SW_MINIMIZE); });
    bx -= 38;
    Add(RectXYWH(bx, 4, 28, 24), [](gfx::Painter& p, const Control& c) { ui::IconButton(p, c, Icon::Folder, th::text2, 15); },
        [] { app().OpenOutputFolder(); });
    bx -= 30;
    Add(RectXYWH(bx, 4, 28, 24), [](gfx::Painter& p, const Control& c) { ui::IconButton(p, c, Icon::List, th::text2, 15); },
        [] { app().ShowOutputs(); });

    // Mode tiles.
    struct TileDef {
        Icon icon;
        const wchar_t* label;
        Mode mode;
    };
    static const TileDef tiles[] = {
        {Icon::SelectArea, L"Selected Area", Mode::Area}, {Icon::Monitor, L"Full screen", Mode::Fullscreen},
        {Icon::Window, L"Window", Mode::Window},          {Icon::Webcam, L"Webcam", Mode::Webcam},
        {Icon::Audio, L"Audio", Mode::Audio},
    };
    const float margin = 12, gap = 8, top = kCaption + 2, tileH = 74;
    float tw = (w - margin * 2 - gap * 4) / 5;
    for (int i = 0; i < 5; ++i) {
        const TileDef& t = tiles[i];
        Add(RectXYWH(margin + i * (tw + gap), top, tw, tileH),
            [t](gfx::Painter& p, const Control& c) { ui::Tile(p, c, t.icon, t.label); },
            [m = t.mode] { app().ChooseMode(m); });
    }

    // Quick toggles row.
    float y = top + tileH + 12, d = 28, x = margin + 2;
    auto toggle = [&](Icon on, Icon off, std::function<bool()> state, std::function<void()> act) {
        Add(RectXYWH(x, y, d, d),
            [on, off, state](gfx::Painter& p, const Control& c) { ui::RoundToggle(p, c, state() ? on : off, state()); },
            act);
        x += d + 9;
    };
    toggle(Icon::Cursor, Icon::Cursor, [] { return S().cursor; }, [this, cx = x, cy = y + d + 4] {
        app().ShowCursorMenu(hwnd_, ToScreen(cx, cy));
    });
    toggle(Icon::Camcorder, Icon::Camcorder, [] { return S().webcam; }, [] { app().ToggleWebcamOverlay(); });
    toggle(Icon::Speaker, Icon::SpeakerOff, [] { return S().recSpeaker; }, [] { app().ToggleSpeaker(); });
    toggle(Icon::Mic, Icon::MicOff, [] { return S().recMic; }, [] { app().ToggleMic(); });
    x += 10;
    toggle(Icon::Annotate, Icon::Annotate, [] { return app().Drawing(); }, [] { app().ToggleDrawing(); });
    toggle(Icon::SelectArea, Icon::SelectArea, [] { return false; }, [] { app().ReselectArea(); });
    x += 10;
    toggle(Icon::Camera, Icon::Camera, [] { return false; }, [this, cx = x, cy = y + d + 4] {
        app().ShowScreenshotMenu(hwnd_, ToScreen(cx, cy));   // area / window / full screen
    });

    Add(RectXYWH(w - margin - 30, y - 1, 30, 30),
        [](gfx::Painter& p, const Control& c) {
            ui::IconButton(p, c, Icon::Gear, th::text2, 20);
            if (app().UpdateAvailable()) p.Circle({c.rc.right - 6, c.rc.top + 6}, 4, Rgb(th::blue));   // update badge
        },
        [] { app().ShowSettings(); });
    float pw = gfx::MeasureText(S().ResolutionLabel(), 12.5f, DWRITE_FONT_WEIGHT_MEDIUM) + 44;
    Add(RectXYWH(w - margin - 30 - 10 - pw, y, pw, d),
        [](gfx::Painter& p, const Control& c) { ui::Pill(p, c, S().ResolutionLabel()); },
        [this, px = w - margin - 40 - pw, py = y + d + 4] { app().ShowResolutionMenu(hwnd_, ToScreen(px, py)); });
}

void MainWindow::Paint(gfx::Painter& p) {
    p.Clear(Rgb(th::bg));
    p.Text(L"SCREENREC", RectF(0, 0, W(), kCaption), 12.5f, Rgb(th::text), DWRITE_FONT_WEIGHT_BOLD,
           DWRITE_TEXT_ALIGNMENT_CENTER);
    PaintControls(p);
}
