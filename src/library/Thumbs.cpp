#include "library/Thumbs.h"
#include "sources/ShaderSource.h"
#include "render/Framebuffer.h"
#include <stb_image.h>
#include <stb_image_write.h>
#include <cstdlib>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

namespace palette {

static std::string homeDir() { const char* h = std::getenv("HOME"); return h ? h : "."; }

bool Thumbs::init() {
    m_cacheDir = (fs::path(homeDir()) / ".palette" / "thumbs").string();
    std::error_code ec; fs::create_directories(m_cacheDir, ec);
    return true;
}

bool Thumbs::tryLoadPng(const std::string& png, Thumb& t, bool easelOrder) {
    if (!fs::exists(png)) return false;
    int w, h, n;
    stbi_set_flip_vertically_on_load(0);
    unsigned char* px = stbi_load(png.c_str(), &w, &h, &n, 4);
    if (!px) return false;
    glGenTextures(1, &t.tex);
    glBindTexture(GL_TEXTURE_2D, t.tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    stbi_image_free(px);
    // A PNG decoded top-down lands in GL as row 0 = bottom, i.e. upside down when
    // sampled with uv (0,0) at the bottom. Easel's cache is already GL-ordered.
    t.flipped = !easelOrder;
    t.pending = false;
    return true;
}

const Thumbs::Thumb& Thumbs::get(const LibraryEntry& e) {
    auto it = m_thumbs.find(e.path);
    if (it != m_thumbs.end()) return it->second;
    Thumb t;
    std::string stem = fs::path(e.file).stem().string();
    if (!tryLoadPng((fs::path(m_cacheDir) / (stem + ".png")).string(), t, true) &&
        !tryLoadPng((fs::path(homeDir()) / ".easel" / "shader_thumbs" / (stem + ".png")).string(), t, true)) {
        m_queue.push_back(e.path);
    }
    m_titles[e.path] = stem;
    return m_thumbs.emplace(e.path, t).first->second;
}

void Thumbs::renderOne(const std::string& path) {
    auto it = m_thumbs.find(path);
    if (it == m_thumbs.end()) return;
    Thumb& t = it->second;
    ShaderSource src;
    GLint prevFbo = 0; glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    GLint vp[4]; glGetIntegerv(GL_VIEWPORT, vp);
    if (!src.loadFromFile(path)) { t.pending = false; return; }
    src.setResolution(kW, kH);
    for (int i = 0; i < 8; ++i) src.update(); // let feedback shaders settle a little
    // copy into our own texture (the source is destroyed at scope end)
    std::vector<unsigned char> px((size_t)kW * kH * 4);
    GLuint fbo; glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, src.textureId(), 0);
    glReadPixels(0, 0, kW, kH, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
    glDeleteFramebuffers(1, &fbo);
    glViewport(vp[0], vp[1], vp[2], vp[3]);
    for (size_t i = 3; i < px.size(); i += 4) px[i] = 255;
    glGenTextures(1, &t.tex);
    glBindTexture(GL_TEXTURE_2D, t.tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kW, kH, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    t.flipped = false; t.pending = false;
    std::string out = (fs::path(m_cacheDir) / (m_titles[path] + ".png")).string();
    stbi_write_png(out.c_str(), kW, kH, 4, px.data(), kW * 4); // GL order, same as Easel's cache
}

void Thumbs::update(int budget) {
    while (budget-- > 0 && !m_queue.empty()) {
        std::string p = m_queue.front(); m_queue.pop_front();
        renderOne(p);
    }
}

bool Thumbs::renderPng(ShaderSource& src, const std::string& outPath, int w, int h) {
    GLint prevFbo = 0; glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    GLint vp[4]; glGetIntegerv(GL_VIEWPORT, vp);
    int ow = src.width(), oh = src.height();
    src.setResolution(w, h);
    src.update();
    std::vector<unsigned char> px((size_t)w * h * 4);
    GLuint fbo; glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, src.textureId(), 0);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
    glDeleteFramebuffers(1, &fbo);
    glViewport(vp[0], vp[1], vp[2], vp[3]);
    if (ow > 0 && oh > 0) src.setResolution(ow, oh);
    for (size_t i = 3; i < px.size(); i += 4) px[i] = 255;
    // flip to top-down for a normal PNG
    std::vector<unsigned char> flipped(px.size());
    for (int y = 0; y < h; ++y) std::copy(px.begin() + (size_t)y * w * 4, px.begin() + (size_t)(y + 1) * w * 4, flipped.begin() + (size_t)(h - 1 - y) * w * 4);
    return stbi_write_png(outPath.c_str(), w, h, 4, flipped.data(), w * 4) != 0;
}

} // namespace palette
