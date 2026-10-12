#include "output_window.h"
#include "app.h"
#include "recorder.h"
#include "winutil.h"
#include <mfmediaengine.h>

namespace {
constexpr float kW = 900, kH = 540, kTitle = 36, kRow = 66;

struct InfoMsg {
    int gen, index;
    double duration;
    int w, h;
    std::wstring thumb;
};

bool IsVideoExt(const std::wstring& e) { return e == L"mp4" || e == L"mkv"; }
bool IsAudioExt(const std::wstring& e) { return e == L"mp3" || e == L"m4a" || e == L"wav"; }
bool IsImageExt(const std::wstring& e) { return e == L"png" || e == L"jpg" || e == L"jpeg"; }

D2D1_RECT_F Fit(D2D1_RECT_F box, float w, float h) {
    float k = std::min(RW(box) / w, RH(box) / h);
    float fw = w * k, fh = h * k;
    auto c = Center(box);
    return RectXYWH(c.x - fw / 2, c.y - fh / 2, fw, fh);
}
}  // namespace

bool OutputWindow::Open(const std::wstring& select) {
    if (!hwnd_) {
        frameless_ = true;
        captionH_ = kTitle;
        float k = GetDpiForSystem() / 96.f;
        RECT mon = win::PrimaryMonitorRect();
        int w = (int)(kW * k), h = (int)(kH * k);
        int x = mon.left + (mon.right - mon.left - w) / 2, y = mon.top + (mon.bottom - mon.top - h) / 2;
        if (!Window::Create(L"ScreenRec Outputs", WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, WS_EX_APPWINDOW,
                            {x, y, x + w, y + h}, nullptr, false))
            return false;
    }
    win::ExcludeFromCapture(hwnd_, S().excludeSelf);
    Reload(select);
    Show(true);
    SetForegroundWindow(hwnd_);
    return true;
}

void OutputWindow::Reload(const std::wstring& select) {
    std::wstring keep = !select.empty() ? select : (Current() ? Current()->path : L"");
    player_.Close();
    items_.clear();
    // Recordings, plus screenshots when they're saved to a folder of their own.
    std::vector<std::wstring> dirs{S().outputDir};
    if (_wcsicmp(S().ScreenshotDir().c_str(), S().outputDir.c_str()) != 0) dirs.push_back(S().ScreenshotDir());
    for (const auto& dir : dirs) {
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileW(JoinPath(dir, L"*").c_str(), &fd);
    if (f != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            std::wstring name = fd.cFileName, ext = FileExt(name);
            if (!IsVideoExt(ext) && !IsAudioExt(ext) && !IsImageExt(ext)) continue;
            if (name.find(L".part") != std::wstring::npos) continue;   // in-progress segments
            Item it;
            it.path = JoinPath(dir, name);
            it.name = name;
            it.size = ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
            it.time = fd.ftLastWriteTime;
            it.isImage = IsImageExt(ext);
            it.isAudio = IsAudioExt(ext);
            if (it.isImage) it.thumb = it.path;
            items_.push_back(std::move(it));
        } while (FindNextFileW(f, &fd));
        FindClose(f);
    }
    }
    std::sort(items_.begin(), items_.end(),
              [](const Item& a, const Item& b) { return CompareFileTime(&a.time, &b.time) > 0; });
    sel_ = -1;
    scroll_ = 0;
    int idx = items_.empty() ? -1 : 0;
    for (int i = 0; i < (int)items_.size(); ++i)
        if (_wcsicmp(items_[i].path.c_str(), keep.c_str()) == 0) idx = i;
    ProbeAll();
    Select(idx);
}

void OutputWindow::ProbeAll() {
    int gen = ++probeGen_;
    std::vector<std::pair<std::wstring, std::wstring>> jobs;   // path, thumb target
    std::wstring dir = JoinPath(LocalDataDir(), L"thumbs");
    CreateDirectoryW(dir.c_str(), nullptr);
    for (auto& it : items_) {
        std::wstring key = it.path + std::to_wstring(it.size) + std::to_wstring(it.time.dwLowDateTime);
        std::wstring thumb = it.isAudio || it.isImage ? L"" : JoinPath(dir, Fmt(L"%016llx.jpg", (unsigned long long)std::hash<std::wstring>{}(key)));
        jobs.push_back({it.path, thumb});
    }
    HWND h = hwnd_;
    std::thread([this, h, gen, jobs] {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        for (int i = 0; i < (int)jobs.size() && probeGen_ == gen; ++i) {
            auto* m = new InfoMsg{gen, i, 0, 0, 0, L""};
            ff::MediaInfo info;
            if (ff::Probe(jobs[i].first, info)) {
                m->duration = info.duration;
                m->w = info.width;
                m->h = info.height;
            }
            if (!jobs[i].second.empty() && (FileExists(jobs[i].second) || ff::Thumbnail(jobs[i].first, jobs[i].second)))
                m->thumb = jobs[i].second;
            if (!PostMessageW(h, WM_APP_MEDIA_INFO, 0, (LPARAM)m)) delete m;
        }
        CoUninitialize();
    }).detach();
}

void OutputWindow::Select(int i) {
    trimming_ = false;
    player_.Close();
    image_.Reset();
    sel_ = i;
    if (Item* it = Current()) {
        if (!it->isImage) player_.Open(hwnd_, it->path);
        int visible = std::max(1, (int)(RH(ListRect()) / kRow));
        int first = (int)(scroll_ / kRow);
        if (i < first) scroll_ = i * kRow;
        else if (i >= first + visible) scroll_ = (i - visible + 1) * kRow;
    }
    if (!timer_ && hwnd_) {
        SetTimer(hwnd_, 1, 30, nullptr);
        timer_ = true;
    }
    Relayout();
}

D2D1_RECT_F OutputWindow::VideoRect() const { return RectF(14, kTitle + 6, SplitX() - 14, H() - 62); }
D2D1_RECT_F OutputWindow::ListRect() const { return RectF(SplitX() + 1, kTitle + 34, W(), H() - 140); }

void OutputWindow::DeleteSelected() {
    Item* it = Current();
    if (!it) return;
    if (!win::Confirm(hwnd_, L"Delete", L"Move \"" + it->name + L"\" to the Recycle Bin?")) return;
    std::wstring path = it->path;
    player_.Close();
    win::RecycleFile(path);
    int next = sel_;
    Reload(L"");
    if (!items_.empty()) Select(std::min(next, (int)items_.size() - 1));
}

void OutputWindow::RenameSelected() {
    Item* it = Current();
    if (!it) return;
    auto name = win::PromptText(hwnd_, L"Rename", it->name);
    if (!name || name->empty() || *name == it->name) return;
    std::wstring n = *name;
    if (FileExt(n) != FileExt(it->name)) n += L"." + FileExt(it->name);
    std::wstring dst = JoinPath(it->path.substr(0, it->path.find_last_of(L'\\')), n);
    std::wstring src = it->path;
    player_.Close();
    if (!MoveFileW(src.c_str(), dst.c_str())) {
        win::Error(hwnd_, L"Could not rename the file. A file with that name may already exist.");
        Reload(src);
        return;
    }
    Reload(dst);
}

void OutputWindow::StartTrim() {
    if (!Current() || Current()->isImage || player_.Duration() <= 0) return;
    trimming_ = true;
    trimA_ = 0;
    trimB_ = player_.Duration();
    player_.Pause();
    Relayout();
}

void OutputWindow::SaveTrim() {
    Item* it = Current();
    if (!it) return;
    std::wstring base = StripExt(it->path) + L"_trim", ext = L"." + FileExt(it->path), dst = base + ext;
    for (int n = 2; FileExists(dst); ++n) dst = base + std::to_wstring(n) + ext;
    ff::TrimAsync(it->path, dst, trimA_, trimB_, hwnd_);
    trimming_ = false;
    Relayout();
}

LRESULT OutputWindow::OnMessage(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CLOSE:
        player_.Close();
        if (timer_) KillTimer(hwnd_, 1);
        timer_ = false;
        Show(false);
        return 0;
    case WM_TIMER:
        if (player_.IsOpen()) {
            bool frame = player_.UpdateFrame();
            if (trimming_ && !player_.Paused() && player_.Position() >= trimB_) player_.Pause();
            if (frame || !player_.Paused()) Invalidate();
        }
        return 0;
    case WM_APP_PLAYER_EVENT:
        if (wp == MF_MEDIA_ENGINE_EVENT_LOADEDMETADATA || wp == MF_MEDIA_ENGINE_EVENT_ENDED ||
            wp == MF_MEDIA_ENGINE_EVENT_PAUSE || wp == MF_MEDIA_ENGINE_EVENT_PLAY || wp == MF_MEDIA_ENGINE_EVENT_SEEKED)
            Relayout();
        return 0;
    case WM_APP_MEDIA_INFO: {
        auto* m = reinterpret_cast<InfoMsg*>(lp);
        if (m->gen == probeGen_ && m->index < (int)items_.size()) {
            Item& it = items_[m->index];
            it.duration = m->duration;
            it.width = m->w;
            it.height = m->h;
            it.probed = true;
            if (!m->thumb.empty()) it.thumb = m->thumb;
            Invalidate();
        }
        delete m;
        return 0;
    }
    case WM_APP_TRIM_DONE: {
        auto* path = reinterpret_cast<std::wstring*>(lp);
        if (wp) Reload(*path);
        else win::Error(hwnd_, L"Trimming failed.");
        delete path;
        return 0;
    }
    case WM_MOUSEWHEEL: {
        POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(hwnd_, &pt);
        if (pt.x / Scale() > SplitX()) {
            float maxScroll = std::max(0.f, items_.size() * kRow - RH(ListRect()));
            float step = GET_WHEEL_DELTA_WPARAM(wp) > 0 ? -kRow : kRow;
            scroll_ = std::clamp(std::round((scroll_ + step) / kRow) * kRow, 0.f, std::ceil(maxScroll / kRow) * kRow);
            Relayout();
        }
        return 0;
    }
    case WM_KEYDOWN:
        if (wp == VK_SPACE && player_.IsOpen()) {
            if (player_.Paused()) player_.Play();
            else player_.Pause();
            Relayout();
        } else if (wp == VK_DELETE) {
            DeleteSelected();
        } else if (wp == VK_ESCAPE) {
            if (trimming_) {
                trimming_ = false;
                Relayout();
            } else {
                SendMessageW(hwnd_, WM_CLOSE, 0, 0);
            }
        } else if ((wp == VK_DOWN || wp == VK_UP) && !items_.empty()) {
            Select(std::clamp(sel_ + (wp == VK_DOWN ? 1 : -1), 0, (int)items_.size() - 1));
        }
        return 0;
    }
    return Window::OnMessage(msg, wp, lp);
}

void OutputWindow::Layout() {
    float w = W(), h = H(), split = SplitX();
    Add(RectXYWH(w - 38, 6, 28, 24), [](gfx::Painter& p, const Control& c) { ui::IconButton(p, c, Icon::Close, th::text2, 13); },
        [this] { SendMessageW(hwnd_, WM_CLOSE, 0, 0); });
    Add(RectXYWH(w - 70, 6, 28, 24), [](gfx::Painter& p, const Control& c) { ui::IconButton(p, c, Icon::Folder, th::text2, 15); },
        [] { app().OpenOutputFolder(); });

    // ---- List
    D2D1_RECT_F lr = ListRect();
    int first = (int)(scroll_ / kRow);
    int visible = (int)(RH(lr) / kRow);
    for (int i = first; i < (int)items_.size() && i < first + visible; ++i) {
        float y = lr.top + (i - first) * kRow;
        auto& c = Add(RectXYWH(split + 8, y + 2, w - split - 16, kRow - 4), [this, i](gfx::Painter& p, const Control& c) {
            Item& it = items_[i];
            bool sel = i == sel_;
            if (sel) p.FillRound(c.rc, 8, Rgb(th::blue));
            else if (c.hot) p.FillRound(c.rc, 8, Rgb(th::tile));
            D2D1_RECT_F tr = RectXYWH(c.rc.left + 6, c.rc.top + 6, 88, RH(c.rc) - 12);
            if (!it.thumb.empty() && (!it.bmp || it.bmpDc != p.dc)) {
                it.bmp = gfx::LoadBitmapFile(p.dc, it.thumb);
                it.bmpDc = p.dc;
            }
            p.FillRound(tr, 4, Rgb(0x000000));
            if (it.bmp) {
                auto s = it.bmp->GetSize();
                p.dc->DrawBitmap(it.bmp.Get(), Fit(tr, s.width, s.height), 1.f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            } else {
                DrawIcon(p, it.isAudio ? Icon::Audio : Icon::Camcorder, RectXYWH(Center(tr).x - 11, Center(tr).y - 11, 22, 22),
                         Rgb(th::text3));
            }
            float tx = tr.right + 10;
            p.Text(it.name, RectF(tx, c.rc.top + 9, c.rc.right - 6, c.rc.top + 31), 13, Rgb(th::text), DWRITE_FONT_WEIGHT_SEMI_BOLD);
            std::wstring info;
            if (it.isImage) info = FormatBytes(it.size) + (it.width ? Fmt(L"  |  %dx%d", it.width, it.height) : L"");
            else {
                info = (it.probed ? FormatDuration(it.duration, true) : std::wstring(L"--:--:--")) + L"  |  " + FormatBytes(it.size);
                if (it.width) info += Fmt(L"  |  %dx%d", it.width, it.height);
            }
            p.Text(info, RectF(tx, c.rc.top + 31, c.rc.right - 6, c.rc.bottom - 8), 11.5f, Rgb(sel ? 0xe5f0ff : th::text2));
        }, [this, i] { Select(i); });
        c.rclick = [this, i] {
            Select(i);
            POINT pt;
            GetCursorPos(&pt);
            int id = win::PopupMenu(hwnd_, pt, {{1, L"Open in default player"}, {2, L"Show in folder"}, {0}, {3, L"Trim video", false, !items_[i].isImage},
                                                {4, L"Rename"}, {5, L"Copy file"}, {7, L"Copy path"}, {0}, {6, L"Delete"}});
            if (!Current()) return;
            switch (id) {
            case 1: win::OpenWithShell(Current()->path); break;
            case 2: win::ShowInFolder(Current()->path); break;
            case 3: StartTrim(); break;
            case 4: RenameSelected(); break;
            case 5: win::CopyFileToClipboard(hwnd_, Current()->path); break;
            case 6: DeleteSelected(); break;
            case 7: win::CopyTextToClipboard(hwnd_, Current()->path); break;
            }
        };
    }

    // ---- File actions (right panel bottom)
    float bw = w - split - 24, bx = split + 12;
    struct Act {
        Icon icon;
        std::function<void()> fn;
    };
    Act acts[] = {{Icon::Folder, [this] { if (Current()) win::ShowInFolder(Current()->path); }},
                  {Icon::Trash, [this] { DeleteSelected(); }},
                  {Icon::Rename, [this] { RenameSelected(); }},
                  {Icon::Copy, [this] { if (Current()) win::CopyFileToClipboard(hwnd_, Current()->path); }}};
    float ax = split + (w - split - 4 * 40) / 2;
    for (auto& a : acts) {
        auto& c = Add(RectXYWH(ax, h - 130, 36, 34),
                      [icon = a.icon](gfx::Painter& p, const Control& c) { ui::IconButton(p, c, icon, th::text2, 18); }, a.fn);
        c.enabled = Current() != nullptr;
        ax += 40;
    }
    bool canTrim = Current() && !Current()->isImage;
    if (!trimming_ && Current() && Current()->isImage) {
        Add(RectXYWH(bx, h - 88, bw, 34),
            [](gfx::Painter& p, const Control& c) { ui::Button(p, c, L"Edit Image", th::blue, th::blueHover, Icon::Pen, true); },
            [this] { app().OpenCapture(Current()->path, false); });
        Add(RectXYWH(bx, h - 48, bw, 34),
            [](gfx::Painter& p, const Control& c) { ui::Button(p, c, L"Open in Viewer", th::green, th::greenHover, Icon::Play, true); },
            [this] { win::OpenWithShell(Current()->path); });
    } else if (!trimming_) {
        auto& t = Add(RectXYWH(bx, h - 88, bw, 34),
                      [](gfx::Painter& p, const Control& c) { ui::Button(p, c, L"Trim Video", th::blue, th::blueHover, Icon::Scissors, true); },
                      [this] { StartTrim(); });
        t.enabled = canTrim;
        auto& o = Add(RectXYWH(bx, h - 48, bw, 34),
                      [](gfx::Painter& p, const Control& c) { ui::Button(p, c, L"Open in Player", th::green, th::greenHover, Icon::Play, true); },
                      [this] { if (Current()) win::OpenWithShell(Current()->path); });
        o.enabled = Current() != nullptr;
    } else {
        Add(RectXYWH(bx, h - 88, bw, 34),
            [](gfx::Painter& p, const Control& c) { ui::Button(p, c, L"Save Trimmed Copy", th::blue, th::blueHover, Icon::Check, true); },
            [this] { SaveTrim(); });
        Add(RectXYWH(bx, h - 48, bw, 34), [](gfx::Painter& p, const Control& c) { ui::Button(p, c, L"Cancel", th::tile, th::tileHover); },
            [this] {
                trimming_ = false;
                Relayout();
            });
    }

    // ---- Player controls (left bottom)
    if (Current() && !Current()->isImage) {
        float y = h - 50;
        Add(RectXYWH(14, y, 34, 34), [this](gfx::Painter& p, const Control& c) {
            ui::IconButton(p, c, player_.Paused() ? Icon::Play : Icon::Pause, th::text, 16, true);
        }, [this] {
            if (player_.Paused()) {
                if (trimming_ && (player_.Position() < trimA_ || player_.Position() >= trimB_ - 0.05)) player_.Seek(trimA_);
                player_.Play();
            } else {
                player_.Pause();
            }
            Relayout();
        });
        float x0 = 14 + 34 + 64, x1 = split - 14 - 64;
        auto& s = Add(RectF(x0, y, x1, y + 34), [this](gfx::Painter& p, const Control& c) {
            double dur = player_.Duration();
            float t = dur > 0 ? (float)(player_.Position() / dur) : 0;
            if (!trimming_) {
                ui::Slider(p, c, t);
                return;
            }
            float yy = Center(c.rc).y, a = c.rc.left + 7, b = c.rc.right - 7;
            float xa = a + (b - a) * (float)(trimA_ / dur), xb = a + (b - a) * (float)(trimB_ / dur);
            p.FillRound(RectF(a, yy - 3, b, yy + 3), 3, Rgb(th::tilePress));
            p.Fill(RectF(xa, yy - 7, xb, yy + 7), Rgb(th::yellow, 0.35f));
            p.Line({a + (b - a) * t, yy - 10}, {a + (b - a) * t, yy + 10}, Rgb(0xffffff), 2);
            for (float hx : {xa, xb}) p.FillRound(RectF(hx - 4, yy - 12, hx + 4, yy + 12), 2, Rgb(th::yellow));
        });
        s.drag = [this, x0 = x0 + 7, x1 = x1 - 7](float x, float, int phase) {
            double dur = player_.Duration();
            if (dur <= 0) return;
            double t = std::clamp((x - x0) / (x1 - x0), 0.f, 1.f) * dur;
            if (trimming_) {
                if (phase == 0) trimDrag_ = std::abs(t - trimA_) <= std::abs(t - trimB_) ? 1 : 2;
                if (trimDrag_ == 1) trimA_ = std::min(t, trimB_ - 0.2);
                else trimB_ = std::max(t, trimA_ + 0.2);
                player_.Seek(trimDrag_ == 1 ? trimA_ : trimB_);
                if (phase == 2) trimDrag_ = 0;
            } else {
                player_.Seek(t);
            }
            Invalidate();
        };
        Add(RectF(14 + 34 + 6, y, x0 - 4, y + 34), [this](gfx::Painter& p, const Control& c) {
            p.Text(FormatDuration(trimming_ ? trimA_ : player_.Position()), c.rc, 12, Rgb(trimming_ ? th::yellow : th::text2),
                   DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_TRAILING);
        });
        Add(RectF(x1 + 4, y, split - 14, y + 34), [this](gfx::Painter& p, const Control& c) {
            p.Text(FormatDuration(trimming_ ? trimB_ : player_.Duration()), c.rc, 12, Rgb(trimming_ ? th::yellow : th::text2));
        });
    }
}

void OutputWindow::Paint(gfx::Painter& p) {
    float w = W(), h = H(), split = SplitX();
    p.Clear(Rgb(th::bg));
    p.Fill(RectF(split, kTitle, w, h), Rgb(0x262628));
    ui::Separator(p, split, kTitle, split, h);
    ui::Separator(p, 0, kTitle, w, kTitle);
    p.Text(Current() ? Current()->name : L"Outputs", RectF(80, 0, w - 80, kTitle), 13, Rgb(th::text), DWRITE_FONT_WEIGHT_SEMI_BOLD,
           DWRITE_TEXT_ALIGNMENT_CENTER);
    p.Text(Fmt(L"Outputs  (%d)", (int)items_.size()), RectF(split + 16, kTitle + 6, w - 12, kTitle + 30), 13, Rgb(th::text),
           DWRITE_FONT_WEIGHT_SEMI_BOLD);

    D2D1_RECT_F vr = VideoRect();
    p.FillRound(vr, 8, Rgb(0x000000));
    Item* it = Current();
    if (!it) {
        p.Text(L"No recordings yet — press REC to make one", vr, 14, Rgb(th::text2), DWRITE_FONT_WEIGHT_NORMAL,
               DWRITE_TEXT_ALIGNMENT_CENTER);
    } else if (it->isImage) {
        if (!image_ || imageDc_ != p.dc) {
            image_ = gfx::LoadBitmapFile(p.dc, it->path);
            imageDc_ = p.dc;
        }
        if (image_) {
            auto s = image_->GetSize();
            p.dc->DrawBitmap(image_.Get(), Fit(Inflate(vr, -4), s.width, s.height), 1.f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        }
    } else if (auto* frame = player_.Frame(p.dc)) {
        auto s = frame->GetSize();
        p.dc->DrawBitmap(frame, Fit(vr, s.width, s.height), 1.f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    } else {
        bool audio = it->isAudio || (player_.IsOpen() && !player_.HasVideo() && player_.Duration() > 0);
        DrawIcon(p, audio ? Icon::Audio : Icon::Play, RectXYWH(Center(vr).x - 22, Center(vr).y - 22, 44, 44), Rgb(th::text3));
    }
    if (trimming_)
        p.Text(L"Drag the yellow handles to choose the part to keep", RectF(vr.left, vr.top + 8, vr.right, vr.top + 30), 12.5f,
               Rgb(th::yellow), DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER);
    PaintControls(p);
}
