#include "sources/ShaderSource.h"
#include "app/MIDIManager.h"
#include "core/Clock.h"
#include "render/FontAtlas.h"
#include "render/Texture.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <iostream>
#include <regex>
#include <cmath>
#include <algorithm>
#include <functional>

using json = nlohmann::json;

// --- ISF JSON Parsing ---

// Helper: get a bool from JSON that might be bool, int, or string
static bool jsonToBool(const json& j, bool fallback) {
    if (j.is_boolean()) return j.get<bool>();
    if (j.is_number()) return j.get<int>() != 0;
    if (j.is_string()) return j.get<std::string>() == "true" || j.get<std::string>() == "1";
    return fallback;
}

// Helper: get a float from JSON that might be float, int, or string
static float jsonToFloat(const json& j, float fallback) {
    if (j.is_number()) return j.get<float>();
    if (j.is_string()) {
        try { return std::stof(j.get<std::string>()); } catch (...) {}
    }
    return fallback;
}

bool ShaderSource::parseISF(const std::string& source) {
    // ISF shaders have a JSON block inside /* { ... } */
    auto start = source.find("/*");
    if (start == std::string::npos) return false;

    // Find the opening brace within the comment
    auto jsonStart = source.find('{', start);
    if (jsonStart == std::string::npos) return false;

    // Find the closing */ to bound our search
    auto commentEnd = source.find("*/", start);
    if (commentEnd == std::string::npos) return false;

    // Find matching closing brace (handle nested braces) within the comment
    int depth = 0;
    size_t jsonEnd = std::string::npos;
    for (size_t i = jsonStart; i < commentEnd; i++) {
        if (source[i] == '{') depth++;
        else if (source[i] == '}') {
            depth--;
            if (depth == 0) {
                jsonEnd = i;
                break;
            }
        }
    }
    if (jsonEnd == std::string::npos) return false;

    std::string jsonStr = source.substr(jsonStart, jsonEnd - jsonStart + 1);

    try {
        json j = json::parse(jsonStr);

        m_description = j.value("DESCRIPTION", "");
        m_credit = j.value("CREDIT", "");

        m_inputs.clear();
        if (j.contains("INPUTS") && j["INPUTS"].is_array()) {
            for (const auto& input : j["INPUTS"]) {
                ISFInput param;
                param.name  = input.value("NAME", "");
                param.type  = input.value("TYPE", "float");
                param.label = input.value("LABEL", "");
                param.group = input.value("GROUP", "");

                if (param.type == "float") {
                    param.minVal = input.contains("MIN") ? jsonToFloat(input["MIN"], 0.0f) : 0.0f;
                    param.maxVal = input.contains("MAX") ? jsonToFloat(input["MAX"], 1.0f) : 1.0f;
                    param.defaultFloat = input.contains("DEFAULT") ? jsonToFloat(input["DEFAULT"], 0.5f) : 0.5f;
                    param.value = param.defaultFloat;
                } else if (param.type == "color") {
                    param.defaultColor = {1.0f, 1.0f, 1.0f, 1.0f};
                    if (input.contains("DEFAULT") && input["DEFAULT"].is_array()) {
                        auto& def = input["DEFAULT"];
                        if (def.size() >= 4) {
                            param.defaultColor = {
                                def[0].get<float>(), def[1].get<float>(),
                                def[2].get<float>(), def[3].get<float>()
                            };
                        } else if (def.size() >= 3) {
                            param.defaultColor = {
                                def[0].get<float>(), def[1].get<float>(),
                                def[2].get<float>(), 1.0f
                            };
                        }
                    }
                    param.value = param.defaultColor;
                } else if (param.type == "bool") {
                    param.defaultBool = input.contains("DEFAULT") ? jsonToBool(input["DEFAULT"], false) : false;
                    param.value = param.defaultBool;
                } else if (param.type == "point2D") {
                    param.defaultVec = {0.5f, 0.5f};
                    param.minVec = {0.0f, 0.0f};
                    param.maxVec = {1.0f, 1.0f};
                    if (input.contains("DEFAULT") && input["DEFAULT"].is_array()) {
                        auto& def = input["DEFAULT"];
                        if (def.size() >= 2) {
                            param.defaultVec = {def[0].get<float>(), def[1].get<float>()};
                        }
                    }
                    if (input.contains("MIN") && input["MIN"].is_array()) {
                        auto& m = input["MIN"];
                        if (m.size() >= 2) param.minVec = {m[0].get<float>(), m[1].get<float>()};
                    }
                    if (input.contains("MAX") && input["MAX"].is_array()) {
                        auto& m = input["MAX"];
                        if (m.size() >= 2) param.maxVec = {m[0].get<float>(), m[1].get<float>()};
                    }
                    param.value = param.defaultVec;
                } else if (param.type == "long") {
                    // Enumeration type — represented as int with VALUES array
                    param.type = "long";
                    param.minVal = 0;
                    param.maxVal = 0;
                    if (input.contains("VALUES") && input["VALUES"].is_array()) {
                        for (const auto& v : input["VALUES"]) {
                            if (v.is_number()) param.longValues.push_back(v.get<int>());
                        }
                        if (!param.longValues.empty())
                            param.maxVal = (float)(param.longValues.size() - 1);
                    }
                    param.defaultFloat = input.contains("DEFAULT") ? jsonToFloat(input["DEFAULT"], 0.0f) : 0.0f;
                    param.value = param.defaultFloat;
                    if (input.contains("LABELS") && input["LABELS"].is_array()) {
                        for (const auto& l : input["LABELS"]) {
                            param.longLabels.push_back(l.get<std::string>());
                        }
                    }
                } else if (param.type == "event") {
                    // Event trigger — behaves like a momentary bool
                    param.defaultBool = false;
                    param.value = false;
                    // Optional ISF fields used by PropertyPanel's hit-button:
                    //   MOMENTARY: press-and-hold mode (uniform mirrors held).
                    //   TARGET:    on tap, randomize this sibling float param.
                    param.momentary   = input.value("MOMENTARY", false);
                    param.eventTarget = input.value("TARGET",    std::string(""));
                } else if (param.type == "text") {
                    // Text input — Shader-Claw compiles to NAME_0..NAME_N + NAME_len uniforms
                    // Store default text and max length
                    if (input.contains("MAX_LENGTH") && input["MAX_LENGTH"].is_number()) {
                        param.maxVal = input["MAX_LENGTH"].get<float>();
                    } else {
                        param.maxVal = 12.0f;
                    }
                    param.defaultText = input.value("DEFAULT", "");
                    // Uppercase the default text
                    for (auto& c : param.defaultText) c = (char)toupper((unsigned char)c);
                    param.value = param.defaultText;
                } else if (param.type == "image") {
                    // Image input — texture sampler, stub for generators
                    param.value = false; // placeholder
                }

                if (!param.name.empty()) {
                    m_inputs.push_back(std::move(param));
                }
            }
        }

        appendMotionKitInputs();

        // Parse PASSES — multi-pass buffer names
        m_passBuffers.clear();
        if (j.contains("PASSES") && j["PASSES"].is_array()) {
            for (const auto& pass : j["PASSES"]) {
                if (pass.contains("TARGET") && pass["TARGET"].is_string()) {
                    m_passBuffers.push_back(pass["TARGET"].get<std::string>());
                }
            }
        }
    } catch (const json::exception& e) {
        std::cerr << "ISF parse error: " << e.what() << std::endl;
        return false;
    }

    return true;
}

// --- ISF -> GLSL 330 core translation ---

std::string ShaderSource::translateFragment(const std::string& isfBody) {
    // Strip the ISF JSON comment block
    std::string body = isfBody;
    auto commentStart = body.find("/*");
    if (commentStart != std::string::npos) {
        auto commentEnd = body.find("*/", commentStart);
        if (commentEnd != std::string::npos) {
            body = body.substr(commentEnd + 2);
        }
    }

    // Strip any embedded #version directive (we provide our own)
    std::regex versionRe(R"(#version\s+\d+[^\n]*)");
    body = std::regex_replace(body, versionRe, "// (version stripped by Easel)");

    // Strip precision qualifiers (GLSL ES, not valid in 330 core)
    body = std::regex_replace(body, std::regex(R"(precision\s+(highp|mediump|lowp)\s+\w+\s*;)"), "// (precision stripped)");

    // Replace legacy GLSL keywords for 330 core compatibility
    // varying -> in (for fragment shader)
    body = std::regex_replace(body, std::regex(R"(\bvarying\b)"), "in");
    // texture2D -> texture
    body = std::regex_replace(body, std::regex(R"(\btexture2D\b)"), "texture");

    // Route every sampling of a top-down image input (NDI, Video, etc.) through
    // a per-input wrapper that flips v when _flip_<name> is true. Without this
    // the _flip_<name> uniform is set but never consumed and the source ends up
    // upside down inside the shader.
    for (const auto& input : m_inputs) {
        if (input.type != "image") continue;
        const std::string& n = input.name;
        body = std::regex_replace(body,
            std::regex("\\btexture\\s*\\(\\s*" + n + "\\s*,"),
            "_flippedTex_" + n + "(");
        body = std::regex_replace(body,
            std::regex("\\bIMG_NORM_PIXEL\\s*\\(\\s*" + n + "\\s*,"),
            "_flippedTex_" + n + "(");
        body = std::regex_replace(body,
            std::regex("\\bIMG_PIXEL\\s*\\(\\s*" + n + "\\s*,"),
            "_flippedTexPixel_" + n + "(");
        body = std::regex_replace(body,
            std::regex("\\bIMG_THIS_PIXEL\\s*\\(\\s*" + n + "\\s*\\)"),
            "_flippedTexThis_" + n + "()");
        body = std::regex_replace(body,
            std::regex("\\bIMG_THIS_NORM_PIXEL\\s*\\(\\s*" + n + "\\s*\\)"),
            "_flippedTexThisNorm_" + n + "()");
    }

    std::stringstream out;
    out << "#version 330 core\n";
    out << "out vec4 FragColor;\n";
    out << "in vec2 isf_FragNormCoord;\n";
    out << "\n";

    // ISF built-in uniforms
    out << "uniform float TIME;\n";
    out << "uniform float TIMEDELTA;\n";
    out << "uniform vec2 RENDERSIZE;\n";
    out << "uniform int PASSINDEX;\n";
    out << "uniform int FRAMEINDEX;\n";
    out << "uniform vec2 mousePos;\n";
    out << "uniform vec2 mouseDelta;\n";
    out << "uniform float pinchHold;\n";
    out << "uniform float msgAge;\n";
    out << "\n";

    // Mouse interaction (for painting shaders)
    out << "uniform float mouseDown;\n";
    out << "\n";

    // Phase V scaffolding — MediaPipe-compatible landmark samplers so
    // shaders authored against ShaderClaw3 web (which feeds these via
    // browser MediaPipe) compile in Easel. VisionSource will populate
    // these in V1.1; until then they sample whatever texture is bound
    // to unit 0 (typically zero — shaders check the alpha channel as
    // confidence before drawing pose-driven elements).
    out << "uniform sampler2D mpPoseLandmarks;\n";
    out << "uniform sampler2D mpFaceLandmarks;\n";
    out << "uniform sampler2D mpHandLandmarks;\n";
    out << "uniform sampler2D mpSegmentation;\n";
    out << "\n";

    // ISF image input stubs — provide samplers + IMG_SIZE + flip uniforms
    bool hasImageInputs = false;
    for (const auto& input : m_inputs) {
        if (input.type == "image") {
            hasImageInputs = true;
            out << "uniform sampler2D " << input.name << ";\n";
            out << "uniform vec2 IMG_SIZE_" << input.name << ";\n";
            out << "uniform bool _flip_" << input.name << ";\n";
        }
    }

    // Multi-pass buffer textures
    for (const auto& buf : m_passBuffers) {
        out << "uniform sampler2D " << buf << ";\n";
        hasImageInputs = true;
    }

    // Shader-Claw font atlas (dummy sampler for text shaders)
    if (isfBody.find("fontAtlasTex") != std::string::npos) {
        out << "uniform sampler2D fontAtlasTex;\n";
        hasImageInputs = true;
    }

    // Shader-Claw audio FFT sampler (always declared — bound by setAudioState)
    out << "uniform sampler2D audioFFT;\n";
    hasImageInputs = true;

    // Shader-Claw audio reactivity builtins (always declared)
    out << "uniform float audioLevel;\n";
    out << "uniform float audioBass;\n";
    out << "uniform float audioMid;\n";
    out << "uniform float audioHigh;\n";

    // --- Audio Feature Bus (always declared; see AudioFeatures.h) ----------
    // Tier 1 — core energy & bands
    out << "uniform float audioSub, audioLowMid, audioHighMid, audioTreble, audioPunch;\n";
    out << "uniform float audioBeat, audioBeatPhase, audioBeatPulse, audioBarPhase, audioBPM, audioTempo01;\n";
    // Tier 2 — spectral character
    out << "uniform float audioBrightness, audioSpread, audioRolloff, audioFlatness, audioTexture;\n";
    out << "uniform float audioFlux, audioOnset, audioOnsetRate, audioTilt, audioZCR;\n";
    // Tier 3 — affect / mood
    out << "uniform float audioValence, audioArousal, audioTension, audioWarmth, audioSoftness, audioRoughness, audioCharm;\n";
    out << "uniform vec2  audioMood;\n";
    // Tier 4 — structure / build-up
    out << "uniform float audioEnergy, audioEnergyVel, audioEnergyAcc, audioBuildup, audioBuildupRate, audioDrop;\n";
    out << "uniform float audioNovelty, audioSectionPhase, audioSectionAge, audioLayers, audioDensity;\n";
    out << "uniform vec4  audioPresence;\n";
    out << "uniform vec2  audioFlow;\n";
    // Tier 5 — palette anchors (linear RGB)
    out << "uniform vec3  audioPalShadow, audioPalMid, audioPalHigh, audioPalAccent;\n";
    out << "uniform float audioPalTemp, audioPalSat;\n";
    // Harmony scalars (chroma[] + chroma/occupancy textures land with the palette engine)
    out << "uniform float audioDominantPitch, audioMajorMinor, audioHCDF;\n";
    // ── EaselAudio v1 (spec/easel_audio_bus.json) ─────────────────────
    // Temperament matrix — Hit / Presence / Time per band + mix.
    // (audioLevelPresence: the schema's mix "audioPresence" name is already
    // taken by the vec4 per-band presence uniform above — nothing renamed.)
    out << "uniform float audioBassHit, audioMidHit, audioHighHit;\n";
    out << "uniform float audioBassPresence, audioMidPresence, audioHighPresence, audioLevelPresence;\n";
    out << "uniform float audioBassTime, audioMidTime, audioHighTime, audioTime;\n";
    // Rhythm bus — detected tempo confidence + multi-beat phase ramps + eased one-shots
    out << "uniform float audioBPMConfidence, audioPhase2, audioPhase4, audioPhase8, audioPhase16;\n";
    out << "uniform float audioOnBeat, audioToggleOnBeat;\n";
    // Tier-1 pseudo-stems (band split + causal median HPSS) + temperaments
    out << "uniform float stemBass, stemDrums, stemMelody, stemAir, stemVocal;\n";
    out << "uniform float stemBassHit, stemDrumsHit, stemMelodyHit, stemAirHit, stemVocalHit;\n";
    out << "uniform float stemBassPresence, stemDrumsPresence, stemMelodyPresence, stemAirPresence, stemVocalPresence;\n";

    // Shader-Claw voice reactivity builtins
    if (isfBody.find("_voiceGlitch") != std::string::npos) {
        out << "uniform float _voiceGlitch;\n";
    }
    out << "uniform float _voiceLevel;\n";

    // ISF texture sampling macros
    // IMG_SIZE returns the actual size of the named image (via IMG_SIZE_<name> uniform),
    // falling back to RENDERSIZE for pass buffers and unknown textures.
    // IMG_PIXEL uses the image's own size for pixel-coordinate lookups.
    out << "#define IMG_NORM_PIXEL(img, coord) texture(img, coord)\n";
    out << "#define IMG_THIS_NORM_PIXEL(img) texture(img, isf_FragNormCoord)\n";
    out << "#define IMG_THIS_PIXEL(img) texture(img, gl_FragCoord.xy / RENDERSIZE)\n";
    out << "\n";
    // Per-image IMG_SIZE and IMG_PIXEL overloads for each image input
    for (const auto& input : m_inputs) {
        if (input.type == "image") {
            // IMG_SIZE(inputName) -> IMG_SIZE_inputName (actual image dimensions)
            out << "vec2 _isf_img_size_" << input.name << "() { return IMG_SIZE_" << input.name << "; }\n";
            // IMG_PIXEL(inputName, coord) -> sample using image's own dimensions
            out << "vec4 _isf_img_pixel_" << input.name << "(vec2 coord) { return texture(" << input.name << ", coord / IMG_SIZE_" << input.name << "); }\n";
            // Flip-aware wrappers — the regex pass on the user body redirects
            // texture/IMG_*_PIXEL calls on this input to these helpers so
            // top-down NDI/Video sources read right-side-up inside the shader.
            const std::string& n = input.name;
            out << "vec4 _flippedTex_" << n << "(vec2 uv) { if (_flip_" << n
                << ") uv.y = 1.0 - uv.y; return texture(" << n << ", uv); }\n";
            out << "vec4 _flippedTexPixel_" << n << "(vec2 px) { return _flippedTex_"
                << n << "(px / IMG_SIZE_" << n << "); }\n";
            out << "vec4 _flippedTexThis_" << n << "() { return _flippedTex_"
                << n << "(gl_FragCoord.xy / RENDERSIZE); }\n";
            out << "vec4 _flippedTexThisNorm_" << n << "() { return _flippedTex_"
                << n << "(isf_FragNormCoord); }\n";
        }
    }
    // Default fallbacks for pass buffers and unknown textures
    out << "#define IMG_SIZE(img) RENDERSIZE\n";
    out << "#define IMG_PIXEL(img, coord) texture(img, (coord) / RENDERSIZE)\n";
    out << "\n";

    // Declare user ISF INPUTS as uniforms
    for (const auto& input : m_inputs) {
        if (input.type == "float") {
            out << "uniform float " << input.name << ";\n";
        } else if (input.type == "color") {
            out << "uniform vec4 " << input.name << ";\n";
        } else if (input.type == "bool" || input.type == "event") {
            out << "uniform bool " << input.name << ";\n";
        } else if (input.type == "point2D") {
            out << "uniform vec2 " << input.name << ";\n";
        } else if (input.type == "long") {
            out << "uniform int " << input.name << ";\n";
        } else if (input.type == "text") {
            // Text inputs in Shader-Claw compile to int arrays: name_0..name_N + name_len
            int maxLen = (int)input.maxVal;
            if (maxLen <= 0) maxLen = 12;
            out << "uniform int " << input.name << "_len;\n";
            for (int i = 0; i < maxLen; i++) {
                out << "uniform int " << input.name << "_" << i << ";\n";
            }
        }
        // image type already declared above
    }
    out << "\n";

    // Redirect gl_FragColor -> FragColor
    out << "#define gl_FragColor FragColor\n";
    out << "\n";

    // --- Audio Feature Bus helpers (auto-injected; see audio-reactive-system.md).
    // Shared idioms so shaders adopt the bus consistently. Names are audio-
    // prefixed to avoid colliding with the audioFFT sampler / user symbols.
    out << "vec3 audioPalette(float t){\n"
           "  t = clamp(t,0.0,1.0);\n"
           "  vec3 c = (t<0.5)? mix(audioPalShadow,audioPalMid,t*2.0)\n"
           "                  : mix(audioPalMid,audioPalHigh,t*2.0-1.0);\n"
           "  return mix(c, audioPalAccent, audioBeat*smoothstep(0.6,1.0,t)*0.6);\n"
           "}\n";
    out << "float audioSpectrum(float f){ return texture(audioFFT, vec2(clamp(f,0.0,1.0),0.5)).r; }\n";
    out << "float audioKick(){ return audioBeatPulse; }\n";      // bass transient
    out << "float audioHit(){ return audioOnset; }\n";           // broadband transient
    out << "float audioBreath(){ return pow(max(audioLevel,0.0),0.6); }\n"; // perceptual loudness
    out << "float audioAlive(float rest,float drive,float amount){ return rest+drive*amount; }\n"; // living baseline
    out << "\n";

    out << body << "\n";

    return out.str();
}

std::string ShaderSource::translateVertex(const std::string& isfBody) {
    // Strip ISF comment if present
    std::string body = isfBody;
    auto commentStart = body.find("/*");
    if (commentStart != std::string::npos) {
        auto commentEnd = body.find("*/", commentStart);
        if (commentEnd != std::string::npos) {
            body = body.substr(commentEnd + 2);
        }
    }

    // Strip embedded #version and legacy keywords
    std::regex versionRe(R"(#version\s+\d+[^\n]*)");
    body = std::regex_replace(body, versionRe, "// (version stripped by Easel)");
    body = std::regex_replace(body, std::regex(R"(\bvarying\b)"), "out");
    body = std::regex_replace(body, std::regex(R"(\battribute\b)"), "in");

    // Strip declarations that the preamble already provides (custom .vs files redeclare these)
    body = std::regex_replace(body, std::regex(R"(out\s+vec2\s+isf_FragNormCoord\s*;)"), "// (provided by Easel)");
    body = std::regex_replace(body, std::regex(R"(in\s+vec2\s+position\s*;)"), "// (provided by Easel)");

    std::stringstream out;
    out << "#version 330 core\n";
    out << "layout(location = 0) in vec2 aPos;\n";
    out << "layout(location = 1) in vec2 aTexCoord;\n";
    out << "out vec2 isf_FragNormCoord;\n";
    out << "#define position aPos\n";
    out << "\n";

    // Provide both naming conventions for the vertex init function
    out << "void isf_vertShaderInit() {\n";
    out << "    gl_Position = vec4(aPos, 0.0, 1.0);\n";
    out << "    isf_FragNormCoord = aTexCoord;\n";
    out << "}\n";
    out << "#define vv_vertShaderInit isf_vertShaderInit\n";
    out << "\n";
    out << body << "\n";

    return out.str();
}

std::string ShaderSource::generateDefaultVertex() {
    return
        "#version 330 core\n"
        "layout(location = 0) in vec2 aPos;\n"
        "layout(location = 1) in vec2 aTexCoord;\n"
        "out vec2 isf_FragNormCoord;\n"
        "\n"
        "void main() {\n"
        "    gl_Position = vec4(aPos, 0.0, 1.0);\n"
        "    isf_FragNormCoord = aTexCoord;\n"
        "}\n";
}

// --- Loading ---

bool ShaderSource::loadFromFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "Failed to open shader: " << path << std::endl;
        return false;
    }

    std::stringstream ss;
    ss << file.rdbuf();
    m_rawFragment = ss.str();
    m_path = path;

    // Check for paired .vs file
    std::string vsPath = path;
    if (vsPath.size() > 3 && vsPath.substr(vsPath.size() - 3) == ".fs") {
        vsPath = vsPath.substr(0, vsPath.size() - 3) + ".vs";
        std::ifstream vsFile(vsPath);
        if (vsFile.is_open()) {
            std::stringstream vss;
            vss << vsFile.rdbuf();
            m_rawVertex = vss.str();
        }
    }

    return loadFromCode(m_rawFragment);
}

// Tiny recursive-descent evaluator for ISF pass WIDTH/HEIGHT expressions:
// numbers, $WIDTH, $HEIGHT, $<float/long input>, + - * / ( ), and
// floor/ceil/round/min/max/abs/sqrt. Anything unparseable -> fallback.
float ShaderSource::evalSizeExpr(const std::string& expr, float fallback) const {
    if (expr.empty()) return fallback;
    size_t pos = 0;
    bool ok = true;
    auto skip = [&]() { while (pos < expr.size() && isspace((unsigned char)expr[pos])) pos++; };
    std::function<float()> parseSum;
    std::function<float()> parseAtom = [&]() -> float {
        skip();
        if (pos >= expr.size()) { ok = false; return 0.0f; }
        char c = expr[pos];
        if (c == '(') { pos++; float v = parseSum(); skip(); if (pos < expr.size() && expr[pos] == ')') pos++; else ok = false; return v; }
        if (c == '-') { pos++; return -parseAtom(); }
        if (c == '+') { pos++; return parseAtom(); }
        if (isdigit((unsigned char)c) || c == '.') {
            size_t used = 0;
            float v = 0.0f;
            try { v = std::stof(expr.substr(pos), &used); } catch (...) { ok = false; return 0.0f; }
            pos += used;
            return v;
        }
        bool isVar = (c == '$');
        if (isVar) pos++;
        size_t s = pos;
        while (pos < expr.size() && (isalnum((unsigned char)expr[pos]) || expr[pos] == '_')) pos++;
        std::string name = expr.substr(s, pos - s);
        if (name.empty()) { ok = false; return 0.0f; }
        if (isVar) {
            if (name == "WIDTH")  return (float)m_width;
            if (name == "HEIGHT") return (float)m_height;
            for (const auto& in : m_inputs) {
                if (in.name != name) continue;
                if (auto f = std::get_if<float>(&in.value)) return *f;
                if (auto b = std::get_if<bool>(&in.value)) return *b ? 1.0f : 0.0f;
            }
            ok = false;
            return 0.0f;
        }
        // function call
        skip();
        if (pos >= expr.size() || expr[pos] != '(') { ok = false; return 0.0f; }
        pos++;
        float a = parseSum(), b = 0.0f;
        skip();
        bool two = pos < expr.size() && expr[pos] == ',';
        if (two) { pos++; b = parseSum(); skip(); }
        if (pos < expr.size() && expr[pos] == ')') pos++; else ok = false;
        if (name == "floor") return std::floor(a);
        if (name == "ceil")  return std::ceil(a);
        if (name == "round") return std::round(a);
        if (name == "abs")   return std::fabs(a);
        if (name == "sqrt")  return std::sqrt(std::max(a, 0.0f));
        if (name == "min" && two) return std::min(a, b);
        if (name == "max" && two) return std::max(a, b);
        ok = false;
        return 0.0f;
    };
    auto parseProduct = [&]() -> float {
        float v = parseAtom();
        for (;;) {
            skip();
            if (pos < expr.size() && expr[pos] == '*') { pos++; v *= parseAtom(); }
            else if (pos < expr.size() && expr[pos] == '/') { pos++; float d = parseAtom(); v = (d != 0.0f) ? v / d : 0.0f; }
            else return v;
        }
    };
    parseSum = [&]() -> float {
        float v = parseProduct();
        for (;;) {
            skip();
            if (pos < expr.size() && expr[pos] == '+') { pos++; v += parseProduct(); }
            else if (pos < expr.size() && expr[pos] == '-') { pos++; v -= parseProduct(); }
            else return v;
        }
    };
    float v = parseSum();
    skip();
    if (!ok || pos != expr.size() || !std::isfinite(v)) return fallback;
    return v;
}

void ShaderSource::passSize(const ISFPass& pass, int& pw, int& ph) const {
    if (pass.widthExpr.empty() && pass.heightExpr.empty()) {
        // Auto-downscale simulation passes on large canvases
        // (fluid sim is low-frequency, half-res looks identical)
        pw = m_width;
        ph = m_height;
        if (m_width > 4096) {
            pw = std::max(1, m_width / 2);
            ph = std::max(1, m_height / 2);
        }
        return;
    }
    // Explicit sizes are clamped to the canvas so a runaway expression
    // can't allocate a giant half-float pair.
    pw = (int)std::lround(evalSizeExpr(pass.widthExpr, (float)m_width));
    ph = (int)std::lround(evalSizeExpr(pass.heightExpr, (float)m_height));
    pw = std::clamp(pw, 1, std::max(1, std::max(m_width, 4096)));
    ph = std::clamp(ph, 1, std::max(1, std::max(m_height, 4096)));
}

// Called each frame: a WIDTH/HEIGHT expression that references an input
// (e.g. a resolution slider) re-allocates its buffers when the value moves.
void ShaderSource::refreshPassSizes() {
    bool changed = false;
    for (auto& pass : m_passes) {
        if (!pass.ppFBO || (pass.widthExpr.empty() && pass.heightExpr.empty())) continue;
        int pw, ph;
        passSize(pass, pw, ph);
        if (pw == pass.simWidth && ph == pass.simHeight) continue;
        pass.simWidth = pw;
        pass.simHeight = ph;
        pass.ppFBO->a.resize(pw, ph);
        pass.ppFBO->b.resize(pw, ph);
        changed = true;
    }
    if (changed) m_frameIndex = 0; // re-seed persistent state at the new size
}

void ShaderSource::createPassFBOs() {
    m_passes.clear();
    m_frameIndex = 0; // reset frame counter so shader seeds correctly

    // Build ISFPass structs from parsed PASSES metadata
    // m_passBuffers has only the named targets; we also need the final pass (no target)
    // Re-parse from raw source to get full PASSES array including final empty pass
    auto start = m_rawFragment.find("/*");
    auto commentEnd = m_rawFragment.find("*/", start);
    auto jsonStart = m_rawFragment.find('{', start);
    if (jsonStart != std::string::npos && jsonStart < commentEnd) {
        int depth = 0;
        size_t jsonEnd = std::string::npos;
        for (size_t i = jsonStart; i < commentEnd; i++) {
            if (m_rawFragment[i] == '{') depth++;
            else if (m_rawFragment[i] == '}') { depth--; if (depth == 0) { jsonEnd = i; break; } }
        }
        if (jsonEnd != std::string::npos) {
            try {
                auto j = json::parse(m_rawFragment.substr(jsonStart, jsonEnd - jsonStart + 1));
                if (j.contains("PASSES") && j["PASSES"].is_array()) {
                    for (const auto& p : j["PASSES"]) {
                        ISFPass pass;
                        if (p.contains("TARGET") && p["TARGET"].is_string())
                            pass.target = p["TARGET"].get<std::string>();
                        if (p.contains("PERSISTENT") && p["PERSISTENT"].is_boolean())
                            pass.persistent = p["PERSISTENT"].get<bool>();
                        auto sizeExpr = [&](const char* key) -> std::string {
                            if (!p.contains(key)) return "";
                            if (p[key].is_string()) return p[key].get<std::string>();
                            if (p[key].is_number()) return std::to_string(p[key].get<double>());
                            return "";
                        };
                        if (p.contains("FLOAT") && p["FLOAT"].is_boolean())
                            pass.floatTarget = p["FLOAT"].get<bool>();
                        pass.widthExpr = sizeExpr("WIDTH");
                        pass.heightExpr = sizeExpr("HEIGHT");

                        if (!pass.target.empty()) {
                            int pw, ph;
                            passSize(pass, pw, ph);
                            pass.simWidth = pw;
                            pass.simHeight = ph;
                            pass.ppFBO = std::make_unique<PingPongFBO>();
                            pass.ppFBO->a.createHalfFloat(pw, ph, pass.floatTarget);
                            pass.ppFBO->b.createHalfFloat(pw, ph, pass.floatTarget);
                        }
                        m_passes.push_back(std::move(pass));
                    }
                }
            } catch (...) {}
        }
    }
}

bool ShaderSource::loadFromCode(const std::string& isfSource) {
    m_rawFragment = isfSource;

    if (!parseISF(isfSource)) {
        std::cerr << "Failed to parse ISF metadata" << std::endl;
        // Still try to compile - might be plain GLSL
    }

    std::string fragSrc = translateFragment(isfSource);
    std::string vertSrc;

    if (!m_rawVertex.empty()) {
        vertSrc = translateVertex(m_rawVertex);
    } else {
        vertSrc = generateDefaultVertex();
    }

    if (!m_shader.loadFromSource(vertSrc, fragSrc)) {
        m_lastError = m_shader.lastError();
        std::cerr << "Failed to compile ISF shader" << std::endl;
        return false;
    }

    if (!m_initialized) {
        if (!m_fbo.create(m_width, m_height)) {
            std::cerr << "Failed to create shader FBO" << std::endl;
            return false;
        }
        m_quad.createQuad();
        m_initialized = true;
    }

    // Create ping-pong FBOs for multi-pass shaders
    if (!m_passBuffers.empty()) {
        createPassFBOs();
    }

    return true;
}

bool ShaderSource::reload(const std::string& isfSource) {
    // Try to compile new source; keep old shader if it fails
    ShaderProgram newShader;
    std::string oldRawFrag = m_rawFragment;
    auto oldInputs = m_inputs;
    auto oldPassBuffers = m_passBuffers;

    m_rawFragment = isfSource;
    parseISF(isfSource);

    std::string fragSrc = translateFragment(isfSource);
    std::string vertSrc;
    if (!m_rawVertex.empty()) {
        vertSrc = translateVertex(m_rawVertex);
    } else {
        vertSrc = generateDefaultVertex();
    }

    if (!newShader.loadFromSource(vertSrc, fragSrc)) {
        // Restore old state. m_passBuffers must roll back too — the next
        // successful reload compares against it to decide whether the pass
        // FBOs need rebuilding, and the failed candidate's layout would
        // make that comparison lie.
        m_rawFragment = oldRawFrag;
        m_inputs = oldInputs;
        m_passBuffers = oldPassBuffers;
        std::cerr << "Shader reload failed, keeping previous version" << std::endl;
        return false;
    }

    // Transfer matching parameter values from old inputs
    for (auto& newInput : m_inputs) {
        for (const auto& oldInput : oldInputs) {
            if (newInput.name == oldInput.name && newInput.type == oldInput.type) {
                newInput.value = oldInput.value;
                break;
            }
        }
    }

    // Swap in the new shader (old one gets destroyed)
    m_shader.loadFromSource(vertSrc, fragSrc);

    // Rebuild the multi-pass ping-pong FBOs when the PASSES layout changed.
    // Without this, a hot-reload that dropped passes left their half-float
    // buffers resident AND still dispatched every frame, while added passes
    // had no storage at all. When the layout is unchanged (the common case —
    // ShaderImprover mandates pass-union preservation) the buffers are kept
    // so persistent-pass state survives the reload.
    if (m_passBuffers != oldPassBuffers) {
        createPassFBOs();
    }
    return true;
}

// --- Rendering ---

// Park the GPU storage while this source is reachable only from undo/redo
// snapshots. Called from UndoStack::suspendOrphanedSources on the main-thread
// frame sweep — the GL context is current there, so the deletes are safe.
// Drops the output FBO (~33 MB RGBA8 at 4K) plus every pass's ping-pong pair
// (2× half-float per pass — up to ~132 MB for multipass shaders at 4K).
// Persistent-pass state (fluid sims etc.) is lost; accepted for undo-orphaned
// sources — a restored shader re-seeds from frame 0.
void ShaderSource::suspend() {
    if (!m_initialized) return;
    m_fbo.destroy();
    if (m_motionFbo.width() > 0) m_motionFbo.destroy();
    m_motionActive = false;
    m_passes.clear();   // frees each pass's ping-pong Framebuffers
    m_initialized = false;
    m_suspended = true;
}

void ShaderSource::update() {
    if (!m_initialized) {
        // Lazy revive after suspend(): rebuild the FBO storage. m_width/
        // m_height still track the host's setResolution() calls while
        // suspended, so buffers come back at the current canvas size, not a
        // stale one. m_frameIndex resets so persistent passes re-seed.
        if (!m_suspended || m_width <= 0 || m_height <= 0 || m_shader.id() == 0) return;
        m_suspended = false;   // one attempt — no retry storm on failure
        if (!m_fbo.create(m_width, m_height)) return;
        if (!m_passBuffers.empty()) createPassFBOs();   // resets m_frameIndex
        m_frameIndex = 0;
        m_initialized = true;
    }

    m_shader.use();

    // Integrated clock — once per frame, not per pass. Speed (motionClock)
    // scales the step, so the phase stays continuous under a moving knob.
    {
        float now = (float)palette::clockSeconds();
        if (!m_clockInit) { m_clock = now; m_lastTime = now; m_clockInit = true; }
        float dtw = now - m_lastTime;
        if (!(dtw > 0.0f)) dtw = 0.0f;
        if (dtw > 0.1f) dtw = 0.1f;
        m_lastTime = now;
        m_clockDelta = m_paused ? 0.0f : dtw * kitValue("motionClock", 1.0f);
        m_clock += m_clockDelta;
    }

    if (m_passes.empty()) {
        // Single-pass shader (no PASSES in ISF header)
        m_fbo.bind();
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT);
        uploadUniforms(0, m_width, m_height);
        m_quad.draw();
        Framebuffer::unbind();
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, 0);
        glUseProgram(0);
    } else {
        refreshPassSizes();

        // Multi-pass rendering with ping-pong persistent buffers
        // Reserve texture units for pass buffers (start after audio=2, font=3)
        int targetBaseUnit = 4;

        for (int i = 0; i < (int)m_passes.size(); i++) {
            auto& pass = m_passes[i];
            bool isFinal = pass.target.empty();

            // Determine output FBO
            Framebuffer* outFBO;
            if (isFinal) {
                outFBO = &m_fbo;
            } else {
                outFBO = &pass.ppFBO->writeFBO();
            }

            // Bind output
            glBindFramebuffer(GL_FRAMEBUFFER, outFBO->fboId());
            glViewport(0, 0, outFBO->width(), outFBO->height());

            // Clear: skip for persistent passes, always clear final
            if (!pass.persistent || isFinal) {
                glClearColor(0, 0, 0, 0);
                glClear(GL_COLOR_BUFFER_BIT);
            }

            // Upload uniforms with current pass index and dimensions
            uploadUniforms(i, outFBO->width(), outFBO->height());

            // Bind all pass buffer textures (read side of ping-pong)
            int unit = targetBaseUnit;
            for (int pi = 0; pi < (int)m_passes.size(); pi++) {
                auto& p = m_passes[pi];
                if (p.target.empty() || !p.ppFBO) continue;

                glActiveTexture(GL_TEXTURE0 + unit);
                glBindTexture(GL_TEXTURE_2D, p.ppFBO->readFBO().textureId());
                m_shader.setInt(p.target, unit);
                unit++;
            }

            m_quad.draw();

            // Swap ping-pong after ANY targeted pass so subsequent passes (and
            // the final pass) read what this pass just wrote. Previously only
            // persistent passes swapped, so a plain intermediate TARGET buffer
            // stayed on the stale read side → later passes sampled an empty
            // buffer (the black-output bug that blocked multi-pass shaders).
            if (pass.ppFBO) {
                pass.ppFBO->swap();
            }
        }

        Framebuffer::unbind();
        glViewport(0, 0, m_width, m_height);
    }

    renderMotionPass();

    // Restore default GL state so other sources (NDI, etc.) aren't affected
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);

    m_frameIndex++;
}

// ── MOTION kit ────────────────────────────────────────────────────────
// Five float inputs every shader gets, placed FIRST so the SHOW surface's
// top knob row reads the same on every shader: Speed · Drift · Spin ·
// Breath · Sway. Defaults are identity (Speed 1, the rest 0), so a shader
// looks exactly as authored until a knob moves. A shader that declares one
// of these names itself keeps its own definition.
void ShaderSource::appendMotionKitInputs() {
    struct K { const char* name; const char* label; float lo, hi, def; };
    static const K kKit[] = {
        {"motionClock",  "Speed",  0.0f, 3.0f, 1.0f},
        {"motionDrift",  "Drift",  0.0f, 1.0f, 0.0f},
        {"motionSpin",   "Spin",  -1.0f, 1.0f, 0.0f},
        {"motionBreath", "Breath", 0.0f, 1.0f, 0.0f},
        {"motionSway",   "Sway",   0.0f, 1.0f, 0.0f},
    };
    std::vector<ISFInput> kit;
    for (const auto& k : kKit) {
        bool have = false;
        for (const auto& in : m_inputs) if (in.name == k.name) { have = true; break; }
        if (have) continue;
        ISFInput p;
        p.name = k.name; p.label = k.label; p.group = "Motion"; p.type = "float";
        p.minVal = k.lo; p.maxVal = k.hi; p.defaultFloat = k.def; p.value = k.def;
        kit.push_back(std::move(p));
    }
    m_inputs.insert(m_inputs.begin(), std::make_move_iterator(kit.begin()),
                    std::make_move_iterator(kit.end()));
}

float ShaderSource::kitValue(const char* name, float fallback) const {
    for (const auto& in : m_inputs) {
        if (in.name != name) continue;
        if (std::holds_alternative<float>(in.value)) return std::get<float>(in.value);
        return fallback;
    }
    return fallback;
}

// Drift / Spin / Breath / Sway: a single resample of the finished frame,
// mirrored at the edges so nothing ever shows a border. Skipped entirely
// (zero cost, textureId() falls through to m_fbo) while all four sit at 0.
void ShaderSource::renderMotionPass() {
    float drift  = kitValue("motionDrift",  0.0f);
    float spin   = kitValue("motionSpin",   0.0f);
    float breath = kitValue("motionBreath", 0.0f);
    float sway   = kitValue("motionSway",   0.0f);
    bool active = std::fabs(drift) > 1e-4f || std::fabs(spin) > 1e-4f ||
                  std::fabs(breath) > 1e-4f || std::fabs(sway) > 1e-4f;
    if (!active) { m_motionActive = false; return; }

    static ShaderProgram* sProg = nullptr;
    static bool sTried = false;
    if (!sProg && !sTried) {
        sTried = true;
        static const char* kVS =
            "#version 330 core\n"
            "layout(location = 0) in vec2 aPos;\n"
            "layout(location = 1) in vec2 aTexCoord;\n"
            "out vec2 vUV;\n"
            "void main() { gl_Position = vec4(aPos, 0.0, 1.0); vUV = aTexCoord; }\n";
        static const char* kFS =
            "#version 330 core\n"
            "in vec2 vUV;\n"
            "out vec4 fragColor;\n"
            "uniform sampler2D uTex;\n"
            "uniform float uTime, uDrift, uSpin, uBreath, uSway, uAspect;\n"
            "vec2 mirrorUV(vec2 p) { return 1.0 - abs(fract(p * 0.5) * 2.0 - 1.0); }\n"
            "void main() {\n"
            "    float t = uTime;\n"
            "    vec2 uv = vUV - 0.5; uv.x *= uAspect;\n"
            "    // breath: slow zoom pulse around centre\n"
            "    uv /= 1.0 + uBreath * 0.18 * sin(t * 0.9);\n"
            "    // spin: continuous rotation, signed\n"
            "    float a = uSpin * t * 0.6; float c = cos(a), s = sin(a);\n"
            "    uv = mat2(c, -s, s, c) * uv;\n"
            "    // sway: organic low-frequency wobble of the whole frame\n"
            "    uv += uSway * 0.035 * vec2(sin(t * 1.3 + uv.y * 3.0), cos(t * 1.1 + uv.x * 3.0));\n"
            "    // drift: bounded lissajous pan, never runs away\n"
            "    uv += uDrift * 0.25 * vec2(sin(t * 0.23), cos(t * 0.19));\n"
            "    uv.x /= uAspect; uv += 0.5;\n"
            "    fragColor = texture(uTex, mirrorUV(uv));\n"
            "}\n";
        auto* p = new ShaderProgram();
        if (p->loadFromSource(kVS, kFS)) sProg = p;
        else { delete p; std::cerr << "[Motion] kit pass failed to compile" << std::endl; }
    }
    if (!sProg) { m_motionActive = false; return; }

    if (m_motionFbo.width() != m_width || m_motionFbo.height() != m_height) {
        bool ok = (m_motionFbo.width() == 0) ? m_motionFbo.create(m_width, m_height)
                                              : (m_motionFbo.resize(m_width, m_height), true);
        if (!ok) { m_motionActive = false; return; }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, m_motionFbo.fboId());
    glViewport(0, 0, m_width, m_height);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    sProg->use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_fbo.textureId());
    sProg->setInt("uTex", 0);
    sProg->setFloat("uTime", m_clock);
    sProg->setFloat("uDrift", drift);
    sProg->setFloat("uSpin", spin);
    sProg->setFloat("uBreath", breath);
    sProg->setFloat("uSway", sway);
    sProg->setFloat("uAspect", m_height > 0 ? (float)m_width / (float)m_height : 1.0f);
    m_quad.draw();
    Framebuffer::unbind();
    glViewport(0, 0, m_width, m_height);
    m_motionActive = true;
}

void ShaderSource::uploadUniforms(int passIndex, int passWidth, int passHeight) {
    if (passWidth <= 0) passWidth = m_width;
    if (passHeight <= 0) passHeight = m_height;

    // ISF built-ins — the integrated clock (see update()).
    m_shader.setFloat("TIME", m_clock);
    m_shader.setFloat("TIMEDELTA", m_clockDelta);
    m_shader.setVec2("RENDERSIZE", glm::vec2((float)passWidth, (float)passHeight));
    m_shader.setInt("PASSINDEX", passIndex);
    m_shader.setInt("FRAMEINDEX", m_frameIndex);

    // Mouse state
    float dx = m_mouseX - m_prevMouseX;
    float dy = m_mouseY - m_prevMouseY;
    m_shader.setVec2("mousePos", glm::vec2(m_mouseX, m_mouseY));
    m_shader.setVec2("mouseDelta", glm::vec2(dx, dy));
    m_shader.setFloat("mouseDown", m_mouseDown);
    m_shader.setFloat("pinchHold", 0.0f);
    m_shader.setFloat("msgAge", m_msgAge);

    // Audio state (Shader-Claw naming convention)
    m_shader.setFloat("audioLevel", m_audioRMS);
    m_shader.setFloat("audioBass", m_audioBass);
    m_shader.setFloat("audioMid", m_audioMid);
    m_shader.setFloat("audioHigh", m_audioHigh);
    m_shader.setFloat("_voiceLevel", m_audioRMS);
    m_shader.setInt("audioFFT", 2);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, m_audioFFTTex ? m_audioFFTTex : 0);

    // --- Audio Feature Bus uploads (legacy audioLevel/Bass/Mid/High above are
    //     the frozen pipeline; these are the new perceptual features) ---------
    {
        const AudioFeatures& f = m_af;
        // Tier 1
        m_shader.setFloat("audioSub", f.sub);
        m_shader.setFloat("audioLowMid", f.lowMid);
        m_shader.setFloat("audioHighMid", f.highMid);
        m_shader.setFloat("audioTreble", f.treble);
        m_shader.setFloat("audioPunch", f.punch);
        m_shader.setFloat("audioBeat", f.beat);
        m_shader.setFloat("audioBeatPhase", f.beatPhase);
        m_shader.setFloat("audioBeatPulse", f.beatPulse);
        m_shader.setFloat("audioBarPhase", f.barPhase);
        m_shader.setFloat("audioBPM", f.bpm);
        m_shader.setFloat("audioTempo01", f.tempo01);
        // Tier 2
        m_shader.setFloat("audioBrightness", f.brightness);
        m_shader.setFloat("audioSpread", f.spread);
        m_shader.setFloat("audioRolloff", f.rolloff);
        m_shader.setFloat("audioFlatness", f.flatness);
        m_shader.setFloat("audioTexture", f.texture);
        m_shader.setFloat("audioFlux", f.flux);
        m_shader.setFloat("audioOnset", f.onset);
        m_shader.setFloat("audioOnsetRate", f.onsetRate);
        m_shader.setFloat("audioTilt", f.tilt);
        m_shader.setFloat("audioZCR", f.zcr);
        // Tier 3
        m_shader.setFloat("audioValence", f.valence);
        m_shader.setFloat("audioArousal", f.arousal);
        m_shader.setFloat("audioTension", f.tension);
        m_shader.setFloat("audioWarmth", f.warmth);
        m_shader.setFloat("audioSoftness", f.softness);
        m_shader.setFloat("audioRoughness", f.roughness);
        m_shader.setFloat("audioCharm", f.charm);
        m_shader.setVec2("audioMood", glm::vec2(f.valence, f.arousal));
        // Tier 4
        m_shader.setFloat("audioEnergy", f.energy);
        m_shader.setFloat("audioEnergyVel", f.energyVel);
        m_shader.setFloat("audioEnergyAcc", f.energyAcc);
        m_shader.setFloat("audioBuildup", f.buildup);
        m_shader.setFloat("audioBuildupRate", f.buildupRate);
        m_shader.setFloat("audioDrop", f.drop);
        m_shader.setFloat("audioNovelty", f.novelty);
        m_shader.setFloat("audioSectionPhase", f.sectionPhase);
        m_shader.setFloat("audioSectionAge", f.sectionAge);
        m_shader.setFloat("audioLayers", f.layers);
        m_shader.setFloat("audioDensity", f.density);
        m_shader.setVec4("audioPresence", glm::vec4(f.presence[0], f.presence[1], f.presence[2], f.presence[3]));
        m_shader.setVec2("audioFlow", glm::vec2(f.flow[0], f.flow[1]));
        // Tier 5
        m_shader.setVec3("audioPalShadow", glm::vec3(f.palShadow[0], f.palShadow[1], f.palShadow[2]));
        m_shader.setVec3("audioPalMid",    glm::vec3(f.palMid[0], f.palMid[1], f.palMid[2]));
        m_shader.setVec3("audioPalHigh",   glm::vec3(f.palHigh[0], f.palHigh[1], f.palHigh[2]));
        m_shader.setVec3("audioPalAccent", glm::vec3(f.palAccent[0], f.palAccent[1], f.palAccent[2]));
        m_shader.setFloat("audioPalTemp", f.palTemp);
        m_shader.setFloat("audioPalSat", f.palSat);
        // Harmony scalars
        m_shader.setFloat("audioDominantPitch", f.dominantPitch);
        m_shader.setFloat("audioMajorMinor", f.majorMinor);
        m_shader.setFloat("audioHCDF", f.hcdf);
        // ── EaselAudio v1 — temperament matrix ─────────────────────────
        m_shader.setFloat("audioBassHit", f.bassHit);
        m_shader.setFloat("audioMidHit", f.midHit);
        m_shader.setFloat("audioHighHit", f.highHit);
        m_shader.setFloat("audioBassPresence", f.bassPresence);
        m_shader.setFloat("audioMidPresence", f.midPresence);
        m_shader.setFloat("audioHighPresence", f.highPresence);
        m_shader.setFloat("audioLevelPresence", f.levelPresence);
        m_shader.setFloat("audioBassTime", f.bassTime);
        m_shader.setFloat("audioMidTime", f.midTime);
        m_shader.setFloat("audioHighTime", f.highTime);
        m_shader.setFloat("audioTime", f.levelTime);
        // Rhythm bus
        m_shader.setFloat("audioBPMConfidence", f.bpmConfidence);
        m_shader.setFloat("audioPhase2", f.phase2);
        m_shader.setFloat("audioPhase4", f.phase4);
        m_shader.setFloat("audioPhase8", f.phase8);
        m_shader.setFloat("audioPhase16", f.phase16);
        m_shader.setFloat("audioOnBeat", f.onBeat);
        m_shader.setFloat("audioToggleOnBeat", f.toggleOnBeat);
        // Pseudo-stems + temperaments
        m_shader.setFloat("stemBass", f.stemBass);
        m_shader.setFloat("stemDrums", f.stemDrums);
        m_shader.setFloat("stemMelody", f.stemMelody);
        m_shader.setFloat("stemAir", f.stemAir);
        m_shader.setFloat("stemVocal", f.stemVocal);
        m_shader.setFloat("stemBassHit", f.stemBassHit);
        m_shader.setFloat("stemDrumsHit", f.stemDrumsHit);
        m_shader.setFloat("stemMelodyHit", f.stemMelodyHit);
        m_shader.setFloat("stemAirHit", f.stemAirHit);
        m_shader.setFloat("stemVocalHit", f.stemVocalHit);
        m_shader.setFloat("stemBassPresence", f.stemBassPresence);
        m_shader.setFloat("stemDrumsPresence", f.stemDrumsPresence);
        m_shader.setFloat("stemMelodyPresence", f.stemMelodyPresence);
        m_shader.setFloat("stemAirPresence", f.stemAirPresence);
        m_shader.setFloat("stemVocalPresence", f.stemVocalPresence);
    }

    // Font atlas for text shaders
    GLuint fontAtlas = FontAtlas::texture();
    m_shader.setInt("fontAtlasTex", 3);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, fontAtlas);

    // User inputs
    for (const auto& input : m_inputs) {
        if (input.type == "float") {
            m_shader.setFloat(input.name, std::get<float>(input.value));
        } else if (input.type == "color") {
            m_shader.setVec4(input.name, std::get<glm::vec4>(input.value));
        } else if (input.type == "bool" || input.type == "event") {
            m_shader.setBool(input.name, std::get<bool>(input.value));
        } else if (input.type == "point2D") {
            m_shader.setVec2(input.name, std::get<glm::vec2>(input.value));
        } else if (input.type == "long") {
            m_shader.setInt(input.name, (int)std::get<float>(input.value));
        } else if (input.type == "text") {
            // Send text as character code uniforms
            // Shader-Claw 3 encoding: A=0..Z=25, space=26, 0-9=27-36
            std::string text = std::get<std::string>(input.value);
            int maxLen = (int)input.maxVal;
            if (maxLen <= 0) maxLen = 12;
            if ((int)text.size() > maxLen) {
                text = text.substr(text.size() - maxLen);
            }
            m_shader.setInt(input.name + "_len", (int)text.size());
            for (int i = 0; i < maxLen; i++) {
                int ch = 26; // space/empty
                if (i < (int)text.size()) {
                    char c = text[i];
                    if (c >= 'A' && c <= 'Z') ch = c - 'A';
                    else if (c >= 'a' && c <= 'z') ch = c - 'a';
                    else if (c >= '0' && c <= '9') ch = 27 + (c - '0');
                    else ch = 26; // space or unknown
                }
                m_shader.setInt(input.name + "_" + std::to_string(i), ch);
            }
        }
        // image inputs: bind external texture if available
        if (input.type == "image") {
            auto it = m_imageBindings.find(input.name);
            if (it != m_imageBindings.end() && it->second.textureId != 0) {
                // Bind to texture unit 8+ (after audio=2, font=3, pass buffers=4+)
                int imgUnit = 8;
                for (const auto& inp : m_inputs) {
                    if (inp.type == "image" && inp.name == input.name) break;
                    if (inp.type == "image") imgUnit++;
                }
                glActiveTexture(GL_TEXTURE0 + imgUnit);
                glBindTexture(GL_TEXTURE_2D, it->second.textureId);
                m_shader.setInt(input.name, imgUnit);
                m_shader.setVec2("IMG_SIZE_" + input.name,
                    glm::vec2((float)it->second.width, (float)it->second.height));
                m_shader.setBool("_flip_" + input.name, it->second.flippedV);
            } else {
                int imgUnit = 8;
                for (const auto& inp : m_inputs) {
                    if (inp.type == "image" && inp.name == input.name) break;
                    if (inp.type == "image") imgUnit++;
                }
                glActiveTexture(GL_TEXTURE0 + imgUnit);
                glBindTexture(GL_TEXTURE_2D, 0);
                m_shader.setInt(input.name, imgUnit);
                m_shader.setVec2("IMG_SIZE_" + input.name, glm::vec2(0.0f, 0.0f));
                m_shader.setBool("_flip_" + input.name, false);
            }
        }
    }

    // Update previous mouse position (only on pass 0 to avoid multi-update)
    if (passIndex == 0) {
        m_prevMouseX = m_mouseX;
        m_prevMouseY = m_mouseY;
    }
}

// --- Parameter setters ---

void ShaderSource::setFloat(const std::string& name, float v) {
    for (auto& input : m_inputs) {
        if (input.name == name && (input.type == "float" || input.type == "long")) {
            input.value = v;
            return;
        }
    }
}

void ShaderSource::setColor(const std::string& name, const glm::vec4& v) {
    for (auto& input : m_inputs) {
        if (input.name == name && input.type == "color") {
            input.value = v;
            return;
        }
    }
}

void ShaderSource::setBool(const std::string& name, bool v) {
    for (auto& input : m_inputs) {
        if (input.name == name && (input.type == "bool" || input.type == "event")) {
            input.value = v;
            return;
        }
    }
}

void ShaderSource::setPoint2D(const std::string& name, const glm::vec2& v) {
    for (auto& input : m_inputs) {
        if (input.name == name && input.type == "point2D") {
            input.value = v;
            return;
        }
    }
}

void ShaderSource::setText(const std::string& name, const std::string& text) {
    for (auto& input : m_inputs) {
        if (input.name == name && input.type == "text") {
            input.value = text;
            return;
        }
    }
}

void ShaderSource::setMouseState(float x, float y, bool down) {
    m_mouseX = x;
    m_mouseY = y;
    m_mouseDown = down ? 1.0f : 0.0f;
}

void ShaderSource::bindImageInput(const std::string& name, GLuint texId, int w, int h, uint32_t sourceLayerId, bool flippedV) {
    m_imageBindings[name] = {texId, w, h, sourceLayerId, flippedV};
}

void ShaderSource::unbindImageInput(const std::string& name) {
    m_imageBindings.erase(name);
}

bool ShaderSource::bindImageFileInput(const std::string& name, const std::string& path) {
    auto tex = std::make_shared<Texture>();
    if (!tex->loadFromFile(path)) return false;
    ImageBinding b;
    b.textureId = tex->id();
    b.width = tex->width();
    b.height = tex->height();
    b.sourceLayerId = 0;        // file-backed: the refresh loop leaves it alone
    b.flippedV = false;         // Texture::loadFromFile lands GL bottom-up
    b.filePath = path;
    b.ownedTex = tex;
    m_imageBindings[name] = b;
    return true;
}

void ShaderSource::applyAudioBindings(float level, float bass, float mid, float high, float beat,
                                      float dt, MIDIManager* midi,
                                      float energy, float build, float drop,
                                      float silence, float momentum) {
    // Clamp dt so a stall / first frame can't blow the exponential up. The
    // smoother is time-constant based (frame-rate independent): alpha is
    // derived from a per-second rate and the real frame dt, identical in
    // spirit to AudioAnalyzer::expSmooth.
    if (!(dt > 0.0f)) dt = 1.0f / 60.0f;
    if (dt > 0.1f)    dt = 0.1f;

    for (auto& [paramName, binding] : m_audioBindings) {
        if (binding.signal == AudioSignal::None) continue;

        // Get raw signal value (0-1)
        float raw = 0.0f;
        bool haveRaw = true;
        switch (binding.signal) {
            case AudioSignal::Level: raw = level; break;
            case AudioSignal::Bass:  raw = bass; break;
            case AudioSignal::Mid:   raw = mid; break;
            case AudioSignal::High:  raw = high; break;
            case AudioSignal::Beat:  raw = beat; break;
            case AudioSignal::Energy:   raw = energy;   break;
            case AudioSignal::Build:    raw = build;    break;
            case AudioSignal::Drop:     raw = drop;     break;
            case AudioSignal::Silence:  raw = silence;  break;
            case AudioSignal::Momentum: raw = momentum; break;
            case AudioSignal::MidiCC: {
                if (midi && binding.midiCC >= 0) {
                    float v = midi->getCCValue(binding.midiChannel, binding.midiCC);
                    if (v < 0.0f) { haveRaw = false; } // no value received yet
                    else raw = v;
                } else {
                    haveRaw = false;
                }
                break;
            }
            default: haveRaw = false; break;
        }
        if (!haveRaw) continue;

        // Frame-rate-independent asymmetric follower + range mapping, now
        // shared with FluidSource via AudioBinding::follow(). [rangeMin,
        // rangeMax] IS the effective depth/strength (default rangeMin=paramMin,
        // rangeMax=paramMax preserves the original 1:1 mapping).
        // MIDI as an endless knob: the CC's movement (not its position) drives
        // the parameter. First sample only records where the knob is; from
        // then on each turn adds the delta to a target that starts at the
        // parameter's current value. Full pot travel = full range.
        if (binding.signal == AudioSignal::MidiCC) {
            float cur01 = 0.5f;
            for (auto& input : m_inputs)
                if (input.name == paramName && (input.type == "float" || input.type == "long")) {
                    float cv = std::holds_alternative<float>(input.value) ? std::get<float>(input.value) : input.minVal;
                    float span = binding.rangeMax - binding.rangeMin;
                    cur01 = std::fabs(span) > 1e-6f ? (cv - binding.rangeMin) / span : 0.5f;
                    break;
                }
            if (binding.ccTarget01 < 0.0f) {
                binding.ccTarget01 = cur01;
                binding.ccLastRaw = raw;
                binding.smoothedValue = cur01; binding.hasSmoothed = true;
                continue;
            }
            float delta = raw - binding.ccLastRaw;
            binding.ccLastRaw = raw;
            if (std::fabs(delta) > 0.0f)
                binding.ccTarget01 = std::max(0.0f, std::min(1.0f, binding.ccTarget01 + delta));
            raw = binding.ccTarget01;   // the follower glides toward the target
        }
        float mapped = binding.follow(raw, dt);

        // Find the input and set its value
        for (auto& input : m_inputs) {
            if (input.name == paramName && (input.type == "float" || input.type == "long")) {
                mapped = std::max(input.minVal, std::min(input.maxVal, mapped));
                input.value = mapped;
                break;
            }
        }
    }
}

void ShaderSource::setAudioState(float rms, float bass, float mid, float high, GLuint fftTex) {
    m_audioRMS = rms;
    m_audioBass = bass;
    m_audioMid = mid;
    m_audioHigh = high;
    m_audioFFTTex = fftTex;
}

float ShaderSource::sMotionEase = 0.35f;

void ShaderSource::setAudioFeatures(const AudioFeatures& f) {
    m_af = f;
    // Global Motion Ease: attack/release smoothing on the core bands so a
    // sudden drop in the music swells the visuals instead of slamming them.
    float e = sMotionEase;
    if (e > 0.001f) {
        float atk = 1.0f - e * 0.80f;   // rises stay fairly quick
        float rel = 1.0f - e * 0.96f;   // falls glide out slowly
        auto sm = [&](float& st, float target) {
            st += (target - st) * ((target > st) ? atk : rel);
            return st;
        };
        m_af.level     = sm(m_easeL,  f.level);
        m_af.bass      = sm(m_easeB,  f.bass);
        m_af.lowMid    = sm(m_easeLM, f.lowMid);
        m_af.highMid   = sm(m_easeHM, f.highMid);
        m_af.treble    = sm(m_easeT,  f.treble);
        m_af.beatPulse = sm(m_easeBt, f.beatPulse);
    }
    // Mirror the legacy fields so old shaders' audioLevel/Bass/Mid/High/FFT
    // keep their exact prior meaning (audioMid = merged lowMid+highMid).
    m_audioRMS  = m_af.level;
    m_audioBass = m_af.bass;
    m_audioMid  = (m_af.lowMid + m_af.highMid) * 0.5f;
    m_audioHigh = m_af.treble;
    m_audioFFTTex = f.fftTex;
}

void ShaderSource::setResolution(int w, int h) {
    if (w == m_width && h == m_height) return;
    m_width = w;
    m_height = h;
    if (m_initialized) {
        m_fbo.resize(w, h);
        // Resize pass FBOs (simulation passes at half-res for large canvases)
        for (auto& pass : m_passes) {
            if (pass.ppFBO) {
                int pw, ph;
                passSize(pass, pw, ph);
                pass.simWidth = pw;
                pass.simHeight = ph;
                pass.ppFBO->a.resize(pw, ph);
                pass.ppFBO->b.resize(pw, ph);
            }
        }
        m_frameIndex = 0; // reset so shader re-seeds
    } else if (w > 0 && h > 0 && m_shader.id() != 0) {
        // Shader compiled but FBO creation failed at load time (0×0 dimensions).
        // Attempt late initialization now that we have valid dimensions.
        // Also revives a suspend()ed source directly at the new size.
        if (m_fbo.create(w, h)) {
            m_quad.createQuad();
            m_initialized = true;
            m_suspended = false;
            if (!m_passBuffers.empty()) {
                createPassFBOs();
            }
        }
    }
}
