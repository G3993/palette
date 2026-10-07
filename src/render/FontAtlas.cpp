#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#include "render/FontAtlas.h"
#include <vector>
#include <cstdio>
#include <cstring>
#include <cstdlib>

GLuint FontAtlas::s_texture = 0;

GLuint FontAtlas::texture() {
    if (!s_texture) generate();
    return s_texture;
}

void FontAtlas::destroy() {
    if (s_texture) {
        glDeleteTextures(1, &s_texture);
        s_texture = 0;
    }
}

void FontAtlas::generate() {
    // Atlas layout: 37 cells horizontally, each cellW x cellH pixels
    // A-Z (0-25), space (26), 0-9 (27-36)
    const int ATLAS_CHARS = 37;
    const int cellW = 128;
    const int cellH = 180; // ~5:7 aspect like bitmap font grid
    const int atlasW = ATLAS_CHARS * cellW;
    const int atlasH = cellH;

    // Try to load a system font
    const char* fontPaths[] = {
#ifdef __APPLE__
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
        "/System/Library/Fonts/SFNS.ttf",
#elif defined(_WIN32)
        "C:/Windows/Fonts/arial.ttf",
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/calibri.ttf",
#else
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/ubuntu/Ubuntu-R.ttf",
#endif
        nullptr
    };

    std::vector<unsigned char> fontData;
    stbtt_fontinfo font;
    bool fontLoaded = false;

    for (int i = 0; fontPaths[i]; i++) {
        FILE* f = fopen(fontPaths[i], "rb");
        if (!f) continue;
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        fontData.resize(size);
        size_t bytesRead = fread(fontData.data(), 1, size, f);
        fclose(f);
        if (bytesRead != static_cast<size_t>(size)) continue;

        if (stbtt_InitFont(&font, fontData.data(), 0)) {
            fontLoaded = true;
            break;
        }
    }

    if (!fontLoaded) {
        fprintf(stderr, "FontAtlas: could not load any system font\n");
        // Create a blank texture so we don't crash
        glGenTextures(1, &s_texture);
        glBindTexture(GL_TEXTURE_2D, s_texture);
        std::vector<unsigned char> blank(atlasW * atlasH, 0);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, atlasW, atlasH, 0,
                     GL_RED, GL_UNSIGNED_BYTE, blank.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        return;
    }

    // Scale font to fill most of cell height
    float pixelHeight = cellH * 0.78f;
    float scale = stbtt_ScaleForPixelHeight(&font, pixelHeight);

    int ascent, descent, lineGap;
    stbtt_GetFontVMetrics(&font, &ascent, &descent, &lineGap);
    float scaledAscent = ascent * scale;

    // Character map: indices 0-25 = A-Z, 26 = space, 27-36 = 0-9
    const char* chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ 0123456789";

    // Allocate atlas buffer (single channel)
    std::vector<unsigned char> atlas(atlasW * atlasH, 0);

    for (int i = 0; i < ATLAS_CHARS; i++) {
        int codepoint = chars[i];
        if (codepoint == ' ') continue; // leave space cell blank

        int glyphIdx = stbtt_FindGlyphIndex(&font, codepoint);
        if (!glyphIdx) {
            printf("FontAtlas: no glyph for '%c' (codepoint %d)\n", (char)codepoint, codepoint);
            continue;
        }

        // Get glyph bitmap
        int gw, gh, xoff, yoff;
        unsigned char* bitmap = stbtt_GetGlyphBitmap(&font, scale, scale,
                                                      glyphIdx, &gw, &gh, &xoff, &yoff);
        if (!bitmap) {
            printf("FontAtlas: null bitmap for '%c'\n", (char)codepoint);
            continue;
        }
        printf("FontAtlas: [%d] '%c' glyph=%d size=%dx%d off=%d,%d\n",
               i, (char)codepoint, glyphIdx, gw, gh, xoff, yoff);

        // Get advance width for centering
        int advW, lsb;
        stbtt_GetGlyphHMetrics(&font, glyphIdx, &advW, &lsb);

        // Center glyph horizontally in cell
        int dstX = i * cellW + (cellW - gw) / 2;
        // Position vertically: baseline at scaledAscent, yoff is relative to top of cell
        // Flip vertically for GL (row 0 = bottom)
        int baselineY = (int)(cellH * 0.15f + scaledAscent);

        for (int y = 0; y < gh; y++) {
            // Source row (top-down from stb_truetype)
            int srcRow = y;
            // Destination row: flip so baseline is near bottom in GL coords
            // In GL texture, row 0 = bottom. stbtt yoff is from top.
            int dstRow = cellH - 1 - (baselineY + yoff + y);
            if (dstRow < 0 || dstRow >= cellH) continue;

            for (int x = 0; x < gw; x++) {
                int dx = dstX + x;
                if (dx < 0 || dx >= atlasW) continue;
                unsigned char val = bitmap[srcRow * gw + x];
                atlas[dstRow * atlasW + dx] = val;
            }
        }

        stbtt_FreeBitmap(bitmap, nullptr);
    }

    // Upload to GL
    glGenTextures(1, &s_texture);
    glBindTexture(GL_TEXTURE_2D, s_texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, atlasW, atlasH, 0,
                 GL_RED, GL_UNSIGNED_BYTE, atlas.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Swizzle so .r reads work AND .rgb reads return white
    GLint swizzle[] = { GL_RED, GL_RED, GL_RED, GL_RED };
    glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, swizzle);

    printf("FontAtlas: generated %dx%d atlas (%d chars)\n", atlasW, atlasH, ATLAS_CHARS);
}
