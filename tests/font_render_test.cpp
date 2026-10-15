// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#define SDL_MAIN_HANDLED
#include "gfx/font.h"
#include <SDL.h>
#include <glad/gl.h>
#include <stb_image_write.h>
#include <cstdio>
#include <filesystem>
#include <fstream>

static int failures;
static void check(bool ok, const char* description) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", description); ++failures; }
}

// A synthetic, solid atlas makes every drawn glyph visible without using game files.
static bool atlas(const std::filesystem::path& path, uint8_t red, uint8_t green, uint8_t blue) {
    std::vector<uint8_t> pixels(64 * 32 * 4);
    for (size_t i = 0; i < pixels.size(); i += 4) {
        pixels[i] = red; pixels[i + 1] = green; pixels[i + 2] = blue; pixels[i + 3] = 255;
    }
    std::ofstream out(path, std::ios::binary);
    auto write = [](void* context, void* data, int size) {
        static_cast<std::ofstream*>(context)->write(static_cast<const char*>(data), size);
    };
    return stbi_write_png_to_func(write, &out, 64, 32, 4, pixels.data(), 64 * 4) && out.good();
}

static std::vector<uint8_t> table(uint16_t height, uint16_t width) {
    const uint16_t count = 96;
    std::vector<uint8_t> bin(4 + count * 6);
    auto set = [&](size_t at, uint16_t value) { bin[at] = (uint8_t)value; bin[at + 1] = (uint8_t)(value >> 8); };
    set(0, count); set(2, height);
    for (unsigned i = 0; i < count; ++i) set(4 + i * 6, width);
    set(4 + (u'B' - u' ') * 6, 7);    // B advances six pixels; other glyphs use width - 1.
    return bin;
}

static bool pixel(int x, int y, uint8_t red, uint8_t green, uint8_t blue) {
    uint8_t rgba[4]{};
    glReadPixels(x, 96 - 1 - y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    return rgba[0] == red && rgba[1] == green && rgba[2] == blue;
}

static void clear() { glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT); glColor4f(1, 1, 1, 1); }

static void run(const std::filesystem::path& folder) {
    const auto white = folder / "white.png", red = folder / "red.png", broken = folder / "broken.png";
    check(atlas(white, 255, 255, 255) && atlas(red, 255, 0, 0), "synthetic font atlases are written");
    { std::ofstream out(broken, std::ios::binary); out << "this is not a PNG"; }
    BitmapFont font;
    const auto original = table(12, 9), replacement = table(20, 11);
    check(font.load(original, white.string()), "synthetic font loads");
    GLint oldTexture = 0; glGetIntegerv(GL_TEXTURE_BINDING_2D, &oldTexture);
    check(font.lineHeight() == 12 && font.glyphWidth(u'A') == 8 && font.glyphWidth(u'B') == 6,
          "Latin glyph metrics retain the reference one-pixel advance inset");
    check(font.textWidth(u"AB\nB") == 14 && font.textWidth(u"\nAB\n") == 14 && font.textWidth(u"\n") == 0,
          "explicit lines measure their widest line rather than adding all lines");
    check(font.textWidth(u"A\xFF03" u"B") == 14 && font.glyphWidth((char16_t)(0x8000 | u'A')) == 8 &&
          font.glyphWidth(u'\u00AD') == font.glyphWidth(u' '), "colour tags and flagged glyphs preserve their advances");

    clear();
    check(font.draw(u"AB\nB", 8, 8, 1) == 14, "newline returns the pen position on the last line");
    check(pixel(10, 10, 255, 255, 255) && pixel(10, 22, 255, 255, 255) && pixel(18, 22, 0, 0, 0),
          "newlines reset the drawing column and advance one line height");
    clear();
    check(font.drawWrapped(u"AB\nB", 8, 8, 100, 1) == 24 && pixel(10, 22, 255, 255, 255),
          "explicit newline handling in wrapped text remains unchanged");

    auto truncated = replacement; truncated.pop_back();
    check(!font.load(truncated, red.string()) && !font.load(replacement, (folder / "missing.png").string()) &&
          !font.load(replacement, broken.string()), "invalid metadata, missing atlases and invalid PNGs fail");
    check(font.lineHeight() == 12 && font.glyphWidth(u'A') == 8, "failed reload keeps the previous glyph metrics");
    check(glIsTexture((GLuint)oldTexture), "failed reload keeps the previous font texture alive");
    clear(); font.draw(u"A", 8, 8, 1);
    check(pixel(10, 10, 255, 255, 255), "failed reload leaves the previous font drawable");

    check(font.load(replacement, red.string()) && font.lineHeight() == 20 && font.glyphWidth(u'A') == 10,
          "successful reload replaces both metrics and atlas");
    check(!glIsTexture((GLuint)oldTexture), "successful reload releases the old atlas texture");
    clear(); font.draw(u"A", 8, 8, 1);
    check(pixel(10, 10, 255, 0, 0), "successful reload uses its replacement image");
    font.setJapanese(true);
    check(font.glyphWidth(u'A') == 11, "Japanese glyph advances retain the full cell width");
    check(glGetError() == GL_NO_ERROR, "font rendering and reload produce no OpenGL errors");
}

int main() {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SKIP: no video device: %s\n", SDL_GetError());
        return 77;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_Window* window = SDL_CreateWindow("Font regression", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                          160, 96, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    SDL_GLContext context = window ? SDL_GL_CreateContext(window) : nullptr;
    if (!context || !gladLoadGL((GLADloadfunc)SDL_GL_GetProcAddress)) {
        std::fprintf(stderr, "SKIP: no OpenGL test context: %s\n", SDL_GetError());
        if (context) SDL_GL_DeleteContext(context);
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
        return 77;
    }
    glViewport(0, 0, 160, 96);
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0, 160, 96, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    const auto folder = std::filesystem::temp_directory_path() / ("ctw-font-test-" + std::to_string(SDL_GetTicks64()));
    std::filesystem::create_directories(folder);
    run(folder);
    std::filesystem::remove_all(folder);
    SDL_GL_DeleteContext(context); SDL_DestroyWindow(window); SDL_Quit();
    return failures ? 1 : 0;
}
