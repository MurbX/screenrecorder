#include "common.h"
#include <shlobj.h>
#include <cstdarg>

std::wstring Widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

std::string Narrow(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring Fmt(const wchar_t* fmt, ...) {
    wchar_t buf[2048];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf_s(buf, _countof(buf), _TRUNCATE, fmt, ap);
    va_end(ap);
    return buf;
}

std::wstring ExeDir() {
    wchar_t p[MAX_PATH];
    GetModuleFileNameW(nullptr, p, MAX_PATH);
    std::wstring s = p;
    return s.substr(0, s.find_last_of(L"\\/"));
}

static std::wstring KnownDir(REFKNOWNFOLDERID id) {
    PWSTR p = nullptr;
    std::wstring out;
    if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_CREATE, nullptr, &p))) out = p;
    CoTaskMemFree(p);
    out = JoinPath(out, L"ScreenRec");
    CreateDirectoryW(out.c_str(), nullptr);
    return out;
}

std::wstring AppDataDir() { return KnownDir(FOLDERID_RoamingAppData); }
std::wstring LocalDataDir() { return KnownDir(FOLDERID_LocalAppData); }

std::wstring JoinPath(const std::wstring& a, const std::wstring& b) {
    if (a.empty()) return b;
    if (a.back() == L'\\' || a.back() == L'/') return a + b;
    return a + L"\\" + b;
}

std::wstring FileName(const std::wstring& path) {
    size_t p = path.find_last_of(L"\\/");
    return p == std::wstring::npos ? path : path.substr(p + 1);
}

std::wstring FileExt(const std::wstring& path) {
    std::wstring name = FileName(path);
    size_t p = name.find_last_of(L'.');
    if (p == std::wstring::npos) return {};
    std::wstring e = name.substr(p + 1);
    for (auto& c : e) c = (wchar_t)towlower(c);
    return e;
}

std::wstring StripExt(const std::wstring& path) {
    size_t slash = path.find_last_of(L"\\/");
    size_t dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos || (slash != std::wstring::npos && dot < slash)) return path;
    return path.substr(0, dot);
}

bool FileExists(const std::wstring& path) {
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

uint64_t FileSize(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &d)) return 0;
    return ((uint64_t)d.nFileSizeHigh << 32) | d.nFileSizeLow;
}

std::wstring TimestampName() {
    SYSTEMTIME t;
    GetLocalTime(&t);
    return Fmt(L"%04d%02d%02d_%02d%02d%02d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
}

std::wstring FormatDuration(double seconds, bool forceHours) {
    int s = (int)std::max(0.0, seconds);
    int h = s / 3600, m = (s / 60) % 60, sec = s % 60;
    if (h > 0 || forceHours) return Fmt(L"%02d:%02d:%02d", h, m, sec);
    return Fmt(L"%02d:%02d", m, sec);
}

std::wstring FormatBytes(uint64_t b) {
    if (b >= 1024ull * 1024 * 1024) return Fmt(L"%.2f GB", b / (1024.0 * 1024 * 1024));
    if (b >= 1024ull * 1024) return Fmt(L"%.1f MB", b / (1024.0 * 1024));
    if (b >= 1024) return Fmt(L"%.0f KB", b / 1024.0);
    return Fmt(L"%llu B", (unsigned long long)b);
}
