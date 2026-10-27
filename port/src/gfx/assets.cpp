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
// Mod textures. A layer (a PNG with transparency) or a rectangle patch is composited on the GPU: the game's texture
// and the PNG are drawn into a temporary render target, alpha-blended over the rectangle with the destination alpha
// kept, and the result is copied back into the same texture object (so every handle stays valid). Nothing is read
// back from the GPU, which used to stall each 1024 x 1024 compressed texture for about 20 ms.
static std::map<int, bool> g_patched;   // ids whose texture now holds mod pixels
static RenderTables g_renderTables;
static int g_effectTexture = -1;
static std::vector<std::array<uint16_t, 4>> g_effectRects;
static int g_pickupTexture = -1;
static std::vector<std::array<uint16_t, 4>> g_pickupRects;

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
        // cAssetManager::LoadAssets loads 0x14C bytes of gGameDir[12] at +0xF0: the pickup sheet's texture id, u16 pad
        // and 41 rects, one per ePickupTexture (cBasePickup::ManagedRender, RenderWorldScaleCamAligned's sheet).
        std::vector<uint8_t> pickups;
        const size_t kPickups = 41, kPickupEnd = 4 + kPickups * 8;
        if (g_pak.read(gameDir[12], pickups) && pickups.size() >= kPickupEnd) {
            uint16_t texture;
            std::memcpy(&texture, pickups.data(), 2);
            g_pickupTexture = texture;
            g_pickupRects.resize(kPickups);
            std::memcpy(g_pickupRects.data(), pickups.data() + 4, kPickups * 8);
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

bool Assets_PickupSprite(int sprite, int& texture, uint16_t rect[4]) {
    if (g_pickupTexture < 0 || sprite < 0 || sprite >= static_cast<int>(g_pickupRects.size())) return false;
    texture = g_pickupTexture;
    std::memcpy(rect, g_pickupRects[sprite].data(), sizeof(uint16_t) * 4);
    return true;
}

// Draws the PNG pixels over [fx, fx + fw] x [fy, fy + fh] of texture id's image (fractions of its size).
static bool compositePNG(int id, float fx, float fy, float fw, float fh, const unsigned char* px, int sw, int sh) {
    const unsigned tex = Assets_Texture(id);
    if (!tex) return false;
    GLint w = 0, h = 0;
    glBindTexture(GL_TEXTURE_2D, tex);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);
    if (w <= 0 || h <= 0) return false;
    GLint oldDraw = 0, oldRead = 0, oldProgram = 0, oldTex = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &oldDraw);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &oldRead);
    glGetIntegerv(GL_CURRENT_PROGRAM, &oldProgram);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &oldTex);
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity(); glOrtho(0, 1, 0, 1, -1, 1);
    glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
    glUseProgram(0);
    GLuint target = 0, layer = 0, fbo = 0;
    glGenTextures(1, &target);
    glBindTexture(GL_TEXTURE_2D, target);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target, 0);
    const bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    if (ok) {
        glViewport(0, 0, w, h);
        glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glDisable(GL_SCISSOR_TEST); glDisable(GL_ALPHA_TEST);
        glDisable(GL_FOG); glDisable(GL_LIGHTING);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glEnable(GL_TEXTURE_2D);
        glColor4f(1, 1, 1, 1);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        auto quad = [](float x0, float y0, float x1, float y1) {   // texture rows map to y, row 0 at 0
            glBegin(GL_QUADS);
            glTexCoord2f(0, 0); glVertex2f(x0, y0); glTexCoord2f(1, 0); glVertex2f(x1, y0);
            glTexCoord2f(1, 1); glVertex2f(x1, y1); glTexCoord2f(0, 1); glVertex2f(x0, y1);
            glEnd();
        };
        // the current image, as it is
        glDisable(GL_BLEND);
        glBindTexture(GL_TEXTURE_2D, tex);
        GLint minF = GL_LINEAR, magF = GL_LINEAR;
        glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &minF);
        glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, &magF);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        quad(0, 0, 1, 1);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minF);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magF);
        // the PNG over its rectangle: colour blended by its alpha, the game's alpha kept
        glGenTextures(1, &layer);
        glBindTexture(GL_TEXTURE_2D, layer);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, sw, sh, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
        quad(fx, fy, fx + fw, fy + fh);
        // back into the game's texture object
        glBindTexture(GL_TEXTURE_2D, tex);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 0, 0, w, h, 0);
    }
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)oldDraw);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)oldRead);
    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &target);
    if (layer) glDeleteTextures(1, &layer);
    glMatrixMode(GL_PROJECTION); glPopMatrix();
    glMatrixMode(GL_MODELVIEW); glPopMatrix();
    glPopAttrib();
    glUseProgram((GLuint)oldProgram);
    glBindTexture(GL_TEXTURE_2D, (GLuint)oldTex);
    if (ok) { g_patched[id] = true; g_sizes.erase(id); }
    return ok;
}

bool Assets_OverrideTexturePNG(int id, const std::string& pngPath) {
    int w, h, n;
    unsigned char* px = stbi_load(pngPath.c_str(), &w, &h, &n, 4);
    if (!px) return false;
    bool layer = false;   // any transparency: draw it over the game's texture instead of replacing it
    for (size_t i = 3; i < (size_t)w * h * 4 && !layer; i += 4) layer = px[i] < 255;
    if (layer) {
        Assets_RestoreTexture(id);   // (the game's own image to draw on: reloaded from game.pak, under a millisecond)
        const bool ok = compositePNG(id, 0.f, 0.f, 1.f, 1.f, px, w, h);
        stbi_image_free(px);
        return ok;
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
    int w, h, n;
    unsigned char* px = stbi_load(pngPath.c_str(), &w, &h, &n, 4);
    if (!px) return false;
    const bool ok = compositePNG(id, fx, fy, fw, fh, px, w, h);
    stbi_image_free(px);
    return ok;
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
    if (it != g_cache.end()) return it->second;
    TexInfo ti;
    unsigned tex = g_open && g_loader.load((uint32_t)id, &ti) ? ti.gl : 0;
    g_cache[id] = tex;
    return tex;
}
