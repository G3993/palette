#include "ui/Widgets.h"
#include "ui/Glass.h"
#include "ui/Theme.h"
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace palette::ui {
using namespace tok;

static ImFont* uiFontOr() { return theme::uiFont(); }

SliderRow sliderRow(const char* id, const char* label, float* v, float lo, float hi, const SliderStyle& st) {
    SliderRow r;
    ImGui::PushID(id);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float w = ImGui::GetContentRegionAvail().x;
    ImVec2 p = ImGui::GetCursorScreenPos();

    // Label line (15pt) with optional "driven by" and the value at right (tabular).
    ImGui::PushFont(uiFontOr(), 15.0f);
    ImVec2 ls = ImGui::CalcTextSize(label);
    dl->AddText(ImVec2(p.x, p.y), kWhite, label);
    if (st.drivenBy) {
        ImGui::PushFont(uiFontOr(), 12.0f);
        dl->AddText(ImVec2(p.x + ls.x + 8, p.y + 2.5f), kBlue, st.drivenBy);
        ImGui::PopFont();
    }
    char vb[32];
    if (st.valueText) std::snprintf(vb, sizeof vb, "%s", st.valueText);
    else std::snprintf(vb, sizeof vb, st.fmt, *v);
    ImVec2 vs = ImGui::CalcTextSize(vb);
    dl->AddText(ImVec2(p.x + w - vs.x, p.y), kW60, vb);
    ImGui::PopFont();
    // invisible buttons for label / value clicks
    ImGui::SetCursorScreenPos(p);
    if (ImGui::InvisibleButton("##lab", ImVec2(std::max(ls.x + 8, 40.0f), 22))) r.labelClicked = true;
    if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    ImGui::SetCursorScreenPos(ImVec2(p.x + w - std::max(vs.x, 40.0f), p.y));
    if (ImGui::InvisibleButton("##val", ImVec2(std::max(vs.x, 40.0f), 22))) r.valueClicked = true;
    if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);

    // Track row (28pt tall, 44pt hit).
    ImVec2 tp(p.x, p.y + 30);
    ImGui::SetCursorScreenPos(ImVec2(tp.x, tp.y - 8));
    ImGui::InvisibleButton("##track", ImVec2(w, kHit));
    bool hovered = ImGui::IsItemHovered(), held = ImGui::IsItemActive();
    r.active = held;
    float span = (hi - lo) == 0 ? 1.0f : (hi - lo);
    float t = std::clamp((*v - lo) / span, 0.0f, 1.0f);
    if (held) {
        float mx = ImGui::GetIO().MousePos.x;
        float fine = ImGui::GetIO().KeyShift ? 0.25f : 1.0f;
        float nt;
        if (fine < 1.0f) {
            // fine mode: move relative to drag delta
            nt = std::clamp(t + (ImGui::GetIO().MouseDelta.x / w) * fine, 0.0f, 1.0f);
        } else {
            nt = std::clamp((mx - tp.x) / w, 0.0f, 1.0f);
        }
        if (st.centerZero && std::abs(nt - 0.5f) < 0.012f) nt = 0.5f; // detent
        float nv = lo + nt * span;
        if (nv != *v) { *v = nv; r.changed = true; t = nt; }
    }
    if (hovered && ImGui::IsMouseDoubleClicked(0)) r.reset = true;

    float cy = tp.y + 14;
    dl->AddRectFilled(ImVec2(tp.x, cy - kTrack / 2), ImVec2(tp.x + w, cy + kTrack / 2), kW14, kTrack / 2);
    if (st.reach0 >= 0 && st.reach1 >= 0)
        dl->AddRectFilled(ImVec2(tp.x + w * st.reach0, cy - 4.5f), ImVec2(tp.x + w * st.reach1, cy + 4.5f), kBlue28, 4.5f);
    ImU32 fill = st.driven ? kBlue : kWhite;
    if (st.centerZero) {
        dl->AddRectFilled(ImVec2(tp.x + w / 2 - 0.75f, cy - 5), ImVec2(tp.x + w / 2 + 0.75f, cy + 5), IM_COL32(255, 255, 255, 90));
        float a = std::min(0.5f, t), b = std::max(0.5f, t);
        dl->AddRectFilled(ImVec2(tp.x + w * a, cy - kTrack / 2), ImVec2(tp.x + w * b, cy + kTrack / 2), fill, kTrack / 2);
    } else {
        dl->AddRectFilled(ImVec2(tp.x, cy - kTrack / 2), ImVec2(tp.x + w * t, cy + kTrack / 2), fill, kTrack / 2);
    }
    ImVec2 kc(tp.x + w * t, cy);
    float kr = kKnob / 2 * (held ? 1.08f : 1.0f);
    dl->AddCircleFilled(ImVec2(kc.x, kc.y + 2), kr, IM_COL32(0, 0, 0, 90));
    dl->AddCircleFilled(kc, kr, kWhite);
    ImGui::SetCursorScreenPos(ImVec2(p.x, tp.y + 36));
    ImGui::Dummy(ImVec2(w, 1));
    ImGui::PopID();
    return r;
}

int segmented(const char* id, const std::vector<std::string>& items, int cur, float h, float width) {
    ImGui::PushID(id);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float w = width > 0 ? width : ImGui::GetContentRegionAvail().x;
    ImVec2 p = ImGui::GetCursorScreenPos();
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), kW08, 12.0f);
    int n = (int)items.size(); float iw = (w - 6) / std::max(1, n);
    int out = cur;
    ImGui::PushFont(uiFontOr(), 13.0f);
    for (int i = 0; i < n; ++i) {
        ImVec2 a(p.x + 3 + iw * i, p.y + 3), b(a.x + iw, p.y + h - 3);
        ImGui::SetCursorScreenPos(a);
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("##s", ImVec2(iw, h - 6))) out = i;
        ImGui::PopID();
        if (i == cur) {
            dl->AddRectFilled(ImVec2(a.x, a.y + 1), b, IM_COL32(0, 0, 0, 60), 9.0f);
            dl->AddRectFilled(a, b, kW14, 9.0f);
        }
        ImVec2 ts = ImGui::CalcTextSize(items[i].c_str());
        dl->AddText(ImVec2(a.x + (iw - ts.x) / 2, a.y + (h - 6 - ts.y) / 2), i == cur ? kWhite : kW60, items[i].c_str());
    }
    ImGui::PopFont();
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h));
    ImGui::Dummy(ImVec2(w, 1));
    ImGui::PopID();
    return out;
}

int chips(const char* id, const std::vector<std::string>& items, int cur) {
    ImGui::PushID(id);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float x = p.x, y = p.y; const float h = 36; const float maxX = p.x + ImGui::GetContentRegionAvail().x;
    int out = -1;
    ImGui::PushFont(uiFontOr(), 13.0f);
    for (int i = 0; i < (int)items.size(); ++i) {
        ImVec2 ts = ImGui::CalcTextSize(items[i].c_str());
        float w = ts.x + 28;
        if (x > p.x && x + w > maxX) { x = p.x; y += h + 8; }
        ImGui::SetCursorScreenPos(ImVec2(x, y));
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("##c", ImVec2(w, h))) out = i;
        bool hov = ImGui::IsItemHovered();
        ImGui::PopID();
        bool on = i == cur;
        dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), on ? kWhite : (hov ? kW14 : kW08), h / 2);
        dl->AddText(ImVec2(x + 14, y + (h - ts.y) / 2), on ? IM_COL32(0, 0, 0, 255) : kW60, items[i].c_str());
        x += w + 8;
    }
    ImGui::PopFont();
    ImGui::SetCursorScreenPos(ImVec2(p.x, y + h));
    ImGui::Dummy(ImVec2(1, 1));
    ImGui::PopID();
    return out;
}

bool toggleRow(const char* id, const char* label, bool* v, const char* sub) {
    ImGui::PushID(id);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float w = ImGui::GetContentRegionAvail().x;
    ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = sub ? 56 : 48;
    ImGui::InvisibleButton("##row", ImVec2(w, h));
    bool clicked = ImGui::IsItemClicked();
    if (clicked) *v = !*v;
    ImGui::PushFont(uiFontOr(), 15.0f);
    dl->AddText(ImVec2(p.x, p.y + (sub ? 10 : 14)), kWhite, label);
    ImGui::PopFont();
    if (sub) { ImGui::PushFont(uiFontOr(), 13.0f); dl->AddText(ImVec2(p.x, p.y + 31), kW40, sub); ImGui::PopFont(); }
    ImVec2 sw(p.x + w - 51, p.y + (h - 31) / 2);
    dl->AddRectFilled(sw, ImVec2(sw.x + 51, sw.y + 31), *v ? kWhite : kW14, 15.5f);
    float kx = *v ? sw.x + 51 - 15.5f : sw.x + 15.5f;
    dl->AddCircleFilled(ImVec2(kx, sw.y + 15.5f), 13.5f, *v ? IM_COL32(10, 10, 12, 255) : kWhite);
    ImGui::PopID();
    return clicked;
}

bool colorRow(const char* id, const char* label, float rgba[4]) {
    ImGui::PushID(id);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float w = ImGui::GetContentRegionAvail().x;
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::PushFont(uiFontOr(), 15.0f);
    dl->AddText(p, kWhite, label);
    ImGui::PopFont();
    ImVec2 sp(p.x, p.y + 30);
    ImU32 c = ImGui::ColorConvertFloat4ToU32(ImVec4(rgba[0], rgba[1], rgba[2], 1.0f));
    dl->AddRectFilled(sp, ImVec2(sp.x + w, sp.y + 28), c, 9.0f);
    dl->AddRect(sp, ImVec2(sp.x + w, sp.y + 28), IM_COL32(255, 255, 255, 64), 9.0f);
    ImGui::SetCursorScreenPos(sp);
    bool changed = false;
    if (ImGui::InvisibleButton("##sw", ImVec2(w, 28))) ImGui::OpenPopup("##pick");
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 16.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14, 14));
    if (ImGui::BeginPopup("##pick")) {
        changed = ImGui::ColorPicker4("##p", rgba, ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_PickerHueWheel);
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(2);
    ImGui::SetCursorScreenPos(ImVec2(p.x, sp.y + 34));
    ImGui::Dummy(ImVec2(w, 1));
    ImGui::PopID();
    return changed;
}

bool bigButton(const char* id, const char* label, bool primary, float h, float w) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (w < 0) w = ImGui::GetContentRegionAvail().x;
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton(id, ImVec2(w, h));
    bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();
    ImU32 bg = primary ? (act ? IM_COL32(220, 220, 224, 255) : kWhite) : (act ? kW14 : (hov ? IM_COL32(255, 255, 255, 30) : kW08));
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), bg, 16.0f);
    ImGui::PushFont(uiFontOr(), 16.0f);
    ImVec2 ts = ImGui::CalcTextSize(label);
    dl->AddText(ImVec2(p.x + (w - ts.x) / 2, p.y + (h - ts.y) / 2), primary ? IM_COL32(0, 0, 0, 255) : kWhite, label);
    ImGui::PopFont();
    return clicked;
}

bool glassIconButton(const char* id, Icon ic, ImVec2 c, const Glass& glass, ImVec2 ds, bool filled, float r) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGui::SetCursorScreenPos(ImVec2(c.x - r, c.y - r));
    bool clicked = ImGui::InvisibleButton(id, ImVec2(r * 2, r * 2));
    bool hov = ImGui::IsItemHovered();
    if (filled) dl->AddCircleFilled(c, r, IM_COL32(245, 245, 247, 235));
    else glass.circle(dl, c, r, ds, hov ? IM_COL32(40, 40, 44, 170) : kTint);
    icon(dl, ic, c, 20.0f, filled ? IM_COL32(0, 0, 0, 255) : kWhite);
    return clicked;
}

void caption(const char* text, ImU32 col) {
    ImGui::PushFont(uiFontOr(), 13.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, col);
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

void sectionLabel(const char* text) {
    ImGui::Dummy(ImVec2(0, 6));
    ImGui::PushFont(uiFontOr(), 12.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, kW40);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, 2));
}

bool sheetHeader(const char* title, const char* subtitle, bool showDone, const char* doneLabel) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float w = ImGui::GetContentRegionAvail().x;
    ImVec2 p = ImGui::GetCursorScreenPos();
    dl->AddRectFilled(ImVec2(p.x + w / 2 - 18, p.y + 8), ImVec2(p.x + w / 2 + 18, p.y + 13), IM_COL32(255, 255, 255, 72), 2.5f);
    ImGui::PushFont(uiFontOr(), 22.0f);
    dl->AddText(ImVec2(p.x, p.y + 30), kWhite, title);
    ImGui::PopFont();
    if (subtitle) { ImGui::PushFont(uiFontOr(), 13.0f); dl->AddText(ImVec2(p.x, p.y + 60), IM_COL32(255, 255, 255, 128), subtitle); ImGui::PopFont(); }
    bool done = false;
    if (showDone) {
        ImGui::PushFont(uiFontOr(), 15.0f);
        ImVec2 ts = ImGui::CalcTextSize(doneLabel);
        ImGui::SetCursorScreenPos(ImVec2(p.x + w - ts.x - 16, p.y + 24));
        done = ImGui::InvisibleButton("##done", ImVec2(ts.x + 16, 36));
        dl->AddText(ImVec2(p.x + w - ts.x, p.y + 33), kWhite, doneLabel);
        ImGui::PopFont();
    }
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + (subtitle ? 92 : 76)));
    ImGui::Dummy(ImVec2(w, 1));
    return done;
}

bool menuRow(const char* id, Icon ic, const char* label, const char* trailing) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float w = ImGui::GetContentRegionAvail().x;
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton(id, ImVec2(w, 48));
    bool hov = ImGui::IsItemHovered();
    if (hov) dl->AddRectFilled(p, ImVec2(p.x + w, p.y + 48), kW08, 14.0f);
    icon(dl, ic, ImVec2(p.x + 22, p.y + 24), 20.0f, IM_COL32(255, 255, 255, 180));
    ImGui::PushFont(uiFontOr(), 15.0f);
    dl->AddText(ImVec2(p.x + 44, p.y + 14), kWhite, label);
    ImGui::PopFont();
    if (trailing) {
        ImGui::PushFont(uiFontOr(), 12.0f);
        ImVec2 ts = ImGui::CalcTextSize(trailing);
        dl->AddText(ImVec2(p.x + w - ts.x - 12, p.y + 17), kW40, trailing);
        ImGui::PopFont();
    }
    return clicked;
}

void hairline(float inset) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float w = ImGui::GetContentRegionAvail().x;
    ImVec2 p = ImGui::GetCursorScreenPos();
    dl->AddLine(ImVec2(p.x + inset, p.y), ImVec2(p.x + w - inset, p.y), IM_COL32(255, 255, 255, 20), 1.0f);
    ImGui::Dummy(ImVec2(w, 1));
}

} // namespace palette::ui
