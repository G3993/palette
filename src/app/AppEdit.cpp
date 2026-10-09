// Shader editing operations: header rewrites, Make Control, Add Control,
// texture support for shaders that never had an image input, image binding.
#include "app/App.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <regex>

namespace fs = std::filesystem;
using ojson = nlohmann::ordered_json;

namespace palette {

std::vector<std::string> App::imageInputs() const {
    std::vector<std::string> v;
    if (shader) for (auto& in : shader->inputs()) if (in.type == "image") v.push_back(in.name);
    return v;
}

static std::string labelFromName(std::string name) {
    if (!name.empty()) name[0] = (char)std::toupper((unsigned char)name[0]);
    for (size_t i = 1; i < name.size(); ++i)
        if (std::isupper((unsigned char)name[i]) && std::islower((unsigned char)name[i - 1])) { name.insert(i, " "); ++i; }
    for (auto& ch : name) if (ch == '_') ch = ' ';
    return name;
}

// Parse the ISF header, let `edit` change it, optionally swap the GLSL body,
// recompile. On a compile failure the previous text is restored so a bad edit
// never leaves the canvas black.
bool App::rewriteHeader(const std::function<bool(ojson&)>& edit, const std::string& bodyEdit) {
    if (!shader) return false;
    size_t ha = codeBuffer.find("/*"), hb = codeBuffer.find("*/");
    if (ha == std::string::npos || hb == std::string::npos) { compileStatus = "no ISF header"; compileOk = false; return false; }
    ojson header;
    try { header = ojson::parse(codeBuffer.substr(ha + 2, hb - ha - 2)); }
    catch (...) { compileStatus = "header is not valid JSON"; compileOk = false; return false; }
    if (!header.contains("INPUTS") || !header["INPUTS"].is_array()) header["INPUTS"] = ojson::array();
    if (!edit(header)) return false;
    std::string body = bodyEdit.empty() ? codeBuffer.substr(hb + 2) : bodyEdit;
    std::string before = codeBuffer;
    codeBuffer = "/*" + header.dump(2) + "*/" + body;
    codeDirty = true;
    recompile();
    if (!compileOk) {
        std::string err = compileStatus;
        codeBuffer = before; recompile();
        compileOk = false; compileStatus = err;
        return false;
    }
    // passes may have changed names; refresh the Code sheet's list
    passNames.clear();
    if (header.contains("PASSES")) for (auto& p : header["PASSES"]) passNames.push_back(p.value("TARGET", std::string("output")));
    if (passNames.empty()) passNames.push_back("output");
    return true;
}

bool App::makeControl(const std::string& name, float lo, float hi, float def, int selA, int selB) {
    if (!shader || selA == selB) return false;
    if (selA > selB) std::swap(selA, selB);
    size_t hb = codeBuffer.find("*/");
    if (hb == std::string::npos || (size_t)selA < hb || selB > (int)codeBuffer.size()) return false;
    std::string body = codeBuffer.substr(hb + 2);
    int bodyA = selA - (int)(hb + 2), bodyB = selB - (int)(hb + 2);
    body.replace(bodyA, bodyB - bodyA, name);
    std::string label = labelFromName(name);
    bool ok = rewriteHeader([&](ojson& h) {
        for (auto& i : h["INPUTS"]) if (i.value("NAME", "") == name) { compileStatus = "a control named " + name + " exists"; compileOk = false; return false; }
        h["INPUTS"].push_back(ojson{{"NAME", name}, {"LABEL", label}, {"TYPE", "float"}, {"GROUP", "Look"}, {"DEFAULT", def}, {"MIN", lo}, {"MAX", hi}});
        return true;
    }, body);
    if (ok) showToast(label + " is now a control");
    return ok;
}

bool App::addControl(const std::string& name, const std::string& type, float lo, float hi, float def, const std::string& group) {
    std::string label = labelFromName(name);
    bool ok = rewriteHeader([&](ojson& h) {
        for (auto& i : h["INPUTS"]) if (i.value("NAME", "") == name) { compileStatus = "a control named " + name + " exists"; compileOk = false; return false; }
        ojson in = {{"NAME", name}, {"LABEL", label}, {"TYPE", type}};
        if (type != "image") in["GROUP"] = group.empty() ? "Look" : group;
        if (type == "float") { in["DEFAULT"] = def; in["MIN"] = lo; in["MAX"] = hi; }
        else if (type == "long") { in["DEFAULT"] = (int)std::lround(def); in["MIN"] = (int)std::lround(lo); in["MAX"] = (int)std::lround(hi); }
        else if (type == "bool") in["DEFAULT"] = def >= 0.5f;
        else if (type == "color") in["DEFAULT"] = ojson::array({1.0, 1.0, 1.0, 1.0});
        else if (type == "point2D") { in["DEFAULT"] = ojson::array({0.5, 0.5}); in["MIN"] = ojson::array({0.0, 0.0}); in["MAX"] = ojson::array({1.0, 1.0}); }
        h["INPUTS"].push_back(in);
        return true;
    });
    if (ok) showToast(type == "image" ? label + " added · pick a file in Source" : label + " added · use " + name + " in Code");
    return ok;
}

// Any shader can take a picture: rename its main, append a wrapper that blends
// the texture into the final pass, and expose Mix / Mode / Scale controls.
bool App::addTextureSupport() {
    if (!shader) return false;
    if (!imageInputs().empty()) return true;
    size_t hb = codeBuffer.find("*/");
    if (hb == std::string::npos) return false;
    std::string body = codeBuffer.substr(hb + 2);
    std::regex mainRe(R"(void\s+main\s*\(\s*(void)?\s*\))");
    if (!std::regex_search(body, mainRe)) { compileStatus = "could not find main()"; compileOk = false; return false; }
    body = std::regex_replace(body, mainRe, "void paletteMain()", std::regex_constants::format_first_only);
    int lastPass = (int)passNames.size() - 1;
    body += "\n\n// Palette: texture blended into the final pass. Edit freely.\n"
            "void main() {\n"
            "    paletteMain();\n"
            "    if (PASSINDEX != " + std::to_string(lastPass) + ") return;\n"
            "    vec2 tuv = (isf_FragNormCoord - 0.5) / max(textureScale, 0.01) + 0.5;\n"
            "    vec4 tex = IMG_NORM_PIXEL(inputImage, tuv);\n"
            "    vec3 base = gl_FragColor.rgb;\n"
            "    vec3 blended = base;\n"
            "    if (textureMode == 0) blended = tex.rgb;\n"
            "    else if (textureMode == 1) blended = base * tex.rgb;\n"
            "    else if (textureMode == 2) blended = base + tex.rgb;\n"
            "    else if (textureMode == 3) blended = 1.0 - (1.0 - base) * (1.0 - tex.rgb);\n"
            "    else blended = base * dot(tex.rgb, vec3(0.299, 0.587, 0.114));\n"
            "    gl_FragColor = vec4(mix(base, blended, textureMix), 1.0);\n"
            "}\n";
    bool ok = rewriteHeader([&](ojson& h) {
        h["INPUTS"].push_back(ojson{{"NAME", "inputImage"}, {"TYPE", "image"}});
        h["INPUTS"].push_back(ojson{{"NAME", "textureMix"}, {"LABEL", "Texture Mix"}, {"TYPE", "float"}, {"GROUP", "Texture"}, {"DEFAULT", 0.6}, {"MIN", 0.0}, {"MAX", 1.0}});
        h["INPUTS"].push_back(ojson{{"NAME", "textureMode"}, {"LABEL", "Texture Mode"}, {"TYPE", "long"}, {"GROUP", "Texture"}, {"DEFAULT", 1},
                                    {"VALUES", ojson::array({0, 1, 2, 3, 4})}, {"LABELS", ojson::array({"Show", "Multiply", "Add", "Screen", "Mask"})}});
        h["INPUTS"].push_back(ojson{{"NAME", "textureScale"}, {"LABEL", "Texture Scale"}, {"TYPE", "float"}, {"GROUP", "Texture"}, {"DEFAULT", 1.0}, {"MIN", 0.25}, {"MAX", 4.0}});
        return true;
    }, body);
    if (ok) showToast("Texture added · see the Texture group in Controls");
    return ok;
}

void App::bindImage(const std::string& path) {
    if (!shader) return;
    auto imgs = imageInputs();
    std::string target = imageTarget;
    if (target.empty() || std::find(imgs.begin(), imgs.end(), target) == imgs.end()) target = imgs.empty() ? "" : imgs[0];
    if (target.empty()) {
        if (!addTextureSupport()) { showToast(compileStatus.empty() ? "Could not add a texture input" : compileStatus); return; }
        target = "inputImage";
    }
    if (!shader->bindImageFileInput(target, path)) { showToast("Could not read that image"); return; }
    imagePaths[target] = path;
    imageTarget.clear();
    if (target == imageInputs()[0]) { sourcePath = path; sourceLabel = fs::path(path).filename().string(); }
    showToast(target + " · " + fs::path(path).filename().string());
    if (easel.connected()) easel.pushCode(codeBuffer);
}

} // namespace palette
