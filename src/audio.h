#pragma once
#include "common.h"

struct AudioDevice {
    std::wstring id, name;
};
std::vector<AudioDevice> ListAudioDevices(bool capture);   // capture=false -> speakers (render)

// Captures one WASAPI endpoint (speaker loopback or microphone) and streams it as raw
// interleaved float32 into a named pipe that ffmpeg opens as an input.
class AudioPipe {
public:
    ~AudioPipe() { Stop(); }
    bool Open(const std::wstring& deviceId, bool loopback, float gain, std::wstring& err);
    void Stop();
    const std::wstring& PipePath() const { return pipePath_; }
    int Rate() const { return rate_; }
    int Channels() const { return channels_; }
    float Level() const { return level_.load(); }   // recent peak, 0..1 (for meters)

private:
    void Run();
    std::wstring deviceId_, pipePath_;
    bool loopback_ = false;
    float gain_ = 1.f;
    int rate_ = 48000, channels_ = 2;
    HANDLE pipe_ = INVALID_HANDLE_VALUE;
    HANDLE ready_ = nullptr;
    std::wstring initError_;
    bool initOk_ = false;
    std::atomic<bool> stop_{false}, connected_{false};
    std::atomic<float> level_{0.f};
    std::thread thread_;
};
