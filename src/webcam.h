#pragma once
#include "ui.h"

std::vector<std::wstring> ListWebcams();

// Floating webcam window. Bubble = picture-in-picture overlay that is recorded with the
// screen; Preview = large preview shown before a webcam-only recording.
class WebcamWindow : public Window {
public:
    enum class Style { Bubble, Preview };
    ~WebcamWindow() override;
    bool Open(Style style);
    void Shut();
    bool IsActive() const { return Visible(); }
    Style GetStyle() const { return style_; }
    RECT ScreenRect() const;
    void ApplySettings();

protected:
    LRESULT OnMessage(UINT msg, WPARAM wp, LPARAM lp) override;
    void Paint(gfx::Painter& p) override;
    LRESULT HitTest(float, float) override { return HTCAPTION; }

private:
    void CaptureLoop(std::wstring name);
    void StopCapture();
    void FitWindow(bool keepCenter);

    Style style_ = Style::Bubble;
    std::thread thread_;
    std::atomic<bool> stop_{false}, pending_{false};
    std::mutex mu_;
    std::vector<uint8_t> frame_;
    int fw_ = 0, fh_ = 0;
    uint64_t frameSeq_ = 0, bmpSeq_ = 0;
    std::wstring status_ = L"Starting camera…";
    ComPtr<ID2D1Bitmap> bmp_;
    ID2D1DeviceContext* bmpDc_ = nullptr;
};
