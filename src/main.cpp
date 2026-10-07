// Palette — light shader editor. Cut 1 vertical slice:
// SDL3 window → GL 3.3 core → lifted Easel ISF runtime → Edits-style rail + sheets.
#include <glad/glad.h>
#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_opengl3.h>
#include <nlohmann/json.hpp>

#include "sources/ShaderSource.h"
#include "core/Clock.h"
#include "ui/Theme.h"
#include "library/Library.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using json = nlohmann::json;

namespace {

// ---- Rail -------------------------------------------------------------------
// Fixed order so muscle memory forms (review §8). Desktop shows all twelve.
enum class Tool { None, Source, Shader, Params, Code, Sim, Scene3D, Light, Sound, Motion, Finish, Quality, Publish };
struct ToolDef { Tool id; const char* label; };
const ToolDef kTools[] = {
    {Tool::Source, "Source"}, {Tool::Shader, "Shader"}, {Tool::Params, "Params"}, {Tool::Code, "Code"},
    {Tool::Sim, "Sim"}, {Tool::Scene3D, "3D"}, {Tool::Light, "Light"}, {Tool::Sound, "Sound"},
    {Tool::Motion, "Motion"}, {Tool::Finish, "Finish"}, {Tool::Quality, "Quality"}, {Tool::Publish, "Publish"},
};

// ---- Quality tiers (review §6) ---------------------------------------------
struct Tier { const char* name; float scale; const char* note; };
const Tier kTiers[] = {
    {"Eco", 0.5f, "0.5× · 30 fps cap · feedback at half res"},
    {"Balanced", 0.5f, "0.5× · 16F · 30–60 adaptive"},
    {"High", 0.75f, "0.75× · 16F · 60 fps · 2 persistent passes"},
    {"Ultra", 1.0f, "1.0× · 32F allowed · 60–120 fps"},
};

struct App {
    SDL_Window* window = nullptr;
    SDL_GLContext gl = nullptr;
    float dpi = 1.0f;

    palette::Library library;
    std::shared_ptr<ShaderSource> shader;
    std::string shaderPath;
    std::string shaderTitle = "Untitled";
    std::string codeBuffer;      // live GLSL buffer for the Code sheet
    bool codeDirty = false;
    std::string compileStatus = "no shader";
    bool compileOk = true;
    double compileMs = 0.0;

    Tool activeTool = Tool::None;
    int tier = 3;                // Ultra on desktop
    bool paused = false;
    char search[128] = "";
    std::string pendingImagePath; // set by the SDL file dialog callback
    std::string sourceLabel = "None";

    // frame stats
    double frameMs = 0.0, fps = 0.0;
};

App* gApp = nullptr;

std::string homeDir() {
    const char* h = std::getenv("HOME");
#ifdef _WIN32
    if (!h) h = std::getenv("USERPROFILE");
#endif
    return h ? h : ".";
}

std::string readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss; ss << f.rdbuf(); return ss.str();
}

bool loadShader(App& app, const std::string& path, const std::string& title) {
    auto src = std::make_shared<ShaderSource>();
    double t0 = palette::clockSeconds();
    bool ok = src->loadFromFile(path);
    app.compileMs = (palette::clockSeconds() - t0) * 1000.0;
    if (!ok) {
        app.compileStatus = src->lastError().empty() ? "compile failed" : src->lastError();
        app.compileOk = false;
        return false;
    }
    app.shader = src;
    app.shaderPath = path;
    app.shaderTitle = title;
    app.codeBuffer = readFile(path);
    app.codeDirty = false;
    app.compileOk = true;
    app.compileStatus = "compiled";
    app.library.touch(path);
    return true;
}

void recompileFromBuffer(App& app) {
    if (!app.shader) return;
    double t0 = palette::clockSeconds();
    bool ok = app.shader->reload(app.codeBuffer);
    app.compileMs = (palette::clockSeconds() - t0) * 1000.0;
    app.compileOk = ok;
    app.compileStatus = ok ? "compiled" : app.shader->lastError();
    app.codeDirty = !ok;
}

void SDLCALL onImagePicked(void* userdata, const char* const* filelist, int) {
    auto* app = static_cast<App*>(userdata);
    if (filelist && filelist[0]) app->pendingImagePath = filelist[0];
}

void openImageDialog(App& app) {
    static const SDL_DialogFileFilter filters[] = {
        {"Images", "png;jpg;jpeg;bmp;tga;gif;psd"},
    };
    SDL_ShowOpenFileDialog(onImagePicked, &app, app.window, filters, 1, nullptr, false);
}

// ---- Widgets -----------------------------------------------------------------
// A parameter row: bind dot · label/value · slider. The dot is the bind point
// (Cut 2 wires it to the Bind sheet); for now it reports whether the param has
// an audio binding so published presets read back correctly.
bool paramRow(ShaderSource& sh, ISFInput& in) {
    using namespace palette::theme;
    ImGui::PushID(in.name.c_str());
    const char* label = in.label.empty() ? in.name.c_str() : in.label.c_str();
    bool changed = false;

    ImGui::BeginGroup();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float rowW = ImGui::GetContentRegionAvail().x;
    auto* dl = ImGui::GetWindowDrawList();
    bool bound = sh.audioBindings().count(in.name) > 0;
    dl->AddCircle(ImVec2(p.x + 9, p.y + 11), 5.0f, bound ? kAccentBlue : kTextTertiary, 0, 1.5f);
    if (bound) dl->AddCircleFilled(ImVec2(p.x + 9, p.y + 11), 5.0f, kAccentBlue);
    ImGui::SetCursorScreenPos(ImVec2(p.x + 24, p.y));
    ImGui::PushStyleColor(ImGuiCol_Text, kTextSecondary);
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImGui::SameLine(rowW - 64);
    ImGui::PushStyleColor(ImGuiCol_Text, kTextTertiary);
    { char vb[48] = ""; if (in.type == "float") std::snprintf(vb, sizeof vb, "%.3f", std::get<float>(in.value)); else if (in.type == "long") std::snprintf(vb, sizeof vb, "%d", (int)std::lround(std::get<float>(in.value))); else std::snprintf(vb, sizeof vb, "%s", in.type.c_str()); ImGui::TextUnformatted(vb); }
    ImGui::PopStyleColor();
    ImGui::SetCursorScreenPos(ImVec2(p.x + 24, p.y + 22));
    ImGui::SetNextItemWidth(rowW - 24);

    if (in.type == "float") {
        float v = std::get<float>(in.value);
        if (ImGui::SliderFloat("##v", &v, in.minVal, in.maxVal, "")) { sh.setFloat(in.name, v); changed = true; }
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) { sh.setFloat(in.name, in.defaultFloat); changed = true; }
    } else if (in.type == "long") {
        float fv = std::get<float>(in.value);
        int v = (int)std::lround(fv);
        if (!in.longLabels.empty()) {
            int idx = 0;
            for (size_t i = 0; i < in.longValues.size(); ++i) if (in.longValues[i] == v) idx = (int)i;
            std::vector<const char*> items; for (auto& s : in.longLabels) items.push_back(s.c_str());
            if (ImGui::Combo("##v", &idx, items.data(), (int)items.size())) { sh.setFloat(in.name, (float)in.longValues[idx]); changed = true; }
        } else {
            if (ImGui::SliderInt("##v", &v, (int)in.minVal, (int)in.maxVal, "")) { sh.setFloat(in.name, (float)v); changed = true; }
        }
    } else if (in.type == "bool") {
        bool v = std::get<bool>(in.value);
        if (ImGui::Checkbox("##v", &v)) { sh.setBool(in.name, v); changed = true; }
    } else if (in.type == "color") {
        glm::vec4 c = std::get<glm::vec4>(in.value);
        if (ImGui::ColorEdit4("##v", &c.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar)) { sh.setColor(in.name, c); changed = true; }
    } else if (in.type == "point2D") {
        glm::vec2 v = std::get<glm::vec2>(in.value);
        if (ImGui::SliderFloat2("##v", &v.x, std::min(in.minVec.x, in.minVec.y), std::max(in.maxVec.x, in.maxVec.y), "%.3f")) { sh.setPoint2D(in.name, v); changed = true; }
    } else if (in.type == "event") {
        if (ImGui::Button("Trigger", ImVec2(-1, 0))) { sh.setBool(in.name, true); changed = true; }
    } else if (in.type == "text") {
        std::string s = std::get<std::string>(in.value);
        char buf[256]; std::snprintf(buf, sizeof buf, "%s", s.c_str());
        if (ImGui::InputText("##v", buf, sizeof buf)) { sh.setText(in.name, buf); changed = true; }
    } else if (in.type == "image") {
        ImGui::PushStyleColor(ImGuiCol_Text, kTextTertiary);
        ImGui::TextUnformatted(in.name == "inputImage" ? gApp->sourceLabel.c_str() : "(bind in Source)");
        ImGui::PopStyleColor();
    }
    ImGui::EndGroup();
    ImGui::Dummy(ImVec2(0, 6));
    ImGui::PopID();
    return changed;
}

// ---- Sheets -----------------------------------------------------------------
void sheetHeader(App& app, const char* title, const char* subtitle) {
    using namespace palette::theme;
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x;
    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(p.x + w / 2 - 18, p.y + 2), ImVec2(p.x + w / 2 + 18, p.y + 6), kGrabber, 2.0f);
    ImGui::Dummy(ImVec2(0, 10));
    if (ImGui::Button("Close", ImVec2(56, 32))) app.activeTool = Tool::None;
    ImGui::SameLine();
    float tw = ImGui::CalcTextSize(title).x;
    ImGui::SetCursorPosX((w - tw) * 0.5f);
    ImGui::BeginGroup();
    ImGui::TextUnformatted(title);
    if (subtitle) { ImGui::PushStyleColor(ImGuiCol_Text, kTextTertiary); ImGui::TextUnformatted(subtitle); ImGui::PopStyleColor(); }
    ImGui::EndGroup();
    ImGui::SameLine(w - 56);
    if (ImGui::Button("Done", ImVec2(56, 32))) app.activeTool = Tool::None;
    ImGui::Dummy(ImVec2(0, 4));
}

void sheetParams(App& app) {
    using namespace palette::theme;
    if (!app.shader) { ImGui::TextUnformatted("Pick a shader first."); return; }
    auto& inputs = app.shader->inputs();
    char sub[64]; std::snprintf(sub, sizeof sub, "%zu parameters", inputs.size());
    sheetHeader(app, "Params", sub);
    ImGui::BeginChild("##params", ImVec2(0, 0), ImGuiChildFlags_None);
    std::string group;
    for (auto& in : inputs) {
        if (in.group != group) {
            group = in.group;
            ImGui::Dummy(ImVec2(0, 4));
            ImGui::PushStyleColor(ImGuiCol_Text, kTextTertiary);
            ImGui::TextUnformatted(group.empty() ? "GENERAL" : group.c_str());
            ImGui::PopStyleColor();
            ImGui::Dummy(ImVec2(0, 2));
        }
        paramRow(*app.shader, in);
    }
    ImGui::Dummy(ImVec2(0, 6));
    ImGui::PushStyleColor(ImGuiCol_Text, kTextTertiary);
    ImGui::TextUnformatted("+ Add parameter (Cut 2) · double-click a slider to reset");
    ImGui::PopStyleColor();
    ImGui::EndChild();
}

void sheetShader(App& app) {
    using namespace palette::theme;
    char sub[96]; std::snprintf(sub, sizeof sub, "%zu shaders · %s", app.library.entries().size(), app.library.root().c_str());
    sheetHeader(app, "Shader", sub);
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##search", "Search title, tags, file", app.search, sizeof app.search);
    ImGui::Dummy(ImVec2(0, 4));
    ImGui::BeginChild("##lib");
    std::string q = app.search; std::transform(q.begin(), q.end(), q.begin(), ::tolower);
    for (auto& e : app.library.entries()) {
        if (e.hidden || e.type == "scene") continue;
        if (!q.empty() && !e.matches(q)) continue;
        ImGui::PushID(e.path.c_str());
        bool current = e.path == app.shaderPath;
        if (current) ImGui::PushStyleColor(ImGuiCol_Button, kSurfaceHover);
        if (ImGui::Button(e.title.c_str(), ImVec2(-80, 32))) loadShader(app, e.path, e.title);
        if (current) ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, kTextTertiary);
        if (e.id >= 0) ImGui::Text("%d", e.id); else ImGui::TextUnformatted("—");
        ImGui::PopStyleColor();
        ImGui::PopID();
    }
    ImGui::EndChild();
}

void sheetSource(App& app) {
    using namespace palette::theme;
    sheetHeader(app, "Source", app.sourceLabel.c_str());
    if (ImGui::Button("Open image…", ImVec2(-1, 40))) openImageDialog(app);
    if (ImGui::Button("None", ImVec2(-1, 40))) {
        if (app.shader) app.shader->unbindImageInput("inputImage");
        app.sourceLabel = "None";
    }
    ImGui::Dummy(ImVec2(0, 8));
    ImGui::PushStyleColor(ImGuiCol_Text, kTextTertiary);
    ImGui::TextWrapped("Camera, video and previous-frame sources arrive in Cut 2. The image binds to the shader's inputImage.");
    ImGui::PopStyleColor();
}

void sheetCode(App& app) {
    using namespace palette::theme;
    if (!app.shader) { ImGui::TextUnformatted("Pick a shader first."); return; }
    char sub[128]; std::snprintf(sub, sizeof sub, "%s · ⌘⏎ compile · %s", app.compileOk ? "ok" : "error", app.shaderPath.c_str());
    sheetHeader(app, "Code", sub);
    if (!app.compileOk) {
        ImGui::PushStyleColor(ImGuiCol_Text, kBad);
        ImGui::TextWrapped("%s", app.compileStatus.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::PushFont(palette::theme::monoFont(), 13.0f);
    // Resizable std::string buffer for ImGui.
    static std::string* bufPtr = nullptr; bufPtr = &app.codeBuffer;
    auto cb = [](ImGuiInputTextCallbackData* d) -> int {
        if (d->EventFlag == ImGuiInputTextFlags_CallbackResize) { bufPtr->resize(d->BufTextLen); d->Buf = bufPtr->data(); }
        return 0;
    };
    if (ImGui::InputTextMultiline("##code", app.codeBuffer.data(), app.codeBuffer.capacity() + 1, ImVec2(-1, -1),
                                  ImGuiInputTextFlags_CallbackResize | ImGuiInputTextFlags_AllowTabInput, cb)) {
        app.codeDirty = true;
    }
    ImGui::PopFont();
    ImGuiIO& io = ImGui::GetIO();
    if (app.codeDirty && (io.KeySuper || io.KeyCtrl) && ImGui::IsKeyPressed(ImGuiKey_Enter)) recompileFromBuffer(app);
}

void sheetQuality(App& app) {
    using namespace palette::theme;
    sheetHeader(app, "Quality", kTiers[app.tier].note);
    for (int i = 0; i < 4; ++i) {
        bool on = app.tier == i;
        if (on) ImGui::PushStyleColor(ImGuiCol_Button, kSurfaceHover);
        if (ImGui::Button(kTiers[i].name, ImVec2(-1, 40))) app.tier = i;
        if (on) ImGui::PopStyleColor();
    }
    ImGui::Dummy(ImVec2(0, 8));
    ImGui::PushStyleColor(ImGuiCol_Text, kTextTertiary);
    ImGui::TextWrapped("Render scale applies now. Buffer precision and thermal step-down arrive with the mobile build.");
    ImGui::PopStyleColor();
}

void sheetPlaceholder(App& app, const char* title, const char* what) {
    using namespace palette::theme;
    sheetHeader(app, title, "Cut 2");
    ImGui::PushStyleColor(ImGuiCol_Text, kTextSecondary);
    ImGui::TextWrapped("%s", what);
    ImGui::PopStyleColor();
}

// ---- Frame ------------------------------------------------------------------
void drawFrame(App& app) {
    using namespace palette::theme;
    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 ds = io.DisplaySize;
    const float topH = 44.0f, stripH = 44.0f, railH = 76.0f;
    const float canvasY0 = topH, canvasY1 = ds.y - stripH - railH;

    // Canvas region in points → pixels for the ISF render.
    int pw = (int)((ds.x) * app.dpi * kTiers[app.tier].scale);
    int ph = (int)((canvasY1 - canvasY0) * app.dpi * kTiers[app.tier].scale);
    if (app.shader && pw > 0 && ph > 0) {
        if (app.shader->width() != pw || app.shader->height() != ph) app.shader->setResolution(pw, ph);
        app.shader->setPaused(app.paused);
        // Mouse in canvas → ISF mouse uniforms.
        ImVec2 m = io.MousePos;
        bool inCanvas = m.y >= canvasY0 && m.y <= canvasY1 && !io.WantCaptureMouse;
        app.shader->setMouseState(m.x / ds.x, 1.0f - (m.y - canvasY0) / (canvasY1 - canvasY0), inCanvas && io.MouseDown[0]);
        app.shader->update();
    }

    auto* bg = ImGui::GetBackgroundDrawList();
    bg->AddRectFilled(ImVec2(0, 0), ds, kPageBg);
    if (app.shader && app.shader->textureId()) {
        bg->AddImage((ImTextureID)(intptr_t)app.shader->textureId(), ImVec2(0, canvasY0), ImVec2(ds.x, canvasY1), ImVec2(0, 1), ImVec2(1, 0));
    } else {
        const char* msg = "Tap Shader to pick one from the library";
        ImVec2 ts = ImGui::CalcTextSize(msg);
        bg->AddText(ImVec2((ds.x - ts.x) / 2, (canvasY0 + canvasY1) / 2), kTextTertiary, msg);
    }
    // HUD (review: budget always visible)
    {
        char hud[160];
        std::snprintf(hud, sizeof hud, "%.0f fps · %.1f ms · %s · %dx%d%s", app.fps, app.frameMs, kTiers[app.tier].name, pw, ph, app.paused ? " · paused" : "");
        ImVec2 ts = ImGui::CalcTextSize(hud);
        bg->AddRectFilled(ImVec2(12, canvasY0 + 10), ImVec2(12 + ts.x + 12, canvasY0 + 10 + ts.y + 6), kHudBg, 4.0f);
        bg->AddText(ImVec2(18, canvasY0 + 13), kTextSecondary, hud);
        char st[96]; std::snprintf(st, sizeof st, "● %s %.0f ms", app.compileOk ? "compiled" : "error", app.compileMs);
        ImVec2 ts2 = ImGui::CalcTextSize(st);
        bg->AddRectFilled(ImVec2(ds.x - 12 - ts2.x - 12, canvasY0 + 10), ImVec2(ds.x - 12, canvasY0 + 10 + ts2.y + 6), kHudBg, 4.0f);
        bg->AddText(ImVec2(ds.x - 12 - ts2.x - 6, canvasY0 + 13), app.compileOk ? kOk : kBad, st);
    }

    // Top bar
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(ds.x, topH));
    ImGui::Begin("##top", nullptr, kBarFlags);
    ImGui::SetCursorPosY(8);
    if (ImGui::Button(app.paused ? "Play" : "Pause", ImVec2(56, 28))) app.paused = !app.paused;
    ImGui::SameLine();
    float tw = ImGui::CalcTextSize(app.shaderTitle.c_str()).x;
    ImGui::SetCursorPosX((ds.x - tw) / 2);
    ImGui::TextUnformatted(app.shaderTitle.c_str());
    ImGui::SameLine(ds.x - 170);
    ImGui::PushStyleColor(ImGuiCol_Button, kTextPrimary);
    ImGui::PushStyleColor(ImGuiCol_Text, kPageBg);
    if (ImGui::Button("Publish", ImVec2(76, 28))) app.activeTool = Tool::Publish;
    ImGui::PopStyleColor(2);
    ImGui::SameLine();
    if (ImGui::Button("Export", ImVec2(76, 28))) {}
    ImGui::End();

    // Layer strip (source · shader · finish)
    ImGui::SetNextWindowPos(ImVec2(0, ds.y - stripH - railH));
    ImGui::SetNextWindowSize(ImVec2(ds.x, stripH));
    ImGui::Begin("##strip", nullptr, kBarFlags);
    ImGui::SetCursorPos(ImVec2(14, 6));
    if (ImGui::Button(app.sourceLabel.c_str(), ImVec2(0, 32))) app.activeTool = Tool::Source;
    ImGui::SameLine();
    if (ImGui::Button(app.shaderTitle.c_str(), ImVec2(0, 32))) app.activeTool = Tool::Shader;
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, kTextTertiary);
    if (ImGui::Button("Finish · 0", ImVec2(0, 32))) app.activeTool = Tool::Finish;
    ImGui::PopStyleColor();
    ImGui::End();

    // Rail
    ImGui::SetNextWindowPos(ImVec2(0, ds.y - railH));
    ImGui::SetNextWindowSize(ImVec2(ds.x, railH));
    ImGui::Begin("##rail", nullptr, kBarFlags);
    const float itemW = 76.0f, itemH = 62.0f;
    int n = (int)(sizeof(kTools) / sizeof(kTools[0]));
    float x = (ds.x - n * itemW) / 2;
    for (int i = 0; i < n; ++i) {
        ImGui::SetCursorPos(ImVec2(x + i * itemW, 6));
        bool on = app.activeTool == kTools[i].id;
        ImGui::PushID(i);
        ImGui::PushStyleColor(ImGuiCol_Button, on ? kSurfaceRaised : IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Text, on ? kTextPrimary : kTextSecondary);
        if (ImGui::Button("##t", ImVec2(itemW - 6, itemH))) app.activeTool = on ? Tool::None : kTools[i].id;
        ImVec2 bp = ImGui::GetItemRectMin();
        auto* dl = ImGui::GetWindowDrawList();
        ImU32 ic = on ? kTextPrimary : kTextSecondary;
        ImVec2 c(bp.x + (itemW - 6) / 2, bp.y + 22);
        dl->AddRect(ImVec2(c.x - 11, c.y - 11), ImVec2(c.x + 11, c.y + 11), ic, 6.0f, 1.5f);
        if (on) dl->AddRectFilled(ImVec2(c.x - 5, c.y - 5), ImVec2(c.x + 5, c.y + 5), ic, 2.0f);
        ImVec2 ls = ImGui::CalcTextSize(kTools[i].label);
        dl->AddText(ImVec2(c.x - ls.x / 2, bp.y + 40), ic, kTools[i].label);
        ImGui::PopStyleColor(2);
        ImGui::PopID();
        // 1–9 / 0 - = select tools
    }
    ImGui::End();

    // Sheet (desktop: floating dock anchored above the rail, right side)
    if (app.activeTool != Tool::None) {
        bool wide = app.activeTool == Tool::Code;
        float sw = wide ? std::min(720.0f, ds.x - 32) : 380.0f;
        float sh = std::min(ds.y * 0.62f, canvasY1 - canvasY0 - 24);
        ImGui::SetNextWindowPos(ImVec2(ds.x - sw - 16, canvasY1 - sh - 12));
        ImGui::SetNextWindowSize(ImVec2(sw, sh));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 16.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16, 8));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, kSheetBg);
        ImGui::Begin("##sheet", nullptr, kSheetFlags);
        switch (app.activeTool) {
            case Tool::Params: sheetParams(app); break;
            case Tool::Shader: sheetShader(app); break;
            case Tool::Source: sheetSource(app); break;
            case Tool::Code: sheetCode(app); break;
            case Tool::Quality: sheetQuality(app); break;
            case Tool::Sim: sheetPlaceholder(app, "Sim", "Spawn → Update → Stages → Render over named 16F state buffers. Fields as the one force primitive."); break;
            case Tool::Scene3D: sheetPlaceholder(app, "3D", "SDF object or scene starter, orbit camera on drag, spline texture, tier-driven raymarch steps."); break;
            case Tool::Light: sheetPlaceholder(app, "Light", "Up to 4 lights on mobile, 8 on desktop: type, colour, intensity, radius, cone, IES. Sky ambient, fog."); break;
            case Tool::Sound: sheetPlaceholder(app, "Sound", "Mic or system tap. Per band Level, Hits, Presence, Time. Beat-locked oscillators. Default smoothing 0.85."); break;
            case Tool::Motion: sheetPlaceholder(app, "Motion", "Speed, Drift, Spin, Breath, Sway plus LFO shapes and beat divisions bindable to any parameter."); break;
            case Tool::Finish: sheetPlaceholder(app, "Finish", "Exposure → bloom → ACES → twelve-slider grade → Blend If → grain → vignette. Reorderable, bakeable."); break;
            case Tool::Publish: sheetPlaceholder(app, "Publish", "Name, file, next free id, pack, tags, thumbnail, lint, manifest write, send to Easel."); break;
            default: break;
        }
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
    }

    // Keyboard: 1-9,0,-,= select rail tools; Space pauses; Esc closes sheet.
    if (!io.WantTextInput) {
        static const ImGuiKey keys[] = {ImGuiKey_1, ImGuiKey_2, ImGuiKey_3, ImGuiKey_4, ImGuiKey_5, ImGuiKey_6, ImGuiKey_7, ImGuiKey_8, ImGuiKey_9, ImGuiKey_0, ImGuiKey_Minus, ImGuiKey_Equal};
        for (int i = 0; i < 12; ++i) if (ImGui::IsKeyPressed(keys[i])) app.activeTool = app.activeTool == kTools[i].id ? Tool::None : kTools[i].id;
        if (ImGui::IsKeyPressed(ImGuiKey_Space)) app.paused = !app.paused;
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) app.activeTool = Tool::None;
    }
}

} // namespace

int main(int argc, char** argv) {
    App app; gApp = &app;
    if (!SDL_Init(SDL_INIT_VIDEO)) { std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    app.window = SDL_CreateWindow("Palette", 1280, 800, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!app.window) { std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return 1; }
    app.gl = SDL_GL_CreateContext(app.window);
    SDL_GL_MakeCurrent(app.window, app.gl);
    SDL_GL_SetSwapInterval(1);
    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) { std::fprintf(stderr, "glad failed\n"); return 1; }
    app.dpi = SDL_GetWindowPixelDensity(app.window);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    palette::theme::apply(app.dpi);
    ImGui_ImplSDL3_InitForOpenGL(app.window, app.gl);
    ImGui_ImplOpenGL3_Init("#version 150");

    // Library: ~/ShaderClaw3/shaders (same lookup order as Easel), templates as a fallback.
    app.library.connect({homeDir() + "/ShaderClaw3/shaders", homeDir() + "/Documents/ShaderClaw3/shaders", std::string(PALETTE_SOURCE_DIR) + "/templates"});
    if (argc > 1) loadShader(app, argv[1], argv[1]);
    else if (auto* e = app.library.find("chrome_ripple")) loadShader(app, e->path, e->title);
    else if (!app.library.entries().empty()) loadShader(app, app.library.entries()[0].path, app.library.entries()[0].title);

    if (const char* t = std::getenv("PALETTE_TOOL")) { int k = std::atoi(t); if (k >= 1 && k <= 12) app.activeTool = kTools[k - 1].id; }
    bool running = true;
    double last = palette::clockSeconds();
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            ImGui_ImplSDL3_ProcessEvent(&ev);
            if (ev.type == SDL_EVENT_QUIT) running = false;
            if (ev.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && ev.window.windowID == SDL_GetWindowID(app.window)) running = false;
            if (ev.type == SDL_EVENT_DROP_FILE && ev.drop.data) app.pendingImagePath = ev.drop.data;
        }
        if (!app.pendingImagePath.empty()) {
            std::string p = app.pendingImagePath; app.pendingImagePath.clear();
            if (p.size() > 3 && p.substr(p.size() - 3) == ".fs") loadShader(app, p, p.substr(p.find_last_of("/\\") + 1));
            else if (app.shader && app.shader->bindImageFileInput("inputImage", p)) app.sourceLabel = p.substr(p.find_last_of("/\\") + 1);
        }

        double now = palette::clockSeconds();
        app.frameMs = (now - last) * 1000.0; last = now;
        app.fps = app.fps * 0.9 + (app.frameMs > 0 ? 1000.0 / app.frameMs : 0) * 0.1;

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        drawFrame(app);
        ImGui::Render();

        int fbw, fbh; SDL_GetWindowSizeInPixels(app.window, &fbw, &fbh);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, fbw, fbh);
        glClearColor(0.04f, 0.04f, 0.05f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(app.window);
    }

    app.shader.reset();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DestroyContext(app.gl);
    SDL_DestroyWindow(app.window);
    SDL_Quit();
    return 0;
}
