#include "render/Framebuffer.h"
#include <iostream>

Framebuffer::~Framebuffer() { destroy(); }

void Framebuffer::destroy() {
    if (m_fbo) glDeleteFramebuffers(1, &m_fbo);
    if (m_texture) glDeleteTextures(1, &m_texture);
    if (m_depth) glDeleteRenderbuffers(1, &m_depth);
    m_fbo = m_texture = m_depth = 0;
    // Reset dimensions so size checks after an explicit destroy() see "not
    // allocated" and recreate instead of rendering into a dead FBO.
    m_width = m_height = 0;
}

bool Framebuffer::create(int width, int height, bool withDepth) {
    destroy();
    m_width = width;
    m_height = height;
    m_hasDepth = withDepth;

    glGenFramebuffers(1, &m_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);

    glGenTextures(1, &m_texture);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_texture, 0);

    if (withDepth) {
        glGenRenderbuffers(1, &m_depth);
        glBindRenderbuffer(GL_RENDERBUFFER, m_depth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_depth);
    }

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "Framebuffer incomplete!" << std::endl;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        destroy();
        return false;
    }

    // Fresh GL textures contain stale GPU memory (often literally the last
    // thing on screen) -- shaders with persistent pass buffers were flashing
    // a "screenshot" ghost on their first frames. Clear to transparent black.
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | (withDepth ? GL_DEPTH_BUFFER_BIT : 0));

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}

bool Framebuffer::createHalfFloat(int width, int height, bool fullFloat) {
    destroy();
    m_width = width;
    m_height = height;
    m_hasDepth = false;
    m_halfFloat = true;
    m_fullFloat = fullFloat;

    glGenFramebuffers(1, &m_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);

    glGenTextures(1, &m_texture);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    if (fullFloat)
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
    else
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_texture, 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
        // Same stale-memory clear as create() -- see comment there.
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "Half-float framebuffer incomplete, falling back to RGBA8" << std::endl;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        destroy();
        m_halfFloat = false;
        m_fullFloat = false;
        return create(width, height, false);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}

void Framebuffer::resize(int width, int height) {
    if (width == m_width && height == m_height) return;
    if (m_halfFloat)
        createHalfFloat(width, height, m_fullFloat);
    else
        create(width, height, m_hasDepth);
}

void Framebuffer::bind() const {
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glViewport(0, 0, m_width, m_height);
}

void Framebuffer::unbind() {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}
