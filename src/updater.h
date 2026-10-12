#pragma once
#include "common.h"

// Update checks against the project's GitHub Releases (latest release + its Setup .exe asset).
namespace updater {

struct Release {
    std::wstring version;        // "1.2.0" (tag without the leading "v")
    std::wstring pageUrl;        // release notes page
    std::wstring installerUrl;   // ScreenRec-Setup-*.exe asset
};

struct CheckResult {
    bool ok = false;              // the request worked
    bool newer = false;           // release.version > the running version
    bool manual = false;          // user clicked "Check now"
    Release release;
    std::wstring error;
};

bool Enabled();                                // a repository is configured
std::wstring CurrentVersion();
bool IsNewer(const std::wstring& candidate, const std::wstring& current);   // semver x.y.z
void CheckAsync(HWND notify, bool manual);     // posts WM_APP_UPDATE_CHECKED (lParam: heap CheckResult*)
// Downloads the installer to %TEMP% and posts WM_APP_UPDATE_DOWNLOADED (wParam: ok, lParam: heap path).
void DownloadAsync(const Release& r, HWND notify);
// Runs a downloaded installer as an in-place upgrade; it closes ScreenRec and relaunches it.
bool RunInstaller(const std::wstring& path);

}  // namespace updater
