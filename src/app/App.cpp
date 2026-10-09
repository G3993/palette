#include "app/App.h"
#include "core/Clock.h"
#include "library/Publisher.h"
#include "ui/Theme.h"
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_opengl3.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;
using ojson = nlohmann::ordered_json;

namespace palette {

static std::string homeDir() { const char* h = std::getenv("HOME"); if (!h) h = std::getenv("USERPROFILE"); return h ? h : "."; }
static std::string readFile(const std::string& p) { std::ifstream f(p, std::ios::binary); std::stringstream ss; ss << f.rdbuf(); return ss.str(); }
static const float kTierScale[4] = {0.5f, 0.5f, 0.75f, 1.0f};

static const char* kBlitVS = R"(#version 330 core
out vec2 vUV; void main(){ vec2 p = vec2((gl_VertexID<<1)&2, gl_VertexID&2); vUV = p; gl_Position = vec4(p*2.0-1.0,0,1); })";
static const char* kBlitFS = R"(#version 330 core
in vec2 vUV; out vec4 FragColor; uniform sampler2D uTex; uniform float uDim;
void main(){ vec3 c = texture(uTex, vUV).rgb * uDim; FragColor = vec4(c, 1.0); })";

bool App::init(SDL_Window* w, SDL_GLContext gl, const std::string& openPath) {
    window = w; m_gl = gl;
    dpi = SDL_GetWindowPixelDensity(window);
    glass.init();
    thumbs.init();
    m_blit.loadFromSource(kBlitVS, kBlitFS);
    glGenVertexArrays(1, &m_vao);
    std::vector<std::string> roots;
    if (const char* lr = std::getenv("PALETTE_LIBRARY")) roots.push_back(lr);
    for (auto& r : {homeDir() + "/ShaderClaw3/shaders", homeDir() + "/Documents/ShaderClaw3/shaders", std::string(PALETTE_SOURCE_DIR) + "/templates"}) roots.push_back(r);
    library.connect(roots);
    if (false) library.connect({homeDir() + "/ShaderClaw3/shaders", homeDir() + "/Documents/ShaderClaw3/shaders", std::string(PALETTE_SOURCE_DIR) + "/templates"});
    if (!openPath.empty()) loadShader(openPath, fs::path(openPath).stem().string());
    else if (auto* e = library.find("chrome_ripple")) loadShader(e->path, e->title, e->id);
    else if (!library.entries().empty()) loadShader(library.entries()[0].path, library.entries()[0].title, library.entries()[0].id);
    audio.start();
    if (const char* t = std::getenv("PALETTE_TEST")) { runSelfTest(t); quit = true; return true; }
    if (const char* s = std::getenv("PALETTE_SHEET")) {
        std::string v = s;
        if (v == "controls") openSheet(Sheet::Controls); else if (v == "shader") openSheet(Sheet::Shader); else if (v == "live") { if (shader && !shader->inputs().empty()) { for (auto& i : shader->inputs()) if (i.type == "float" && i.group != "Motion") { bindSource(i.name); break; } } }
        else if (v == "publish") openSheet(Sheet::Publish); else if (v == "code") openSheet(Sheet::Code); else if (v == "more") moreOpen = true; else if (v == "quality") openSheet(Sheet::Quality); else if (v == "connect") openSheet(Sheet::Connect);
    }
    return true;
}

void App::shutdown() {
    easel.disconnect();
    audio.stop();
    shader.reset();
}

// ---------------------------------------------------------------- shader
bool App::loadShader(const std::string& path, const std::string& title, int id) {
    auto src = std::make_shared<ShaderSource>();
    double t0 = clockSeconds();
    bool ok = src->loadFromFile(path);
    compileMs = (clockSeconds() - t0) * 1000.0;
    if (!ok) { compileOk = false; compileStatus = src->lastError().empty() ? "compile failed" : src->lastError(); return false; }
    shader = src; shaderPath = path; shaderTitle = title; shaderId = id;
    codeBuffer = readFile(path); codeDirty = false; compileOk = true; compileStatus = "compiled";
    motion.clear(); liveParam.clear(); controlsGroup = 0;
    // kind from categories
    shaderKind = "Shader";
    if (auto* e = library.find(fs::path(path).stem().string())) {
        for (auto& c : e->categories) {
            std::string lc = c; std::transform(lc.begin(), lc.end(), lc.begin(), ::tolower);
            if (lc == "effect") shaderKind = "Effect"; else if (lc == "simulation" || lc == "particles") shaderKind = "Simulation";
            else if (lc == "3d") shaderKind = "3D"; else if (lc == "generator" && shaderKind == "Shader") shaderKind = "Generative";
        }
        if (id < 0) shaderId = e->id;
    }
    // pass names for the Code sheet
    passNames.clear();
    try {
        size_t a = codeBuffer.find("/*"), b = codeBuffer.find("*/");
        if (a != std::string::npos && b != std::string::npos) {
            ojson h = ojson::parse(codeBuffer.substr(a + 2, b - a - 2));
            if (h.contains("PASSES")) for (auto& p : h["PASSES"]) passNames.push_back(p.value("TARGET", std::string("output")));
        }
    } catch (...) {}
    if (passNames.empty()) passNames.push_back("output");
    codePass = (int)passNames.size() - 1;
    if (!sourcePath.empty()) shader->bindImageFileInput("inputImage", sourcePath);
    library.touch(path);
    if (easel.connected()) easel.pushCode(codeBuffer);
    std::snprintf(pubName, sizeof pubName, "%s v2", title.c_str());
    return true;
}

void App::recompile() {
    if (!shader) return;
    double t0 = clockSeconds();
    bool ok = shader->reload(codeBuffer);
    compileMs = (clockSeconds() - t0) * 1000.0;
    compileOk = ok; compileStatus = ok ? "compiled" : shader->lastError();
    if (ok) { codeDirty = false; if (easel.connected()) easel.pushCode(codeBuffer); }
}

void App::bindImage(const std::string& path) {
    if (!shader) return;
    if (shader->bindImageFileInput("inputImage", path)) { sourcePath = path; sourceLabel = fs::path(path).filename().string(); showToast("Source · " + sourceLabel); }
    else showToast("Could not read that image");
}

void App::setTier(int t) { tier = std::clamp(t, 0, 3); }

// ---------------------------------------------------------------- sheets
void App::openSheet(Sheet s) {
    moreOpen = false;
    if (sheet == s) { closeSheet(); return; }
    sheet = s; closing = Sheet::None;
    sheetAnim.target = 1.0f;
    if (s == Sheet::Publish && shader) {
        std::string base = shaderTitle;
        if (base.size() > 3 && base.compare(base.size() - 3, 3, " v2") == 0) base.replace(base.size() - 3, 3, " v3");
        else base += " v2";
        std::snprintf(pubName, sizeof pubName, "%s", base.c_str());
    }
}
void App::closeSheet() { if (sheet == Sheet::None) return; closing = sheet; sheet = Sheet::None; sheetAnim.target = 0.0f; makeControlOpen = false; typingParam.clear(); }

// ---------------------------------------------------------------- bindings
void App::bindSource(const std::string& param) {
    liveParam = param;
    auto& ab = shader->audioBindings();
    if (ab.count(param) && ab[param].signal != AudioSignal::None) liveSource = 0;
    else if (motion.count(param)) liveSource = 1;
    else liveSource = 0;
    openSheet(Sheet::Live);
}

static const char* signalName(AudioSignal s) {
    switch (s) { case AudioSignal::Level: return "level"; case AudioSignal::Bass: return "bass"; case AudioSignal::Mid: return "mid";
                 case AudioSignal::High: return "high"; case AudioSignal::Beat: return "beat"; default: return "off"; }
}

void App::setAudioBind(const std::string& param, AudioSignal sig, float reach, float smooth, int shape) {
    if (!shader) return;
    motion.erase(param);
    const ISFInput* in = nullptr;
    for (auto& i : shader->inputs()) if (i.name == param) in = &i;
    if (!in) return;
    float cur = std::holds_alternative<float>(in->value) ? std::get<float>(in->value) : in->minVal;
    AudioBinding& b = shader->audioBindings()[param];
    bool fresh = b.signal == AudioSignal::None;
    b.signal = sig;
    b.rangeMin = cur;
    b.rangeMax = std::min(in->maxVal, cur + reach * (in->maxVal - in->minVal));
    b.smoothing = smooth;
    // shape: 0 Follow 1 Ease 2 Pulse 3 Gate (character runs smooth → punchy)
    static const float kChar[4] = {-1.0f, -0.4f, 0.5f, 1.0f};
    b.character = kChar[std::clamp(shape, 0, 3)];
    if (fresh) { b.hasSmoothed = false; b.rampAge = 0; }
    easel.pushAudioBind(param, signalName(sig), reach, smooth);
}

void App::setMotionBind(const std::string& param, const MotionBinding& mb) {
    if (!shader) return;
    shader->audioBindings().erase(param);
    MotionBinding m = mb;
    for (auto& i : shader->inputs()) if (i.name == param && std::holds_alternative<float>(i.value) && !motion.count(param)) m.base = std::get<float>(i.value);
    if (motion.count(param)) m.base = motion[param].base;
    motion[param] = m;
}

void App::unbind(const std::string& param) {
    if (!shader) return;
    shader->audioBindings().erase(param);
    if (motion.count(param)) { shader->setFloat(param, motion[param].base); motion.erase(param); }
    easel.pushAudioBind(param, "off", 0, 0.85f);
}

void App::updateBindings(float dt) {
    if (!shader) return;
    const auto& f = audio.features();
    shader->setAudioFeatures(f);
    shader->applyAudioBindings(audio.level(), audio.bass(), audio.mid(), audio.high(), audio.beat(), dt, nullptr);
    for (auto& [name, m] : motion) {
        const ISFInput* in = nullptr;
        for (auto& i : shader->inputs()) if (i.name == name) in = &i;
        if (!in) continue;
        float l = m.lfo.step(paused ? 0.0f : dt, audio.bpm());
        float target = m.base + l * m.reach * (in->maxVal - in->minVal);
        target = std::clamp(target, in->minVal, in->maxVal);
        if (!m.has) { m.smoothed = target; m.has = true; }
        float a = 1.0f - std::pow(m.smooth, dt * 60.0f);
        m.smoothed += (target - m.smoothed) * std::clamp(a, 0.0f, 1.0f);
        shader->setFloat(name, m.smoothed);
    }
}

void App::paramChanged(const ISFInput& in) {
    if (!easel.connected()) return;
    if (std::holds_alternative<float>(in.value)) easel.pushParam(in.name, std::get<float>(in.value));
    else if (std::holds_alternative<bool>(in.value)) easel.pushParam(in.name, std::get<bool>(in.value));
    else if (std::holds_alternative<glm::vec4>(in.value)) { auto c = std::get<glm::vec4>(in.value); easel.pushColor(in.name, c.r, c.g, c.b, c.a); }
}

// ---------------------------------------------------------------- make control
bool App::makeControl(const std::string& name, float lo, float hi, float def, int selA, int selB) {
    if (!shader || selA == selB) return false;
    if (selA > selB) std::swap(selA, selB);
    if (selB > (int)codeBuffer.size()) return false;
    size_t ha = codeBuffer.find("/*"), hb = codeBuffer.find("*/");
    if (ha == std::string::npos || hb == std::string::npos || (size_t)selA < hb) return false;
    ojson header;
    try { header = ojson::parse(codeBuffer.substr(ha + 2, hb - ha - 2)); } catch (...) { compileStatus = "header is not valid JSON"; compileOk = false; return false; }
    if (!header.contains("INPUTS") || !header["INPUTS"].is_array()) header["INPUTS"] = ojson::array();
    for (auto& i : header["INPUTS"]) if (i.value("NAME", "") == name) { compileStatus = "a control named " + name + " exists"; compileOk = false; return false; }
    std::string label = name; if (!label.empty()) label[0] = (char)std::toupper(label[0]);
    for (size_t i = 1; i < label.size(); ++i) if (std::isupper((unsigned char)label[i]) && std::islower((unsigned char)label[i - 1])) { label.insert(i, " "); ++i; }
    ojson input = {{"NAME", name}, {"LABEL", label}, {"TYPE", "float"}, {"GROUP", "Look"}, {"DEFAULT", def}, {"MIN", lo}, {"MAX", hi}};
    header["INPUTS"].push_back(input);
    std::string newHeader = "/*" + header.dump(2) + "*/";
    std::string body = codeBuffer.substr(hb + 2);
    int bodyA = selA - (int)(hb + 2), bodyB = selB - (int)(hb + 2);
    body.replace(bodyA, bodyB - bodyA, name);
    codeBuffer = newHeader + body;
    codeDirty = true;
    recompile();
    if (compileOk) showToast(label + " is now a control");
    return compileOk;
}

// ---------------------------------------------------------------- publish / export
bool App::publish() {
    if (!shader) return false;
    static const char* kPacks[] = {"mine", "toybox", "artsy", "3d", "fluid", "mono", "music"};
    PublishRequest req;
    req.title = pubName;
    req.stem = slugify(req.title);
    req.description = shaderTitle + " variation made in Palette";
    req.categories = {shaderKind == "Effect" ? "Effect" : "Generator", "Audio Reactive"};
    if (shaderKind == "3D") req.categories.push_back("3D");
    if (shaderKind == "Simulation") req.categories.push_back("Simulation");
    req.pack = kPacks[std::clamp(pubPack, 0, 6)];
    req.basedOnId = shaderId;
    req.hidden = pubHidden;
    req.isfSource = codeBuffer;
    std::string tmpPng = (fs::path(homeDir()) / ".palette" / "publish_thumb.png").string();
    if (Thumbs::renderPng(*shader, tmpPng, 640, 400)) req.thumbnailPng = tmpPng;
    PublishResult r = publishToLibrary(library, req);
    if (!r.ok) { showToast(r.error); return false; }
    library.refresh();
    showToast("Published " + std::to_string(r.id) + " · " + fs::path(r.path).filename().string());
    if (pubShowOnEasel && easel.connected()) easel.showShader(r.path);
    // the document is now that file
    loadShader(r.path, req.title, r.id);
    closeSheet();
    return true;
}

static std::string stamp() { std::time_t t = std::time(nullptr); char b[32]; std::strftime(b, sizeof b, "%Y-%m-%d %H.%M.%S", std::localtime(&t)); return b; }

bool App::exportStill() {
    if (!shader) return false;
    std::string out = homeDir() + "/Desktop/Palette " + shaderTitle + " " + stamp() + ".png";
    bool ok = Thumbs::renderPng(*shader, out, 1920, 1080);
    showToast(ok ? "Saved still to Desktop" : "Could not save still");
    return ok;
}
bool App::exportFs() {
    if (!shader) return false;
    std::string out = homeDir() + "/Desktop/" + slugify(shaderTitle) + ".fs";
    std::ofstream f(out, std::ios::binary); if (!f) { showToast("Could not write .fs"); return false; }
    f << codeBuffer; showToast("Saved .fs to Desktop"); return true;
}

std::vector<std::string> App::groups() const {
    std::vector<std::string> g;
    if (!shader) return g;
    for (auto& in : shader->inputs()) {
        if (in.type == "image") continue;
        std::string gr = in.group.empty() ? "General" : in.group;
        if (gr == "Motion") continue;
        if (std::find(g.begin(), g.end(), gr) == g.end()) g.push_back(gr);
    }
    g.push_back("Motion");
    g.push_back("All");
    return g;
}

// ---------------------------------------------------------------- events / frame
void App::handleEvent(const SDL_Event& e) {
    if (e.type == SDL_EVENT_DROP_FILE && e.drop.data) pendingFile = e.drop.data;
}

void App::frame(float dt) {
    ImGuiIO& io = ImGui::GetIO();
    if (std::getenv("PALETTE_DEBUG")) { for (int b = 0; b < 3; ++b) if (io.MouseClicked[b]) std::fprintf(stderr, "[dbg] click b%d at %.0f,%.0f src=%d frame=%d\n", b, io.MousePos.x, io.MousePos.y, (int)io.MouseSource, ImGui::GetFrameCount()); if (io.MouseDown[0] && ImGui::GetFrameCount() < 5) std::fprintf(stderr, "[dbg] mouse down at startup frame %d\n", ImGui::GetFrameCount()); }
    ds = io.DisplaySize;
    phone = ds.x < 600;
    frameMs = dt * 1000.0; fps = fps * 0.9 + (dt > 0 ? 1.0 / dt : 0) * 0.1;
    sheetAnim.step(dt);
    if (sheetAnim.settled() && sheet == Sheet::None) closing = Sheet::None;
    if (toastT > 0) toastT -= dt;

    if (!pendingFile.empty()) {
        std::string p = pendingFile; pendingFile.clear();
        std::string ext = fs::path(p).extension().string();
        if (ext == ".fs") loadShader(p, fs::path(p).stem().string());
        else bindImage(p);
    }

    // audio + bindings + render
    audio.update(dt);
    if (shader) {
        int pw = (int)(ds.x * dpi * kTierScale[tier]), ph = (int)(ds.y * dpi * kTierScale[tier]);
        if (pw > 0 && ph > 0 && (shader->width() != pw || shader->height() != ph)) shader->setResolution(pw, ph);
        shader->setPaused(paused);
        bool inCanvas = !io.WantCaptureMouse;
        shader->setMouseState(io.MousePos.x / ds.x, 1.0f - io.MousePos.y / ds.y, inCanvas && io.MouseDown[0]);
        updateBindings(dt);
        shader->update();
    }
    thumbs.update(sheet == Sheet::Shader ? 2 : 1);
    renderScene();

    // keyboard
    if (!io.WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) { if (moreOpen) moreOpen = false; else closeSheet(); }
        if (ImGui::IsKeyPressed(ImGuiKey_Space)) paused = !paused;
        if (ImGui::IsKeyPressed(ImGuiKey_1)) openSheet(Sheet::Source);
        if (ImGui::IsKeyPressed(ImGuiKey_2)) openSheet(Sheet::Shader);
        if (ImGui::IsKeyPressed(ImGuiKey_3)) openSheet(Sheet::Controls);
        if (ImGui::IsKeyPressed(ImGuiKey_4)) openSheet(Sheet::Live);
        if (ImGui::IsKeyPressed(ImGuiKey_5)) openSheet(Sheet::Publish);
        if (ImGui::IsKeyPressed(ImGuiKey_C) && (io.KeySuper || io.KeyCtrl) == false && io.KeyAlt) openSheet(Sheet::Code);
        if (ImGui::IsKeyPressed(ImGuiKey_E) && (io.KeySuper || io.KeyCtrl)) exportStill();
        if (ImGui::IsKeyPressed(ImGuiKey_L) && (io.KeySuper || io.KeyCtrl)) openSheet(Sheet::Connect);
        if (ImGui::IsKeyPressed(ImGuiKey_Comma) && (io.KeySuper || io.KeyCtrl)) moreOpen = !moreOpen;
    }
    if ((io.KeySuper || io.KeyCtrl) && ImGui::IsKeyPressed(ImGuiKey_Enter) && codeDirty) recompile();

    // UI
    auto* bg = ImGui::GetBackgroundDrawList();
    if (m_scene.textureId()) bg->AddImage((ImTextureID)(intptr_t)m_scene.textureId(), ImVec2(0, 0), ds, ImVec2(0, 1), ImVec2(1, 0));
    else bg->AddRectFilled(ImVec2(0, 0), ds, IM_COL32(9, 9, 10, 255));
    drawChrome();
    drawSheet();
    drawDock();
    drawMore();
    drawToast();
}

void App::renderScene() {
    int fbw, fbh; SDL_GetWindowSizeInPixels(window, &fbw, &fbh);
    if (fbw <= 0 || fbh <= 0) return;
    if (m_scene.width() != fbw || m_scene.height() != fbh) { m_scene.destroy(); m_scene.create(fbw, fbh); }
    m_scene.bind();
    glViewport(0, 0, fbw, fbh);
    glDisable(GL_BLEND);
    glClearColor(0.035f, 0.035f, 0.04f, 1); glClear(GL_COLOR_BUFFER_BIT);
    if (shader && shader->textureId()) {
        m_blit.use();
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, shader->textureId());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        m_blit.setInt("uTex", 0);
        m_blit.setFloat("uDim", (sheet == Sheet::Publish || closing == Sheet::Publish) ? 1.0f - 0.45f * sheetAnim.v : 1.0f);
        glBindVertexArray(m_vao); glDrawArrays(GL_TRIANGLES, 0, 3); glBindVertexArray(0);
    }
    Framebuffer::unbind();
    glass.update(m_scene.textureId(), fbw, fbh);
    glEnable(GL_BLEND);
}

void App::render() {
    int fbw, fbh; SDL_GetWindowSizeInPixels(window, &fbw, &fbh);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, fbw, fbh);
    glClearColor(0.035f, 0.035f, 0.04f, 1); glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

} // namespace palette

// ---------------------------------------------------------------- self test (PALETTE_TEST=makecontrol|publish)
#include <regex>
namespace palette {
void App::runSelfTest(const std::string& which) {
    if (!shader) { std::fprintf(stderr, "TEST %s: no shader loaded\n", which.c_str()); return; }
    if (which == "makecontrol") {
        size_t hb = codeBuffer.find("*/");
        std::regex num(R"(\b(\d+\.\d+)\b)");
        std::smatch m; std::string body = codeBuffer.substr(hb + 2);
        if (!std::regex_search(body, m, num)) { std::fprintf(stderr, "TEST makecontrol: no literal found\n"); return; }
        int a = (int)(hb + 2 + m.position(1)), b = a + (int)m.length(1);
        float val = (float)std::atof(m.str(1).c_str());
        size_t before = shader->inputs().size();
        bool ok = makeControl("testAmount", 0.0f, std::max(1.0f, val * 3), val, a, b);
        bool has = false; for (auto& in : shader->inputs()) if (in.name == "testAmount") has = true;
        std::fprintf(stderr, "TEST makecontrol: literal=%s ok=%d compileOk=%d inputs %zu->%zu hasControl=%d status=%s\n", m.str(1).c_str(), ok, compileOk, before, shader->inputs().size(), has, compileStatus.c_str());
        std::fprintf(stderr, "TEST makecontrol: body now contains 'testAmount' at %d: %s\n", (int)codeBuffer.find("testAmount", hb), codeBuffer.find("testAmount", hb) != std::string::npos ? "yes" : "no");
    } else if (which == "publish") {
        std::snprintf(pubName, sizeof pubName, "Palette Test Shader");
        pubPack = 0; pubShowOnEasel = false;
        int before = library.nextFreeId();
        bool ok = publish();
        auto* e = library.find("palette_test_shader");
        std::fprintf(stderr, "TEST publish: ok=%d nextIdBefore=%d found=%d id=%d path=%s toast=%s\n", ok, before, e != nullptr, e ? e->id : -1, e ? e->path.c_str() : "", toast.c_str());
    }
}
}
