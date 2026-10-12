#pragma once
#include "audio.h"
#include "settings.h"
#include "ui.h"

class SettingsWindow : public Window {
public:
    bool Open(int tab = -1);   // tabs: 0 General, 1 Video, 2 Image, 3 Overlay, 4 Hotkey

protected:
    LRESULT OnMessage(UINT msg, WPARAM wp, LPARAM lp) override;
    void Layout() override;
    void Paint(gfx::Painter& p) override;

private:
    void EndCapture();
    int tab_ = 0;
    std::vector<std::pair<D2D1_RECT_F, std::wstring>> groups_;
    std::vector<std::pair<D2D1_RECT_F, std::wstring>> labels_;
    Hotkey* capturing_ = nullptr;
    std::vector<AudioDevice> speakers_, mics_;
    std::vector<std::wstring> cams_;
};
