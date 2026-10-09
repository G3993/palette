#pragma once
#include <glad/glad.h>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include "library/Library.h"

class ShaderSource;
class Framebuffer;

namespace palette {

// Thumbnails for the Shader sheet. Reads Easel's cache first
// (~/.easel/shader_thumbs/<stem>.png, stored in GL row order), otherwise
// renders one shader per frame offscreen and caches to ~/.palette/thumbs.
class Thumbs {
public:
    struct Thumb { GLuint tex = 0; bool flipped = false; bool pending = true; };
    static constexpr int kW = 180, kH = 225;
    bool init();
    // Returns the thumb; schedules a render if missing. Call each frame while visible.
    const Thumb& get(const LibraryEntry& e);
    // Render at most `budget` queued thumbnails this frame (call once per frame, GL bound).
    void update(int budget = 1);
    // Render a one-off PNG of a live ShaderSource at kW×kH (for publish).
    static bool renderPng(ShaderSource& src, const std::string& outPath, int w = 640, int h = 400);
private:
    std::map<std::string, Thumb> m_thumbs; // key = path
    std::deque<std::string> m_queue;
    std::map<std::string, std::string> m_titles;
    std::string m_cacheDir;
    bool tryLoadPng(const std::string& png, Thumb& t, bool easelOrder);
    void renderOne(const std::string& path);
};

} // namespace palette
