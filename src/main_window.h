#pragma once
#include "ui.h"

// The compact Bandicam-for-Mac style launcher: mode tiles + quick toggles.
class MainWindow : public Window {
public:
    bool Create();

protected:
    LRESULT OnMessage(UINT msg, WPARAM wp, LPARAM lp) override;
    void Layout() override;
    void Paint(gfx::Painter& p) override;
};
