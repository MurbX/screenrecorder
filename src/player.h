#pragma once
#include "common.h"
#include <mfapi.h>
#include <mfmediaengine.h>

// Plays a media file with the Media Foundation Media Engine in frame-server mode:
// frames are copied into a GPU texture that the window draws with Direct2D.
class VideoPlayer {
public:
    ~VideoPlayer() { Close(); }
    bool Open(HWND notify, const std::wstring& path);   // posts WM_APP_PLAYER_EVENT(wParam = MF_MEDIA_ENGINE_EVENT)
    void Close();
    bool IsOpen() const { return engine_ != nullptr; }
    void Play();
    void Pause();
    bool Paused() const;
    bool Ended() const;
    double Duration() const;
    double Position() const;
    void Seek(double seconds);
    bool HasVideo() const;
    SIZE VideoSize() const;
    bool UpdateFrame();                                  // true when a new frame was copied
    ID2D1Bitmap1* Frame(ID2D1DeviceContext* dc);         // nullptr until the first frame

private:
    ComPtr<IMFMediaEngine> engine_;
    ComPtr<IMFDXGIDeviceManager> dxgiMgr_;
    ComPtr<IUnknown> notify_;
    ComPtr<ID3D11Texture2D> tex_;
    ComPtr<ID2D1Bitmap1> bmp_;
    ID2D1DeviceContext* bmpDc_ = nullptr;
    bool haveFrame_ = false;
};
