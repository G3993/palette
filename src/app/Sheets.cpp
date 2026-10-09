// All UI drawing for Palette: chrome, dock, sheets, More menu, toast.
#include "app/App.h"
#include "core/Clock.h"
#include "ui/Theme.h"
#include "ui/Icons.h"
#include "library/Publisher.h"
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>

namespace fs = std::filesystem;
using namespace palette::ui;
using namespace palette::ui::tok;
using palette::theme::kBarFlags;
using palette::theme::kSheetFlags;

namespace palette {

static App* sApp = nullptr;
static void SDLCALL onFilePicked(void* ud, const char* const* files, int) { auto* a = static_cast<App*>(ud); if (files && files[0]) a->pendingFile = files[0]; }
static void openFileDialog(App& app, bool images) {
    static const SDL_DialogFileFilter img[] = {{"Images", "png;jpg;jpeg;bmp;tga;gif;psd"}};
    static const SDL_DialogFileFilter fsf[] = {{"ISF shader", "fs"}};
    SDL_ShowOpenFileDialog(onFilePicked, &app, app.window, images ? img : fsf, 1, nullptr, false);
}

static const char* kTierName[4] = {"Eco", "Balanced", "High", "Ultra"};
static const char* kTierNote[4] = {"Half resolution · 30 fps cap · for long sessions on a phone", "Half resolution · adaptive 30–60", "Three-quarter resolution · 60 fps", "Full resolution · everything on"};
static const char* kPacks[] = {"Mine", "Toy Box", "Artsy", "3D", "Fluid", "Mono", "Music"};

static ImFont* F() { return theme::uiFont(); }

// ------------------------------------------------------------- chrome
void App::drawChrome() {
    sApp = this;
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ds);
    ImGui::Begin("##chrome", nullptr, kBarFlags | ImGuiWindowFlags_NoInputs);
    ImGui::End();
    // a separate window just for the two buttons so they take input
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(ds.x, 72));
    ImGui::Begin("##topbar", nullptr, kBarFlags);
    auto* dl = ImGui::GetWindowDrawList();
    const float y = phone ? 62 + kBtn / 2 : 16 + kBtn / 2;
    if (glassIconButton("##back", Icon::Back, ImVec2(16 + kBtn / 2, y), glass, ds)) { if (sheet != Sheet::None) closeSheet(); else openSheet(Sheet::Shader); }
    if (glassIconButton("##more", Icon::More, ImVec2(ds.x - 16 - kBtn / 2, y), glass, ds, moreOpen)) moreOpen = !moreOpen;
    // title
    ImGui::PushFont(F(), 15.0f);
    ImVec2 ts = ImGui::CalcTextSize(shaderTitle.c_str());
    dl->AddText(ImVec2((ds.x - ts.x) / 2 + 1, y - 17 + 1), IM_COL32(0, 0, 0, 120), shaderTitle.c_str());
    dl->AddText(ImVec2((ds.x - ts.x) / 2, y - 17), kWhite, shaderTitle.c_str());
    ImGui::PopFont();
    char sub[160];
    std::snprintf(sub, sizeof sub, "%s · %s%s", shaderKind.c_str(), sourceLabel == "None" ? kTierName[tier] : sourceLabel.c_str(), easel.connected() ? " · Easel live" : "");
    ImGui::PushFont(F(), 12.0f);
    ImVec2 ss = ImGui::CalcTextSize(sub);
    dl->AddText(ImVec2((ds.x - ss.x) / 2, y + 2), IM_COL32(255, 255, 255, 150), sub);
    ImGui::PopFont();
    ImGui::End();

    // HUD bottom-left on desktop
    if (!phone) {
        char hud[128];
        std::snprintf(hud, sizeof hud, "%.0f fps · %.1f ms · %s%s%s", fps, frameMs, kTierName[tier], paused ? " · paused" : "", compileOk ? "" : " · shader error");
        ImGui::PushFont(F(), 12.0f);
        ImVec2 hs = ImGui::CalcTextSize(hud);
        ImGui::GetBackgroundDrawList()->AddText(ImVec2(24 + 1, ds.y - 24 - hs.y + 1), IM_COL32(0, 0, 0, 140), hud);
        ImGui::GetBackgroundDrawList()->AddText(ImVec2(24, ds.y - 24 - hs.y), compileOk ? IM_COL32(255, 255, 255, 150) : kRed, hud);
        ImGui::PopFont();
    }
}

// ------------------------------------------------------------- dock
void App::drawDock() {
    struct Item { Sheet s; Icon ic; const char* label; };
    static const Item items[] = {{Sheet::Source, Icon::Source, "Source"}, {Sheet::Shader, Icon::Shader, "Shader"}, {Sheet::Controls, Icon::Controls, "Controls"}, {Sheet::Live, Icon::Live, "Live"}, {Sheet::Publish, Icon::Publish, "Publish"}};
    const int n = 5;
    const float itemW = phone ? kDockItemW : 84, h = phone ? kDockH : 64;
    const float w = n * itemW + 16;
    ImVec2 p0((ds.x - w) / 2, ds.y - (phone ? 36 : 24) - h), p1(p0.x + w, p0.y + h);
    ImGui::SetNextWindowPos(ImVec2(p0.x, p0.y));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::Begin("##dock", nullptr, kBarFlags);
    auto* dl = ImGui::GetWindowDrawList();
    glass.rect(dl, p0, p1, h / 2, ds);
    ImGui::PushFont(F(), 10.5f);
    for (int i = 0; i < n; ++i) {
        ImVec2 a(p0.x + 8 + i * itemW, p0.y + (h - kDockItemH) / 2), b(a.x + itemW, a.y + kDockItemH);
        ImGui::SetCursorScreenPos(a);
        ImGui::PushID(i);
        bool clicked = ImGui::InvisibleButton("##d", ImVec2(itemW, kDockItemH));
        bool hov = ImGui::IsItemHovered();
        ImGui::PopID();
        bool on = sheet == items[i].s;
        bool pri = items[i].s == Sheet::Publish;
        if (on) dl->AddRectFilled(a, b, IM_COL32(255, 255, 255, 26), kDockItemH / 2);
        else if (hov) dl->AddRectFilled(a, b, IM_COL32(255, 255, 255, 12), kDockItemH / 2);
        ImU32 col = (on || pri) ? kWhite : IM_COL32(255, 255, 255, 140);
        ImVec2 ic(a.x + itemW / 2, a.y + 20);
        if (pri) { dl->AddRectFilled(ImVec2(ic.x - 13, ic.y - 13), ImVec2(ic.x + 13, ic.y + 13), kWhite, 8.0f); icon(dl, items[i].ic, ic, 20, IM_COL32(0, 0, 0, 255)); }
        else icon(dl, items[i].ic, ic, 24, col);
        ImVec2 ls = ImGui::CalcTextSize(items[i].label);
        dl->AddText(ImVec2(ic.x - ls.x / 2, a.y + 37), col, items[i].label);
        if (clicked) openSheet(items[i].s);
    }
    ImGui::PopFont();
    ImGui::End();
}

// ------------------------------------------------------------- more
void App::drawMore() {
    static Spring anim; anim.target = moreOpen ? 1.0f : 0.0f; anim.step(ImGui::GetIO().DeltaTime, 22.0f);
    if (anim.v < 0.01f) return;
    const float w = 236;
    ImVec2 p0(ds.x - 16 - w, (phone ? 108 : 62) - 8 * (1 - anim.v));
    ImGui::SetNextWindowPos(p0);
    ImGui::SetNextWindowSize(ImVec2(w, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 6));
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, anim.v);
    ImGui::Begin("##more", nullptr, kSheetFlags | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoBackground);
    auto* dl = ImGui::GetWindowDrawList();
    ImVec2 wp = ImGui::GetWindowPos(), wsz = ImGui::GetWindowSize();
    glass.rect(dl, wp, ImVec2(wp.x + wsz.x, wp.y + wsz.y), 20.0f, ds, 0, IM_COL32(22, 22, 24, 200));
    char passes[32]; std::snprintf(passes, sizeof passes, "%zu pass%s", passNames.size(), passNames.size() == 1 ? "" : "es");
    if (menuRow("##code", Icon::Code, "Code", passes)) openSheet(Sheet::Code);
    if (menuRow("##quality", Icon::Quality, "Quality", kTierName[tier])) openSheet(Sheet::Quality);
    if (menuRow("##connect", Icon::Link, "Connect to Easel", easel.connected() ? easel.zone().c_str() : "off")) openSheet(Sheet::Connect);
    hairline(12);
    if (menuRow("##export", Icon::Export, "Export", "Still · .fs")) openSheet(Sheet::Export);
    if (menuRow("##pause", paused ? Icon::Play : Icon::Pause, paused ? "Play" : "Pause", "Space")) { paused = !paused; moreOpen = false; }
    bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    ImGui::End();
    ImGui::PopStyleVar(2);
    if (moreOpen && ImGui::IsMouseClicked(0) && !hovered) {
        ImVec2 m = ImGui::GetIO().MousePos;
        bool onButton = std::hypot(m.x - (ds.x - 16 - kBtn / 2), m.y - ((phone ? 62 : 16) + kBtn / 2)) < kBtn;
        if (!onButton) moreOpen = false;
    }
}

// ------------------------------------------------------------- toast
void App::drawToast() {
    if (toastT <= 0 || toast.empty()) return;
    float a = std::clamp(toastT / 0.3f, 0.0f, 1.0f);
    ImGui::PushFont(F(), 13.0f);
    ImVec2 ts = ImGui::CalcTextSize(toast.c_str());
    float w = ts.x + 40, h = 40;
    ImVec2 p0((ds.x - w) / 2, (phone ? 124 : 76) - 6 * (1 - a));
    auto* dl = ImGui::GetForegroundDrawList();
    ImU32 tint = IM_COL32(22, 22, 24, (int)(200 * a));
    glass.rect(dl, p0, ImVec2(p0.x + w, p0.y + h), h / 2, ds, 0, tint);
    dl->AddCircleFilled(ImVec2(p0.x + 18, p0.y + h / 2), 4, IM_COL32(57, 196, 123, (int)(255 * a)));
    dl->AddText(ImVec2(p0.x + 30, p0.y + (h - ts.y) / 2), IM_COL32(245, 245, 247, (int)(255 * a)), toast.c_str());
    ImGui::PopFont();
}

// ------------------------------------------------------------- sheet container
void App::drawSheet() {
    Sheet s = sheet != Sheet::None ? sheet : closing;
    if (s == Sheet::None || sheetAnim.v < 0.005f) return;
    const bool big = s == Sheet::Code || s == Sheet::Shader || s == Sheet::Controls;
    const float dockTop = ds.y - (phone ? 36 : 24) - (phone ? kDockH : 64);
    float w = phone ? ds.x : (s == Sheet::Code ? std::min(560.0f, ds.x - 48) : 380.0f);
    float maxH = phone ? ds.y * 0.86f : dockTop - 72 - 16;
    ImGuiWindowFlags flags = kSheetFlags;
    bool fixedH = big;
    float fixed = phone ? (s == Sheet::Code ? ds.y * 0.80f : s == Sheet::Shader ? ds.y * 0.84f : ds.y * 0.62f) : maxH;
    ImGuiWindow* prev = ImGui::FindWindowByName("##sheet");
    float lastH = prev ? prev->Size.y : 300.0f;
    float hNow = fixedH ? fixed : std::min(lastH, maxH);
    if (phone) {
        ImGui::SetNextWindowPos(ImVec2(0, ds.y - hNow * sheetAnim.v));
    } else {
        ImGui::SetNextWindowPos(ImVec2(ds.x - 24 - w, 72 + 14 * (1 - sheetAnim.v)));
    }
    if (fixedH) ImGui::SetNextWindowSize(ImVec2(w, fixed));
    else { ImGui::SetNextWindowSizeConstraints(ImVec2(w, 0), ImVec2(w, maxH)); flags |= ImGuiWindowFlags_AlwaysAutoResize; }
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(kGutter, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, phone ? 1.0f : sheetAnim.v);
    ImGui::Begin("##sheet", nullptr, flags);
    auto* dl = ImGui::GetWindowDrawList();
    ImVec2 wp = ImGui::GetWindowPos(), wsz = ImGui::GetWindowSize();
    ImVec2 p1(wp.x + wsz.x, wp.y + wsz.y + (phone ? 40 : 0));
    glass.rect(dl, wp, p1, phone ? kSheetR : kPanelR, ds, phone ? ImDrawFlags_RoundTop : 0);
    ImGui::SetCursorPos(ImVec2(kGutter, 0));
    ImGui::BeginGroup();
    switch (s) {
        case Sheet::Controls: sheetControls(); break;
        case Sheet::Live: sheetLive(); break;
        case Sheet::Shader: sheetShader(); break;
        case Sheet::Source: sheetSource(); break;
        case Sheet::Publish: sheetPublish(); break;
        case Sheet::Code: sheetCode(); break;
        case Sheet::Quality: sheetQuality(); break;
        case Sheet::Connect: sheetConnect(); break;
        case Sheet::Export: sheetExport(); break;
        default: break;
    }
    ImGui::EndGroup();
    ImGui::Dummy(ImVec2(0, phone ? 44 : 20));
    ImGui::End();
    ImGui::PopStyleVar(2);
}

// ------------------------------------------------------------- controls
void App::sheetControls() {
    if (!shader) { sheetHeader("Controls", "Pick a shader first"); return; }
    auto g = groups();
    controlsGroup = std::clamp(controlsGroup, 0, (int)g.size() - 1);
    const std::string& cur = g[controlsGroup];
    auto& inputs = shader->inputs();
    int shown = 0, total = 0;
    for (auto& in : inputs) if (in.type != "image") { ++total; std::string gr = in.group.empty() ? "General" : in.group; if (cur == "All" || gr == cur) ++shown; }
    char sub[64]; std::snprintf(sub, sizeof sub, "%s · %d of %d", cur.c_str(), shown, total);
    if (sheetHeader("Controls", sub)) closeSheet();
    if (g.size() > 1) {
        if (g.size() <= 4) controlsGroup = segmented("##groups", g, controlsGroup);
        else {
            ImGui::BeginChild("##gchips", ImVec2(0, 40), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoScrollbar);
            int ng = chips("##groups", g, controlsGroup); if (ng >= 0) controlsGroup = ng;
            if (ImGui::IsWindowHovered()) ImGui::SetScrollX(ImGui::GetScrollX() - ImGui::GetIO().MouseWheel * 30 - ImGui::GetIO().MouseWheelH * 30);
            ImGui::EndChild();
        }
    }
    ImGui::Dummy(ImVec2(0, 4));
    ImGui::BeginChild("##rows", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    auto& ab = shader->audioBindings();
    for (auto& in : inputs) {
        if (in.type == "image") continue;
        std::string gr = in.group.empty() ? "General" : in.group;
        if (cur != "All" && gr != cur) continue;
        ImGui::PushID(in.name.c_str());
        const char* label = in.label.empty() ? in.name.c_str() : in.label.c_str();
        if (in.type == "float" || in.type == "long") {
            float v = std::get<float>(in.value);
            SliderStyle st;
            char driven[48] = "";
            auto it = ab.find(in.name);
            if (it != ab.end() && it->second.signal != AudioSignal::None) {
                st.driven = true;
                static const char* nm[] = {"", "Level", "Bass", "Mid", "High", "Beat", "MIDI", "Energy", "Build", "Drop", "Silence", "Momentum"};
                std::snprintf(driven, sizeof driven, "Sound · %s", nm[(int)it->second.signal]);
                float span = in.maxVal - in.minVal; if (span <= 0) span = 1;
                st.reach0 = (it->second.rangeMin - in.minVal) / span; st.reach1 = (it->second.rangeMax - in.minVal) / span;
                st.drivenBy = driven;
            } else if (motion.count(in.name)) {
                st.driven = true;
                std::snprintf(driven, sizeof driven, "Motion · %s", live::Lfo::shapeName(motion[in.name].lfo.shape));
                st.drivenBy = driven;
                float span = in.maxVal - in.minVal; if (span <= 0) span = 1;
                st.reach0 = (motion[in.name].base - in.minVal) / span; st.reach1 = std::min(1.0f, st.reach0 + motion[in.name].reach);
            }
            st.centerZero = in.minVal < 0 && in.maxVal > 0 && std::abs(in.minVal + in.maxVal) < 1e-4f;
            char vt[24];
            if (in.type == "long") { std::snprintf(vt, sizeof vt, "%d", (int)std::lround(v)); st.valueText = vt; }
            else if (st.centerZero) { std::snprintf(vt, sizeof vt, "%+.2f", v); st.valueText = vt; }
            if (in.type == "long" && !in.longLabels.empty() && in.longLabels.size() <= 5) {
                ImGui::PushFont(F(), 15.0f); ImGui::TextUnformatted(label); ImGui::PopFont();
                int idx = 0; for (size_t i = 0; i < in.longValues.size(); ++i) if (in.longValues[i] == (int)std::lround(v)) idx = (int)i;
                int ni = segmented("##long", in.longLabels, idx);
                if (ni != idx) { shader->setFloat(in.name, (float)in.longValues[ni]); paramChanged(in); }
                ImGui::Dummy(ImVec2(0, 10));
            } else {
                SliderRow r = sliderRow("##s", label, &v, in.minVal, in.maxVal, st);
                if (r.changed) {
                    if (in.type == "long") v = std::round(v);
                    if (st.driven && it != ab.end()) { float d = v - std::get<float>(in.value); it->second.rangeMin += d; it->second.rangeMax = std::min(in.maxVal, it->second.rangeMax + d); }
                    if (motion.count(in.name)) motion[in.name].base = v;
                    shader->setFloat(in.name, v); paramChanged(in);
                }
                if (r.reset) { shader->setFloat(in.name, in.defaultFloat); if (motion.count(in.name)) motion[in.name].base = in.defaultFloat; paramChanged(in); }
                if (r.labelClicked) bindSource(in.name);
                if (r.valueClicked) { typingParam = in.name; std::snprintf(typingBuf, sizeof typingBuf, "%.3f", v); ImGui::OpenPopup("##type"); }
                if (typingParam == in.name) {
                    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 14.0f);
                    if (ImGui::BeginPopup("##type")) {
                        ImGui::PushFont(theme::monoFont(), 14.0f);
                        ImGui::SetKeyboardFocusHere();
                        if (ImGui::InputText("##tv", typingBuf, sizeof typingBuf, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
                            float nv = (float)std::atof(typingBuf); nv = std::clamp(nv, in.minVal, in.maxVal);
                            shader->setFloat(in.name, nv); paramChanged(in); ImGui::CloseCurrentPopup(); typingParam.clear();
                        }
                        ImGui::PopFont();
                        ImGui::EndPopup();
                    } else typingParam.clear();
                    ImGui::PopStyleVar();
                }
            }
        } else if (in.type == "bool") {
            bool b = std::get<bool>(in.value);
            if (toggleRow("##b", label, &b)) { shader->setBool(in.name, b); paramChanged(in); }
        } else if (in.type == "color") {
            glm::vec4 c = std::get<glm::vec4>(in.value);
            if (colorRow("##c", label, &c.x)) { shader->setColor(in.name, c); paramChanged(in); }
        } else if (in.type == "point2D") {
            glm::vec2 v = std::get<glm::vec2>(in.value);
            char lx[64], ly[64]; std::snprintf(lx, sizeof lx, "%s X", label); std::snprintf(ly, sizeof ly, "%s Y", label);
            SliderRow rx = sliderRow("##px", lx, &v.x, in.minVec.x, in.maxVec.x);
            SliderRow ry = sliderRow("##py", ly, &v.y, in.minVec.y, in.maxVec.y);
            if (rx.changed || ry.changed) shader->setPoint2D(in.name, v);
            if (rx.reset || ry.reset) shader->setPoint2D(in.name, in.defaultVec);
        } else if (in.type == "event") {
            if (bigButton("##ev", label, false, 44)) shader->setBool(in.name, true);
            ImGui::Dummy(ImVec2(0, 8));
        } else if (in.type == "text") {
            std::string s = std::get<std::string>(in.value);
            char buf[256]; std::snprintf(buf, sizeof buf, "%s", s.c_str());
            ImGui::PushFont(F(), 15.0f); ImGui::TextUnformatted(label); ImGui::PopFont();
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputText("##t", buf, sizeof buf)) shader->setText(in.name, buf);
            ImGui::Dummy(ImVec2(0, 8));
        }
        ImGui::PopID();
    }
    ImGui::Dummy(ImVec2(0, 6));
    caption("Tap a name to drive it live · double-tap a knob to reset · tap a value to type · Shift for fine", kW40);
    ImGui::EndChild();
}

// ------------------------------------------------------------- live
void App::sheetLive() {
    if (!shader) { sheetHeader("Live", "Pick a shader first"); return; }
    if (liveParam.empty()) {
        if (sheetHeader("Live", "Choose a control to drive")) closeSheet();
        caption("Open Controls and tap the name of any value, or pick one here.", kW60);
        ImGui::Dummy(ImVec2(0, 8));
        for (auto& in : shader->inputs()) {
            if (in.type != "float") continue;
            ImGui::PushID(in.name.c_str());
            bool driven = (shader->audioBindings().count(in.name) && shader->audioBindings()[in.name].signal != AudioSignal::None) || motion.count(in.name);
            if (menuRow("##p", driven ? Icon::Live : Icon::Controls, in.label.empty() ? in.name.c_str() : in.label.c_str(), driven ? "driven" : nullptr)) bindSource(in.name);
            ImGui::PopID();
        }
        return;
    }
    const ISFInput* in = nullptr;
    for (auto& i : shader->inputs()) if (i.name == liveParam) in = &i;
    if (!in) { liveParam.clear(); return; }
    auto& ab = shader->audioBindings();
    bool isAudio = ab.count(liveParam) && ab[liveParam].signal != AudioSignal::None;
    bool isMotion = motion.count(liveParam) > 0;
    const char* label = in->label.empty() ? in->name.c_str() : in->label.c_str();
    if (sheetHeader(label, isAudio ? "Driven by Sound" : isMotion ? "Driven by Motion" : "Not driven")) closeSheet();

    int src = isAudio ? 0 : isMotion ? 1 : 2;
    int ns = chips("##src", {"Sound", "Motion", "Off"}, src);
    if (ns == 2 && src != 2) { unbind(liveParam); src = 2; }
    if (ns == 0 && src != 0) { setAudioBind(liveParam, AudioSignal::Bass, 0.5f, 0.85f, 0); src = 0; }
    if (ns == 1 && src != 1) { MotionBinding m; setMotionBind(liveParam, m); src = 1; }
    ImGui::Dummy(ImVec2(0, 8));

    if (src == 0) {
        AudioBinding& b = ab[liveParam];
        static const AudioSignal sigs[] = {AudioSignal::Level, AudioSignal::Bass, AudioSignal::Mid, AudioSignal::High, AudioSignal::Beat};
        int cur = 1; for (int i = 0; i < 5; ++i) if (sigs[i] == b.signal) cur = i;
        float span = in->maxVal - in->minVal; if (span <= 0) span = 1;
        float reach = (b.rangeMax - b.rangeMin) / span;
        int shape = b.character <= -0.9f ? 0 : b.character < 0 ? 1 : b.character < 0.9f ? 2 : 3;
        int nc = chips("##out", {"Level", "Bass", "Mid", "High", "Beat"}, cur);
        if (nc >= 0 && nc != cur) setAudioBind(liveParam, sigs[nc], reach, b.smoothing, shape);
        // waveform of the chosen signal
        ImGui::Dummy(ImVec2(0, 8));
        ImVec2 p = ImGui::GetCursorScreenPos(); float w = ImGui::GetContentRegionAvail().x, h = 64;
        auto* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), IM_COL32(255, 255, 255, 12), 14.0f);
        const auto& hist = audio.history();
        ImVec2 pts[128];
        for (int i = 0; i < 128; ++i) { float v = hist[i]; pts[i] = ImVec2(p.x + 8 + (w - 16) * i / 127.0f, p.y + h - 8 - v * (h - 16)); }
        dl->AddPolyline(pts, 128, kBlue, 2.0f);
        float live = b.lastRaw;
        dl->AddCircleFilled(ImVec2(p.x + w - 8, p.y + h - 8 - std::clamp(live, 0.0f, 1.0f) * (h - 16)), 4, kWhite);
        ImGui::Dummy(ImVec2(w, h + 8));
        float r = reach, sm = b.smoothing;
        SliderStyle rs; rs.driven = true;
        if (sliderRow("##reach", "Reach", &r, 0.0f, 1.0f, rs).changed) setAudioBind(liveParam, b.signal, r, sm, shape);
        if (sliderRow("##smooth", "Smooth", &sm, 0.5f, 0.98f).changed) setAudioBind(liveParam, b.signal, r, sm, shape);
        ImGui::Dummy(ImVec2(0, 10));
        int nsh = segmented("##shape", {"Follow", "Ease", "Pulse", "Gate"}, shape);
        if (nsh != shape) setAudioBind(liveParam, b.signal, r, sm, nsh);
        ImGui::Dummy(ImVec2(0, 6));
        caption(audio.running() ? "Listening to the microphone." : audio.error().c_str(), kW40);
    } else if (src == 1) {
        MotionBinding& m = motion[liveParam];
        static const live::Lfo::Shape shapes[] = {live::Lfo::Shape::Sine, live::Lfo::Shape::Triangle, live::Lfo::Shape::Saw, live::Lfo::Shape::Square, live::Lfo::Shape::Drift};
        int cur = 0; for (int i = 0; i < 5; ++i) if (shapes[i] == m.lfo.shape) cur = i;
        int nc = chips("##shape", {"Sine", "Triangle", "Saw", "Square", "Drift"}, cur);
        if (nc >= 0) m.lfo.shape = shapes[nc];
        ImGui::Dummy(ImVec2(0, 8));
        // rate: free or beat-locked
        static const float rates[] = {0.05f, 0.2f, 0.8f};
        int rsel = m.lfo.beatDiv == 0 ? (m.lfo.rateHz < 0.1f ? 0 : m.lfo.rateHz < 0.5f ? 1 : 2) : (m.lfo.beatDiv == 1 ? 3 : m.lfo.beatDiv == 2 ? 4 : m.lfo.beatDiv == 4 ? 5 : 6);
        int nr = segmented("##rate", {"Slow", "Medium", "Fast", "1 beat", "2", "4", "8"}, rsel);
        if (nr != rsel) { if (nr < 3) { m.lfo.beatDiv = 0; m.lfo.rateHz = rates[nr]; } else m.lfo.beatDiv = nr == 3 ? 1 : nr == 4 ? 2 : nr == 5 ? 4 : 8; }
        ImGui::Dummy(ImVec2(0, 10));
        // preview line
        ImVec2 p = ImGui::GetCursorScreenPos(); float w = ImGui::GetContentRegionAvail().x, h = 48;
        auto* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), IM_COL32(255, 255, 255, 12), 14.0f);
        live::Lfo preview = m.lfo; preview.phase = 0; preview.driftT = 1;
        ImVec2 pts[96];
        for (int i = 0; i < 96; ++i) { float v = preview.step(2.0f / 96.0f / std::max(0.01f, preview.beatDiv ? (audio.bpm() / 60.0f) / preview.beatDiv : preview.rateHz), audio.bpm()); pts[i] = ImVec2(p.x + 8 + (w - 16) * i / 95.0f, p.y + h - 8 - v * (h - 16)); }
        dl->AddPolyline(pts, 96, kBlue, 2.0f);
        ImGui::Dummy(ImVec2(w, h + 8));
        SliderStyle rs; rs.driven = true;
        sliderRow("##reach", "Reach", &m.reach, 0.0f, 1.0f, rs);
        sliderRow("##smooth", "Smooth", &m.smooth, 0.5f, 0.98f);
        caption(m.lfo.beatDiv ? "Locked to the beat Palette hears." : "Free running.", kW40);
    } else {
        caption("This value is yours alone. Choose Sound or Motion to drive it.", kW60);
    }
}

// ------------------------------------------------------------- shader
void App::sheetShader() {
    char sub[64]; std::snprintf(sub, sizeof sub, "%zu in the library", library.entries().size());
    if (sheetHeader("Shader", sub)) closeSheet();
    // search field
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 14.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14, 12));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(255, 255, 255, 20));
    ImGui::SetNextItemWidth(-1);
    ImGui::PushFont(F(), 15.0f);
    ImGui::InputTextWithHint("##q", "Search", search, sizeof search);
    ImGui::PopFont();
    ImGui::PopStyleColor(); ImGui::PopStyleVar(2);
    ImGui::Dummy(ImVec2(0, 8));
    int nf = chips("##f", {"All", "Effects", "Simulations", "3D", "Text"}, shaderFilter);
    if (nf >= 0) shaderFilter = nf;
    ImGui::Dummy(ImVec2(0, 8));
    ImGui::BeginChild("##grid", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    std::string q = search; std::transform(q.begin(), q.end(), q.begin(), ::tolower);
    float w = ImGui::GetContentRegionAvail().x;
    int cols = w > 520 ? 3 : 2;
    float gap = 12, tw = (w - gap * (cols - 1)) / cols, th = tw * 1.25f;
    auto* dl = ImGui::GetWindowDrawList();
    int i = 0;
    ImVec2 origin = ImGui::GetCursorScreenPos();
    for (auto& e : library.entries()) {
        if (e.hidden || e.type == "scene") continue;
        if (!q.empty() && !e.matches(q)) continue;
        if (shaderFilter > 0) {
            static const char* want[] = {"", "effect", "simulation", "3d", "text"};
            bool ok = false;
            for (auto& c : e.categories) { std::string lc = c; std::transform(lc.begin(), lc.end(), lc.begin(), ::tolower); if (lc.find(want[shaderFilter]) != std::string::npos) ok = true; }
            if (shaderFilter == 2) for (auto& c : e.categories) { std::string lc = c; std::transform(lc.begin(), lc.end(), lc.begin(), ::tolower); if (lc.find("particle") != std::string::npos || lc.find("fluid") != std::string::npos) ok = true; }
            if (!ok) continue;
        }
        int cx = i % cols, cy = i / cols;
        ImVec2 a(origin.x + cx * (tw + gap), origin.y + cy * (th + gap)), b(a.x + tw, a.y + th);
        ImGui::SetCursorScreenPos(a);
        ImGui::PushID(e.path.c_str());
        bool clicked = ImGui::InvisibleButton("##t", ImVec2(tw, th));
        bool hov = ImGui::IsItemHovered();
        ImGui::PopID();
        bool visible = ImGui::IsRectVisible(a, b);
        if (visible) {
            const Thumbs::Thumb& t = thumbs.get(e);
            if (t.tex) dl->AddImageRounded((ImTextureID)(intptr_t)t.tex, a, b, ImVec2(0, t.flipped ? 0 : 1), ImVec2(1, t.flipped ? 1 : 0), IM_COL32_WHITE, 18.0f);
            else dl->AddRectFilled(a, b, IM_COL32(255, 255, 255, 14), 18.0f);
            // bottom gradient for legibility
            dl->AddRectFilledMultiColor(ImVec2(a.x, b.y - 56), b, IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 150), IM_COL32(0, 0, 0, 150));
            if (e.path == shaderPath) dl->AddRect(a, b, kWhite, 18.0f, 2.0f);
            else if (hov) dl->AddRect(a, b, IM_COL32(255, 255, 255, 90), 18.0f, 1.0f);
            ImGui::PushFont(F(), 13.0f);
            dl->AddText(ImVec2(a.x + 12, b.y - 24), kWhite, e.title.c_str());
            if (e.id >= 0) { char idb[16]; std::snprintf(idb, sizeof idb, "%d", e.id); ImVec2 is = ImGui::CalcTextSize(idb); dl->AddText(ImVec2(b.x - 12 - is.x, b.y - 23), IM_COL32(255, 255, 255, 170), idb); }
            ImGui::PopFont();
        }
        if (clicked) { loadShader(e.path, e.title, e.id); if (phone) closeSheet(); }
        ++i;
    }
    int rows = (i + cols - 1) / cols;
    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + rows * (th + gap)));
    ImGui::Dummy(ImVec2(w, 1));
    ImGui::EndChild();
}

// ------------------------------------------------------------- source
void App::sheetSource() {
    if (sheetHeader("Source", sourceLabel.c_str())) closeSheet();
    if (bigButton("##img", "Choose an image…", true)) openFileDialog(*this, true);
    ImGui::Dummy(ImVec2(0, 8));
    if (bigButton("##fs", "Open a shader file…", false)) openFileDialog(*this, false);
    ImGui::Dummy(ImVec2(0, 8));
    if (bigButton("##none", "No source", false)) { if (shader) shader->unbindImageInput("inputImage"); sourcePath.clear(); sourceLabel = "None"; }
    ImGui::Dummy(ImVec2(0, 12));
    bool hasImageInput = false;
    if (shader) for (auto& in : shader->inputs()) if (in.type == "image") hasImageInput = true;
    caption(hasImageInput ? "This shader takes an image. Drop one anywhere on the window." : "This shader generates its own picture. An image here is kept for the next effect you open.", kW40);
    ImGui::Dummy(ImVec2(0, 6));
    caption("Camera and video arrive with the next cut.", kW30);
}

// ------------------------------------------------------------- publish
void App::sheetPublish() {
    if (!shader) { sheetHeader("Publish", "Pick a shader first"); return; }
    if (sheetHeader("Publish to ShaderClaw", "Into the library Easel plays from", false)) {}
    // preview
    ImVec2 p = ImGui::GetCursorScreenPos(); float w = ImGui::GetContentRegionAvail().x, h = w * 0.5625f;
    auto* dl = ImGui::GetWindowDrawList();
    if (shader->textureId()) dl->AddImageRounded((ImTextureID)(intptr_t)shader->textureId(), p, ImVec2(p.x + w, p.y + h), ImVec2(0, 1), ImVec2(1, 0), IM_COL32_WHITE, 16.0f);
    ImGui::Dummy(ImVec2(w, h + 14));
    // name
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 12.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14, 11));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(255, 255, 255, 20));
    ImGui::SetNextItemWidth(-1);
    ImGui::PushFont(F(), 16.0f);
    ImGui::InputTextWithHint("##name", "Name", pubName, sizeof pubName);
    ImGui::PopFont();
    ImGui::PopStyleColor(); ImGui::PopStyleVar(2);
    ImGui::Dummy(ImVec2(0, 10));
    // kv
    auto kv = [&](const char* k, const std::string& v) {
        ImVec2 q = ImGui::GetCursorScreenPos(); float ww = ImGui::GetContentRegionAvail().x;
        ImGui::PushFont(F(), 15.0f);
        dl->AddText(q, IM_COL32(255, 255, 255, 128), k);
        ImVec2 vs = ImGui::CalcTextSize(v.c_str());
        dl->AddText(ImVec2(q.x + ww - vs.x, q.y), kWhite, v.c_str());
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(ww, 30));
    };
    kv("Number", std::to_string(library.nextFreeId()));
    kv("File", slugify(pubName) + ".fs");
    if (shaderId >= 0) kv("Based on", std::to_string(shaderId) + " · " + shaderTitle);
    ImGui::Dummy(ImVec2(0, 4));
    int np = chips("##pack", std::vector<std::string>(std::begin(kPacks), std::end(kPacks)), pubPack);
    if (np >= 0) pubPack = np;
    ImGui::Dummy(ImVec2(0, 8));
    toggleRow("##easel", "Show on Easel after", &pubShowOnEasel, easel.connected() ? ("Zone " + easel.zone()).c_str() : "Connect to Easel first");
    toggleRow("##hidden", "Hidden in galleries", &pubHidden);
    ImGui::Dummy(ImVec2(0, 10));
    float bw = (ImGui::GetContentRegionAvail().x - 10) / 2;
    if (bigButton("##no", "Not yet", false, 50, bw)) closeSheet();
    ImGui::SameLine(0, 10);
    if (bigButton("##go", "Publish", true, 50, bw)) publish();
}

// ------------------------------------------------------------- code
void App::sheetCode() {
    if (!shader) { sheetHeader("Code", "Pick a shader first"); return; }
    char sub[128];
    std::snprintf(sub, sizeof sub, "%s · %s%s", passNames[std::clamp(codePass, 0, (int)passNames.size() - 1)].c_str(), compileOk ? "compiled" : "error", codeDirty ? " · edited" : "");
    if (sheetHeader("Code", sub)) closeSheet();
    if (passNames.size() > 1) { codePass = segmented("##pass", passNames, codePass, 34, std::min(320.0f, ImGui::GetContentRegionAvail().x)); ImGui::Dummy(ImVec2(0, 8)); }
    if (!compileOk) { ImGui::PushStyleColor(ImGuiCol_Text, kRed); ImGui::PushFont(theme::monoFont(), 12.0f); ImGui::TextWrapped("%s", compileStatus.c_str()); ImGui::PopFont(); ImGui::PopStyleColor(); ImGui::Dummy(ImVec2(0, 4)); }
    float footer = 52;
    ImVec2 avail = ImGui::GetContentRegionAvail();
    static std::string* bufPtr = nullptr; bufPtr = &codeBuffer;
    auto cb = [](ImGuiInputTextCallbackData* d) -> int {
        if (d->EventFlag == ImGuiInputTextFlags_CallbackResize) { bufPtr->resize(d->BufTextLen); d->Buf = bufPtr->data(); }
        return 0;
    };
    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 90));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 14.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14, 12));
    ImGui::PushFont(theme::monoFont(), 12.5f);
    static double lastEdit = 0;
    if (ImGui::InputTextMultiline("##code", codeBuffer.data(), codeBuffer.capacity() + 1, ImVec2(avail.x, avail.y - footer - (phone ? 44 : 20)),
                                  ImGuiInputTextFlags_CallbackResize | ImGuiInputTextFlags_AllowTabInput, cb)) { codeDirty = true; lastEdit = clockSeconds(); }
    ImGuiID codeId = ImGui::GetItemID();
    ImGui::PopFont();
    ImGui::PopStyleVar(2); ImGui::PopStyleColor();
    if (codeDirty && clockSeconds() - lastEdit > 0.45 && lastEdit > 0 && ImGui::IsItemActive()) { recompile(); if (!compileOk) codeDirty = true; lastEdit = 0; }
    // footer: selection + Make Control
    int selA = 0, selB = 0; std::string selText;
    if (ImGuiInputTextState* st = ImGui::GetInputTextState(codeId)) {
        if (st->HasSelection()) { selA = st->GetSelectionStart(); selB = st->GetSelectionEnd(); if (selA > selB) std::swap(selA, selB); if (selB <= (int)codeBuffer.size()) selText = codeBuffer.substr(selA, selB - selA); }
    }
    bool numeric = !selText.empty() && selText.size() < 16 && selText.find_first_not_of("0123456789.-") == std::string::npos && selText.find_first_of("0123456789") != std::string::npos;
    ImGui::Dummy(ImVec2(0, 8));
    ImVec2 fp = ImGui::GetCursorScreenPos(); float fw = ImGui::GetContentRegionAvail().x;
    ImGui::PushFont(F(), 13.0f);
    std::string hint = numeric ? selText + " selected" : codeDirty ? "⌘⏎ to compile" : "Select a number to make it a control";
    ImGui::GetWindowDrawList()->AddText(ImVec2(fp.x, fp.y + 12), IM_COL32(255, 255, 255, 128), hint.c_str());
    ImGui::PopFont();
    ImGui::SetCursorScreenPos(ImVec2(fp.x + fw - 130, fp.y));
    if (numeric) {
        if (bigButton("##mk", "Make Control", true, 40, 130)) {
            mcSelA = selA; mcSelB = selB; mcDefault = (float)std::atof(selText.c_str());
            float mag = std::max(std::abs(mcDefault), 0.01f);
            mcLo = mcDefault < 0 ? -mag * 3 : 0; mcHi = mcDefault == 0 ? 1.0f : mag * 3;
            std::snprintf(mcName, sizeof mcName, "amount");
            makeControlOpen = true; ImGui::OpenPopup("##mc");
        }
    } else if (codeDirty) {
        if (bigButton("##cmp", "Compile", true, 40, 130)) recompile();
    } else ImGui::Dummy(ImVec2(130, 40));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 18.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18, 16));
    ImGui::SetNextWindowSize(ImVec2(300, 0));
    if (ImGui::BeginPopup("##mc")) {
        ImGui::PushFont(F(), 17.0f); ImGui::TextUnformatted("Make Control"); ImGui::PopFont();
        ImGui::Dummy(ImVec2(0, 6));
        ImGui::PushFont(F(), 14.0f);
        ImGui::SetNextItemWidth(-1); ImGui::InputTextWithHint("##n", "name", mcName, sizeof mcName);
        ImGui::Dummy(ImVec2(0, 4));
        ImGui::PushFont(theme::monoFont(), 13.0f);
        ImGui::SetNextItemWidth(120); ImGui::InputFloat("##lo", &mcLo, 0, 0, "%.3g"); ImGui::SameLine(0, 8); ImGui::TextUnformatted("to"); ImGui::SameLine(0, 8);
        ImGui::SetNextItemWidth(120); ImGui::InputFloat("##hi", &mcHi, 0, 0, "%.3g");
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(0, 10));
        float bw = (ImGui::GetContentRegionAvail().x - 10) / 2;
        if (bigButton("##c", "Cancel", false, 44, bw)) ImGui::CloseCurrentPopup();
        ImGui::SameLine(0, 10);
        if (bigButton("##ok", "Create", true, 44, bw)) {
            std::string nm = mcName; for (auto& ch : nm) if (!std::isalnum((unsigned char)ch)) ch = '_';
            if (!nm.empty() && !std::isdigit((unsigned char)nm[0]) && makeControl(nm, mcLo, mcHi, mcDefault, mcSelA, mcSelB)) ImGui::CloseCurrentPopup();
        }
        ImGui::PopFont();
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(2);
}

// ------------------------------------------------------------- quality
void App::sheetQuality() {
    if (sheetHeader("Quality", kTierNote[tier])) closeSheet();
    for (int i = 3; i >= 0; --i) {
        ImGui::PushID(i);
        bool on = tier == i;
        ImVec2 p = ImGui::GetCursorScreenPos(); float w = ImGui::GetContentRegionAvail().x;
        if (ImGui::InvisibleButton("##t", ImVec2(w, 56))) setTier(i);
        auto* dl = ImGui::GetWindowDrawList();
        if (on) dl->AddRectFilled(p, ImVec2(p.x + w, p.y + 56), IM_COL32(255, 255, 255, 20), 16.0f);
        ImGui::PushFont(F(), 15.0f); dl->AddText(ImVec2(p.x + 16, p.y + 10), kWhite, kTierName[i]); ImGui::PopFont();
        ImGui::PushFont(F(), 12.0f); dl->AddText(ImVec2(p.x + 16, p.y + 32), kW40, kTierNote[i]); ImGui::PopFont();
        if (on) icon(dl, Icon::Check, ImVec2(p.x + w - 24, p.y + 28), 18, kWhite);
        ImGui::PopID();
    }
    ImGui::Dummy(ImVec2(0, 8));
    caption("Render scale changes now. Phones step down on their own when they run warm.", kW40);
}

// ------------------------------------------------------------- connect
void App::sheetConnect() {
    if (sheetHeader("Connect to Easel", easel.connected() ? "Live on this Mac" : "Edit on the wall")) closeSheet();
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 12.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14, 11));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(255, 255, 255, 20));
    ImGui::PushFont(F(), 15.0f);
    float w = ImGui::GetContentRegionAvail().x;
    ImGui::SetNextItemWidth((w - 10) / 2); ImGui::InputTextWithHint("##zone", "Zone", connZone, sizeof connZone);
    ImGui::SameLine(0, 10);
    ImGui::SetNextItemWidth((w - 10) / 2); ImGui::InputTextWithHint("##slot", "Slot", connSlot, sizeof connSlot);
    ImGui::PopFont();
    ImGui::PopStyleColor(); ImGui::PopStyleVar(2);
    ImGui::Dummy(ImVec2(0, 10));
    if (!easel.connected()) {
        if (bigButton("##go", "Connect", true)) {
            if (easel.connect(library.root(), connZone, connSlot)) { easel.pushCode(codeBuffer); showToast("Live on Easel · " + std::string(connZone)); }
        }
    } else {
        if (bigButton("##stop", "Disconnect", false)) { easel.disconnect(); showToast("Disconnected from Easel"); }
    }
    ImGui::Dummy(ImVec2(0, 12));
    caption("Palette writes a live copy of this shader into the ShaderClaw3 folder and asks Easel to show it on the zone. Every edit here reloads there and keeps its state.", kW40);
    if (!easel.log().empty()) {
        ImGui::Dummy(ImVec2(0, 8));
        ImGui::PushFont(theme::monoFont(), 11.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, kW40);
        int n = (int)easel.log().size();
        for (int i = std::max(0, n - 6); i < n; ++i) ImGui::TextUnformatted(easel.log()[i].c_str());
        ImGui::PopStyleColor(); ImGui::PopFont();
    }
}

// ------------------------------------------------------------- export
void App::sheetExport() {
    if (sheetHeader("Export", shaderTitle.c_str())) closeSheet();
    if (bigButton("##still", "Still · 1920 × 1080 PNG", true)) exportStill();
    ImGui::Dummy(ImVec2(0, 8));
    if (bigButton("##fs", "Shader file · .fs", false)) exportFs();
    ImGui::Dummy(ImVec2(0, 12));
    caption("Both land on the Desktop. Loop video comes with the next cut.", kW40);
}

} // namespace palette
