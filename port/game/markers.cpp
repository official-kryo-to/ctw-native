// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "markers.h"
#include "gfx/assets.h"
#include "os/pak.h"
#include "os/datafile.h"
#include <glad/gl.h>
#include <cmath>
#include <cstring>
#include <vector>

bool Markers::load(const std::string& dataDir) {
    std::vector<uint8_t> t;
    coloursOk_ = Data_ReadAll(dataDir + "/marker_tables.bin", t) && t.size() == 8 + 33 + 9 &&
                 !std::memcmp(t.data(), "CTWMARK2", 8);
    if (coloursOk_) {
        for (int i = 0; i < 11; ++i)
            colours_[i] = (uint16_t)(t[8 + i * 3] | t[9 + i * 3] << 5 | t[10 + i * 3] << 10);
        std::memcpy(palettes_, &t[8 + 33], 9);
    }
    std::vector<uint8_t> dir, d;   // cAssetManager::LoadAssets: gGameDir[13] into +0x248, its two textures from +0x24A
    uint16_t gd[28];
    if (Assets_Pak().read(0, dir) && dir.size() >= sizeof gd) {
        std::memcpy(gd, dir.data(), sizeof gd);
        if (Assets_Pak().read(gd[13], d) && d.size() >= 6) texture_ = d[4] | d[5] << 8;
    }
    return coloursOk_ && texture_ >= 0;
}

uint16_t Markers::paletteColour(int palette) const {
    return colour(palette >= 4 && palette <= 12 && coloursOk_ ? palettes_[palette - 4] : 2);
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

void Markers::renderChevrons(const int32_t centre[3], int colourIndex, int16_t angle, int type, int32_t size,
                             uint32_t frame) const {
    int count, w;
    bool runs;
    switch (type) {   // eChevronType
        case 0: w = (int)(((uint32_t)size * 3u) >> 2); count = 4; runs = true; break;
        case 1: w = size >> 1; count = 4; runs = true; break;
        case 2: w = size >> 1; count = 2; runs = false; break;
        default: return;
    }
    const uint16_t c = colour(colourIndex);
    // sub_90AA2C: each component below 16 (else 16) plus 15 is the colour that runs along them
    const unsigned r = (c & 0x10) ? 0x10 : (c & 0x1F), g = ((c >> 5) & 0x10) ? 0x10 : (c >> 5 & 0x1F),
                   b = ((c >> 10) & 0x10) ? 0x10 : (c >> 10 & 0x1F);
    const uint16_t lit = (uint16_t)((r + 0xF) | (g + 0xF) << 5 | (b + 0xF) << 10);
    const int32_t step = 0x1000, depth = 0x1000;   // one unit apart, each arm a unit deep (before the x 2 scale)
    const unsigned period = (unsigned)((step >> 9) & ~7);   // 8 frames
    const unsigned f8 = frame % period, run = frame % (unsigned)(count * 4);
    int32_t base = -(step >> 1) * count + (int32_t)((f8 << 12) >> 3);
    glDisable(GL_TEXTURE_2D);   // cGlInterface::UnbindTexture
    glDisable(GL_LIGHTING);
    glDisable(GL_CULL_FACE);    // (cAreaManager::Render: Cull(0))
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();   // CMatrix43::MakeRotationZ(angle ^ 0x8000), scaled 2 x 2 x 1, 0.36 above the centre
    glTranslatef(centre[0] / 4096.f, centre[1] / 4096.f, (centre[2] + 0xCCC + 0x28F) / 4096.f);
    const float a = (int16_t)(angle ^ 0x8000) * 9.587378e-05f;
    const float rot[16] = {std::cos(a), -std::sin(a), 0, 0, std::sin(a), std::cos(a), 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    glMultMatrixf(rot);
    glScalef(2.f, 2.f, 1.f);
    // immsprite3d::Render(colour, p0, z, p1, p3, p2): the triangles p0 p1 p2 and p1 p3 p2
    auto arm = [](float x0, float y0, float x1, float y1, float x3, float y3, float x2, float y2) {
        glVertex3f(x0, y0, 0); glVertex3f(x1, y1, 0); glVertex3f(x2, y2, 0);
        glVertex3f(x1, y1, 0); glVertex3f(x3, y3, 0); glVertex3f(x2, y2, 0);
    };
    for (int i = 0; i < count; ++i) {
        unsigned alpha = 16;
        if (i == 0) alpha = f8 * 2 + 1;   // the first fades in, the last out, as they move along a step
        if (i == count - 1) alpha = i == 0 ? alpha - (unsigned)(((int)(f8 << 12) >> 12) * (int)alpha) / period
                                           : 16 - (f8 * 16) / period;
        const bool bright = runs && (int)(run >> 2) - i == 0;   // the run lights one every 4 frames
        const uint16_t col = bright ? lit : c;
        glColor4f((col & 0x1F) / 31.f, (col >> 5 & 0x1F) / 31.f, (col >> 10 & 0x1F) / 31.f, alpha / 31.f);
        const float k = 1.f / 4096.f, wf = w * k, y0 = base * k, y1 = (base + depth) * k, y2 = (base + 2 * depth) * k;
        glBegin(GL_TRIANGLES);
        arm(wf, y1, wf, y0, 0.f, y1, 0.f, y2);     // the right arm, up to the middle
        arm(0.f, y2, 0.f, y1, -wf, y0, -wf, y1);   // and the left
        glEnd();
        base += step;
    }
    glPopMatrix();
    glColor4f(1, 1, 1, 1);   // immsprite3d::End
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
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
