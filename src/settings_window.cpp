#include "settings_window.h"
#include "app.h"
#include "recorder.h"
#include "updater.h"
#include "webcam.h"
#include "winutil.h"
#include <shlobj.h>

namespace {
constexpr float kW = 580, kH = 640, kTabsH = 92, kLabelW = 160, kRowH = 34;

struct Choice {
    int id;
    std::wstring text;
};
}  // namespace

bool SettingsWindow::Open(int tab) {
    if (tab >= 0) tab_ = tab;
    if (!hwnd_) {
        frameless_ = true;
        captionH_ = kTabsH;
        float k = GetDpiForSystem() / 96.f;
        RECT mon = win::PrimaryMonitorRect();
        int w = (int)(kW * k), h = (int)(kH * k);
        int x = mon.left + (mon.right - mon.left - w) / 2, y = mon.top + (mon.bottom - mon.top - h) / 2;
        if (!Window::Create(L"ScreenRec Settings", WS_POPUP | WS_CAPTION | WS_SYSMENU, WS_EX_APPWINDOW, {x, y, x + w, y + h},
                            nullptr, false))
            return false;
    }
    win::ExcludeFromCapture(hwnd_, S().excludeSelf);
    speakers_ = ListAudioDevices(false);
    mics_ = ListAudioDevices(true);
    cams_ = ListWebcams();
    Relayout();
    Show(true);
    SetForegroundWindow(hwnd_);
    return true;
}

void SettingsWindow::EndCapture() {
    if (!capturing_) return;
    capturing_ = nullptr;
    app().SuspendHotkeys(false);
    Relayout();
}

LRESULT SettingsWindow::OnMessage(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CLOSE:
        EndCapture();
        Show(false);
        return 0;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (capturing_) {
            UINT vk = (UINT)wp;
            if (vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU || vk == VK_LWIN || vk == VK_RWIN) return 0;
            if (vk == VK_ESCAPE) {
                EndCapture();
                return 0;
            }
            if (vk == VK_BACK || vk == VK_DELETE) {
                *capturing_ = {};
            } else {
                UINT mods = 0;
                if (GetKeyState(VK_CONTROL) & 0x8000) mods |= MOD_CONTROL;
                if (GetKeyState(VK_SHIFT) & 0x8000) mods |= MOD_SHIFT;
                if (GetKeyState(VK_MENU) & 0x8000) mods |= MOD_ALT;
                if ((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000) mods |= MOD_WIN;
                *capturing_ = {mods, vk};
            }
            EndCapture();
            app().SettingsChanged();
            return 0;
        }
        if (wp == VK_ESCAPE) {
            Show(false);
            return 0;
        }
        break;
    case WM_SYSCHAR:
        if (capturing_) return 0;   // swallow Alt+key beeps while capturing
        break;
    case WM_ACTIVATE:
        if (LOWORD(wp) == WA_INACTIVE) EndCapture();
        break;
    }
    return Window::OnMessage(msg, wp, lp);
}

void SettingsWindow::Layout() {
    groups_.clear();
    labels_.clear();
    float w = W();
    Settings& s = S();
    auto changed = [] { app().SettingsChanged(); };

    // Close button + tabs.
    Add(RectXYWH(w - 38, 8, 28, 24), [](gfx::Painter& p, const Control& c) { ui::IconButton(p, c, Icon::Close, th::text2, 13); },
        [this] { SendMessageW(hwnd_, WM_CLOSE, 0, 0); });
    struct TabDef {
        Icon icon;
        const wchar_t* name;
    };
    static const TabDef tabs[] = {{Icon::Gear, L"General"},   {Icon::Camcorder, L"Video"}, {Icon::Camera, L"Image"},
                                  {Icon::Overlay, L"Overlay"}, {Icon::Keyboard, L"Hotkey"}};
    constexpr int kTabs = 5;
    float tw = 76, tx = (w - tw * kTabs - 6 * (kTabs - 1)) / 2;
    for (int i = 0; i < kTabs; ++i) {
        Add(RectXYWH(tx + i * (tw + 6), 34, tw, 52),
            [this, i](gfx::Painter& p, const Control& c) {
                bool sel = tab_ == i;
                if (sel || c.hot) p.FillRound(c.rc, 8, Rgb(sel ? th::tileHover : th::tile));
                uint32_t col = sel ? th::blue : th::text2;
                DrawIcon(p, tabs[i].icon, RectXYWH(Center(c.rc).x - 11, c.rc.top + 6, 22, 22), Rgb(col));
                p.Text(tabs[i].name, RectF(c.rc.left, c.rc.top + 30, c.rc.right, c.rc.bottom - 4), 11.5f,
                       Rgb(sel ? th::text : th::text2), DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER);
            },
            [this, i] {
                EndCapture();
                tab_ = i;
                Relayout();
            });
    }

    const float gx = 20, gw = w - 40, cx = gx + kLabelW + 12;
    float y = kTabsH + 14, groupTop = 0;
    std::wstring groupTitle;
    auto beginGroup = [&](const wchar_t* title) {
        groupTitle = title;
        y += 22;
        groupTop = y;
        y += 10;
    };
    auto endGroup = [&] {
        y += 6;
        groups_.push_back({RectF(gx, groupTop, gx + gw, y), groupTitle});
        y += 12;
    };
    auto label = [&](const wchar_t* text) { labels_.push_back({RectXYWH(gx, y, kLabelW, kRowH), text}); };
    auto check = [&](float x, const std::wstring& text, bool& v, float width = 0, bool enabled = true) {
        float cw = width ? width : gfx::MeasureText(text, 13) + 30;
        auto& c = Add(RectXYWH(x, y, cw, kRowH),
                      [text, &v](gfx::Painter& p, const Control& c) { ui::Checkbox(p, c, text, v); },
                      [&v, changed] {
                          v = !v;
                          changed();
                      });
        c.enabled = enabled;
        return cw;
    };
    auto dropdown = [&](float x, float dw, std::function<std::wstring()> value, std::function<std::vector<win::MenuItem>()> items,
                        std::function<void(int)> pick, bool enabled = true) {
        auto& c = Add(RectXYWH(x, y + 3, dw, kRowH - 6),
                      [value](gfx::Painter& p, const Control& c) { ui::Dropdown(p, c, value()); },
                      [this, x, yy = y + kRowH, items, pick] {
                          int id = win::PopupMenu(hwnd_, ToScreen(x, yy), items());
                          if (id > 0) pick(id);
                      });
        c.enabled = enabled;
    };
    auto slider = [&](float x, float sw, int& v, int lo, int hi, const wchar_t* unit) {
        auto& c = Add(RectXYWH(x, y, sw, kRowH), [&v, lo, hi](gfx::Painter& p, const Control& c) {
            ui::Slider(p, c, (v - lo) / (float)(hi - lo));
        });
        c.drag = [this, &v, lo, hi, x0 = x + 7, x1 = x + sw - 7](float px, float, int phase) {
            v = (int)std::lround(lo + std::clamp((px - x0) / (x1 - x0), 0.f, 1.f) * (hi - lo));
            Invalidate();
            if (phase == 2) app().SettingsChanged();
        };
        std::wstring u = unit;
        Add(RectXYWH(x + sw + 8, y, 60, kRowH), [&v, u](gfx::Painter& p, const Control& c) {
            p.Text(std::to_wstring(v) + u, c.rc, 13, Rgb(th::text2));
        });
    };
    auto row = [&] { y += kRowH; };

    if (tab_ == 0) {   // ------------------------------------------------------------- General
        beginGroup(L"General");
        label(L"Options:");
        check(cx, L"Launch at login", s.launchAtLogin);
        row();
        check(cx, L"Exclude ScreenRec windows from capture", s.excludeSelf);
        row();
        float cw = check(cx, L"Countdown before recording starts", s.countdown);
        dropdown(cx + cw + 6, 90, [&s] { return Fmt(L"%d sec", s.countdownSec); },
                 [&s] {
                     std::vector<win::MenuItem> it;
                     for (int n : {1, 2, 3, 5, 10}) it.push_back({n, Fmt(L"%d sec", n), s.countdownSec == n});
                     return it;
                 },
                 [&s, changed](int id) {
                     s.countdownSec = id;
                     changed();
                 },
                 s.countdown);
        row();
        endGroup();

        beginGroup(L"Output");
        label(L"Output folder:");
        Add(RectXYWH(cx, y + 3, gx + gw - cx - 86, kRowH - 6),
            [&s](gfx::Painter& p, const Control& c) {
                p.FillRound(c.rc, 6, Rgb(c.hot ? th::tile : th::bgDeep));
                p.Text(s.outputDir, RectF(c.rc.left + 10, c.rc.top, c.rc.right - 8, c.rc.bottom), 12.5f, Rgb(th::text));
            },
            [this, &s, changed] {
                if (auto dir = win::PickFolder(hwnd_, s.outputDir)) {
                    s.outputDir = *dir;
                    changed();
                }
            });
        Add(RectXYWH(gx + gw - 78, y + 3, 66, kRowH - 6),
            [](gfx::Painter& p, const Control& c) { ui::Button(p, c, L"Open", th::tile, th::tileHover); },
            [] { app().OpenOutputFolder(); });
        row();
        check(cx, L"Show a floating preview after recording", s.recThumb);
        row();
        check(cx, L"Also open the output list after recording", s.showOutputAfter);
        row();
        endGroup();

        beginGroup(L"Updates");
        label(L"Version:");
        labels_.push_back({RectXYWH(cx, y, 200, kRowH), L"ScreenRec " + updater::CurrentVersion()});
        if (app().UpdateAvailable()) {
            auto ver = app().PendingUpdate()->version;
            auto& inst = Add(RectXYWH(gx + gw - 236, y + 3, 140, kRowH - 6), [ver](gfx::Painter& p, const Control& c) {
                ui::Button(p, c, L"Install " + ver, th::blue, th::blueHover);
            }, [] { app().InstallUpdate(); });
            inst.enabled = !app().UpdateBusy();
            Add(RectXYWH(gx + gw - 90, y + 3, 78, kRowH - 6),
                [](gfx::Painter& p, const Control& c) { ui::Button(p, c, L"What's new", th::tile, th::tileHover); },
                [] { win::OpenWithShell(app().PendingUpdate()->pageUrl); });
        } else {
            auto& chk = Add(RectXYWH(gx + gw - 118, y + 3, 106, kRowH - 6),
                            [](gfx::Painter& p, const Control& c) { ui::Button(p, c, L"Check now", th::tile, th::tileHover); },
                            [] { app().CheckForUpdates(true); });
            chk.enabled = updater::Enabled() && !app().UpdateBusy();
        }
        row();
        if (!app().UpdateStatus().empty()) {
            labels_.push_back({RectXYWH(cx, y, gw - kLabelW - 24, kRowH), app().UpdateStatus()});
            row();
        }
        check(cx, L"Check for updates automatically", s.autoUpdate);
        row();
        endGroup();

        Add(RectXYWH(gx, y + 4, 150, 30),
            [](gfx::Painter& p, const Control& c) { ui::Button(p, c, L"Reset all settings", th::tile, th::tileHover); },
            [this, changed] {
                if (win::Confirm(hwnd_, L"ScreenRec", L"Reset all settings to their defaults?")) {
                    S().Reset();
                    changed();
                }
            });
    } else if (tab_ == 1) {   // ------------------------------------------------------ Video
        beginGroup(L"Format");
        label(L"File format:");
        dropdown(cx, 170, [&s] { return s.container == L"mkv" ? std::wstring(L"MKV") : std::wstring(L"MP4"); },
                 [&s] { return std::vector<win::MenuItem>{{1, L"MP4", s.container == L"mp4"}, {2, L"MKV", s.container == L"mkv"}}; },
                 [&s, changed](int id) {
                     s.container = id == 2 ? L"mkv" : L"mp4";
                     changed();
                 });
        row();
        label(L"Codec:");
        dropdown(cx, 280,
                 [&s] {
                     if (s.encoder == L"auto")
                         return L"Auto — " + ff::EncoderLabel(ff::EncodersReady() ? ff::ResolveEncoder(L"auto") : L"detecting…");
                     return ff::EncoderLabel(s.encoder);
                 },
                 [&s] {
                     std::vector<win::MenuItem> it{{1, L"Auto (fastest available)", s.encoder == L"auto"}};
                     auto list = ff::Encoders();
                     for (int i = 0; i < (int)list.size(); ++i)
                         it.push_back({i + 2, ff::EncoderLabel(list[i]), s.encoder == list[i]});
                     return it;
                 },
                 [&s, changed](int id) {
                     auto list = ff::Encoders();
                     s.encoder = id == 1 || id - 2 >= (int)list.size() ? L"auto" : list[id - 2];
                     changed();
                 });
        row();
        label(L"Quality:");
        slider(cx, 220, s.quality, 10, 100, L"");
        row();
        label(L"Max resolution:");
        dropdown(cx, 170, [&s] { return s.maxRes == 0 ? std::wstring(L"Original") : Fmt(L"%dp", s.maxRes); },
                 [&s] {
                     std::vector<win::MenuItem> it{{1, L"Original", s.maxRes == 0}};
                     for (int r : {2160, 1440, 1080, 720, 480}) it.push_back({r, Fmt(L"%dp", r), s.maxRes == r});
                     return it;
                 },
                 [&s, changed](int id) {
                     s.maxRes = id == 1 ? 0 : id;
                     changed();
                 });
        row();
        label(L"FPS:");
        dropdown(cx, 170, [&s] { return std::to_wstring(s.fps); },
                 [&s] {
                     std::vector<win::MenuItem> it;
                     for (int f : {15, 24, 30, 60}) it.push_back({f, std::to_wstring(f), s.fps == f});
                     return it;
                 },
                 [&s, changed](int id) {
                     s.fps = id;
                     changed();
                 });
        row();
        label(L"Audio:");
        dropdown(cx, 170, [&s] { return Fmt(L"AAC %d kbps", s.audioKbps); },
                 [&s] {
                     std::vector<win::MenuItem> it;
                     for (int k : {128, 192, 256, 320}) it.push_back({k, Fmt(L"AAC %d kbps", k), s.audioKbps == k});
                     return it;
                 },
                 [&s, changed](int id) {
                     s.audioKbps = id;
                     changed();
                 });
        row();
        endGroup();

        auto deviceDropdown = [&](std::vector<AudioDevice>& devs, std::wstring& id) {
            dropdown(cx, gx + gw - cx - 12,
                     [&devs, &id] {
                         for (auto& d : devs)
                             if (d.id == id) return d.name;
                         return std::wstring(L"Default device");
                     },
                     [&devs, &id] {
                         std::vector<win::MenuItem> it{{1, L"Default device", id.empty()}};
                         for (int i = 0; i < (int)devs.size(); ++i) it.push_back({i + 2, devs[i].name, devs[i].id == id});
                         return it;
                     },
                     [&devs, &id, changed](int sel) {
                         id = sel == 1 ? L"" : devs[sel - 2].id;
                         changed();
                     });
        };
        beginGroup(L"Speaker & Microphone");
        check(cx, L"Record speaker audio", s.recSpeaker);
        row();
        label(L"Speaker:");
        deviceDropdown(speakers_, s.speakerId);
        row();
        label(L"Volume:");
        slider(cx, 220, s.speakerVol, 0, 200, L"%");
        row();
        check(cx, L"Record microphone audio", s.recMic);
        row();
        label(L"Microphone:");
        deviceDropdown(mics_, s.micId);
        row();
        label(L"Volume:");
        slider(cx, 220, s.micVol, 0, 200, L"%");
        row();
        endGroup();
    } else if (tab_ == 2) {   // -------------------------------------------------------- Image
        beginGroup(L"Screenshots");
        label(L"Save to:");
        Add(RectXYWH(cx, y + 3, gx + gw - cx - (s.shotDir.empty() ? 86 : 160), kRowH - 6),
            [&s](gfx::Painter& p, const Control& c) {
                p.FillRound(c.rc, 6, Rgb(c.hot ? th::tile : th::bgDeep));
                bool same = s.shotDir.empty();
                p.Text(same ? L"Same folder as videos" : s.shotDir, RectF(c.rc.left + 10, c.rc.top, c.rc.right - 8, c.rc.bottom),
                       12.5f, Rgb(same ? th::text2 : th::text));
            },
            [this, &s, changed] {
                if (auto dir = win::PickFolder(hwnd_, s.ScreenshotDir())) {
                    s.shotDir = *dir == s.outputDir ? L"" : *dir;
                    changed();
                }
            });
        if (!s.shotDir.empty())
            Add(RectXYWH(gx + gw - 152, y + 3, 66, kRowH - 6),
                [](gfx::Painter& p, const Control& c) { ui::Button(p, c, L"Reset", th::tile, th::tileHover); },
                [&s, changed] {
                    s.shotDir.clear();
                    changed();
                });
        Add(RectXYWH(gx + gw - 78, y + 3, 66, kRowH - 6),
            [](gfx::Painter& p, const Control& c) { ui::Button(p, c, L"Open", th::tile, th::tileHover); },
            [&s] {
                SHCreateDirectoryExW(nullptr, s.ScreenshotDir().c_str(), nullptr);
                win::OpenWithShell(s.ScreenshotDir());
            });
        row();
        label(L"Format:");
        dropdown(cx, 200, [&s] { return s.shotFormat == L"jpg" ? std::wstring(L"JPG (smaller)") : std::wstring(L"PNG (best quality)"); },
                 [&s] {
                     return std::vector<win::MenuItem>{{1, L"PNG (best quality)", s.shotFormat == L"png"},
                                                       {2, L"JPG (smaller)", s.shotFormat == L"jpg"}};
                 },
                 [&s, changed](int id) {
                     s.shotFormat = id == 2 ? L"jpg" : L"png";
                     changed();
                 });
        row();
        if (s.shotFormat == L"jpg") {
            label(L"JPG quality:");
            slider(cx, 220, s.jpgQuality, 50, 100, L"");
            row();
        }
        endGroup();

        beginGroup(L"Options");
        check(cx, L"Include the mouse cursor", s.shotCursor);
        row();
        check(cx, L"Copy each screenshot to the clipboard", s.shotClipboard);
        row();
        check(cx, L"Show a floating preview after capturing", s.shotNotify);
        row();
        check(cx, L"Play a sound", s.shotSound);
        row();
        endGroup();

        beginGroup(L"Shortcut");
        label(L"Quick screenshot:");
        Hotkey* hk = &s.hkShot;
        Add(RectXYWH(cx, y + 3, 180, kRowH - 6),
            [this, hk](gfx::Painter& p, const Control& c) {
                bool active = capturing_ == hk;
                ui::Field(p, c, active ? L"Press a key…" : hk->Empty() ? L"None" : hk->Text(), active);
            },
            [this, hk] {
                capturing_ = hk;
                app().SuspendHotkeys(true);
                SetForegroundWindow(hwnd_);
                SetFocus(hwnd_);
                Invalidate();
            });
        row();
        endGroup();
        labels_.push_back({RectXYWH(gx, y, gw, 30),
                           L"Tip: use the camera button to capture an area or a window."});
    } else if (tab_ == 3) {   // ------------------------------------------------------ Overlay
        beginGroup(L"Mouse Effect");
        check(cx, L"Add cursor to the output video", s.cursor);
        row();
        label(L"Mouse pointer:");
        dropdown(cx, 170, [&s] { return s.pointer == 1 ? std::wstring(L"Draw dot") : std::wstring(L"Draw cursor"); },
                 [&s] { return std::vector<win::MenuItem>{{1, L"Draw cursor", s.pointer == 0}, {2, L"Draw dot", s.pointer == 1}}; },
                 [&s, changed](int id) {
                     s.pointer = id - 1;
                     changed();
                 },
                 s.cursor);
        row();
        check(cx, L"Highlight effect", s.highlight, 0, s.cursor);
        row();
        check(cx, L"Click effect", s.clickEffect, 0, s.cursor);
        row();
        endGroup();

        beginGroup(L"Webcam Overlay");
        check(cx, L"Show webcam video on the screen", s.webcam);
        row();
        label(L"Camera:");
        dropdown(cx, gx + gw - cx - 12,
                 [this, &s] {
                     if (!s.webcamName.empty()) return s.webcamName;
                     return cams_.empty() ? std::wstring(L"No camera found") : cams_[0];
                 },
                 [this, &s] {
                     std::vector<win::MenuItem> it;
                     for (int i = 0; i < (int)cams_.size(); ++i)
                         it.push_back({i + 1, cams_[i], cams_[i] == s.webcamName || (s.webcamName.empty() && i == 0)});
                     return it;
                 },
                 [this, &s, changed](int id) {
                     s.webcamName = cams_[id - 1];
                     changed();
                 });
        row();
        label(L"Shape:");
        dropdown(cx, 170, [&s] { return s.camShape == 0 ? std::wstring(L"Circle") : std::wstring(L"Rounded rectangle"); },
                 [&s] {
                     return std::vector<win::MenuItem>{{1, L"Circle", s.camShape == 0}, {2, L"Rounded rectangle", s.camShape == 1}};
                 },
                 [&s, changed](int id) {
                     s.camShape = id - 1;
                     changed();
                 });
        row();
        float mw = check(cx, L"Mirror", s.mirror);
        check(cx + mw + 16, L"Flip vertically", s.flip);
        row();
        check(cx, L"Shadow", s.shadow);
        row();
        label(L"Border:");
        slider(cx, 180, s.borderWidth, 0, 20, L" px");
        dropdown(cx + 250, 120,
                 [&s] {
                     switch (s.borderColor) {
                     case th::yellow: return std::wstring(L"Yellow");
                     case 0xffffff: return std::wstring(L"White");
                     case th::red: return std::wstring(L"Red");
                     case th::blue: return std::wstring(L"Blue");
                     case th::green: return std::wstring(L"Green");
                     case 0x000000: return std::wstring(L"Black");
                     }
                     return std::wstring(L"Custom");
                 },
                 [&s] {
                     return std::vector<win::MenuItem>{{1, L"Yellow", s.borderColor == th::yellow}, {2, L"White", s.borderColor == 0xffffff},
                                                       {3, L"Red", s.borderColor == th::red},       {4, L"Blue", s.borderColor == th::blue},
                                                       {5, L"Green", s.borderColor == th::green},   {6, L"Black", s.borderColor == 0}};
                 },
                 [&s, changed](int id) {
                     const uint32_t cols[] = {th::yellow, 0xffffff, th::red, th::blue, th::green, 0x000000};
                     s.borderColor = cols[id - 1];
                     changed();
                 });
        row();
        endGroup();
        labels_.push_back({RectXYWH(gx, y, gw, 40),
                           L"Tip: drag the webcam bubble to move it, scroll over it to resize."});
    } else {   // ---------------------------------------------------------------------- Hotkey
        beginGroup(L"Hotkeys");
        struct HkDef {
            const wchar_t* name;
            Hotkey* hk;
        };
        HkDef defs[] = {{L"Record current display", &s.hkRecord}, {L"Record current window", &s.hkWindow},
                        {L"Stop recording", &s.hkStop},           {L"Pause / Resume", &s.hkPause},
                        {L"Drawing tools", &s.hkDraw},            {L"Screenshot", &s.hkShot}};
        for (auto& d : defs) {
            label(d.name);
            Hotkey* hk = d.hk;
            Add(RectXYWH(cx, y + 3, 180, kRowH - 6),
                [this, hk](gfx::Painter& p, const Control& c) {
                    bool active = capturing_ == hk;
                    std::wstring t = active ? L"Press a key…" : hk->Empty() ? L"None" : hk->Text();
                    ui::Field(p, c, t, active);
                },
                [this, hk] {
                    capturing_ = hk;
                    app().SuspendHotkeys(true);
                    SetForegroundWindow(hwnd_);
                    SetFocus(hwnd_);
                    Invalidate();
                });
            row();
        }
        endGroup();
        labels_.push_back({RectXYWH(gx, y, gw, 30), L"Click a field and press the new shortcut. Backspace clears it."});
        if (win::F12ReservedByDebugger())
            labels_.push_back({RectXYWH(gx, y + 26, gw, 30),
                               L"Note: plain F12 is reserved by the Visual Studio debugger on this PC — use it with Alt/Ctrl/Shift."});
    }
}

void SettingsWindow::Paint(gfx::Painter& p) {
    p.Clear(Rgb(th::bg));
    static const wchar_t* titles[] = {L"General", L"Video", L"Image", L"Overlay", L"Hotkey"};
    p.Text(titles[tab_], RectF(0, 0, W(), 32), 13, Rgb(th::text), DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER);
    ui::Separator(p, 0, kTabsH, W(), kTabsH);
    for (auto& [r, title] : groups_) {
        p.Text(title, RectF(r.left + 2, r.top - 24, r.right, r.top - 2), 13, Rgb(th::text), DWRITE_FONT_WEIGHT_SEMI_BOLD);
        p.FillRound(r, 9, Rgb(th::group));
    }
    for (auto& [r, text] : labels_) {
        bool tip = RW(r) > kLabelW;
        p.Text(text, r, tip ? 12.f : 13.f, Rgb(th::text2), DWRITE_FONT_WEIGHT_NORMAL,
               tip ? DWRITE_TEXT_ALIGNMENT_LEADING : DWRITE_TEXT_ALIGNMENT_TRAILING);
    }
    PaintControls(p);
}
