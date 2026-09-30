// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "assets.h"
#include "os/pak.h"
#include "os/dxtbin.h"
#include "gfx/texload.h"
#include <glad/gl.h>
#include <stb_image.h>
#include <stb_image_write.h>
#include <algorithm>
#include <map>
#include <cstdio>
#include <array>
#include <cstring>
#include <vector>

static Pak g_pak;
static DxtBin g_dxt;
static TextureLoader g_loader;
static std::map<int, unsigned> g_cache;
static std::map<int, std::pair<int, int>> g_sizes;
static bool g_open = false;
// Rectangle patches (mods): the texture is read back once, patched on the CPU and uploaded again when it is next used.
struct PatchedTexture { int w = 0, h = 0; std::vector<unsigned char> rgba; bool dirty = false; };
static std::map<int, PatchedTexture> g_patched;
static std::map<std::string, std::vector<unsigned char>> g_patchPng;   // path -> RGBA
static std::map<std::string, std::pair<int, int>> g_patchPngSize;
static void uploadPatched(int id, unsigned tex) {
    PatchedTexture& p = g_patched[id];
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, p.w, p.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, p.rgba.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    p.dirty = false;
}
static RenderTables g_renderTables;
static int g_effectTexture = -1;
static std::vector<std::array<uint16_t, 4>> g_effectRects;

bool Assets_Open(const std::string& dataDir) {
    if (g_open) return true;
    if (!g_renderTables.load(dataDir + "/render_tables.bin")) {
        std::fprintf(stderr, "render_tables.bin missing or invalid: run scripts/setup_game.py\n");
        return false;
    }
    if (!g_pak.open(dataDir + "/game.pak")) return false;
    std::vector<uint8_t> directory, effects;
    uint16_t gameDir[28];
    if (g_pak.read(0, directory) && directory.size() >= sizeof gameDir) {
        std::memcpy(gameDir, directory.data(), sizeof gameDir);
        // cAssetManager (gGameDir[9]): u16 texture id, u16 pad, then 26 sprite rects {u16 x, y, w, h}. The resource
        // is padded to whole pages, so its size says nothing about the number of rects.
        const size_t kSprites = 26, kEnd = 4 + kSprites * 8;
        if (g_pak.read(gameDir[9], effects) && effects.size() >= kEnd) {
            uint16_t texture;
            std::memcpy(&texture, effects.data(), 2);
            g_effectTexture = texture;
            g_effectRects.resize(kSprites);
            std::memcpy(g_effectRects.data(), effects.data() + 4, kSprites * 8);
        }
    }
    bool dxt = g_dxt.open(dataDir + "/dxt.bin");
    g_loader.init(&g_pak, dxt ? &g_dxt : nullptr);
    g_open = true;
    return true;
}

Pak& Assets_Pak() { return g_pak; }
const RenderTables& Assets_RenderTables() { return g_renderTables; }
bool Assets_EffectSprite(int sprite, int& texture, uint16_t rect[4]) {
    if (g_effectTexture < 0 || g_effectRects.empty()) return false;
    if (sprite < 0 || sprite >= static_cast<int>(g_effectRects.size())) sprite = 0;
    texture = g_effectTexture;
    std::memcpy(rect, g_effectRects[sprite].data(), sizeof(uint16_t) * 4);
    return true;
}

bool Assets_OverrideTexturePNG(int id, const std::string& pngPath) {
    int w, h, n;
    unsigned char* px = stbi_load(pngPath.c_str(), &w, &h, &n, 4);
    if (!px) return false;
    bool layer = false;   // any transparency: draw it over the game's texture instead of replacing it
    for (size_t i = 3; i < (size_t)w * h * 4 && !layer; i += 4) layer = px[i] < 255;
    if (layer) {
        stbi_image_free(px);
        Assets_RestoreTexture(id);   // (a fresh copy of the game's texture to draw on)
        return Assets_PatchTexturePNG(id, 0.f, 0.f, 1.f, 1.f, pngPath);
    }
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    stbi_image_free(px);
    g_patched.erase(id);
    auto it = g_cache.find(id);
    if (it != g_cache.end() && it->second) glDeleteTextures(1, &it->second);
    g_cache[id] = t;
    g_sizes.erase(id);
    return true;
}

bool Assets_PatchTexturePNG(int id, float fx, float fy, float fw, float fh, const std::string& pngPath) {
    if (fw <= 0 || fh <= 0) return false;
    auto png = g_patchPng.find(pngPath);
    if (png == g_patchPng.end()) {
        int w, h, n;
        unsigned char* px = stbi_load(pngPath.c_str(), &w, &h, &n, 4);
        if (!px) return false;
        png = g_patchPng.emplace(pngPath, std::vector<unsigned char>(px, px + (size_t)w * h * 4)).first;
        g_patchPngSize[pngPath] = {w, h};
        stbi_image_free(px);
    }
    int sw = g_patchPngSize[pngPath].first, sh = g_patchPngSize[pngPath].second;
    unsigned tex = Assets_Texture(id);
    if (!tex) return false;
    PatchedTexture& p = g_patched[id];
    if (p.rgba.empty()) {   // first patch: read the game's texture back as RGBA (this also decodes compressed formats)
        GLint tw = 0, th = 0;
        glBindTexture(GL_TEXTURE_2D, tex);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &tw);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &th);
        if (tw <= 0 || th <= 0) { g_patched.erase(id); return false; }
        p.w = tw; p.h = th;
        p.rgba.resize((size_t)tw * th * 4);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, p.rgba.data());
    }
    int x0 = (int)(fx * p.w + 0.5f), y0 = (int)(fy * p.h + 0.5f);
    int x1 = (int)((fx + fw) * p.w + 0.5f), y1 = (int)((fy + fh) * p.h + 0.5f);
    x0 = std::max(0, x0); y0 = std::max(0, y0); x1 = std::min(p.w, x1); y1 = std::min(p.h, y1);
    if (x1 <= x0 || y1 <= y0) return false;
    const std::vector<unsigned char>& src = png->second;
    for (int y = y0; y < y1; ++y) {
        int sy = std::min(sh - 1, (y - y0) * sh / (y1 - y0));
        for (int x = x0; x < x1; ++x) {
            int sx = std::min(sw - 1, (x - x0) * sw / (x1 - x0));
            const unsigned char* s4 = &src[((size_t)sy * sw + sx) * 4];
            unsigned char* d4 = &p.rgba[((size_t)y * p.w + x) * 4];
            unsigned a = s4[3];   // blend over the original so transparent PNG pixels keep the game's artwork
            for (int c = 0; c < 3; ++c) d4[c] = (unsigned char)((s4[c] * a + d4[c] * (255 - a)) / 255);
        }
    }
    p.dirty = true;
    return true;
}

int Assets_TextureIdLimit() { return g_open ? (int)g_pak.count() : 0; }

bool Assets_ExportTexturePNG(int id, const std::string& pngPath) {
    if (g_patched.count(id) || !g_open) return false;
    TexInfo ti;
    if (!g_loader.load((uint32_t)id, &ti) || !ti.gl) return false;   // a private copy: mods do not show up here
    GLint w = 0, h = 0;
    glBindTexture(GL_TEXTURE_2D, ti.gl);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);
    bool ok = w > 0 && h > 0;
    if (ok) {
        std::vector<unsigned char> px((size_t)w * h * 4);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        FILE* f = fopen(pngPath.c_str(), "wb");
        ok = f && stbi_write_png_to_func([](void* file, void* data, int size) { fwrite(data, 1, (size_t)size, (FILE*)file); },
                                         f, w, h, 4, px.data(), w * 4) != 0;
        if (f) fclose(f);
    }
    GLuint t = ti.gl;
    glDeleteTextures(1, &t);
    return ok;
}

void Assets_RestoreTexture(int id) {
    g_patched.erase(id);
    auto it = g_cache.find(id);
    if (it == g_cache.end()) return;
    if (it->second) glDeleteTextures(1, &it->second);
    g_cache.erase(it);
    g_sizes.erase(id);
}

bool Assets_TextureSize(int id, int* w, int* h) {
    auto it = g_sizes.find(id);
    if (it == g_sizes.end()) {
        unsigned t = Assets_Texture(id);
        if (!t) return false;
        GLint tw = 0, th = 0;
        glBindTexture(GL_TEXTURE_2D, t);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &tw);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &th);
        it = g_sizes.emplace(id, std::make_pair((int)tw, (int)th)).first;
    }
    *w = it->second.first;
    *h = it->second.second;
    return *w > 0 && *h > 0;
}

unsigned Assets_Texture(int id) {
    auto it = g_cache.find(id);
    if (it != g_cache.end()) {
        if (it->second && !g_patched.empty()) {
            auto p = g_patched.find(id);
            if (p != g_patched.end() && p->second.dirty) uploadPatched(id, it->second);
        }
        return it->second;
    }
    TexInfo ti;
    unsigned tex = g_open && g_loader.load((uint32_t)id, &ti) ? ti.gl : 0;
    g_cache[id] = tex;
    return tex;
}
