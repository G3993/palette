#pragma once
#include <glad/glad.h>
#include <SDL3/SDL.h>
#include <imgui.h>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <nlohmann/json.hpp>

#include "sources/ShaderSource.h"
#include "render/Framebuffer.h"
#include "render/ShaderProgram.h"
#include "library/Library.h"
#include "library/Thumbs.h"
#include "live/Audio.h"
#include "live/Lfo.h"
#include "companion/EaselLink.h"
#include "ui/Glass.h"
#include "ui/Widgets.h"

namespace palette {

enum class Sheet { None, Source, Shader, Controls, Live, Publish, Code, Quality, Connect, Export };

// A parameter driven by a Motion source (LFO). Audio-driven parameters live in
// ShaderSource::audioBindings() so they publish as Easel SOUND presets.
struct MotionBinding {
    live::Lfo lfo;
    float reach = 0.5f;     // 0..1 of the parameter's range, pushed up from the base value
    float base = 0.0f;      // value when the binding was made
    float smooth = 0.85f;
    float smoothed = 0.0f; bool has = false;
};

class App {
public:
    bool init(SDL_Window* window, SDL_GLContext gl, const std::string& openPath);
    void handleEvent(const SDL_Event& e);
    void frame(float dt);       // update + draw UI (inside ImGui frame)
    void render();              // GL composite to the default framebuffer
    void shutdown();
    bool quit = false;

    // ---- state (shared with Sheets.cpp) ----
    SDL_Window* window = nullptr;
    float dpi = 1.0f;
    ImVec2 ds;                  // display size in points
    bool phone = false;         // narrow layout

    Library library;
    Thumbs thumbs;
    ui::Glass glass;
    live::Audio audio;
    companion::EaselLink easel;

    std::shared_ptr<ShaderSource> shader;
    std::string shaderPath, shaderTitle = "Palette", shaderKind;
    int shaderId = -1;
    std::string codeBuffer; bool codeDirty = false;
    bool compileOk = true; std::string compileStatus = "no shader"; double compileMs = 0;
    std::string sourceLabel = "None", sourcePath;
    std::map<std::string, std::string> imagePaths;   // image input name -> file (kept across shaders)
    std::string imageTarget;                          // which image input the next picked file binds to
    // add-control form
    bool addControlOpen = false; char acName[48] = ""; int acType = 0; float acLo = 0, acHi = 1, acDef = 0.5f; char acGroup[32] = "Look";
    std::map<std::string, MotionBinding> motion;

    Sheet sheet = Sheet::None, closing = Sheet::None;
    ui::Spring sheetAnim;       // 0 closed → 1 open
    bool moreOpen = false, paused = false;
    int tier = 3;               // Eco Balanced High Ultra
    int controlsGroup = 0;
    std::string liveParam;      // the parameter the Live sheet edits
    int liveSource = 0;         // 0 Sound 1 Motion 2 Off
    char search[96] = ""; int shaderFilter = 0;
    std::string pendingFile;    // from SDL dialog / drop
    double frameMs = 0, fps = 0;

    // publish / export / connect forms
    char pubName[96] = ""; int pubPack = 0; bool pubShowOnEasel = true, pubHidden = false;
    char connZone[32] = "Main"; char connSlot[32] = "palette:1";
    std::string toast; float toastT = 0;

    // code sheet
    int codePass = 0; std::vector<std::string> passNames;
    bool makeControlOpen = false; char mcName[48] = ""; float mcLo = 0, mcHi = 1, mcDefault = 0; int mcSelA = 0, mcSelB = 0;
    std::string typingParam; char typingBuf[32] = "";

    // ---- actions ----
    bool loadShader(const std::string& path, const std::string& title, int id = -1);
    void recompile();
    void openSheet(Sheet s);
    void closeSheet();
    void setTier(int t);
    void bindSource(const std::string& param);                 // open Live for param
    void setAudioBind(const std::string& param, AudioSignal sig, float reach, float smooth, int shape);
    void setMotionBind(const std::string& param, const MotionBinding& mb);
    void unbind(const std::string& param);
    void paramChanged(const ISFInput& in);                     // push to Easel
    bool makeControl(const std::string& name, float lo, float hi, float def, int selA, int selB);
    bool publish();
    bool exportStill();
    bool exportFs();
    void showToast(const std::string& t) { toast = t; toastT = 2.6f; }
    void bindImage(const std::string& path);
    std::vector<std::string> imageInputs() const;            // names of image INPUTS
    bool rewriteHeader(const std::function<bool(nlohmann::ordered_json&)>& edit, const std::string& bodyEdit = ""); // header JSON edit + optional new body, then recompile
    bool addControl(const std::string& name, const std::string& type, float lo, float hi, float def, const std::string& group);
    bool addTextureSupport();                                 // inputImage + Texture group, blended into the last pass
    std::vector<std::string> groups() const;
    void runSelfTest(const std::string& which);

private:
    Framebuffer m_scene; ShaderProgram m_blit; GLuint m_vao = 0;
    SDL_GLContext m_gl = nullptr;
    void updateBindings(float dt);
    void renderScene();
    // UI (Sheets.cpp)
    void drawChrome();
    void drawDock();
    void drawSheet();
    void drawMore();
    void drawToast();
    void sheetControls(); void sheetLive(); void sheetShader(); void sheetSource();
    void sheetPublish(); void sheetCode(); void sheetQuality(); void sheetConnect(); void sheetExport();
};

} // namespace palette
