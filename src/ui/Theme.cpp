#include "ui/Theme.h"
#include <cstdio>
#include <string>
#include <vector>

namespace palette::theme {

static ImFont* sUi = nullptr;
static ImFont* sMono = nullptr;

static ImFont* loadFirst(const std::vector<const char*>& paths, float px) {
    ImGuiIO& io = ImGui::GetIO();
    for (auto* p : paths) {
        if (FILE* f = std::fopen(p, "rb")) {
            std::fclose(f);
            if (ImFont* font = io.Fonts->AddFontFromFileTTF(p, px)) return font;
        }
    }
    return nullptr;
}

void apply(float dpi) {
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard; // pointer and touch first; no nav focus stealing on new sheets

    // Easel rasterises at 2x and scales back; same here so text is crisp on Retina.
    const float ui = 15.0f, mono = 13.0f; (void)dpi; // ImGui 1.92 rasterises per framebuffer scale
    sUi = loadFirst({
        "/System/Library/Fonts/SFNS.ttf", "/System/Library/Fonts/Helvetica.ttc",
        "C:/Windows/Fonts/SegUIVar.ttf", "C:/Windows/Fonts/segoeui.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    }, ui);
    sMono = loadFirst({
        "/System/Library/Fonts/SFNSMono.ttf", "/System/Library/Fonts/Menlo.ttc",
        "C:/Windows/Fonts/CascadiaMono.ttf", "C:/Windows/Fonts/consola.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
    }, mono);
    if (!sUi) sUi = io.Fonts->AddFontDefault();
    if (!sMono) sMono = sUi;

    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowPadding = ImVec2(16, 12);
    s.FramePadding = ImVec2(10, 7);
    s.ItemSpacing = ImVec2(8, 6);
    s.ItemInnerSpacing = ImVec2(8, 4);
    s.IndentSpacing = 16;
    s.ScrollbarSize = 8;
    s.GrabMinSize = 18;
    s.WindowRounding = 16;
    s.ChildRounding = 10;
    s.FrameRounding = 8;
    s.PopupRounding = 12;
    s.GrabRounding = 9;
    s.ScrollbarRounding = 6;
    s.WindowBorderSize = 0; s.ChildBorderSize = 0; s.FrameBorderSize = 0; s.PopupBorderSize = 1;
    s.CircleTessellationMaxError = 0.10f;

    ImVec4* c = s.Colors;
    auto col = [](ImU32 u) { return ImGui::ColorConvertU32ToFloat4(u); };
    c[ImGuiCol_WindowBg] = col(kPageBg);
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = col(kSheetBg);
    c[ImGuiCol_Border] = col(kHairline);
    c[ImGuiCol_FrameBg] = ImVec4(1, 1, 1, 0.06f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(1, 1, 1, 0.10f);
    c[ImGuiCol_FrameBgActive] = ImVec4(1, 1, 1, 0.14f);
    c[ImGuiCol_Text] = col(kTextPrimary);
    c[ImGuiCol_TextDisabled] = col(kTextTertiary);
    c[ImGuiCol_Button] = ImVec4(1, 1, 1, 0.06f);
    c[ImGuiCol_ButtonHovered] = ImVec4(1, 1, 1, 0.11f);
    c[ImGuiCol_ButtonActive] = ImVec4(1, 1, 1, 0.16f);
    c[ImGuiCol_SliderGrab] = ImVec4(1, 1, 1, 0.92f);
    c[ImGuiCol_SliderGrabActive] = ImVec4(1, 1, 1, 1.0f);
    c[ImGuiCol_CheckMark] = ImVec4(1, 1, 1, 0.95f);
    c[ImGuiCol_Header] = ImVec4(1, 1, 1, 0.08f);
    c[ImGuiCol_HeaderHovered] = ImVec4(1, 1, 1, 0.12f);
    c[ImGuiCol_HeaderActive] = ImVec4(1, 1, 1, 0.16f);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = ImVec4(1, 1, 1, 0.15f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(1, 1, 1, 0.25f);
    c[ImGuiCol_ScrollbarGrabActive] = ImVec4(1, 1, 1, 0.35f);
    c[ImGuiCol_Separator] = col(kHairline);
    c[ImGuiCol_NavCursor] = ImVec4(1, 1, 1, 0.35f);
}

ImFont* uiFont() { return sUi; }
ImFont* monoFont() { return sMono; }

} // namespace palette::theme
