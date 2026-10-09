#pragma once
#include <glad/glad.h>
#include <imgui.h>
#include "render/Framebuffer.h"
#include "render/ShaderProgram.h"

namespace palette::ui {

// Frosted glass. Each frame the composited scene (window-sized texture) is
// downsampled and blurred; UI surfaces sample that blur through their own
// rounded rect, then tint it. Real backdrop blur, so a sheet never hides the
// render it is adjusting.
class Glass {
public:
    bool init();
    // sceneTex is the full-window render, in pixels. Called once per frame.
    void update(GLuint sceneTex, int sceneW, int sceneH);
    GLuint texture() const { return m_pong.textureId(); }

    // Draw a glass rect in screen points. displaySize in points, GL y-up handled here.
    void rect(ImDrawList* dl, ImVec2 p0, ImVec2 p1, float rounding, ImVec2 displaySize,
              ImDrawFlags flags = 0, ImU32 tint = IM_COL32(22, 22, 24, 158)) const;
    void circle(ImDrawList* dl, ImVec2 center, float radius, ImVec2 displaySize,
                ImU32 tint = IM_COL32(22, 22, 24, 158)) const;

private:
    Framebuffer m_ping, m_pong;
    ShaderProgram m_blur;
    GLuint m_vao = 0;
    int m_w = 0, m_h = 0;
    void pass(GLuint src, Framebuffer& dst, float dx, float dy);
};

} // namespace palette::ui
