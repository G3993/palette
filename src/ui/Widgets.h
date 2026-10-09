#pragma once
#include <imgui.h>
#include <string>
#include <vector>
#include "ui/Icons.h"

namespace palette::ui {

class Glass;

// Critically damped spring for sheet open/close and value easing.
struct Spring {
    float v = 0, target = 0, vel = 0;
    void step(float dt, float omega = 18.0f) {
        float x = v - target;
        float a = -omega * omega * x - 2.0f * omega * vel;
        vel += a * dt; v += vel * dt;
        if (std::abs(v - target) < 0.0005f && std::abs(vel) < 0.001f) { v = target; vel = 0; }
    }
    bool settled() const { return v == target && vel == 0; }
};

// Design tokens (spec table of the design page).
namespace tok {
constexpr float kDockH = 68, kDockItemW = 66, kDockItemH = 56, kDockR = 34;
constexpr float kSheetR = 36, kPanelR = 24, kGutter = 20;
constexpr float kKnob = 28, kTrack = 3, kHit = 44;
constexpr float kBtn = 38;
constexpr ImU32 kWhite = IM_COL32(245, 245, 247, 255);
constexpr ImU32 kW60 = IM_COL32(245, 245, 247, 153);
constexpr ImU32 kW40 = IM_COL32(245, 245, 247, 102);
constexpr ImU32 kW30 = IM_COL32(245, 245, 247, 77);
constexpr ImU32 kW14 = IM_COL32(255, 255, 255, 36);
constexpr ImU32 kW08 = IM_COL32(255, 255, 255, 20);
constexpr ImU32 kBlue = IM_COL32(79, 140, 255, 255);
constexpr ImU32 kBlue28 = IM_COL32(79, 140, 255, 71);
constexpr ImU32 kGreen = IM_COL32(57, 196, 123, 255);
constexpr ImU32 kRed = IM_COL32(240, 80, 80, 255);
constexpr ImU32 kTint = IM_COL32(22, 22, 24, 158);
}

struct SliderRow {
    bool changed = false;     // value moved
    bool reset = false;       // double-click on knob
    bool labelClicked = false;// opens Live
    bool valueClicked = false;// opens typed entry
    bool active = false;      // being dragged
};
struct SliderStyle {
    bool driven = false;      // blue fill
    float reach0 = -1, reach1 = -1; // 0..1 of track: band showing a source's reach
    bool centerZero = false;
    const char* drivenBy = nullptr;  // e.g. "Sound · Bass"
    const char* valueText = nullptr; // override formatted value
    const char* fmt = "%.2f";
};

// Label row + 28pt knob slider on a 3pt track. 56pt tall on phone metrics.
SliderRow sliderRow(const char* id, const char* label, float* v, float lo, float hi, const SliderStyle& st = {});
// Segmented control, returns new index.
int segmented(const char* id, const std::vector<std::string>& items, int cur, float h = 38.0f, float width = -1.0f);
// Horizontal chips; returns index clicked or -1. `cur` highlighted.
int chips(const char* id, const std::vector<std::string>& items, int cur);
// iOS-style switch.
bool toggleRow(const char* id, const char* label, bool* v, const char* sub = nullptr);
// Colour swatch row; returns true when colour changed (opens popup picker).
bool colorRow(const char* id, const char* label, float rgba[4]);
// Big flat button, filled when primary.
bool bigButton(const char* id, const char* label, bool primary, float h = 50.0f, float w = -1.0f);
// Round glass button with an icon. Returns clicked.
bool glassIconButton(const char* id, Icon ic, ImVec2 center, const Glass& glass, ImVec2 ds, bool filled = false, float r = tok::kBtn / 2);
// Caption text in 60% / 40% white.
void caption(const char* text, ImU32 col = tok::kW60);
void sectionLabel(const char* text);
// Sheet header: title + subtitle left, Done right (returns true when Done pressed), grabber on top.
bool sheetHeader(const char* title, const char* subtitle, bool showDone = true, const char* doneLabel = "Done");
// Menu row for the More popover.
bool menuRow(const char* id, Icon ic, const char* label, const char* trailing = nullptr);
// Thin separator.
void hairline(float inset = 0.0f);

} // namespace palette::ui
