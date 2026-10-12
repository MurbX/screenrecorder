#pragma once
#include "player.h"
#include "ui.h"

class OutputWindow : public Window {
public:
    bool Open(const std::wstring& select);
    void Reload(const std::wstring& select);

protected:
    LRESULT OnMessage(UINT msg, WPARAM wp, LPARAM lp) override;
    void Layout() override;
    void Paint(gfx::Painter& p) override;

private:
    struct Item {
        std::wstring path, name;
        uint64_t size = 0;
        FILETIME time{};
        double duration = 0;
        int width = 0, height = 0;
        bool probed = false, isImage = false, isAudio = false;
        std::wstring thumb;
        ComPtr<ID2D1Bitmap> bmp;
        ID2D1DeviceContext* bmpDc = nullptr;
    };
    void Select(int i);
    void ProbeAll();
    void DeleteSelected();
    void RenameSelected();
    void StartTrim();
    void SaveTrim();
    Item* Current() { return sel_ >= 0 && sel_ < (int)items_.size() ? &items_[sel_] : nullptr; }
    D2D1_RECT_F VideoRect() const;
    D2D1_RECT_F ListRect() const;
    float SplitX() const { return W() - 300; }

    std::vector<Item> items_;
    int sel_ = -1;
    float scroll_ = 0;
    VideoPlayer player_;
    bool timer_ = false;
    bool trimming_ = false;
    double trimA_ = 0, trimB_ = 0;
    int trimDrag_ = 0;   // 0 none, 1 start handle, 2 end handle
    std::atomic<int> probeGen_{0};
    ComPtr<ID2D1Bitmap> image_;
    ID2D1DeviceContext* imageDc_ = nullptr;
};
