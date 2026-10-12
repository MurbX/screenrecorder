#include "app.h"
#include "main_window.h"
#include "output_window.h"
#include "overlays.h"
#include "settings_window.h"
#include "webcam.h"
#include "winutil.h"
#include "cursorfx.h"
#include "editor.h"
#include "screenshot.h"
#include <mmsystem.h>
#include <shellapi.h>
#include <shlobj.h>

namespace {
App* g_app = nullptr;
constexpr UINT_PTR kCountdownTimer = 77;
constexpr UINT_PTR kShotTimer = 78;
enum HotkeyId { HK_RECORD = 1, HK_WINDOW, HK_STOP, HK_PAUSE, HK_DRAW, HK_SHOT };
int g_countdownLeft = 0;
bool g_quitAfterFinish = false;

bool ValidRegion(RECT r) { return r.right - r.left >= 16 && r.bottom - r.top >= 16; }
}  // namespace

App& app() { return *g_app; }

App::App() { g_app = this; }
App::~App() { g_app = nullptr; }

HWND App::MainHwnd() const { return main_ ? main_->Hwnd() : nullptr; }
bool App::Drawing() const { return drawbar_ && drawbar_->Visible(); }

bool App::Init() {
    S().Load();
    cursorfx::RecoverIfNeeded();   // a crash mid-recording could have left the halo cursors in place
    if (win::F12ReservedByDebugger() && S().hkRecord.mods == 0 && S().hkRecord.vk == VK_F12) {
        S().hkRecord = {MOD_ALT, VK_F12};   // plain F12 would never fire on this PC
        S().Save();
    }
    win::EnableDarkMenus();
    main_ = std::make_unique<MainWindow>();
    area_ = std::make_unique<AreaOverlay>();
    bar_ = std::make_unique<ControlBar>();
    canvas_ = std::make_unique<Canvas>();
    drawbar_ = std::make_unique<DrawToolbar>();
    webcam_ = std::make_unique<WebcamWindow>();
    settings_ = std::make_unique<SettingsWindow>();
    outputs_ = std::make_unique<OutputWindow>();
    toast_ = std::make_unique<Toast>();
    editor_ = std::make_unique<ImageEditor>();
    if (!main_->Create() || !area_->Create() || !bar_->Create() || !canvas_->Create() || !drawbar_->Create() ||
        !toast_->Create())
        return false;
    region_ = S().region;
    ApplyExclusion();
    main_->Show(true);
    SetForegroundWindow(main_->Hwnd());
    RegisterHotkeys();
    ff::DetectEncodersAsync(main_->Hwnd());
    if (S().autoUpdate && (long long)time(nullptr) - S().lastUpdateCheck > 24 * 3600) CheckForUpdates(false);
    SyncWebcamOverlay();
    if (!ff::Available())
        win::Error(main_->Hwnd(), L"FFmpeg was not found, so recording won't work yet.\n\n"
                                  L"Install it with:  winget install Gyan.FFmpeg\n"
                                  L"or copy ffmpeg.exe next to ScreenRec.exe.");
    return true;
}

int App::Run() {
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    cursorfx::Restore();
    UnregisterHotkeys();
    S().Save();
    return (int)msg.wParam;
}

void App::Quit() {
    if (state_ == RecState::Recording) {
        if (!win::Confirm(MainHwnd(), L"ScreenRec", L"A recording is in progress. Stop it and quit?")) return;
        g_quitAfterFinish = true;
        Stop();
        return;
    }
    if (state_ == RecState::Finishing) {
        g_quitAfterFinish = true;
        return;
    }
    S().Save();
    PostQuitMessage(0);
}

// ---------------------------------------------------------------------------------------------
// Mode selection

void App::ChooseMode(Mode m, bool pickDisplay) {
    if (Busy()) return;
    if (state_ == RecState::Preparing) {
        area_->Hide();
        bar_->Show(false);
        if (webcam_->IsActive() && webcam_->GetStyle() == WebcamWindow::Style::Preview) webcam_->Shut();
    }
    mode_ = m;
    state_ = RecState::Preparing;
    ShowWindow(main_->Hwnd(), SW_HIDE);
    POINT cursor;
    GetCursorPos(&cursor);
    RECT mon = win::MonitorAt(cursor).rect;
    switch (m) {
    case Mode::Area:
        if (ValidRegion(S().region)) {
            region_ = S().region;
            area_->BeginAdjust(region_);
            bar_->Place(region_, false);
        } else {
            region_ = {};
            area_->BeginSelect();
        }
        break;
    case Mode::Fullscreen:
        if (pickDisplay && win::Monitors().size() > 1) {
            region_ = {};
            area_->BeginPickMonitor();   // several screens: let the user click one
        } else {
            OnMonitorPicked(mon);
        }
        break;
    case Mode::Window:
        region_ = {};
        area_->BeginPick();
        break;
    case Mode::Webcam:
        region_ = {};
        if (webcam_->IsActive()) webcam_->Shut();
        webcam_->Open(WebcamWindow::Style::Preview);
        bar_->Place(webcam_->ScreenRect(), false);
        break;
    case Mode::Audio: {
        region_ = {};
        LONG cx = (mon.left + mon.right) / 2, cy = (mon.top + mon.bottom) / 2;
        bar_->Place({cx - 1, cy - 1, cx + 1, cy + 1}, false);
        break;
    }
    }
    Restack();
    main_->Relayout();
}

void App::ReselectArea() {
    if (Busy()) return;
    if (state_ != RecState::Preparing || mode_ != Mode::Area) {
        S().region = {};
        ChooseMode(Mode::Area);
        return;
    }
    bar_->Show(false);
    area_->BeginSelect();
    Restack();
}

void App::OnRegionChanged(RECT r) {
    region_ = r;
    if (mode_ == Mode::Area) S().region = r;
    if (area_->GetKind() == AreaOverlay::Kind::Adjust) area_->SetRegion(r);
    bar_->Place(r, false);
    if (Drawing()) drawbar_->PlaceNear(r);
    Restack();
}

void App::OnWindowPicked(HWND target) {
    RECT b = win::WindowBounds(target), mon = win::MonitorFor(b).rect, r{};
    if (!IntersectRect(&r, &b, &mon) || !ValidRegion(r)) return;
    SetForegroundWindow(target);
    if (shotMode_) {
        CaptureSoon(r);
        return;
    }
    region_ = r;
    area_->ShowFrame(region_, true);
    bar_->Place(region_, false);
    Restack();
}

void App::OnMonitorPicked(RECT monitor) {
    if (shotMode_) {
        CaptureSoon(monitor);
        return;
    }
    region_ = monitor;
    area_->ShowFrame(region_, false);
    bar_->Place(region_, true);
    Restack();
}

void App::Cancel() {
    if (shotMode_) {
        EndScreenshot();
        return;
    }
    if (state_ == RecState::Countdown) {
        KillTimer(MainHwnd(), kCountdownTimer);
        area_->SetCountdown(0);
        state_ = RecState::Preparing;
        if (mode_ == Mode::Area) area_->BeginAdjust(region_);
        else if (mode_ != Mode::Window && mode_ != Mode::Fullscreen) area_->Hide();
        bar_->Refresh();
        Restack();
        return;
    }
    if (state_ == RecState::Preparing) BackToIdle();
}

void App::BackToIdle() {
    KillTimer(MainHwnd(), kCountdownTimer);
    state_ = RecState::Idle;
    area_->Hide();
    bar_->Show(false);
    if (Drawing()) ToggleDrawing();
    canvas_->SetEffects(false);
    cursorfx::Restore();
    if (webcam_->IsActive() && webcam_->GetStyle() == WebcamWindow::Style::Preview) webcam_->Shut();
    SyncWebcamOverlay();
    main_->Show(true);
    SetForegroundWindow(main_->Hwnd());
    RefreshUi();
}

// ---------------------------------------------------------------------------------------------
// Recording

void App::Record() {
    switch (state_) {
    case RecState::Idle:
        RecordCurrentDisplay();
        return;
    case RecState::Recording:
        Stop();
        return;
    case RecState::Countdown:
        Cancel();
        return;
    case RecState::Finishing:
        return;
    case RecState::Preparing:
        break;
    }
    if ((mode_ == Mode::Area || mode_ == Mode::Window) && !ValidRegion(region_)) return;
    if (!S().countdown) {
        StartRecording();
        return;
    }
    state_ = RecState::Countdown;
    g_countdownLeft = S().countdownSec;
    PlaceCountdown();
    area_->SetCountdown(g_countdownLeft);
    SetTimer(MainHwnd(), kCountdownTimer, 1000, nullptr);
    bar_->Refresh();
    Restack();
}

// The 3-2-1 number is drawn in the middle of the overlay's region: the recorded area, or for
// Audio/Webcam (no area) just above the control bar / on the camera preview, never under the bar.
void App::PlaceCountdown() {
    if (mode_ == Mode::Area || mode_ == Mode::Window) {
        area_->ShowFrame(region_, true);
        return;
    }
    if (mode_ == Mode::Fullscreen) {
        area_->ShowFrame(region_, false);
        return;
    }
    if (mode_ == Mode::Webcam && webcam_->IsActive()) {
        area_->ShowFrame(webcam_->ScreenRect(), false);
        return;
    }
    RECT b{};
    GetWindowRect(bar_->Hwnd(), &b);
    float s = win::DpiOf(bar_->Hwnd()) / 96.f;
    int size = (int)(150 * s), gap = (int)(20 * s), cx = (b.left + b.right) / 2;
    area_->ShowFrame({cx - size / 2, b.top - gap - size, cx + size / 2, b.top - gap}, false);
}

void App::OnCountdownDone() {
    KillTimer(MainHwnd(), kCountdownTimer);
    area_->SetCountdown(0);
    StartRecording();
}

void App::StartRecording() {
    RecordConfig cfg;
    cfg.kind = mode_ == Mode::Audio ? RecordConfig::AudioOnly : mode_ == Mode::Webcam ? RecordConfig::Webcam : RecordConfig::Screen;
    cfg.region = region_;
    cfg.fps = std::clamp(S().fps, 5, 60);
    cfg.maxRes = S().maxRes;
    cfg.cursor = S().cursor;   // the dot pointer is a system cursor now, so it is captured like any cursor
    cfg.encoder = ff::ResolveEncoder(S().encoder);
    cfg.quality = S().quality;
    cfg.audioKbps = S().audioKbps;
    cfg.speaker = S().recSpeaker;
    cfg.mic = S().recMic;
    cfg.speakerId = S().speakerId;
    cfg.micId = S().micId;
    cfg.speakerGain = S().speakerVol / 100.f;
    cfg.micGain = S().micVol / 100.f;
    if (cfg.kind == RecordConfig::Webcam) {
        cfg.webcamName = S().webcamName;
        if (cfg.webcamName.empty()) {
            auto cams = ListWebcams();
            if (!cams.empty()) cfg.webcamName = cams[0];
        }
        webcam_->Shut();   // release the camera so ffmpeg can open it
    }
    std::wstring ext = cfg.kind == RecordConfig::AudioOnly ? L"mp3" : S().container;
    cfg.outPath = JoinPath(S().outputDir, L"ScreenRec_" + TimestampName() + L"." + ext);

    std::wstring err;
    if (!rec_.Start(cfg, MainHwnd(), err)) {
        state_ = RecState::Preparing;
        if (mode_ == Mode::Webcam) webcam_->Open(WebcamWindow::Style::Preview);
        if (mode_ == Mode::Area) area_->BeginAdjust(region_);
        bar_->Refresh();
        win::Error(bar_->Hwnd(), L"Could not start recording.\n\n" + err);
        return;
    }
    state_ = RecState::Recording;
    if (mode_ == Mode::Area || mode_ == Mode::Window) area_->ShowFrame(region_, true);
    else area_->Hide();
    bool video = cfg.kind == RecordConfig::Screen;
    if (video) ApplyCursorEffects();
    bar_->Refresh();
    Restack();
}

void App::Stop() {
    if (state_ != RecState::Recording) return;
    state_ = RecState::Finishing;
    canvas_->SetEffects(false);
    cursorfx::Restore();
    area_->Hide();
    bar_->Refresh();
    rec_.Stop();
}

void App::TogglePause() {
    if (state_ != RecState::Recording) return;
    if (rec_.Paused()) {
        std::wstring err;
        if (!rec_.Resume(err)) {
            win::Error(bar_->Hwnd(), L"Could not resume recording.\n\n" + err);
            Stop();
            return;
        }
    } else {
        rec_.Pause();
    }
    bar_->Refresh();
}

void App::FinishRecording(RecordResult* res) {
    std::wstring tail = rec_.ErrorTail();
    BackToIdle();
    if (!res->ok) {
        win::Error(MainHwnd(), L"The recording could not be saved.\n\n" + res->error);
    } else if (!g_quitAfterFinish) {
        if (S().recThumb) toast_->Pop(res->path, true);   // floating preview: drag it anywhere or click to open
        if (S().showOutputAfter) ShowOutputs(res->path);
    }
    if (g_quitAfterFinish) {
        S().Save();
        PostQuitMessage(0);
    }
}

void App::RecordCurrentDisplay() {
    if (state_ == RecState::Recording) {
        Stop();
        return;
    }
    if (state_ == RecState::Countdown || state_ == RecState::Finishing) return;
    if (state_ == RecState::Idle) ChooseMode(Mode::Fullscreen, false);   // the screen under the mouse
    Record();
}

void App::RecordCurrentWindow() {
    if (state_ == RecState::Recording) {
        Stop();
        return;
    }
    if (Busy()) return;
    HWND fg = GetForegroundWindow();
    DWORD pid = 0;
    if (fg) GetWindowThreadProcessId(fg, &pid);
    if (state_ == RecState::Preparing) BackToIdle();
    if (!fg || pid == GetCurrentProcessId()) {
        ChooseMode(Mode::Window);
        return;
    }
    mode_ = Mode::Window;
    state_ = RecState::Preparing;
    ShowWindow(main_->Hwnd(), SW_HIDE);
    OnWindowPicked(fg);
    if (ValidRegion(region_)) Record();
    else BackToIdle();
}

// ---------------------------------------------------------------------------------------------
// Toggles and windows

void App::ToggleDrawing() {
    if (Drawing()) {
        drawbar_->Show(false);
        canvas_->SetTool(Tool::Pointer);
        canvas_->Clear();
    } else {
        RECT anchor = ValidRegion(region_) && state_ != RecState::Idle ? region_ : RECT{};
        drawbar_->PlaceNear(anchor);
        canvas_->SetTool(Tool::Pen);
    }
    Restack();
    RefreshUi();
}

void App::ToggleWebcamOverlay() {
    S().webcam = !S().webcam;
    SettingsChanged();
}

void App::ToggleCursor() {
    S().cursor = !S().cursor;
    SettingsChanged();
}

void App::ShowCursorMenu(HWND owner, POINT pt) {
    Settings& s = S();
    int id = win::PopupMenu(owner, pt,
                            {{1, L"Show mouse cursor", s.cursor},
                             {0},
                             {2, L"Yellow highlight around the cursor", s.highlight, s.cursor},
                             {3, L"Show mouse clicks", s.clickEffect, s.cursor},
                             {0},
                             {4, L"Pointer: normal cursor", s.pointer == 0, s.cursor},
                             {5, L"Pointer: dot", s.pointer == 1, s.cursor}});
    switch (id) {
    case 1: s.cursor = !s.cursor; break;
    case 2: s.highlight = !s.highlight; break;
    case 3: s.clickEffect = !s.clickEffect; break;
    case 4: s.pointer = 0; break;
    case 5: s.pointer = 1; break;
    default: return;
    }
    SettingsChanged();
}

void App::ToggleSpeaker() {
    S().recSpeaker = !S().recSpeaker;
    SettingsChanged();
}

void App::ToggleMic() {
    S().recMic = !S().recMic;
    SettingsChanged();
}

void App::SyncWebcamOverlay() {
    bool previewMode = mode_ == Mode::Webcam && state_ != RecState::Idle;
    if (previewMode) return;
    if (S().webcam) {
        if (!webcam_->IsActive()) webcam_->Open(WebcamWindow::Style::Bubble);
    } else if (webcam_->IsActive()) {
        webcam_->Shut();
    }
    Restack();
}

void App::ShowSettings() { settings_->Open(UpdateAvailable() ? 0 : -1); }   // General tab shows the update

void App::ShowOutputs(const std::wstring& select) { outputs_->Open(select); }

void App::CheckForUpdates(bool manual) {
    if (!updater::Enabled() || updateBusy_) return;
    updateBusy_ = true;
    if (manual) updateStatus_ = L"Checking…";
    updater::CheckAsync(MainHwnd(), manual);
    RefreshUi();
}

void App::InstallUpdate() {
    if (!update_) return;
    if (Busy() || state_ == RecState::Preparing) {
        win::Error(MainHwnd(), L"Finish or cancel the current recording first, then install the update.");
        return;
    }
    if (update_->installerUrl.empty()) {   // no installer attached: send the user to the release page
        win::OpenWithShell(update_->pageUrl);
        return;
    }
    updateBusy_ = true;
    updateStatus_ = L"Downloading ScreenRec " + update_->version + L"…";
    updater::DownloadAsync(*update_, MainHwnd());
    RefreshUi();
}

void App::OpenCapture(const std::wstring& path, bool video) {
    if (video) ShowOutputs(path);
    else editor_->Open(path);
}

void App::OpenOutputFolder() {
    SHCreateDirectoryExW(nullptr, S().outputDir.c_str(), nullptr);
    ShellExecuteW(nullptr, L"open", S().outputDir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void App::ShowResolutionMenu(HWND owner, POINT pt) {
    // Disjoint id ranges: resolutions are 10000 + height, frame rates 20000 + fps.
    std::vector<win::MenuItem> items{{1, L"Max resolution", false, false}, {10000, L"Original", S().maxRes == 0}};
    for (int r : {2160, 1440, 1080, 720, 480}) items.push_back({10000 + r, r == 2160 ? L"2160p (4K)" : Fmt(L"%dp", r), S().maxRes == r});
    items.push_back({0});
    items.push_back({2, L"Frame rate", false, false});
    for (int f : {24, 30, 60}) items.push_back({20000 + f, Fmt(L"%d FPS", f), S().fps == f});
    int id = win::PopupMenu(owner, pt, items);
    if (id >= 20000) S().fps = id - 20000;
    else if (id >= 10000) S().maxRes = id - 10000;
    else return;
    SettingsChanged();
}

// Quick capture (F11 / camera on the recording bar): the area being recorded, otherwise the
// whole screen under the mouse.
void App::Screenshot() {
    if (state_ != RecState::Idle && ValidRegion(region_) && mode_ != Mode::Webcam && mode_ != Mode::Audio) {
        DoShot(region_);
        return;
    }
    POINT pt;
    GetCursorPos(&pt);
    DoShot(win::MonitorAt(pt).rect);
}

void App::ShowScreenshotMenu(HWND owner, POINT pt) {
    int id = win::PopupMenu(owner, pt,
                            {{1, L"Capture an area…"},
                             {2, L"Capture a window…"},
                             {3, L"Capture full screen"},
                             {0},
                             {4, L"Screenshot settings…"}});
    switch (id) {
    case 1: BeginScreenshot(Mode::Area); break;
    case 2: BeginScreenshot(Mode::Window); break;
    case 3: BeginScreenshot(Mode::Fullscreen); break;
    case 4: settings_->Open(2); break;
    }
}

void App::BeginScreenshot(Mode kind) {
    if (Busy() || state_ == RecState::Preparing) return;
    shotMode_ = true;
    ShowWindow(main_->Hwnd(), SW_HIDE);   // don't cover what the user wants to capture
    if (kind == Mode::Area) area_->BeginSelect(true);
    else if (kind == Mode::Window) area_->BeginPick(true);
    else if (win::Monitors().size() > 1) area_->BeginPickMonitor(true);
    else CaptureSoon(win::Monitors().front().rect);
    Restack();
}

void App::OnShotArea(RECT r) {
    if (ValidRegion(r)) CaptureSoon(r);
    else EndScreenshot();
}

// Hide the picker, give the screen a moment to repaint (e.g. a window brought to the front),
// then capture.
void App::CaptureSoon(RECT r) {
    area_->Hide();
    pendingShot_ = r;
    SetTimer(MainHwnd(), kShotTimer, 180, nullptr);
}

void App::EndScreenshot() {
    shotMode_ = false;
    area_->Hide();
    if (state_ == RecState::Idle) main_->Show(true);
}

void App::DoShot(RECT r) {
    ShotOptions o;
    o.region = r;
    o.cursor = S().shotCursor;
    o.jpg = S().shotFormat == L"jpg";
    o.jpgQuality = S().jpgQuality;
    o.clipboard = S().shotClipboard;
    o.path = JoinPath(S().ScreenshotDir(), L"ScreenRec_" + TimestampName() + (o.jpg ? L".jpg" : L".png"));
    if (S().shotSound) PlaySoundW(L"SystemAsterisk", nullptr, SND_ALIAS | SND_ASYNC);
    TakeScreenshot(o, MainHwnd());
}

// ---------------------------------------------------------------------------------------------
// Settings, hotkeys, z-order

void App::SettingsChanged() {
    S().Save();
    win::SetRunAtLogin(S().launchAtLogin);
    ApplyExclusion();
    if (!hotkeySuspend_) {
        UnregisterHotkeys();
        RegisterHotkeys();
    }
    SyncWebcamOverlay();
    webcam_->ApplySettings();
    RefreshUi();
}

void App::RefreshUi() {
    main_->Relayout();
    if (bar_->Visible()) bar_->Refresh();
    if (drawbar_->Visible()) drawbar_->Relayout();
    if (settings_->Visible()) settings_->Relayout();
    if (state_ == RecState::Recording && mode_ != Mode::Webcam && mode_ != Mode::Audio) ApplyCursorEffects();
}

void App::ApplyCursorEffects() {
    canvas_->SetEffects(S().cursor && S().clickEffect);
    cursorfx::Apply(S().cursor && S().highlight, S().cursor && S().pointer == 1);
}

void App::ApplyExclusion() {
    for (Window* w : {(Window*)main_.get(), (Window*)settings_.get(), (Window*)outputs_.get()})
        if (w && w->Hwnd()) win::ExcludeFromCapture(w->Hwnd(), S().excludeSelf);
}

void App::Restack() {
    // Bottom to top. During the countdown the overlay goes above the webcam preview so the
    // number is never hidden; the control bar always stays on top.
    bool countdown = state_ == RecState::Countdown;
    Window* order[] = {countdown ? (Window*)canvas_.get() : (Window*)area_.get(),
                       countdown ? (Window*)webcam_.get() : (Window*)canvas_.get(),
                       countdown ? (Window*)area_.get() : (Window*)webcam_.get(), (Window*)drawbar_.get(), (Window*)bar_.get()};
    for (Window* w : order) {
        if (w && w->Visible())
            SetWindowPos(w->Hwnd(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}

void App::RegisterHotkeys() {
    HWND h = MainHwnd();
    auto reg = [h](int id, const Hotkey& k) {
        if (!k.Empty()) RegisterHotKey(h, id, k.mods | MOD_NOREPEAT, k.vk);
    };
    reg(HK_RECORD, S().hkRecord);
    reg(HK_WINDOW, S().hkWindow);
    reg(HK_STOP, S().hkStop);
    reg(HK_PAUSE, S().hkPause);
    reg(HK_DRAW, S().hkDraw);
    reg(HK_SHOT, S().hkShot);
    hotkeysRegistered_ = true;
}

void App::UnregisterHotkeys() {
    if (!hotkeysRegistered_) return;
    for (int id = HK_RECORD; id <= HK_SHOT; ++id) UnregisterHotKey(MainHwnd(), id);
    hotkeysRegistered_ = false;
}

void App::SuspendHotkeys(bool suspend) {
    hotkeySuspend_ = std::max(0, hotkeySuspend_ + (suspend ? 1 : -1));
    if (hotkeySuspend_) UnregisterHotkeys();
    else if (!hotkeysRegistered_) RegisterHotkeys();
}

bool App::HandleAppMessage(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_HOTKEY:
        switch ((int)wp) {
        case HK_RECORD: RecordCurrentDisplay(); break;
        case HK_WINDOW: RecordCurrentWindow(); break;
        case HK_STOP: Stop(); break;
        case HK_PAUSE: TogglePause(); break;
        case HK_DRAW: ToggleDrawing(); break;
        case HK_SHOT: Screenshot(); break;
        }
        return true;
    case WM_TIMER:
        if (wp == kShotTimer) {
            KillTimer(MainHwnd(), kShotTimer);
            DoShot(pendingShot_);
            EndScreenshot();
            return true;
        }
        if (wp != kCountdownTimer) return false;
        if (--g_countdownLeft <= 0) OnCountdownDone();
        else area_->SetCountdown(g_countdownLeft);
        return true;
    case WM_APP_ENCODERS_READY:
        if (settings_->Visible()) settings_->Relayout();
        return true;
    case WM_APP_RECORD_DONE: {
        auto* res = reinterpret_cast<RecordResult*>(lp);
        FinishRecording(res);
        delete res;
        return true;
    }
    case WM_APP_FFMPEG_DIED:
        // Finishing reports what was saved (or ffmpeg's error) in a single dialog.
        if (state_ == RecState::Recording && !rec_.Paused()) Stop();
        return true;
    case WM_APP_UPDATE_CHECKED: {
        std::unique_ptr<updater::CheckResult> r(reinterpret_cast<updater::CheckResult*>(lp));
        updateBusy_ = false;
        S().lastUpdateCheck = (long long)time(nullptr);
        S().Save();
        if (r->ok && r->newer) {
            update_ = r->release;
            updateStatus_ = L"ScreenRec " + r->release.version + L" is available.";
        } else if (r->manual) {
            updateStatus_ = r->ok ? L"You're up to date." : r->error;
        }
        RefreshUi();
        return true;
    }
    case WM_APP_UPDATE_DOWNLOADED: {
        std::unique_ptr<std::wstring> path(reinterpret_cast<std::wstring*>(lp));
        updateBusy_ = false;
        if (wp && updater::RunInstaller(*path)) {
            Quit();   // the installer replaces the files and relaunches ScreenRec
        } else {
            updateStatus_ = L"The download failed. You can get the update from the release page.";
            if (update_) update_->installerUrl.clear();   // next click opens the page instead
            RefreshUi();
        }
        return true;
    }
    case WM_APP_SCREENSHOT_DONE: {
        auto* path = reinterpret_cast<std::wstring*>(lp);
        if (path->empty()) win::Error(MainHwnd(), L"The screenshot could not be saved.");
        else if (S().shotNotify) toast_->Pop(*path, false);
        if (!path->empty() && outputs_->Visible()) outputs_->Reload(*path);
        delete path;
        return true;
    }
    }
    return false;
}

SIZE App::OutputSizeForLabel() const {
    POINT pt;
    GetCursorPos(&pt);
    RECT r = ValidRegion(region_) ? region_ : win::MonitorAt(pt).rect;
    return ff::OutputSize(r.right - r.left, r.bottom - r.top, S().maxRes);
}
