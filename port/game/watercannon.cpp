// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "watercannon.h"
#include "cargens.h"   // Rand32Critical
#include "gfx/assets.h"
#include "world/worldrenderer.h"
#include <glad/gl.h>
#include <algorithm>
#include <cmath>

void WaterCannon::input(const int32_t p[3], const int32_t v[3], uint32_t frame) {   // CWaterCannon::Update_NewInput
    for (int k = 0; k < 3; ++k) { pos_[head_][k] = p[k]; vel_[head_][k] = v[k]; }
    age_[head_] = 1;
    if (flags & 1)   // a little extra lift that changes with the frame counter
        vel_[head_][2] += (int32_t)(((frame & 15) << 7) + Rand32Critical(0x51)) >> 1;
    // (flag 8: cParticleEmitterWaterSplash at the nozzle - not ported yet)
}

void WaterCannon::step() {   // CWaterCannon::Update_OncePerFrame
    head_ = (int16_t)((head_ + 1) & 15);
    age_[head_] = 0;
    for (int k = 0; k < 16; ++k) {
        uint8_t& age = age_[k];
        int32_t* v = vel_[k];
        int32_t* p = pos_[k];
        if ((flags & 2) && age == 1) {
            const int64_t l2 = (int64_t)v[0] * v[0] + (int64_t)v[1] * v[1] + (int64_t)v[2] * v[2];
            if (l2 >= 0x418A) {   // Normalise: + one unit along the jet
                const double l = std::sqrt((double)l2);
                for (int i = 0; i < 3; ++i) v[i] += (int32_t)std::lround(v[i] * 4096.0 / l);
            }
        } else if (age >= 2) {
            for (int i = 0; i < 3; ++i) v[i] = (int32_t)(((int64_t)v[i] * 0xE66) >> 12);
            if (flags & 4) v[2] -= 0x10B;
            for (int i = 0; i < 3; ++i) p[i] += v[i];
            if (p[2] <= -0xA001) age = 0;
        }
        if (age >= 1 && age <= 24) ++age;
    }
}

void WaterCannon::render(const WorldCamera& cam, uint32_t ambient) const {   // CWaterCannon::Render
    int texture;
    uint16_t r[4];
    if (!Assets_EffectSprite(20, texture, r)) return;
    int tw = 256, th = 256;
    Assets_TextureSize(texture, &tw, &th);
    const float u0 = r[0] / (float)tw, u1 = (r[0] + r[2]) / (float)tw, v0 = r[1] / (float)th, v1 = (r[1] + r[3]) / (float)th;
    // colour: RGB555 -> 8 bit, then 0x99 / 0x100 of the way to the ambient colour
    auto mix = [&](uint16_t c555, float out[3]) {
        const int c[3] = {(c555 & 0x1F) << 3, (c555 >> 5 & 0x1F) << 3, (c555 >> 10 & 0x1F) << 3};
        for (int i = 0; i < 3; ++i) {
            const int a = (int)(ambient >> (8 * i) & 0xFF);
            out[i] = (c[i] + (a - c[i]) * 0x99 / 0x100) / 255.f;
        }
    };
    float base[3], light[3];
    mix(colour, base);
    mix(0x7FBD, light);
    const float alpha = (flags & 0x20) ? 0x7B / 255.f : 1.f;
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, Assets_Texture(texture));
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glDisable(GL_LIGHTING);
    glBegin(GL_TRIANGLE_STRIP);
    float side[3] = {cam.right[0], cam.right[1], cam.right[2]};
    int drawn = 0;
    for (int n = 1; n <= 16 && drawn < maxPoints; ++n) {   // newest first
        const int k = (head_ - n) & 15;
        if (!age_[k]) break;
        const float p[3] = {pos_[k][0] / 4096.f, pos_[k][1] / 4096.f, pos_[k][2] / 4096.f};
        const float v[3] = {vel_[k][0] / 4096.f, vel_[k][1] / 4096.f, vel_[k][2] / 4096.f};
        // across the stream: perpendicular to the view and to the flow
        const float c[3] = {cam.fwd[1] * v[2] - cam.fwd[2] * v[1], cam.fwd[2] * v[0] - cam.fwd[0] * v[2], cam.fwd[0] * v[1] - cam.fwd[1] * v[0]};
        const float l = std::sqrt(c[0] * c[0] + c[1] * c[1] + c[2] * c[2]);
        if (l > 0.05f) for (int i = 0; i < 3; ++i) side[i] = c[i] / l;
        const float w = age_[k] / 16.f;
        const float* col = ((flags & 0x10) && (k & 3) == 0) ? light : base;
        glColor4f(col[0], col[1], col[2], alpha);
        const float tv = v0 + (v1 - v0) * (drawn & 1);
        glTexCoord2f(u0, tv); glVertex3f(p[0] + side[0] * w, p[1] + side[1] * w, p[2] + side[2] * w);
        glTexCoord2f(u1, tv); glVertex3f(p[0] - side[0] * w, p[1] - side[1] * w, p[2] - side[2] * w);
        ++drawn;
    }
    glEnd();
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glDisable(GL_TEXTURE_2D);
}

Fountain::Fountain(const int32_t p[3], const int16_t v[3]) {
    for (int k = 0; k < 3; ++k) { pos_[k] = p[k]; vel_[k] = v[k]; }
    jet_.flags = 0xDE | 1 | 0x100;   // cFountainStream(..., flags 1, keep the default flags, colour 0x7F39)
    jet_.colour = 0x7F39;
}

void Fountain::tick(uint32_t frame) {   // cFountainStream::Process -> CWaterCannons::UpdateOne, then Process
    // the jet wobbles: + (frame * 0xF4000 & 0xFC000) * 0x100805 >> 32 (up to about 6 %)
    const uint32_t k = (uint32_t)(((uint64_t)((frame * 0xF4000u) & 0xFC000u) * 0x100805u) >> 32);
    int32_t v[3];
    for (int i = 0; i < 3; ++i) v[i] = (int16_t)(vel_[i] + (int16_t)((uint32_t)(k * vel_[i]) >> 12));
    jet_.input(pos_, v, frame);
    jet_.step();
}
