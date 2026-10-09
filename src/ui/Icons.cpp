#include "ui/Icons.h"
#include <cmath>
#include <initializer_list>

namespace palette::ui {

namespace {
struct Pen {
    ImDrawList* dl; ImVec2 c; float s; ImU32 col; float w;
    ImVec2 P(float x, float y) const { return ImVec2(c.x + (x - 12.0f) * s, c.y + (y - 12.0f) * s); }
    void line(float x0, float y0, float x1, float y1) const { dl->AddLine(P(x0, y0), P(x1, y1), col, w); }
    void poly(std::initializer_list<float> pts, bool closed = false) const {
        const float* p = pts.begin(); int n = (int)pts.size() / 2;
        dl->PathClear();
        for (int i = 0; i < n; ++i) dl->PathLineTo(P(p[i * 2], p[i * 2 + 1]));
        dl->PathStroke(col, w, closed ? ImDrawFlags_Closed : 0);
    }
    void circ(float x, float y, float r) const { dl->AddCircle(P(x, y), r * s, col, 0, w); }
    void dot(float x, float y, float r) const { dl->AddCircleFilled(P(x, y), r * s, col); }
    void rrect(float x0, float y0, float x1, float y1, float r) const { dl->AddRect(P(x0, y0), P(x1, y1), col, r * s, w); }
    void frect(float x0, float y0, float x1, float y1, float r) const { dl->AddRectFilled(P(x0, y0), P(x1, y1), col, r * s); }
};
}

void icon(ImDrawList* dl, Icon which, ImVec2 center, float size, ImU32 col, float stroke) {
    float s = size / 24.0f;
    Pen p{dl, center, s, col, stroke * s * 1.0f};
    switch (which) {
    case Icon::Source:
        p.rrect(3.5f, 5, 20.5f, 19, 3); p.circ(9, 10, 1.6f);
        p.poly({4, 17, 8.5f, 12.5f, 12, 16, 15, 13, 20, 18});
        break;
    case Icon::Shader:
        p.circ(12, 12, 8.5f); p.circ(12, 12, 3.2f);
        p.line(12, 3.5f, 12, 5.8f); p.line(12, 18.2f, 12, 20.5f); p.line(3.5f, 12, 5.8f, 12); p.line(18.2f, 12, 20.5f, 12);
        break;
    case Icon::Controls:
        p.line(4, 7, 20, 7); p.line(4, 12, 20, 12); p.line(4, 17, 20, 17);
        { ImU32 bg = IM_COL32(12, 12, 14, 255); Pen k = p; k.col = bg;
          k.dot(9, 7, 2.4f); k.dot(15, 12, 2.4f); k.dot(8, 17, 2.4f); }
        p.circ(9, 7, 2.2f); p.circ(15, 12, 2.2f); p.circ(8, 17, 2.2f);
        break;
    case Icon::Live:
        p.poly({3, 12, 5.5f, 12, 7.5f, 6, 10.5f, 18, 13.5f, 9, 15.5f, 15, 17, 12, 21, 12});
        break;
    case Icon::Publish:
        p.w = stroke * s * 1.15f;
        p.line(12, 15, 12, 5); p.poly({7.5f, 9.5f, 12, 5, 16.5f, 9.5f});
        p.poly({5, 15, 5, 17.5f, 6.5f, 19, 17.5f, 19, 19, 17.5f, 19, 15});
        break;
    case Icon::More:
        p.dot(6, 12, 1.7f); p.dot(12, 12, 1.7f); p.dot(18, 12, 1.7f);
        break;
    case Icon::Back:
        p.w = stroke * s * 1.15f; p.poly({14.5f, 6.5f, 9, 12, 14.5f, 17.5f});
        break;
    case Icon::Code:
        p.poly({8.5f, 7.5f, 4, 12, 8.5f, 16.5f}); p.poly({15.5f, 7.5f, 20, 12, 15.5f, 16.5f}); p.line(13.5f, 5, 10.5f, 19);
        break;
    case Icon::Quality:
        p.line(5, 17, 5, 13); p.line(10, 17, 10, 8); p.line(15, 17, 15, 11); p.line(20, 17, 20, 5);
        break;
    case Icon::Link:
        dl->PathClear();
        dl->PathArcTo(p.P(15.5f, 8.5f), 4.0f * s, 3.1416f * 0.75f, 3.1416f * 1.9f);
        dl->PathStroke(col, p.w);
        dl->PathClear();
        dl->PathArcTo(p.P(8.5f, 15.5f), 4.0f * s, -3.1416f * 0.25f, 3.1416f * 0.9f);
        dl->PathStroke(col, p.w);
        p.line(10, 14, 14, 10);
        break;
    case Icon::Export:
        p.line(12, 4, 12, 14); p.poly({8, 10, 12, 14, 16, 10});
        p.poly({5, 17, 5, 18.5f, 6.5f, 20, 17.5f, 20, 19, 18.5f, 19, 17});
        break;
    case Icon::Pause:
        p.frect(7, 5, 10.5f, 19, 1); p.frect(13.5f, 5, 17, 19, 1);
        break;
    case Icon::Play:
        dl->AddTriangleFilled(p.P(8, 5), p.P(19, 12), p.P(8, 19), col);
        break;
    case Icon::Search:
        p.circ(11, 11, 6.5f); p.line(16, 16, 20, 20);
        break;
    case Icon::Check:
        p.w = stroke * s * 1.3f; p.poly({5, 12.5f, 10, 17.5f, 19, 7});
        break;
    case Icon::Close:
        p.line(6.5f, 6.5f, 17.5f, 17.5f); p.line(17.5f, 6.5f, 6.5f, 17.5f);
        break;
    case Icon::Plus:
        p.line(12, 5, 12, 19); p.line(5, 12, 19, 12);
        break;
    case Icon::Reset:
        dl->PathClear();
        dl->PathArcTo(p.P(12, 12), 7.0f * s, -3.1416f * 0.45f, 3.1416f * 1.25f);
        dl->PathStroke(col, p.w);
        p.poly({16.5f, 3.5f, 16.5f, 8, 12, 8});
        break;
    }
}

} // namespace palette::ui
