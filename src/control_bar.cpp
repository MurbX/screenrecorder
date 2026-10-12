#include "app.h"
#include "overlays.h"
#include "winutil.h"

namespace {
constexpr float kH = 48, kBtn = 28, kGap = 8;

void RecButton(gfx::Painter& p, const Window::Control& c) {
    auto ctr = Center(c.rc);
    float r = std::min(RW(c.rc), RH(c.rc)) / 2;
    p.Circle(ctr, r, Rgb(c.hot ? 0xffffff : th::toggleOn, c.enabled ? 1.f : 0.4f));
    p.Circle(ctr, r - 4, Rgb(c.hot ? th::redHover : th::red, c.enabled ? 1.f : 0.4f));
}
}  // namespace

bool ControlBar::Create() {
    frameless_ = true;
    if (!Window::Create(L"ScreenRec Controls", WS_POPUP, WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                        {0, 0, 400, 48}, nullptr, false))
        return false;
    win::ExcludeFromCapture(hwnd_, true);
    return true;
}

void ControlBar::BuildItems(bool add, float& width) {
    Mode m = app().CurrentMode();
    RecState st = app().State();
    bool busy = app().Busy();
    bool video = m == Mode::Area || m == Mode::Fullscreen || m == Mode::Window;
    float x = 10, y = (kH - kBtn) / 2;

    auto put = [&](float w, std::function<void(gfx::Painter&, const Control&)> paint, std::function<void()> click,
                   bool enabled = true) {
        if (add) {
            auto& c = Add(RectXYWH(x, (kH - std::min(w, 34.f)) / 2, w, std::min(w, 34.f)), std::move(paint), std::move(click));
            c.enabled = enabled;
        }
        x += w + kGap;
    };
    // meter: -1 none, 0 speaker, 1 microphone (live level shown under the button while recording)
    auto toggle = [&](Icon on, Icon off, std::function<bool()> state, std::function<void()> act, bool enabled, int meter = -1) {
        put(kBtn, [on, off, state, meter](gfx::Painter& p, const Control& c) {
            ui::RoundToggle(p, c, state() ? on : off, state());
            if (meter < 0 || !state() || app().State() != RecState::Recording) return;
            float level = app().AudioLevel(meter == 1);
            float db = level > 0 ? 20 * log10f(level) : -100.f;
            float t = std::clamp((db + 60) / 60, 0.f, 1.f);
            D2D1_RECT_F track = RectXYWH(Center(c.rc).x - 12, c.rc.bottom + 3, 24, 3);
            p.FillRound(track, 1.5f, Rgb(th::tilePress));
            uint32_t col = db > -3 ? th::red : db > -12 ? th::yellow : th::green;
            if (t > 0) p.FillRound(RectF(track.left, track.top, track.left + RW(track) * t, track.bottom), 1.5f, Rgb(col));
        }, std::move(act), enabled);
    };
    auto separator = [&] {
        if (add) {
            float sx = x + 2;
            Add(RectF(sx, 12, sx + 1, kH - 12),
                [](gfx::Painter& p, const Control& c) { ui::Separator(p, c.rc.left, c.rc.top, c.rc.left, c.rc.bottom); });
        }
        x += 6 + kGap;
    };

    put(kBtn, [](gfx::Painter& p, const Control& c) { ui::RoundToggle(p, c, Icon::Close, false); }, [] { app().Cancel(); },
        !busy);
    if (video) {
        toggle(Icon::Cursor, Icon::Cursor, [] { return S().cursor; }, [this, cx = x, cy = kH] {
            app().ShowCursorMenu(hwnd_, ToScreen(cx, cy));
        }, !busy);
        toggle(Icon::Camcorder, Icon::Camcorder, [] { return S().webcam; }, [] { app().ToggleWebcamOverlay(); }, true);
    }
    toggle(Icon::Speaker, Icon::SpeakerOff, [] { return S().recSpeaker; }, [] { app().ToggleSpeaker(); }, !busy, 0);
    toggle(Icon::Mic, Icon::MicOff, [] { return S().recMic; }, [] { app().ToggleMic(); }, !busy, 1);
    if (video) {
        separator();
        toggle(Icon::Annotate, Icon::Annotate, [] { return app().Drawing(); }, [] { app().ToggleDrawing(); }, true);
        toggle(Icon::SelectArea, Icon::SelectArea, [] { return false; }, [] { app().ReselectArea(); },
               !busy && m != Mode::Fullscreen);
        toggle(Icon::Camera, Icon::Camera, [] { return false; }, [] { app().Screenshot(); }, true);   // works while recording
    }

    // Size / timer field.
    float fw = 112;
    if (add) {
        auto& c = Add(RectXYWH(x, (kH - 30) / 2, fw, 30), [m, video](gfx::Painter& p, const Control& c) {
            std::wstring text;
            RecState st = app().State();
            if (st == RecState::Recording || st == RecState::Finishing) text = FormatDuration(app().Elapsed(), true);
            else if (st == RecState::Countdown) text = L"Get ready…";
            else if (video) {
                RECT r = app().Region();
                text = Fmt(L"%d x %d", r.right - r.left, r.bottom - r.top);
            } else text = m == Mode::Webcam ? L"Webcam" : L"Audio only";
            p.FillRound(c.rc, 7, Rgb(c.hot ? th::tile : th::bgDeep));
            bool live = st == RecState::Recording && !app().Paused();
            p.Text(text, c.rc, 14, Rgb(live ? th::text : th::text2), DWRITE_FONT_WEIGHT_MEDIUM, DWRITE_TEXT_ALIGNMENT_CENTER);
        });
        if (m == Mode::Area && !busy) {
            c.click = [this, cx = x, cy = (kH + 30) / 2 + 4] {
                RECT r = app().Region();
                int w = r.right - r.left, h = r.bottom - r.top;
                struct Preset {
                    int w, h;
                };
                static const Preset presets[] = {{3840, 2160}, {2560, 1440}, {1920, 1080}, {1280, 720}, {854, 480},
                                                 {1080, 1920}, {1080, 1080}, {800, 600},   {640, 480}};
                std::vector<win::MenuItem> items;
                for (int i = 0; i < (int)std::size(presets); ++i)
                    items.push_back({i + 1, Fmt(L"%d x %d", presets[i].w, presets[i].h),
                                     presets[i].w == w && presets[i].h == h});
                int id = win::PopupMenu(hwnd_, ToScreen(cx, cy), items);
                if (id <= 0) return;
                RECT mon = win::MonitorFor(r).rect;
                int nw = std::min<int>(presets[id - 1].w, mon.right - mon.left);
                int nh = std::min<int>(presets[id - 1].h, mon.bottom - mon.top);
                int cxr = (r.left + r.right) / 2, cyr = (r.top + r.bottom) / 2;
                RECT n{cxr - nw / 2, cyr - nh / 2, cxr - nw / 2 + nw, cyr - nh / 2 + nh};
                OffsetRect(&n, std::max(0L, mon.left - n.left), std::max(0L, mon.top - n.top));
                OffsetRect(&n, std::min(0L, mon.right - n.right), std::min(0L, mon.bottom - n.bottom));
                app().OnRegionChanged(n);
            };
        }
    }
    x += fw + kGap;

    // Primary actions.
    if (st == RecState::Preparing || st == RecState::Idle) {
        put(34, RecButton, [] { app().Record(); });
    } else if (st == RecState::Countdown) {
        put(30, [](gfx::Painter& p, const Control& c) { ui::IconButton(p, c, Icon::Stop, th::red, 18); }, [] { app().Cancel(); });
    } else if (st == RecState::Recording) {
        bool paused = app().Paused();
        put(30, [paused](gfx::Painter& p, const Control& c) {
            ui::IconButton(p, c, paused ? Icon::Play : Icon::Pause, th::text, 18);
        }, [] { app().TogglePause(); });
        put(30, [](gfx::Painter& p, const Control& c) { ui::IconButton(p, c, Icon::Stop, th::red, 20); }, [] { app().Stop(); });
    } else {   // Finishing
        if (add)
            Add(RectXYWH(x, 0, 70, kH), [](gfx::Painter& p, const Control& c) {
                p.Text(L"Saving…", c.rc, 13, Rgb(th::text2), DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER);
            });
        x += 70 + kGap;
    }
    width = x - kGap + 10;
}

float ControlBar::ContentWidth() {
    float w = 0;
    BuildItems(false, w);
    return w;
}

void ControlBar::Layout() {
    float w = 0;
    BuildItems(true, w);
}

void ControlBar::Place(RECT anchor, bool inside) {
    anchor_ = anchor;
    inside_ = inside;
    float s = (float)win::DpiOf(hwnd_) / 96.f;
    int w = (int)(ContentWidth() * s), h = (int)(kH * s);
    HMONITOR hm = MonitorFromRect(&anchor, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(hm, &mi);
    RECT work = mi.rcWork;
    int x = (anchor.left + anchor.right) / 2 - w / 2;
    int y = inside ? anchor.bottom - h - (int)(28 * s) : anchor.bottom + (int)(12 * s);
    if (y + h > work.bottom - (int)(8 * s)) y = anchor.bottom - h - (int)(14 * s);
    x = std::clamp<int>(x, work.left + 8, std::max<int>(work.left + 8, work.right - w - 8));
    y = std::clamp<int>(y, work.top + 8, std::max<int>(work.top + 8, work.bottom - h - 8));
    SetWindowPos(hwnd_, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    bool busy = app().Busy();
    if (busy && !timer_) {
        SetTimer(hwnd_, 1, 100, nullptr);   // timer text + audio meters
        timer_ = true;
    } else if (!busy && timer_) {
        KillTimer(hwnd_, 1);
        timer_ = false;
    }
    Relayout();
}

void ControlBar::Refresh() {
    if (Visible()) Place(anchor_, inside_);
}

LRESULT ControlBar::OnMessage(UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_TIMER) {
        Invalidate();
        return 0;
    }
    if (msg == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    return Window::OnMessage(msg, wp, lp);
}

LRESULT ControlBar::HitTest(float x, float y) {
    for (auto& c : controls_)
        if (c.click && InRect(c.rc, x, y)) return HTCLIENT;
    return HTCAPTION;
}

void ControlBar::Paint(gfx::Painter& p) {
    p.Clear(Rgb(th::bg));
    PaintControls(p);
}
