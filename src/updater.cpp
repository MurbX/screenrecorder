#include "updater.h"
#include "version.h"
#include <shellapi.h>
#include <winhttp.h>

namespace updater {
namespace {

// Minimal HTTPS GET (follows redirects). Writes to `file` when given, else returns the body.
bool HttpGet(const std::wstring& url, std::string* body, const std::wstring& file) {
    URL_COMPONENTS uc{sizeof(uc)};
    wchar_t host[256] = {}, path[2048] = {};
    uc.lpszHostName = host;
    uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = 2048;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) return false;
    std::wstring ua = std::wstring(L"ScreenRec/") + SCREENREC_VERSION;
    HINTERNET s = WinHttpOpen(ua.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) return false;
    bool ok = false;
    HINTERNET c = WinHttpConnect(s, host, uc.nPort, 0);
    HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                         uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)
                    : nullptr;
    if (r) {
        const wchar_t* headers = L"Accept: application/vnd.github+json\r\n";
        DWORD status = 0, len = sizeof(status);
        if (WinHttpSendRequest(r, headers, (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(r, nullptr) &&
            WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status, &len, nullptr) &&
            status == 200) {
            HANDLE out = INVALID_HANDLE_VALUE;
            if (!file.empty()) out = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
            ok = file.empty() || out != INVALID_HANDLE_VALUE;
            char buf[64 * 1024];
            DWORD n = 0;
            while (ok && WinHttpReadData(r, buf, sizeof(buf), &n) && n) {
                if (body) body->append(buf, n);
                DWORD w = 0;
                if (out != INVALID_HANDLE_VALUE && !WriteFile(out, buf, n, &w, nullptr)) ok = false;
            }
            if (out != INVALID_HANDLE_VALUE) CloseHandle(out);
            if (!ok && !file.empty()) DeleteFileW(file.c_str());
        }
        WinHttpCloseHandle(r);
    }
    if (c) WinHttpCloseHandle(c);
    WinHttpCloseHandle(s);
    return ok;
}

// Value of the first "key":"..." after `from` in a JSON text (enough for the Releases API).
std::string JsonString(const std::string& json, const std::string& key, size_t from = 0, size_t* at = nullptr) {
    size_t k = json.find("\"" + key + "\"", from);
    if (k == std::string::npos) return {};
    size_t q = json.find('"', json.find(':', k) + 1);
    if (q == std::string::npos) return {};
    std::string v;
    for (size_t i = q + 1; i < json.size() && json[i] != '"'; ++i) {
        if (json[i] == '\\' && i + 1 < json.size()) ++i;
        v += json[i];
    }
    if (at) *at = q;
    return v;
}

std::vector<int> Parts(const std::wstring& v) {
    std::vector<int> p;
    size_t i = (!v.empty() && (v[0] == L'v' || v[0] == L'V')) ? 1 : 0;
    while (i < v.size() && p.size() < 3) {
        int n = 0;
        bool any = false;
        while (i < v.size() && iswdigit(v[i])) n = n * 10 + (v[i++] - L'0'), any = true;
        if (!any) break;
        p.push_back(n);
        if (i < v.size() && v[i] == L'.') ++i;
        else break;
    }
    p.resize(3, 0);
    return p;
}
}  // namespace

bool Enabled() { return wcslen(SCREENREC_GITHUB_REPO) > 0; }
std::wstring CurrentVersion() { return SCREENREC_VERSION; }
bool IsNewer(const std::wstring& candidate, const std::wstring& current) { return Parts(candidate) > Parts(current); }

void CheckAsync(HWND notify, bool manual) {
    std::thread([notify, manual] {
        auto* res = new CheckResult;
        res->manual = manual;
        std::string json;
        std::wstring url = std::wstring(L"https://api.github.com/repos/") + SCREENREC_GITHUB_REPO + L"/releases/latest";
        if (!Enabled()) {
            res->error = L"Update checks are not configured for this build.";
        } else if (!HttpGet(url, &json, L"")) {
            res->error = L"Couldn't reach GitHub. Check your internet connection.";
        } else {
            std::wstring tag = Widen(JsonString(json, "tag_name"));
            res->release.version = (!tag.empty() && (tag[0] == L'v' || tag[0] == L'V')) ? tag.substr(1) : tag;
            res->release.pageUrl = Widen(JsonString(json, "html_url"));
            // First asset whose download URL is the Setup .exe.
            for (size_t at = 0;;) {
                std::string u = JsonString(json, "browser_download_url", at, &at);
                if (u.empty()) break;
                std::string lower = u;
                for (auto& ch : lower) ch = (char)tolower((unsigned char)ch);
                if (lower.find("setup") != std::string::npos && lower.size() > 4 && lower.substr(lower.size() - 4) == ".exe") {
                    res->release.installerUrl = Widen(u);
                    break;
                }
                ++at;
            }
            res->ok = !res->release.version.empty();
            res->newer = res->ok && IsNewer(res->release.version, CurrentVersion());
            if (!res->ok) res->error = L"No release information found.";
        }
        if (!PostMessageW(notify, WM_APP_UPDATE_CHECKED, 0, (LPARAM)res)) delete res;
    }).detach();
}

void DownloadAsync(const Release& r, HWND notify) {
    std::thread([r, notify] {
        wchar_t tmp[MAX_PATH];
        GetTempPathW(MAX_PATH, tmp);
        std::wstring path = JoinPath(tmp, L"ScreenRec-Setup-" + r.version + L".exe");
        bool ok = !r.installerUrl.empty() && HttpGet(r.installerUrl, nullptr, path) && FileSize(path) > 100000;
        PostMessageW(notify, WM_APP_UPDATE_DOWNLOADED, ok, (LPARAM) new std::wstring(path));
    }).detach();
}

bool RunInstaller(const std::wstring& path) {
    // Inno Setup: show progress only, close the running app, relaunch it afterwards.
    std::wstring args = L"/SILENT /SP- /CLOSEAPPLICATIONS /RESTARTAPPLICATIONS";
    return (INT_PTR)ShellExecuteW(nullptr, L"open", path.c_str(), args.c_str(), nullptr, SW_SHOWNORMAL) > 32;
}

}  // namespace updater
