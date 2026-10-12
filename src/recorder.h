#pragma once
#include "audio.h"
#include "common.h"

struct RecordConfig {
    enum Kind { Screen, Webcam, AudioOnly } kind = Screen;
    RECT region{};              // screen px on the primary monitor; empty = whole monitor
    int fps = 60;
    int maxRes = 1080;          // 0 = original
    bool cursor = true;
    std::wstring encoder;       // resolved ffmpeg encoder name
    int quality = 80;
    int audioKbps = 192;
    bool speaker = true, mic = false;
    std::wstring speakerId, micId;
    float speakerGain = 1.f, micGain = 1.f;
    std::wstring webcamName;
    std::wstring outPath;       // final output file
};

struct RecordResult {
    bool ok = false;
    std::wstring path;
    std::wstring error;
};

namespace ff {
const std::wstring& Ffmpeg();   // empty when not found
bool Available();

void DetectEncodersAsync(HWND notify);   // posts WM_APP_ENCODERS_READY
std::vector<std::wstring> Encoders();    // working encoders, best first
bool EncodersReady();
std::wstring ResolveEncoder(const std::wstring& preference);
std::wstring EncoderLabel(const std::wstring& name);

SIZE OutputSize(int srcW, int srcH, int maxRes);

struct Grab {
    std::wstring device;   // " -init_hw_device ... -filter_hw_device ..." for the monitor's GPU
    std::wstring source;   // ddagrab=... filter-graph source
    int width = 0, height = 0;
};
Grab GrabFor(RECT region, bool cursor, int fps);

// Background jobs; each posts its message to `notify` when finished.
void TrimAsync(const std::wstring& src, const std::wstring& dst, double start, double end, HWND notify);

struct MediaInfo {
    double duration = 0;
    int width = 0, height = 0;
};
bool Probe(const std::wstring& path, MediaInfo& info);
bool Thumbnail(const std::wstring& path, const std::wstring& thumbPath);
}  // namespace ff

class Recorder {
public:
    ~Recorder();
    bool Start(const RecordConfig& cfg, HWND notify, std::wstring& err);
    void Pause();
    bool Resume(std::wstring& err);
    void Stop();   // finishes asynchronously, posts WM_APP_RECORD_DONE with a heap RecordResult*
    bool Active() const { return active_; }
    bool Paused() const { return paused_; }
    double Elapsed() const;
    uint64_t Bytes() const;
    std::wstring ErrorTail() const;
    float Level(bool mic) const {
        const auto& p = mic ? mic_ : sys_;
        return p ? p->Level() : 0.f;
    }

private:
    bool StartSegment(std::wstring& err);
    void EndSegment();
    std::wstring BuildCommand(const std::wstring& segPath) const;

    RecordConfig cfg_;
    HWND notify_ = nullptr;
    std::vector<std::wstring> segments_;
    mutable std::mutex mu_;

    HANDLE process_ = nullptr, stdin_ = nullptr, stderr_ = nullptr;
    std::thread errReader_, watcher_, finisher_;
    std::string errTail_;
    std::unique_ptr<AudioPipe> sys_, mic_;

    bool active_ = false, paused_ = false;
    std::atomic<bool> ending_{false};
    double accumulated_ = 0;
    LARGE_INTEGER segStart_{};
};
