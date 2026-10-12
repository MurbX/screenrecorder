#include "recorder.h"
#include "winutil.h"
#include <shlobj.h>

using win::Quote;

namespace ff {
namespace {
std::wstring g_ffmpeg;
std::once_flag g_findOnce;
std::mutex g_encMu;
std::vector<std::wstring> g_encoders;
std::atomic<bool> g_encReady{false};

std::wstring FindInWingetPackages() {
    wchar_t local[MAX_PATH];
    if (!GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH)) return {};
    std::wstring pkgs = JoinPath(local, L"Microsoft\\WinGet\\Packages");
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileW(JoinPath(pkgs, L"Gyan.FFmpeg*").c_str(), &fd);
    if (f == INVALID_HANDLE_VALUE) return {};
    std::wstring found;
    do {
        std::wstring pkg = JoinPath(pkgs, fd.cFileName);
        WIN32_FIND_DATAW sub;
        HANDLE g = FindFirstFileW(JoinPath(pkg, L"ffmpeg-*").c_str(), &sub);
        if (g != INVALID_HANDLE_VALUE) {
            std::wstring exe = JoinPath(JoinPath(pkg, sub.cFileName), L"bin\\ffmpeg.exe");
            if (FileExists(exe)) found = exe;
            FindClose(g);
        }
    } while (found.empty() && FindNextFileW(f, &fd));
    FindClose(f);
    return found;
}

void Find() {
    std::wstring local = JoinPath(ExeDir(), L"ffmpeg.exe");
    if (FileExists(local)) {
        g_ffmpeg = local;
    } else {
        wchar_t buf[MAX_PATH];
        if (SearchPathW(nullptr, L"ffmpeg.exe", nullptr, MAX_PATH, buf, nullptr)) g_ffmpeg = buf;
        else g_ffmpeg = FindInWingetPackages();
    }
}
}  // namespace

const std::wstring& Ffmpeg() {
    std::call_once(g_findOnce, Find);
    return g_ffmpeg;
}
bool Available() { return !Ffmpeg().empty(); }

void DetectEncodersAsync(HWND notify) {
    std::thread([notify] {
        std::vector<std::wstring> found;
        if (Available()) {
            for (const wchar_t* enc : {L"h264_nvenc", L"h264_qsv", L"h264_amf", L"libx264"}) {
                std::wstring pix = wcscmp(enc, L"libx264") == 0 ? L"yuv420p" : L"nv12";
                std::wstring cmd = Quote(Ffmpeg()) +
                                   L" -hide_banner -loglevel error -f lavfi -i color=black:s=320x240:r=30 -frames:v 5"
                                   L" -pix_fmt " + pix + L" -c:v " + enc + L" -f null -";
                if (win::RunHidden(cmd, nullptr, 15000) == 0) found.push_back(enc);
            }
        }
        {
            std::lock_guard lk(g_encMu);
            g_encoders = found;
        }
        g_encReady = true;
        PostMessageW(notify, WM_APP_ENCODERS_READY, 0, 0);
    }).detach();
}

std::vector<std::wstring> Encoders() {
    std::lock_guard lk(g_encMu);
    return g_encoders;
}
bool EncodersReady() { return g_encReady; }

std::wstring ResolveEncoder(const std::wstring& pref) {
    auto list = Encoders();
    if (pref != L"auto" && std::find(list.begin(), list.end(), pref) != list.end()) return pref;
    return list.empty() ? L"libx264" : list.front();
}

std::wstring EncoderLabel(const std::wstring& name) {
    if (name == L"auto") return L"Auto";
    if (name == L"h264_nvenc") return L"H.264 (NVIDIA NVENC)";
    if (name == L"h264_qsv") return L"H.264 (Intel Quick Sync)";
    if (name == L"h264_amf") return L"H.264 (AMD AMF)";
    if (name == L"libx264") return L"H.264 (CPU, x264)";
    return name;
}

SIZE OutputSize(int w, int h, int maxRes) {
    double k = 1.0;
    if (maxRes > 0) {
        // Fit inside a 16:9 box of the chosen height (rotated for portrait regions), never upscale.
        double maxW = maxRes * 16.0 / 9.0, maxH = maxRes;
        if (w < h) std::swap(maxW, maxH);
        k = std::min({1.0, maxW / w, maxH / h});
    }
    int ow = std::max(2, (int)std::lround(w * k) & ~1);
    int oh = std::max(2, (int)std::lround(h * k) & ~1);
    return {ow, oh};
}

// Screen grab for `r` (screen px; empty = monitor under the mouse). ddagrab must run inside the
// filter graph (not as an -f lavfi input) so that -filter_hw_device puts it on the GPU that drives
// that monitor; otherwise it silently captures the default adapter's screen.
Grab GrabFor(RECT r, bool cursor, int fps) {
    win::Monitor m;
    if (r.right > r.left && r.bottom > r.top) {
        m = win::MonitorFor(r);
        IntersectRect(&r, &r, &m.rect);
    } else {
        POINT pt;
        GetCursorPos(&pt);
        m = win::MonitorAt(pt);
        r = m.rect;
    }
    Grab g;
    g.width = (r.right - r.left) & ~1;
    g.height = (r.bottom - r.top) & ~1;
    g.device = Fmt(L" -init_hw_device d3d11va=scr:%d -filter_hw_device scr", m.adapter);
    g.source = Fmt(L"ddagrab=output_idx=%d:draw_mouse=%d", m.output, cursor ? 1 : 0);
    if (fps > 0) g.source += Fmt(L":framerate=%d", fps);
    if (g.width != m.rect.right - m.rect.left || g.height != m.rect.bottom - m.rect.top || r.left != m.rect.left ||
        r.top != m.rect.top)
        g.source += Fmt(L":offset_x=%d:offset_y=%d:video_size=%dx%d", r.left - m.rect.left, r.top - m.rect.top, g.width,
                        g.height);
    return g;
}
void TrimAsync(const std::wstring& src, const std::wstring& dst, double start, double end, HWND notify) {
    std::thread([=] {
        std::wstring cmd = Quote(Ffmpeg()) + Fmt(L" -hide_banner -loglevel error -y -ss %.3f -to %.3f -i ", start, end) +
                           Quote(src) + L" -map 0 -c copy -avoid_negative_ts make_zero " + Quote(dst);
        bool ok = win::RunHidden(cmd, nullptr, 600000) == 0 && FileSize(dst) > 0;
        PostMessageW(notify, WM_APP_TRIM_DONE, ok, (LPARAM) new std::wstring(dst));
    }).detach();
}

// Duration and video size from "ffmpeg -i" (no ffprobe needed, so only ffmpeg.exe ships).
bool Probe(const std::wstring& path, MediaInfo& info) {
    if (!Available()) return false;
    std::string out;
    win::RunHidden(Quote(Ffmpeg()) + L" -hide_banner -i " + Quote(path), &out, 15000);   // exits 1: no output given
    size_t d = out.find("Duration: ");
    if (d == std::string::npos) return false;
    int hh = 0, mm = 0;
    double ss = 0;
    if (sscanf_s(out.c_str() + d + 10, "%d:%d:%lf", &hh, &mm, &ss) == 3) info.duration = hh * 3600 + mm * 60 + ss;
    size_t v = out.find("Video: ");
    if (v != std::string::npos) {
        std::string line = out.substr(v, out.find('\n', v) - v);
        for (size_t i = 0; i + 1 < line.size(); ++i) {   // first "<w>x<h>" made of digits
            if (!isdigit((unsigned char)line[i]) || (i && isdigit((unsigned char)line[i - 1]))) continue;
            int w = 0, h = 0;
            if (sscanf_s(line.c_str() + i, "%dx%d", &w, &h) == 2 && w >= 16 && h >= 16) {
                info.width = w;
                info.height = h;
                break;
            }
        }
    }
    return true;
}
bool Thumbnail(const std::wstring& path, const std::wstring& thumbPath) {
    for (const wchar_t* at : {L"1", L"0"}) {
        std::wstring cmd = Quote(Ffmpeg()) + L" -hide_banner -loglevel error -y -ss " + at + L" -i " + Quote(path) +
                           L" -frames:v 1 -vf scale=320:-2 " + Quote(thumbPath);
        if (win::RunHidden(cmd, nullptr, 20000) == 0 && FileSize(thumbPath) > 0) return true;
    }
    return false;
}
}  // namespace ff

// =============================================================================================

static int CrfFor(int quality) { return std::clamp((int)std::lround(40 - quality * 0.22), 14, 40); }

Recorder::~Recorder() {
    if (active_) {
        ending_ = true;
        EndSegment();
    }
    if (finisher_.joinable()) finisher_.join();
}

std::wstring Recorder::BuildCommand(const std::wstring& segPath) const {
    const auto& c = cfg_;
    std::wstring cmd = Quote(ff::Ffmpeg()) + L" -hide_banner -nostats -loglevel error -y";
    int nextInput = 0;
    bool hasVideo = c.kind != RecordConfig::AudioOnly;
    ff::Grab grab;

    if (c.kind == RecordConfig::Screen) {
        grab = ff::GrabFor(c.region, c.cursor, c.fps);   // a filter-graph source, not an input
        cmd += grab.device;
    } else if (c.kind == RecordConfig::Webcam) {
        cmd += L" -f dshow -rtbufsize 256M -i " + Quote(L"video=" + c.webcamName);
        nextInput++;
    }
    std::vector<int> audioInputs;
    for (AudioPipe* a : {sys_.get(), mic_.get()}) {
        if (!a) continue;
        cmd += Fmt(L" -thread_queue_size 4096 -f f32le -ar %d -ac %d -i ", a->Rate(), a->Channels()) + Quote(a->PipePath());
        audioInputs.push_back(nextInput++);
    }

    std::wstring graph;
    bool hw = c.encoder != L"libx264";
    if (hasVideo) {
        int srcW, srcH;
        if (c.kind == RecordConfig::Screen) {
            srcW = grab.width;
            srcH = grab.height;
            graph = grab.source + L",hwdownload,format=bgra";
        } else {
            srcW = srcH = 0;   // webcam: size unknown up front
            graph = L"[0:v]null";
        }
        if (srcW > 0) {
            SIZE out = ff::OutputSize(srcW, srcH, c.maxRes);
            if (out.cx != srcW || out.cy != srcH) graph += Fmt(L",scale=%d:%d:flags=bicubic", out.cx, out.cy);
        } else if (c.maxRes > 0) {
            graph += Fmt(L",scale=-2:'min(%d,ih)'", c.maxRes);
        }
        graph += hw ? L",format=nv12[v]" : L",format=yuv420p[v]";
    }
    if (audioInputs.size() == 2) {
        if (!graph.empty()) graph += L";";
        graph += Fmt(L"[%d:a][%d:a]amix=inputs=2:duration=longest:normalize=0[a]", audioInputs[0], audioInputs[1]);
    }
    if (!graph.empty()) cmd += L" -filter_complex " + Quote(graph);
    if (hasVideo) cmd += L" -map [v]";
    if (audioInputs.size() == 2) cmd += L" -map [a]";
    else if (audioInputs.size() == 1) cmd += Fmt(L" -map %d:a", audioInputs[0]);

    if (hasVideo) {
        int crf = CrfFor(c.quality);
        if (c.encoder == L"h264_nvenc") cmd += Fmt(L" -c:v h264_nvenc -preset p4 -rc vbr -cq %d -b:v 0", crf);
        else if (c.encoder == L"h264_qsv") cmd += Fmt(L" -c:v h264_qsv -preset veryfast -global_quality %d", crf);
        else if (c.encoder == L"h264_amf") cmd += Fmt(L" -c:v h264_amf -quality speed -rc cqp -qp_i %d -qp_p %d", crf, crf);
        else cmd += Fmt(L" -c:v libx264 -preset veryfast -crf %d", crf);
        if (c.kind == RecordConfig::Screen) cmd += Fmt(L" -fps_mode cfr -r %d -g %d", c.fps, c.fps * 2);
    }
    if (!audioInputs.empty()) {
        if (c.kind == RecordConfig::AudioOnly) cmd += Fmt(L" -c:a libmp3lame -b:a %dk -ac 2", c.audioKbps);
        else cmd += Fmt(L" -c:a aac -b:a %dk -ac 2 -ar 48000", c.audioKbps);
    }
    cmd += L" " + Quote(segPath);
    return cmd;
}

bool Recorder::Start(const RecordConfig& cfg, HWND notify, std::wstring& err) {
    if (active_) return false;
    if (finisher_.joinable()) finisher_.join();
    if (!ff::Available()) {
        err = L"FFmpeg was not found. Put ffmpeg.exe next to ScreenRec.exe or install it with:\n"
              L"winget install Gyan.FFmpeg";
        return false;
    }
    cfg_ = cfg;
    notify_ = notify;
    if (cfg_.kind == RecordConfig::AudioOnly && !cfg_.speaker && !cfg_.mic) {
        err = L"Turn on the speaker or the microphone to record audio.";
        return false;
    }
    std::wstring dir = cfg_.outPath.substr(0, cfg_.outPath.find_last_of(L'\\'));
    SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    {
        std::lock_guard lk(mu_);
        segments_.clear();
    }
    accumulated_ = 0;
    paused_ = false;
    if (!StartSegment(err)) return false;
    active_ = true;
    return true;
}

bool Recorder::StartSegment(std::wstring& err) {
    std::wstring seg;
    {
        std::lock_guard lk(mu_);
        seg = StripExt(cfg_.outPath) + Fmt(L".part%d.", (int)segments_.size() + 1) + FileExt(cfg_.outPath);
    }
    if (cfg_.speaker) {
        sys_ = std::make_unique<AudioPipe>();
        if (!sys_->Open(cfg_.speakerId, true, cfg_.speakerGain, err)) {
            sys_.reset();
            return false;
        }
    }
    if (cfg_.mic) {
        mic_ = std::make_unique<AudioPipe>();
        if (!mic_->Open(cfg_.micId, false, cfg_.micGain, err)) {
            mic_.reset();
            sys_.reset();
            return false;
        }
    }

    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE inRd, inWr, errRd, errWr;
    CreatePipe(&inRd, &inWr, &sa, 0);
    SetHandleInformation(inWr, HANDLE_FLAG_INHERIT, 0);
    CreatePipe(&errRd, &errWr, &sa, 0);
    SetHandleInformation(errRd, HANDLE_FLAG_INHERIT, 0);
    HANDLE nul = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);

    STARTUPINFOW si{sizeof(si)};
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = inRd;
    si.hStdOutput = nul;
    si.hStdError = errWr;
    PROCESS_INFORMATION pi{};
    std::wstring cmd = BuildCommand(seg);
    {   // keep the exact command for troubleshooting (%LOCALAPPDATA%\ScreenRec\last_ffmpeg_command.txt)
        std::string line = Narrow(cmd);
        HANDLE f = CreateFileW(JoinPath(LocalDataDir(), L"last_ffmpeg_command.txt").c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                               nullptr, CREATE_ALWAYS, 0, nullptr);
        DWORD n;
        if (f != INVALID_HANDLE_VALUE) {
            WriteFile(f, line.data(), (DWORD)line.size(), &n, nullptr);
            CloseHandle(f);
        }
    }
    BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | ABOVE_NORMAL_PRIORITY_CLASS,
                             nullptr, nullptr, &si, &pi);
    CloseHandle(inRd);
    CloseHandle(errWr);
    CloseHandle(nul);
    if (!ok) {
        CloseHandle(inWr);
        CloseHandle(errRd);
        sys_.reset();
        mic_.reset();
        err = L"Could not start FFmpeg.";
        return false;
    }
    CloseHandle(pi.hThread);
    process_ = pi.hProcess;
    stdin_ = inWr;
    stderr_ = errRd;
    {
        std::lock_guard lk(mu_);
        segments_.push_back(seg);
        errTail_.clear();
    }
    ending_ = false;
    errReader_ = std::thread([this, h = errRd] {
        char buf[1024];
        DWORD n;
        while (ReadFile(h, buf, sizeof(buf), &n, nullptr) && n) {
            std::lock_guard lk(mu_);
            errTail_.append(buf, n);
            if (errTail_.size() > 4000) errTail_.erase(0, errTail_.size() - 4000);
        }
    });
    watcher_ = std::thread([this, h = pi.hProcess] {
        WaitForSingleObject(h, INFINITE);
        if (!ending_) PostMessageW(notify_, WM_APP_FFMPEG_DIED, 0, 0);
    });
    QueryPerformanceCounter(&segStart_);
    return true;
}

void Recorder::EndSegment() {
    if (!process_) return;
    ending_ = true;
    DWORD n;
    WriteFile(stdin_, "q", 1, &n, nullptr);
    if (WaitForSingleObject(process_, 15000) != WAIT_OBJECT_0) {
        TerminateProcess(process_, 1);
        WaitForSingleObject(process_, 3000);
    }
    if (watcher_.joinable()) watcher_.join();
    if (errReader_.joinable()) errReader_.join();
    // ffmpeg has exited, so the audio pipes are broken and their threads return.
    sys_.reset();
    mic_.reset();
    CloseHandle(process_);
    CloseHandle(stdin_);
    CloseHandle(stderr_);
    process_ = stdin_ = stderr_ = nullptr;

    LARGE_INTEGER now, freq;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&freq);
    accumulated_ += (now.QuadPart - segStart_.QuadPart) / (double)freq.QuadPart;
}

void Recorder::Pause() {
    if (!active_ || paused_) return;
    EndSegment();
    paused_ = true;
}

bool Recorder::Resume(std::wstring& err) {
    if (!active_ || !paused_) return false;
    if (!StartSegment(err)) return false;
    paused_ = false;
    return true;
}

void Recorder::Stop() {
    if (!active_) return;
    if (!paused_) EndSegment();
    active_ = false;
    paused_ = false;
    std::vector<std::wstring> segs;
    {
        std::lock_guard lk(mu_);
        segs = segments_;
    }
    std::wstring tail = ErrorTail();
    finisher_ = std::thread([this, segs, tail, out = cfg_.outPath, notify = notify_] {
        auto* res = new RecordResult;
        res->path = out;
        std::vector<std::wstring> good;
        for (auto& s : segs)
            if (FileSize(s) > 1024) good.push_back(s);
            else DeleteFileW(s.c_str());
        if (good.empty()) {
            res->error = L"Nothing was recorded.";
            if (!tail.empty()) res->error += L"\n\nFFmpeg said:\n" + tail;
        } else if (good.size() == 1) {
            res->ok = MoveFileExW(good[0].c_str(), out.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
            if (!res->ok) res->error = L"Could not save " + out;
        } else {
            std::wstring list = JoinPath(LocalDataDir(), L"concat.txt");
            std::string body;
            for (auto& s : good) {
                std::string p = Narrow(s);
                std::replace(p.begin(), p.end(), '\\', '/');
                std::string esc;
                for (char ch : p) esc += ch == '\'' ? std::string("'\\''") : std::string(1, ch);
                body += "file '" + esc + "'\n";
            }
            HANDLE f = CreateFileW(list.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
            DWORD n;
            WriteFile(f, body.data(), (DWORD)body.size(), &n, nullptr);
            CloseHandle(f);
            std::string out2;
            std::wstring cmd = Quote(ff::Ffmpeg()) + L" -hide_banner -loglevel error -y -f concat -safe 0 -i " +
                               Quote(list) + L" -c copy " + Quote(out);
            res->ok = win::RunHidden(cmd, &out2, 600000) == 0 && FileSize(out) > 0;
            if (res->ok) {
                for (auto& s : good) DeleteFileW(s.c_str());
            } else {
                res->error = L"Joining the paused parts failed; the parts were kept next to the output.\n" + Widen(out2);
            }
            DeleteFileW(list.c_str());
        }
        PostMessageW(notify, WM_APP_RECORD_DONE, 0, (LPARAM)res);
    });
}

double Recorder::Elapsed() const {
    if (!active_) return accumulated_;
    if (paused_) return accumulated_;
    LARGE_INTEGER now, freq;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&freq);
    return accumulated_ + (now.QuadPart - segStart_.QuadPart) / (double)freq.QuadPart;
}

uint64_t Recorder::Bytes() const {
    std::lock_guard lk(mu_);
    uint64_t total = 0;
    for (auto& s : segments_) total += FileSize(s);
    return total;
}

std::wstring Recorder::ErrorTail() const {
    std::lock_guard lk(mu_);
    return Widen(errTail_);
}
