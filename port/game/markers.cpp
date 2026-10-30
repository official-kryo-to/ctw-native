// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "markers.h"
#include "gfx/assets.h"
#include "os/pak.h"
#include "os/datafile.h"
#include <glad/gl.h>
#include <cstring>
#include <vector>

bool Markers::load(const std::string& dataDir) {
    std::vector<uint8_t> t;
    coloursOk_ = Data_ReadAll(dataDir + "/marker_tables.bin", t) && t.size() == 8 + 33 && !std::memcmp(t.data(), "CTWMARK1", 8);
    if (coloursOk_)
        for (int i = 0; i < 11; ++i)
            colours_[i] = (uint16_t)(t[8 + i * 3] | t[9 + i * 3] << 5 | t[10 + i * 3] << 10);
    std::vector<uint8_t> dir, d;   // cAssetManager::LoadAssets: gGameDir[13] into +0x248, its two textures from +0x24A
    uint16_t gd[28];
    if (Assets_Pak().read(0, dir) && dir.size() >= sizeof gd) {
        std::memcpy(gd, dir.data(), sizeof gd);
        if (Assets_Pak().read(gd[13], d) && d.size() >= 6) texture_ = d[4] | d[5] << 8;
    }
    return coloursOk_ && texture_ >= 0;
}

uint16_t Markers::colour(int i) const {
    return coloursOk_ && i >= 0 && i < 11 ? colours_[i] : 0x7FFF;
}

namespace {
// sub_909F88: a flat square of half-width `half` 0.5 above `at`, the whole texture, RGB555 colour and 5-bit alpha
void flatSquare(const int32_t at[3], uint16_t c, int32_t half, unsigned alpha5) {
    const uint32_t t = (uint32_t)(((uint64_t)((alpha5 & 0xFFFF) << 12) * 0x10842109u) >> 33);   // alpha x 4096 / 31
    const unsigned a = (((t << 20) - (t << 12)) >> 24) & 0xFF;
    glColor4ub((GLubyte)((c & 0x1F) << 3), (GLubyte)((c >> 5 & 0x1F) << 3), (GLubyte)((c >> 10 & 0x1F) << 3), (GLubyte)a);
    const float x0 = (at[0] - half) / 4096.f, y0 = (at[1] - half) / 4096.f, w = (half << 1) / 4096.f;
    const float z = ((at[2] << 4) + 0x8000) / 65536.f;
    glBegin(GL_TRIANGLE_STRIP);
    glTexCoord2f(0.f, 0.f); glVertex3f(x0, y0, z);
    glTexCoord2f(0.f, 1.f); glVertex3f(x0, y0 + w, z);
    glTexCoord2f(1.f, 0.f); glVertex3f(x0 + w, y0, z);
    glTexCoord2f(1.f, 1.f); glVertex3f(x0 + w, y0 + w, z);
    glEnd();
}
}

void Markers::renderMarker3D(const int32_t centre[3], int colourIndex, int32_t size, uint32_t frame) const {
    const GLuint tex = texture_ >= 0 ? Assets_Texture(texture_) : 0;
    if (!tex) return;
    const uint16_t c = colour(colourIndex);
    const int32_t phase = (int32_t)(frame & 15) << 8;
    const unsigned a1 = (unsigned)(phase * 0x1E + 0x1000) >> 12;
    const int32_t s = (int32_t)(((int64_t)size * 0xCCC) >> 12);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, tex);
    glDisable(GL_LIGHTING);
    glDisable(GL_CULL_FACE);
    glDisable(GL_ALPHA_TEST);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    const int32_t low[3] = {centre[0], centre[1], centre[2] + 0x199}, high[3] = {centre[0], centre[1], centre[2] + 0x333};
    flatSquare(low, c, (int32_t)(((int64_t)s * (0x2000 - phase)) >> 12), a1);
    flatSquare(high, c, s, 0x1F - a1);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glDisable(GL_TEXTURE_2D);
}
