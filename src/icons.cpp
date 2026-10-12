#include "icons.h"

namespace {

constexpr float kPi = 3.14159265f;

struct Pen {
    gfx::Painter& p;
    D2D1_COLOR_F c;
    float sw;

    ID2D1StrokeStyle* cap() { return gfx::RoundCapStyle(); }
    void L(float x1, float y1, float x2, float y2) { p.dc->DrawLine({x1, y1}, {x2, y2}, p.B(c), sw, cap()); }
    void R(float x, float y, float w, float h, float r) {
        p.dc->DrawRoundedRectangle({RectXYWH(x, y, w, h), r, r}, p.B(c), sw, cap());
    }
    void FR(float x, float y, float w, float h, float r) { p.dc->FillRoundedRectangle({RectXYWH(x, y, w, h), r, r}, p.B(c)); }
    void C(float x, float y, float r) { p.dc->DrawEllipse({{x, y}, r, r}, p.B(c), sw); }
    void E(float x, float y, float rx, float ry) { p.dc->DrawEllipse({{x, y}, rx, ry}, p.B(c), sw); }
    void FC(float x, float y, float r) { p.dc->FillEllipse({{x, y}, r, r}, p.B(c)); }

    // Polyline / polygon through the given points.
    void Poly(std::initializer_list<D2D1_POINT_2F> pts, bool closed, bool fill = false) {
        ComPtr<ID2D1PathGeometry> g;
        gfx::D2D()->CreatePathGeometry(&g);
        ComPtr<ID2D1GeometrySink> s;
        g->Open(&s);
        auto it = pts.begin();
        s->BeginFigure(*it, fill ? D2D1_FIGURE_BEGIN_FILLED : D2D1_FIGURE_BEGIN_HOLLOW);
        for (++it; it != pts.end(); ++it) s->AddLine(*it);
        s->EndFigure(closed ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
        s->Close();
        if (fill) p.dc->FillGeometry(g.Get(), p.B(c));
        else p.dc->DrawGeometry(g.Get(), p.B(c), sw, cap());
    }

    // Arc around (cx,cy); angles in degrees, 0 = right, clockwise (screen coordinates).
    void Arc(float cx, float cy, float r, float a0, float a1) {
        auto pt = [&](float a) { return D2D1::Point2F(cx + r * cosf(a * kPi / 180), cy + r * sinf(a * kPi / 180)); };
        ComPtr<ID2D1PathGeometry> g;
        gfx::D2D()->CreatePathGeometry(&g);
        ComPtr<ID2D1GeometrySink> s;
        g->Open(&s);
        s->BeginFigure(pt(a0), D2D1_FIGURE_BEGIN_HOLLOW);
        s->AddArc({pt(a1), {r, r}, 0, D2D1_SWEEP_DIRECTION_CLOCKWISE,
                   (a1 - a0) > 180 ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL});
        s->EndFigure(D2D1_FIGURE_END_OPEN);
        s->Close();
        p.dc->DrawGeometry(g.Get(), p.B(c), sw, cap());
    }
};

void Gear(Pen& d) {
    // Eight rounded teeth around a ring.
    const int teeth = 8;
    ComPtr<ID2D1PathGeometry> g;
    gfx::D2D()->CreatePathGeometry(&g);
    ComPtr<ID2D1GeometrySink> s;
    g->Open(&s);
    const float ro = 10.f, ri = 7.6f;
    for (int i = 0; i < teeth * 4; ++i) {
        float a = (i / (float)(teeth * 4)) * 2 * kPi - kPi / (teeth * 4);
        float r = ((i % 4) == 1 || (i % 4) == 2) ? ro : ri;
        D2D1_POINT_2F pt{12 + r * cosf(a), 12 + r * sinf(a)};
        if (i == 0) s->BeginFigure(pt, D2D1_FIGURE_BEGIN_HOLLOW);
        else s->AddLine(pt);
    }
    s->EndFigure(D2D1_FIGURE_END_CLOSED);
    s->Close();
    d.p.dc->DrawGeometry(g.Get(), d.p.B(d.c), d.sw, d.cap());
    d.C(12, 12, 3.2f);
}

void SpeakerBody(Pen& d) { d.Poly({{3, 9}, {7, 9}, {12, 4.5f}, {12, 19.5f}, {7, 15}, {3, 15}}, true, true); }

void MicBody(Pen& d) {
    d.R(9, 2.5f, 6, 12, 3);
    d.Arc(12, 11, 6.5f, 0, 180);
    d.L(12, 17.5f, 12, 21);
    d.L(8.5f, 21, 15.5f, 21);
}

}  // namespace

void DrawIcon(gfx::Painter& p, Icon icon, D2D1_RECT_F box, D2D1_COLOR_F color, float stroke) {
    float size = std::min(RW(box), RH(box));
    float k = size / 24.f;
    D2D1_MATRIX_3X2_F old;
    p.dc->GetTransform(&old);
    float ox = box.left + (RW(box) - size) / 2, oy = box.top + (RH(box) - size) / 2;
    p.dc->SetTransform(D2D1::Matrix3x2F::Scale(k, k) * D2D1::Matrix3x2F::Translation(ox, oy) * old);
    Pen d{p, color, stroke};

    switch (icon) {
    case Icon::SelectArea:
        d.Poly({{3, 8}, {3, 3}, {8, 3}}, false);
        d.Poly({{16, 3}, {21, 3}, {21, 8}}, false);
        d.Poly({{21, 16}, {21, 21}, {16, 21}}, false);
        d.Poly({{8, 21}, {3, 21}, {3, 16}}, false);
        break;
    case Icon::Monitor:
        d.R(2, 3.5f, 20, 13.5f, 2);
        d.L(12, 17, 12, 20.5f);
        d.L(7.5f, 20.5f, 16.5f, 20.5f);
        break;
    case Icon::Window:
        d.R(2, 4, 20, 16, 2.5f);
        d.L(2, 8.5f, 22, 8.5f);
        d.FC(5, 6.3f, 0.9f);
        d.FC(7.8f, 6.3f, 0.9f);
        d.FC(10.6f, 6.3f, 0.9f);
        break;
    case Icon::Webcam:
        d.C(12, 10, 7.5f);
        d.C(12, 10, 3);
        d.L(12, 17.5f, 12, 20.5f);
        d.L(7, 21, 17, 21);
        break;
    case Icon::Audio: {
        const float hs[] = {4, 9, 15, 8, 18, 11, 6};
        for (int i = 0; i < 7; ++i) d.L(3.f + i * 3.f, 12 - hs[i] / 2, 3.f + i * 3.f, 12 + hs[i] / 2);
        break;
    }
    case Icon::Cursor:
    case Icon::Pointer:
        d.Poly({{6, 3}, {6, 19.5f}, {10.2f, 15.6f}, {13, 21.5f}, {15.6f, 20.3f}, {12.8f, 14.5f}, {18.5f, 14.5f}}, true, true);
        break;
    case Icon::Camcorder:
        d.FR(2, 6.5f, 13.5f, 11, 2.5f);
        d.Poly({{16.5f, 10.5f}, {22, 7}, {22, 17}, {16.5f, 13.5f}}, true, true);
        break;
    case Icon::Speaker:
        SpeakerBody(d);
        d.Arc(12, 12, 4, -50, 50);
        d.Arc(12, 12, 8, -50, 50);
        break;
    case Icon::SpeakerOff:
        SpeakerBody(d);
        d.L(15.5f, 9, 21.5f, 15);
        d.L(21.5f, 9, 15.5f, 15);
        break;
    case Icon::Mic:
        MicBody(d);
        break;
    case Icon::MicOff:
        MicBody(d);
        d.L(4, 3, 20, 21);
        break;
    case Icon::Annotate:
        d.C(12, 12, 10);
        d.Poly({{12, 5.5f}, {16, 17}, {8, 17}}, true);
        d.L(12, 5.5f, 12, 12);
        break;
    case Icon::Gear:
        Gear(d);
        break;
    case Icon::Folder:
        d.Poly({{2.5f, 6}, {2.5f, 19}, {21.5f, 19}, {21.5f, 8}, {11, 8}, {9, 5}, {3.5f, 5}}, true);
        break;
    case Icon::List:
        d.L(3, 6, 14, 6);
        d.L(3, 11, 14, 11);
        d.L(3, 16, 10, 16);
        d.Poly({{15.5f, 13}, {21.5f, 16.5f}, {15.5f, 20}}, true, true);
        break;
    case Icon::Pause:
        d.FR(6, 4, 4, 16, 1);
        d.FR(14, 4, 4, 16, 1);
        break;
    case Icon::Stop:
        d.FR(5, 5, 14, 14, 2.5f);
        break;
    case Icon::Record:
        d.FC(12, 12, 8);
        break;
    case Icon::Dot:
        d.FC(12, 12, 4);
        break;
    case Icon::Close:
        d.L(6.5f, 6.5f, 17.5f, 17.5f);
        d.L(17.5f, 6.5f, 6.5f, 17.5f);
        break;
    case Icon::Minimize:
        d.L(6, 12, 18, 12);
        break;
    case Icon::Play:
        d.Poly({{7, 4}, {20, 12}, {7, 20}}, true, true);
        break;
    case Icon::Chevron:
        d.Poly({{7, 9.5f}, {12, 14.5f}, {17, 9.5f}}, false);
        break;
    case Icon::Pen:
        d.Poly({{16, 3.5f}, {20.5f, 8}, {9, 19.5f}, {3.5f, 20.5f}, {4.5f, 15}}, true);
        d.L(13.5f, 6, 18, 10.5f);
        break;
    case Icon::Highlighter:
        d.Poly({{15, 3}, {21, 9}, {12, 18}, {6, 12}}, true);
        d.Poly({{6, 12}, {3.5f, 18.5f}, {5.5f, 20.5f}, {12, 18}}, false);
        d.L(3, 21.5f, 9, 21.5f);
        break;
    case Icon::Line:
        d.L(4, 20, 20, 4);
        break;
    case Icon::Arrow:
        d.L(4, 20, 19, 5);
        d.Poly({{10, 5}, {19, 5}, {19, 14}}, false);
        break;
    case Icon::Rect:
        d.R(3, 5, 18, 14, 1.5f);
        break;
    case Icon::Ellipse:
        d.E(12, 12, 9.5f, 7.5f);
        break;
    case Icon::Text:
        d.L(5, 5, 19, 5);
        d.L(12, 5, 12, 20);
        d.L(9, 20, 15, 20);
        break;
    case Icon::Width:
        d.L(4, 6, 20, 6);
        d.sw = stroke * 1.5f;
        d.L(4, 12, 20, 12);
        d.sw = stroke * 2.2f;
        d.L(4, 18.5f, 20, 18.5f);
        break;
    case Icon::Undo:
        d.Poly({{8, 4}, {3.5f, 8.5f}, {8, 13}}, false);
        d.L(3.5f, 8.5f, 14, 8.5f);
        d.Arc(14, 14.25f, 5.75f, -90, 90);
        d.L(14, 20, 8, 20);
        break;
    case Icon::Trash:
        d.L(3, 6, 21, 6);
        d.Poly({{5, 6}, {6.3f, 21}, {17.7f, 21}, {19, 6}}, false);
        d.Poly({{9, 6}, {9, 3}, {15, 3}, {15, 6}}, false);
        d.L(10, 10, 10, 17);
        d.L(14, 10, 14, 17);
        break;
    case Icon::Scissors:
        d.C(6, 18, 3);
        d.C(18, 18, 3);
        d.L(8, 16, 18, 3);
        d.L(16, 16, 6, 3);
        break;
    case Icon::Copy:
        d.R(8, 8, 13, 13, 2);
        d.Poly({{16, 8}, {16, 3}, {3, 3}, {3, 16}, {8, 16}}, false);
        break;
    case Icon::Rename:
        d.Poly({{16, 3}, {21, 8}, {8, 21}, {3, 21}, {3, 16}}, true);
        d.L(13, 6, 18, 11);
        break;
    case Icon::Camera:
        d.R(2, 7, 20, 13.5f, 2.5f);
        d.Poly({{7.5f, 7}, {9.2f, 4}, {14.8f, 4}, {16.5f, 7}}, false);
        d.C(12, 13.5f, 3.8f);
        break;
    case Icon::Keyboard:
        d.R(2, 6, 20, 12.5f, 2);
        for (int i = 0; i < 5; ++i) d.FC(6.f + i * 3.f, 10.3f, 0.9f);
        d.L(7, 14.5f, 17, 14.5f);
        break;
    case Icon::Overlay:
        d.R(2, 4, 20, 16, 2.5f);
        d.C(12, 10, 3);
        d.Arc(12, 20, 5.5f, 180, 360);
        break;
    case Icon::Link: {   // two chain links at 45 degrees
        D2D1_MATRIX_3X2_F t;
        p.dc->GetTransform(&t);
        p.dc->SetTransform(D2D1::Matrix3x2F::Rotation(-45, {12, 12}) * t);
        d.R(1.5f, 8.5f, 11.5f, 7, 3.5f);
        d.R(11, 8.5f, 11.5f, 7, 3.5f);
        p.dc->SetTransform(t);
        break;
    }
    case Icon::Step:   // numbered marker
        d.C(12, 12, 9);
        d.Poly({{9.5f, 9}, {12.5f, 7}, {12.5f, 17}}, false);
        break;
    case Icon::Blur:   // droplet
        d.Poly({{12, 3}, {17.5f, 11}, {18.5f, 14.5f}, {17, 18}, {14, 20.5f}, {10, 20.5f}, {7, 18}, {5.5f, 14.5f}, {6.5f, 11}}, true);
        break;
    case Icon::Pixelate:   // 3x3 checker
        for (int gy = 0; gy < 3; ++gy)
            for (int gx = 0; gx < 3; ++gx)
                if ((gx + gy) % 2 == 0) d.FR(4.f + gx * 5.5f, 4.f + gy * 5.5f, 5, 5, 1);
                else d.R(4.5f + gx * 5.5f, 4.5f + gy * 5.5f, 4, 4, 0.8f);
        break;
    case Icon::Spotlight:   // circle with rays
        d.C(12, 12, 4.5f);
        for (int i = 0; i < 8; ++i) {
            float a = i * 3.14159265f / 4;
            d.L(12 + 7.5f * cosf(a), 12 + 7.5f * sinf(a), 12 + 10 * cosf(a), 12 + 10 * sinf(a));
        }
        break;
    case Icon::Magnify:
        d.C(10.5f, 10.5f, 6.5f);
        d.L(15.3f, 15.3f, 20.5f, 20.5f);
        d.L(8, 10.5f, 13, 10.5f);
        d.L(10.5f, 8, 10.5f, 13);
        break;
    case Icon::Callout:   // speech bubble
        d.Poly({{4, 4.5f}, {20, 4.5f}, {20, 15.5f}, {11, 15.5f}, {6.5f, 20}, {7, 15.5f}, {4, 15.5f}}, true);
        break;
    case Icon::Crop:
        d.Poly({{6, 2.5f}, {6, 18}, {21.5f, 18}}, false);
        d.Poly({{2.5f, 6}, {18, 6}, {18, 21.5f}}, false);
        break;
    case Icon::Redo:
        d.Poly({{16, 4}, {20.5f, 8.5f}, {16, 13}}, false);
        d.L(20.5f, 8.5f, 10, 8.5f);
        d.Arc(10, 14.25f, 5.75f, 90, 270);
        d.L(10, 20, 16, 20);
        break;
    case Icon::Save:   // download into tray
        d.L(12, 3.5f, 12, 14.5f);
        d.Poly({{7.5f, 10}, {12, 14.5f}, {16.5f, 10}}, false);
        d.Poly({{4, 15}, {4, 20}, {20, 20}, {20, 15}}, false);
        break;
    case Icon::Grip:   // 2x3 dots
        for (int gy = 0; gy < 3; ++gy)
            for (int gx = 0; gx < 2; ++gx) d.FC(9.f + gx * 6.f, 6.f + gy * 6.f, 1.6f);
        break;
    case Icon::Fill:   // half-filled square
        d.R(4, 4, 16, 16, 2.5f);
        d.Poly({{4, 20}, {20, 4}, {20, 20}}, true, true);
        break;
    case Icon::Check:        d.Poly({{5, 12.5f}, {10, 17.5f}, {19, 7}}, false);
        break;
    }
    p.dc->SetTransform(old);
}
