#pragma once
#include <glad/glad.h>

class Framebuffer {
public:
    Framebuffer() = default;
    ~Framebuffer();

    Framebuffer(const Framebuffer&) = delete;
    Framebuffer& operator=(const Framebuffer&) = delete;

    // Move semantics for container storage
    Framebuffer(Framebuffer&& o) noexcept
        : m_fbo(o.m_fbo), m_texture(o.m_texture), m_depth(o.m_depth),
          m_width(o.m_width), m_height(o.m_height), m_hasDepth(o.m_hasDepth),
          m_halfFloat(o.m_halfFloat), m_fullFloat(o.m_fullFloat) {
        o.m_fbo = o.m_texture = o.m_depth = 0;
    }
    Framebuffer& operator=(Framebuffer&& o) noexcept {
        if (this != &o) {
            destroy();
            m_fbo = o.m_fbo; m_texture = o.m_texture; m_depth = o.m_depth;
            m_width = o.m_width; m_height = o.m_height;
            m_hasDepth = o.m_hasDepth; m_halfFloat = o.m_halfFloat; m_fullFloat = o.m_fullFloat;
            o.m_fbo = o.m_texture = o.m_depth = 0;
        }
        return *this;
    }

    bool create(int width, int height, bool withDepth = false);
    // fullFloat = RGBA32F instead of RGBA16F (ISF pass "FLOAT": true)
    bool createHalfFloat(int width, int height, bool fullFloat = false);
    void resize(int width, int height);
    void destroy();
    void bind() const;
    static void unbind();

    GLuint fboId() const { return m_fbo; }
    GLuint textureId() const { return m_texture; }
    int width() const { return m_width; }
    int height() const { return m_height; }

private:
    GLuint m_fbo = 0;
    GLuint m_texture = 0;
    GLuint m_depth = 0;
    int m_width = 0, m_height = 0;
    bool m_hasDepth = false;
    bool m_halfFloat = false;
    bool m_fullFloat = false;  // float buffer is RGBA32F, not RGBA16F
};
