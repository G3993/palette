#pragma once
#include <glad/glad.h>

// Generates a 37-cell font atlas texture matching Shader-Claw 3 layout:
// A-Z (indices 0-25), space (26), 0-9 (indices 27-36)
// Each cell is 1/37 of the texture width. Shaders sample:
//   texture(fontAtlasTex, vec2((float(ch) + uv.x) / 37.0, uv.y)).r
class FontAtlas {
public:
    // Get the singleton atlas texture (created on first call)
    static GLuint texture();

    // Destroy the texture (call at shutdown)
    static void destroy();

private:
    static GLuint s_texture;
    static void generate();
};
