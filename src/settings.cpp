#include "settings.h"
#include <shlobj.h>

Settings& S() {
    static Settings s;
    return s;
}

std::wstring Hotkey::Text() const {
    if (!vk) return L"";
    std::wstring s;
    if (mods & MOD_CONTROL) s += L"Ctrl+";
    if (mods & MOD_ALT) s += L"Alt+";
    if (mods & MOD_SHIFT) s += L"Shift+";
    if (mods & MOD_WIN) s += L"Win+";
    if (vk >= VK_F1 && vk <= VK_F24) return s + Fmt(L"F%u", vk - VK_F1 + 1);
    if ((vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z')) return s + (wchar_t)vk;
    wchar_t name[64] = {};
    UINT sc = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    LONG lp = sc << 16;
    if (vk == VK_INSERT || vk == VK_DELETE || vk == VK_HOME || vk == VK_END || vk == VK_PRIOR || vk == VK_NEXT ||
        (vk >= VK_LEFT && vk <= VK_DOWN))
        lp |= 1 << 24;   // extended key
    if (GetKeyNameTextW(lp, name, 64)) return s + name;
    return s + Fmt(L"Key %u", vk);
}

static std::wstring IniPath() { return JoinPath(AppDataDir(), L"settings.ini"); }

static std::wstring DefaultOutputDir() {
    PWSTR p = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Videos, KF_FLAG_CREATE, nullptr, &p))) dir = p;
    CoTaskMemFree(p);
    return JoinPath(dir, L"ScreenRec");
}

namespace {
struct Ini {
    std::wstring path;
    std::wstring Str(const wchar_t* sec, const wchar_t* key, const std::wstring& def) const {
        wchar_t buf[1024];
        GetPrivateProfileStringW(sec, key, def.c_str(), buf, 1024, path.c_str());
        return buf;
    }
    int Int(const wchar_t* sec, const wchar_t* key, int def) const {
        return (int)GetPrivateProfileIntW(sec, key, def, path.c_str());
    }
    bool Bool(const wchar_t* sec, const wchar_t* key, bool def) const { return Int(sec, key, def ? 1 : 0) != 0; }
    void Put(const wchar_t* sec, const wchar_t* key, const std::wstring& v) const {
        WritePrivateProfileStringW(sec, key, v.c_str(), path.c_str());
    }
    void Put(const wchar_t* sec, const wchar_t* key, int v) const { Put(sec, key, std::to_wstring(v)); }
    Hotkey Hk(const wchar_t* key, Hotkey def) const {
        int v = Int(L"Hotkey", key, (int)((def.mods << 16) | def.vk));
        return Hotkey{(UINT)(v >> 16) & 0xffff, (UINT)v & 0xffff};
    }
    void PutHk(const wchar_t* key, Hotkey h) const { Put(L"Hotkey", key, (int)((h.mods << 16) | h.vk)); }
};
}  // namespace

void Settings::Load() {
    Settings d;   // defaults
    Ini ini{IniPath()};
    excludeSelf = ini.Bool(L"General", L"ExcludeSelf", d.excludeSelf);
    countdown = ini.Bool(L"General", L"Countdown", d.countdown);
    countdownSec = std::clamp(ini.Int(L"General", L"CountdownSec", d.countdownSec), 1, 10);
    launchAtLogin = ini.Bool(L"General", L"LaunchAtLogin", d.launchAtLogin);
    outputDir = ini.Str(L"General", L"OutputDir", L"");
    if (outputDir.empty()) outputDir = DefaultOutputDir();
    showOutputAfter = ini.Bool(L"General", L"ShowOutputAfter", d.showOutputAfter);
    recThumb = ini.Bool(L"General", L"RecordingPreview", d.recThumb);
    autoUpdate = ini.Bool(L"Updates", L"Auto", d.autoUpdate);
    lastUpdateCheck = _wtoi64(ini.Str(L"Updates", L"LastCheck", L"0").c_str());

    container = ini.Str(L"Video", L"Container", d.container);
    if (container != L"mp4" && container != L"mkv") container = L"mp4";
    encoder = ini.Str(L"Video", L"Encoder", d.encoder);
    quality = std::clamp(ini.Int(L"Video", L"Quality", d.quality), 10, 100);
    maxRes = ini.Int(L"Video", L"MaxRes", d.maxRes);
    fps = std::clamp(ini.Int(L"Video", L"Fps", d.fps), 5, 60);
    audioKbps = ini.Int(L"Video", L"AudioKbps", d.audioKbps);
    recSpeaker = ini.Bool(L"Audio", L"Speaker", d.recSpeaker);
    recMic = ini.Bool(L"Audio", L"Mic", d.recMic);
    speakerId = ini.Str(L"Audio", L"SpeakerId", L"");
    micId = ini.Str(L"Audio", L"MicId", L"");
    speakerVol = std::clamp(ini.Int(L"Audio", L"SpeakerVol", d.speakerVol), 0, 200);
    micVol = std::clamp(ini.Int(L"Audio", L"MicVol", d.micVol), 0, 200);

    shotDir = ini.Str(L"Image", L"Folder", L"");
    shotFormat = ini.Str(L"Image", L"Format", d.shotFormat);
    if (shotFormat != L"png" && shotFormat != L"jpg") shotFormat = L"png";
    jpgQuality = std::clamp(ini.Int(L"Image", L"JpgQuality", d.jpgQuality), 50, 100);
    shotCursor = ini.Bool(L"Image", L"Cursor", d.shotCursor);
    shotClipboard = ini.Bool(L"Image", L"Clipboard", d.shotClipboard);
    shotNotify = ini.Bool(L"Image", L"Notify", d.shotNotify);
    shotSound = ini.Bool(L"Image", L"Sound", d.shotSound);

    cursor = ini.Bool(L"Overlay", L"Cursor", d.cursor);
    pointer = ini.Int(L"Overlay", L"Pointer", d.pointer);
    highlight = ini.Bool(L"Overlay", L"Highlight", d.highlight);
    clickEffect = ini.Bool(L"Overlay", L"ClickEffect", d.clickEffect);
    webcam = ini.Bool(L"Overlay", L"Webcam", d.webcam);
    webcamName = ini.Str(L"Overlay", L"WebcamName", L"");
    mirror = ini.Bool(L"Overlay", L"Mirror", d.mirror);
    flip = ini.Bool(L"Overlay", L"Flip", d.flip);
    shadow = ini.Bool(L"Overlay", L"Shadow", d.shadow);
    borderWidth = std::clamp(ini.Int(L"Overlay", L"BorderWidth", d.borderWidth), 0, 20);
    borderColor = (uint32_t)ini.Int(L"Overlay", L"BorderColor", (int)d.borderColor);
    camShape = std::clamp(ini.Int(L"Overlay", L"CamShape", d.camShape), 0, 1);
    camX = ini.Int(L"Overlay", L"CamX", d.camX);
    camY = ini.Int(L"Overlay", L"CamY", d.camY);
    camW = std::clamp(ini.Int(L"Overlay", L"CamW", d.camW), 120, 1600);

    hkRecord = ini.Hk(L"Record", d.hkRecord);
    hkWindow = ini.Hk(L"Window", d.hkWindow);
    hkStop = ini.Hk(L"Stop", d.hkStop);
    hkPause = ini.Hk(L"Pause", d.hkPause);
    hkDraw = ini.Hk(L"Draw", d.hkDraw);
    hkShot = ini.Hk(L"Screenshot", d.hkShot);

    region.left = ini.Int(L"State", L"RegionL", 0);
    region.top = ini.Int(L"State", L"RegionT", 0);
    region.right = ini.Int(L"State", L"RegionR", 0);
    region.bottom = ini.Int(L"State", L"RegionB", 0);
}

void Settings::Save() const {
    Ini ini{IniPath()};
    ini.Put(L"General", L"ExcludeSelf", excludeSelf);
    ini.Put(L"General", L"Countdown", countdown);
    ini.Put(L"General", L"CountdownSec", countdownSec);
    ini.Put(L"General", L"LaunchAtLogin", launchAtLogin);
    ini.Put(L"General", L"OutputDir", outputDir);
    ini.Put(L"General", L"ShowOutputAfter", showOutputAfter);
    ini.Put(L"General", L"RecordingPreview", recThumb);
    ini.Put(L"Updates", L"Auto", autoUpdate);
    ini.Put(L"Updates", L"LastCheck", std::to_wstring(lastUpdateCheck));

    ini.Put(L"Video", L"Container", container);
    ini.Put(L"Video", L"Encoder", encoder);
    ini.Put(L"Video", L"Quality", quality);
    ini.Put(L"Video", L"MaxRes", maxRes);
    ini.Put(L"Video", L"Fps", fps);
    ini.Put(L"Video", L"AudioKbps", audioKbps);
    ini.Put(L"Audio", L"Speaker", recSpeaker);
    ini.Put(L"Audio", L"Mic", recMic);
    ini.Put(L"Audio", L"SpeakerId", speakerId);
    ini.Put(L"Audio", L"MicId", micId);
    ini.Put(L"Audio", L"SpeakerVol", speakerVol);
    ini.Put(L"Audio", L"MicVol", micVol);

    ini.Put(L"Image", L"Folder", shotDir);
    ini.Put(L"Image", L"Format", shotFormat);
    ini.Put(L"Image", L"JpgQuality", jpgQuality);
    ini.Put(L"Image", L"Cursor", shotCursor);
    ini.Put(L"Image", L"Clipboard", shotClipboard);
    ini.Put(L"Image", L"Notify", shotNotify);
    ini.Put(L"Image", L"Sound", shotSound);

    ini.Put(L"Overlay", L"Cursor", cursor);
    ini.Put(L"Overlay", L"Pointer", pointer);
    ini.Put(L"Overlay", L"Highlight", highlight);
    ini.Put(L"Overlay", L"ClickEffect", clickEffect);
    ini.Put(L"Overlay", L"Webcam", webcam);
    ini.Put(L"Overlay", L"WebcamName", webcamName);
    ini.Put(L"Overlay", L"Mirror", mirror);
    ini.Put(L"Overlay", L"Flip", flip);
    ini.Put(L"Overlay", L"Shadow", shadow);
    ini.Put(L"Overlay", L"BorderWidth", borderWidth);
    ini.Put(L"Overlay", L"BorderColor", (int)borderColor);
    ini.Put(L"Overlay", L"CamShape", camShape);
    ini.Put(L"Overlay", L"CamX", camX);
    ini.Put(L"Overlay", L"CamY", camY);
    ini.Put(L"Overlay", L"CamW", camW);

    ini.PutHk(L"Record", hkRecord);
    ini.PutHk(L"Window", hkWindow);
    ini.PutHk(L"Stop", hkStop);
    ini.PutHk(L"Pause", hkPause);
    ini.PutHk(L"Draw", hkDraw);
    ini.PutHk(L"Screenshot", hkShot);

    ini.Put(L"State", L"RegionL", region.left);
    ini.Put(L"State", L"RegionT", region.top);
    ini.Put(L"State", L"RegionR", region.right);
    ini.Put(L"State", L"RegionB", region.bottom);
}

void Settings::Reset() {
    RECT keep = region;
    *this = Settings{};
    region = keep;
    outputDir = DefaultOutputDir();
}

std::wstring Settings::ResolutionLabel() const {
    std::wstring res = maxRes == 0 ? L"Original" : maxRes == 2160 ? L"4K" : Fmt(L"%dp", maxRes);
    return res + L" · " + Fmt(L"%dFPS", fps);
}
