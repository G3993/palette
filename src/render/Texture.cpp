#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "render/Texture.h"
#include <iostream>

Texture::~Texture() { destroy(); }

Texture::Texture(Texture&& other) noexcept
    : m_texture(other.m_texture), m_width(other.m_width), m_height(other.m_height) {
    other.m_texture = 0;
}

Texture& Texture::operator=(Texture&& other) noexcept {
    if (this != &other) {
        destroy();
        m_texture = other.m_texture;
        m_width = other.m_width;
        m_height = other.m_height;
        other.m_texture = 0;
    }
    return *this;
}

void Texture::destroy() {
    if (m_texture) {
        glDeleteTextures(1, &m_texture);
        m_texture = 0;
    }
}

bool Texture::loadFromFile(const std::string& path) {
    stbi_set_flip_vertically_on_load(true);
    int channels;
    unsigned char* data = stbi_load(path.c_str(), &m_width, &m_height, &channels, 4);
    if (!data) {
        std::cerr << "Failed to load image: " << path << std::endl;
        return false;
    }

    destroy();
    glGenTextures(1, &m_texture);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    // Reset unpack state: the ImGui GL3 backend leaves GL_UNPACK_ROW_LENGTH
    // at its atlas width after dynamic font updates, which strides this
    // upload through the buffer (first rows squished, rest heap noise).
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_width, m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    stbi_image_free(data);
    return true;
}

void Texture::formatForInternal(GLenum internalFormat, GLenum& format, GLenum& type) {
    switch (internalFormat) {
        case GL_R8:
            format = GL_RED; type = GL_UNSIGNED_BYTE; break;
        case GL_R32F:
        case GL_R16F:
            format = GL_RED; type = GL_FLOAT; break;
        case GL_RG8:
            format = GL_RG; type = GL_UNSIGNED_BYTE; break;
        case GL_RG32F:
        case GL_RG16F:
            format = GL_RG; type = GL_FLOAT; break;
        case GL_RGB8:
            format = GL_RGB; type = GL_UNSIGNED_BYTE; break;
        case GL_RGB32F:
        case GL_RGB16F:
            format = GL_RGB; type = GL_FLOAT; break;
        case GL_RGBA16F:
        case GL_RGBA32F:
            format = GL_RGBA; type = GL_FLOAT; break;
        case GL_RGBA8:
        default:
            format = GL_RGBA; type = GL_UNSIGNED_BYTE; break;
    }
}

bool Texture::createEmpty(int width, int height, GLenum internalFormat) {
    destroy();
    m_width = width;
    m_height = height;
    m_internalFormat = internalFormat;
    GLenum format, type;
    formatForInternal(internalFormat, format, type);
    glGenTextures(1, &m_texture);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, format, type, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return true;
}

void Texture::updateData(const void* data, int width, int height, GLenum format, GLenum type) {
    if (!m_texture) return;
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
    if (width != m_width || height != m_height) {
        m_width = width;
        m_height = height;
        glTexImage2D(GL_TEXTURE_2D, 0, m_internalFormat, width, height, 0, format, type, data);
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, format, type, data);
    }
}
