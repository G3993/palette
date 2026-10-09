#include "ui/Glass.h"

namespace palette::ui {

static const char* kVS = R"(#version 330 core
out vec2 vUV;
void main() {
    // fullscreen triangle from gl_VertexID, no buffers
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vUV = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
})";

// 9-tap separable Gaussian, run twice per axis on a 1/6 scale buffer → wide soft blur.
static const char* kFS = R"(#version 330 core
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uTex;
uniform vec2 uDir;      // (1/w, 0) or (0, 1/h)
uniform float uSat;     // saturation boost (frosted glass lifts colour)
void main() {
    float w[5] = float[](0.2270270270, 0.1945945946, 0.1216216216, 0.0540540541, 0.0162162162);
    vec3 c = texture(uTex, vUV).rgb * w[0];
    for (int i = 1; i < 5; ++i) {
        c += texture(uTex, vUV + uDir * float(i) * 1.5).rgb * w[i];
        c += texture(uTex, vUV - uDir * float(i) * 1.5).rgb * w[i];
    }
    float l = dot(c, vec3(0.299, 0.587, 0.114));
    c = mix(vec3(l), c, uSat);
    FragColor = vec4(c, 1.0);
})";

bool Glass::init() {
    if (!m_blur.loadFromSource(kVS, kFS)) return false;
    glGenVertexArrays(1, &m_vao);
    return true;
}

void Glass::pass(GLuint src, Framebuffer& dst, float dx, float dy) {
    dst.bind();
    glViewport(0, 0, dst.width(), dst.height());
    m_blur.use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, src);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    m_blur.setInt("uTex", 0);
    m_blur.setVec2("uDir", glm::vec2(dx, dy));
    m_blur.setFloat("uSat", 1.0f);
    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}

void Glass::update(GLuint sceneTex, int sceneW, int sceneH) {
    if (!sceneTex || sceneW <= 0 || sceneH <= 0) return;
    int w = sceneW / 6, h = sceneH / 6;
    if (w < 8) w = 8; if (h < 8) h = 8;
    if (w != m_w || h != m_h) {
        m_ping.destroy(); m_pong.destroy();
        m_ping.create(w, h); m_pong.create(w, h);
        m_w = w; m_h = h;
    }
    glDisable(GL_BLEND);
    // downsample (first pass also blurs horizontally), then three more passes.
    pass(sceneTex, m_pong, 1.0f / w, 0);
    pass(m_pong.textureId(), m_ping, 0, 1.0f / h);
    pass(m_ping.textureId(), m_pong, 1.0f / w, 0);
    pass(m_pong.textureId(), m_ping, 0, 1.0f / h);
    // saturate on the final tap
    m_ping.bind(); // no-op layout; final result lives in m_pong after one more pass
    pass(m_ping.textureId(), m_pong, 0, 0);
    Framebuffer::unbind();
}

void Glass::rect(ImDrawList* dl, ImVec2 p0, ImVec2 p1, float rounding, ImVec2 ds, ImDrawFlags flags, ImU32 tint) const {
    if (!texture()) { dl->AddRectFilled(p0, p1, IM_COL32(22, 22, 24, 235), rounding, flags); return; }
    ImVec2 uv0(p0.x / ds.x, 1.0f - p0.y / ds.y), uv1(p1.x / ds.x, 1.0f - p1.y / ds.y);
    dl->AddImageRounded((ImTextureID)(intptr_t)texture(), p0, p1, uv0, uv1, IM_COL32_WHITE, rounding, flags);
    dl->AddRectFilled(p0, p1, tint, rounding, flags);
    dl->AddRect(p0, p1, IM_COL32(255, 255, 255, 30), rounding, 1.0f, flags);
}

void Glass::circle(ImDrawList* dl, ImVec2 c, float r, ImVec2 ds, ImU32 tint) const {
    ImVec2 p0(c.x - r, c.y - r), p1(c.x + r, c.y + r);
    rect(dl, p0, p1, r, ds, 0, tint);
}

} // namespace palette::ui
