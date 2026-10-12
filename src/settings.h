#pragma once
#include "common.h"

struct Hotkey {
    UINT mods = 0;   // MOD_CONTROL | MOD_SHIFT | MOD_ALT | MOD_WIN
    UINT vk = 0;
    bool Empty() const { return vk == 0; }
    std::wstring Text() const;
};

enum class Mode { Area, Fullscreen, Window, Webcam, Audio };

struct Settings {
    // General
    bool excludeSelf = true;
    bool countdown = true;
    int countdownSec = 3;
    bool launchAtLogin = false;
    std::wstring outputDir;
    bool autoUpdate = true;            // check GitHub Releases once a day
    long long lastUpdateCheck = 0;     // unix seconds
    bool recThumb = true;              // floating preview after recording
    bool showOutputAfter = false;      // also open the output list

    // Video / audio format
    std::wstring container = L"mp4";   // mp4 | mkv
    std::wstring encoder = L"auto";    // auto | h264_nvenc | h264_qsv | h264_amf | libx264
    int quality = 80;                  // 10..100
    int maxRes = 1080;                 // 0 = original, else max output height
    int fps = 60;
    int audioKbps = 192;
    bool recSpeaker = true;
    bool recMic = false;
    std::wstring speakerId;            // empty = system default
    std::wstring micId;
    int speakerVol = 100;              // 0..200 %
    int micVol = 100;

    // Image (screenshots)
    std::wstring shotDir;              // empty = same folder as videos
    std::wstring shotFormat = L"png";  // png | jpg
    int jpgQuality = 90;               // 50..100
    bool shotCursor = true;
    bool shotClipboard = true;         // also copy the image to the clipboard
    bool shotNotify = true;
    bool shotSound = false;
    std::wstring ScreenshotDir() const { return shotDir.empty() ? outputDir : shotDir; }

    // Overlay
    bool cursor = true;
    int pointer = 0;                   // 0 = draw cursor, 1 = draw dot
    bool highlight = true;             // yellow halo around the cursor
    bool clickEffect = true;           // ripple on mouse clicks
    bool webcam = false;
    std::wstring webcamName;           // empty = first camera
    bool mirror = true;
    bool flip = false;
    bool shadow = true;
    int borderWidth = 0;
    uint32_t borderColor = 0xffd60a;
    int camShape = 0;                  // 0 = circle, 1 = rounded rectangle
    int camX = -1, camY = -1, camW = 360;   // bubble position/size in physical px

    // Hotkeys
    Hotkey hkRecord{0, VK_F12};
    Hotkey hkWindow{MOD_CONTROL, VK_F12};
    Hotkey hkStop{};
    Hotkey hkPause{MOD_SHIFT, VK_F12};
    Hotkey hkDraw{MOD_CONTROL | MOD_SHIFT, 'D'};
    Hotkey hkShot{0, VK_F11};

    // Remembered state
    RECT region{};                     // last selected area (screen px)

    void Load();
    void Save() const;
    void Reset();
    std::wstring ResolutionLabel() const;   // "1080p · 60FPS"
};

Settings& S();
