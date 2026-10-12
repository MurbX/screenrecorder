#pragma once
#include "recorder.h"
#include "settings.h"
#include "updater.h"

class MainWindow;
class AreaOverlay;
class ControlBar;
class Canvas;
class DrawToolbar;
class WebcamWindow;
class SettingsWindow;
class OutputWindow;
class Toast;
class ImageEditor;

enum class RecState {
    Idle,        // only the main window is visible
    Preparing,   // area/window chosen, control bar shown, not recording yet
    Countdown,
    Recording,
    Finishing,   // ffmpeg is finalising the file
};

class App {
public:
    App();
    ~App();
    bool Init();
    int Run();
    void Quit();

    // Main window / control bar actions
    void ChooseMode(Mode m, bool pickDisplay = true);
    void Cancel();
    void Record();            // REC button: start (with countdown) or stop
    void Stop();
    void TogglePause();
    void ReselectArea();
    void ToggleDrawing();
    void ToggleWebcamOverlay();
    void ToggleCursor();
    void ShowCursorMenu(HWND owner, POINT screenPt);   // cursor visibility + highlight/click effects
    void ToggleSpeaker();
    void ToggleMic();
    void ShowSettings();
    void ShowOutputs(const std::wstring& select = L"");
    void OpenOutputFolder();
    void OpenCapture(const std::wstring& path, bool video);   // images -> editor, videos -> output list
    void ShowResolutionMenu(HWND owner, POINT screenPt);
    void Screenshot();                                    // quick: recorded area or screen under the mouse
    void ShowScreenshotMenu(HWND owner, POINT screenPt);  // area / window / full screen
    void BeginScreenshot(Mode kind);
    void OnShotArea(RECT r);
    void RecordCurrentDisplay();
    void RecordCurrentWindow();

    // Callbacks from overlays
    void OnRegionChanged(RECT r);
    void OnWindowPicked(HWND target);
    void OnMonitorPicked(RECT monitor);
    void OnCountdownDone();

    // Updates
    void CheckForUpdates(bool manual);
    void InstallUpdate();
    bool UpdateAvailable() const { return update_.has_value(); }
    const std::optional<updater::Release>& PendingUpdate() const { return update_; }
    const std::wstring& UpdateStatus() const { return updateStatus_; }
    bool UpdateBusy() const { return updateBusy_; }

    Canvas& GetCanvas() { return *canvas_; }
    void RefreshUi();         // re-layout every visible window after a state change
    void SettingsChanged();   // persist + apply side effects
    void SuspendHotkeys(bool suspend);
    bool HandleAppMessage(UINT msg, WPARAM wp, LPARAM lp);   // WM_APP_* / WM_HOTKEY forwarded by MainWindow

    RecState State() const { return state_; }
    Mode CurrentMode() const { return mode_; }
    bool Recording() const { return state_ == RecState::Recording; }
    bool Paused() const { return rec_.Paused(); }
    bool Busy() const { return state_ == RecState::Countdown || state_ == RecState::Recording || state_ == RecState::Finishing; }
    double Elapsed() const { return rec_.Elapsed(); }
    float AudioLevel(bool mic) const { return rec_.Level(mic); }
    uint64_t Bytes() const { return rec_.Bytes(); }
    RECT Region() const { return region_; }
    bool Drawing() const;
    HWND MainHwnd() const;
    SIZE OutputSizeForLabel() const;

private:
    void EnterPreparing();
    void StartRecording();
    void FinishRecording(struct RecordResult* res);
    void BackToIdle();
    void Restack();
    void RegisterHotkeys();
    void UnregisterHotkeys();
    void ApplyExclusion();
    void SyncWebcamOverlay();
    void ApplyCursorEffects();   // halo/dot cursors + click ripples while recording
    void CaptureSoon(RECT r);
    void EndScreenshot();
    void DoShot(RECT r);
    void PlaceCountdown();

    std::unique_ptr<MainWindow> main_;
    std::unique_ptr<AreaOverlay> area_;
    std::unique_ptr<ControlBar> bar_;
    std::unique_ptr<Canvas> canvas_;
    std::unique_ptr<DrawToolbar> drawbar_;
    std::unique_ptr<WebcamWindow> webcam_;
    std::unique_ptr<SettingsWindow> settings_;
    std::unique_ptr<OutputWindow> outputs_;
    std::unique_ptr<Toast> toast_;
    std::unique_ptr<ImageEditor> editor_;

    Recorder rec_;
    RecState state_ = RecState::Idle;
    Mode mode_ = Mode::Area;
    RECT region_{};
    bool hotkeysRegistered_ = false;
    bool shotMode_ = false;      // a screenshot picker is open
    RECT pendingShot_{};
    int hotkeySuspend_ = 0;
    std::optional<updater::Release> update_;
    std::wstring updateStatus_;
    bool updateBusy_ = false;
};

App& app();
