#pragma once
#include <imgui.h>

// Palette design tokens. Dark default, mirrors Easel's 2026-09-27 monochrome
// tokens (UITokens in easel/src/ui/UIManager.h) so a Palette sheet and an Easel
// panel read as one family. Blue is the single chromatic exception, reserved
// for "driven" (bound) state, exactly as Easel's SOUND popover uses it.
namespace palette::theme {

constexpr ImU32 kPageBg        = IM_COL32(10, 11, 13, 255);
constexpr ImU32 kSheetBg       = IM_COL32(20, 20, 23, 250);
constexpr ImU32 kSurfaceRaised = IM_COL32(31, 31, 36, 255);
constexpr ImU32 kSurfaceHover  = IM_COL32(44, 44, 50, 255);
constexpr ImU32 kHairline      = IM_COL32(255, 255, 255, 26);
constexpr ImU32 kTextPrimary   = IM_COL32(242, 243, 245, 255);
constexpr ImU32 kTextSecondary = IM_COL32(185, 185, 192, 255);
constexpr ImU32 kTextTertiary  = IM_COL32(120, 122, 130, 255);
constexpr ImU32 kGrabber       = IM_COL32(58, 58, 64, 255);
constexpr ImU32 kHudBg         = IM_COL32(0, 0, 0, 110);
constexpr ImU32 kAccentBlue    = IM_COL32(79, 140, 255, 255);
constexpr ImU32 kOk            = IM_COL32(57, 196, 123, 255);
constexpr ImU32 kBad           = IM_COL32(240, 80, 80, 255);

constexpr ImGuiWindowFlags kBarFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
    ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoBackground;
constexpr ImGuiWindowFlags kSheetFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
    ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar;

// Apply style + load fonts (system SF / Segoe / DejaVu, mono companion).
void apply(float dpiScale);
ImFont* uiFont();
ImFont* monoFont();

} // namespace palette::theme
