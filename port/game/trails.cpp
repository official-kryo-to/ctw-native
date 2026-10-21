// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "trails.h"
#include "fixedmath.h"
#include "gfx/assets.h"
#include "particles.h"
#include <glad/gl.h>
#include <algorithm>
#include <cmath>

static Trails g_trails;
Trails& TheTrails() { return g_trails; }

int32_t Trails::fallingEpsilon = 0xCC;

namespace {
int fastsin(int a) { return (int)(sinf((float)a * 9.587378e-05f) * 4096.f); }
int16_t atan2q(int32_t x, int32_t y) {   // ATan2: atan2f(x, y) x 10430.38, clamped to int16
    return (int16_t)std::clamp((int)(atan2f((float)x, (float)y) * 10430.378f), -32768, 32767);
}
int8_t next(int8_t i) { return (int8_t)(i + 1 > 11 ? 0 : i + 1); }
}   // namespace

void Trails::clear(int id) {   // ReleaseTrail / Reset: no owner, the origin at zero, no points
    owners_[id] = nullptr;
    Trail& t = trails_[id];
    t.origin[0] = t.origin[1] = t.origin[2] = 0;
    t.head = t.tail = 0;
}

void Trails::reset() {   // cTrailManager::Reset
    for (int i = 0; i < 12; ++i) clear(i);
}

int Trails::getFree(const Emitter* owner) {   // cTrailManager::GetFreeTrail
    for (int i = 0; i < 12; ++i)
        if (!owners_[i]) { owners_[i] = owner; return i; }
    return -1;
}

void Trails::init(int id, const int32_t pos[3], const int32_t dir[3], uint16_t colour, int32_t width, bool a,
                  bool taper, bool drift, int d, int segments, bool additive) {   // cTrailManager::InitTrail
    if (id < 0 || id >= 12) return;
    Trail& t = trails_[id];
    for (int k = 0; k < 3; ++k) t.origin[k] = pos[k] >> 3;
    t.heading = (int16_t)-atan2q(-dir[0], dir[1]);
    t.width = (int16_t)width;
    t.colour = colour;
    t.head = 0; t.tail = 0; t.fade = 0x1E; t.stopped = false;
    t.a = a; t.taper = taper; t.drift = drift;
    t.d = (uint8_t)d; t.segments = (uint8_t)segments;
    t.additive = additive;
}

void Trails::addPoint(int id, const int32_t pos[3]) {   // cTrailManager::AddPointToTrail (cSimpleTrail::AddPoint)
    if (id < 0 || id >= 12) return;
    Trail& t = trails_[id];
    if (t.stopped) return;
    // an uninitialised trail (origin at zero and empty) takes no points
    if (!t.origin[0] && !t.origin[1] && !t.origin[2] && !t.head && !t.tail) return;
    for (int k = 0; k < 3; ++k) t.pts[(int)t.tail][k] = (pos[k] >> 3) - t.origin[k];
    t.tail = next(t.tail);
    if (t.tail == t.head) t.head = next(t.head);   // full: the oldest point goes
}

void Trails::release(int id) {   // cTrailManager::ReleaseTrail
    if (id >= 0 && id < 12) clear(id);
}

void Trails::stop(int id) {   // cTrailManager::StopTrail
    if (id >= 0 && id < 12) trails_[id].stopped = true;
}

void Trails::forget(const Emitter* owner) {
    for (const Emitter*& o : owners_) if (o == owner) o = nullptr;
}

void Trails::process() {   // cTrailManager::Process (cSimpleTrail::Process for each owned trail)
    for (int i = 0; i < 12; ++i) {
        if (!owners_[i]) continue;
        Trail& t = trails_[i];
        if (t.head == t.tail || (int8_t)(t.head + 1) == t.tail || (int8_t)(t.head + 2) == t.tail) continue;
        if (t.drift) {   // from the newest point back to the oldest: spread out by 1/64 and lift by 1/64 - epsilon
            int8_t k = t.tail;
            do {
                k = (int8_t)(k - 1 < 0 ? 11 : k - 1);
                int32_t* p = t.pts[(int)k];
                if (p[2] >= 0xCC) {
                    p[2] = (int32_t)((uint32_t)p[2] + ((uint32_t)p[2] >> 6)) - fallingEpsilon;
                    p[0] += p[0] >> 6;
                    p[1] += p[1] >> 6;
                }
            } while (k != t.head);
        }
        if (t.stopped && t.fade >= 2) --t.fade;
    }
}

void Trails::render() {   // cTrailManager::Render: each owned trail at its owner's position
    for (int i = 0; i < 12; ++i)
        if (owners_[i]) renderTrail(trails_[i], owners_[i]->pos);
}

void Trails::renderTrail(Trail& t, const int32_t at[3]) const {   // cSimpleTrail::Render
    // the origin follows the owner: eighths of its position, or the position itself right at the world origin
    const int64_t at2 = (int64_t)at[0] * at[0] + (int64_t)at[1] * at[1] + (int64_t)at[2] * at[2];
    int32_t origin[3], delta[3];
    for (int k = 0; k < 3; ++k) {
        origin[k] = at2 > 0x28000 ? at[k] >> 3 : at[k];
        delta[k] = t.origin[k] - origin[k];
        t.origin[k] = origin[k];
    }
    if (t.head == t.tail || (int8_t)(t.head + 1) == t.tail || (int8_t)(t.head + 2) == t.tail || !t.segments) return;
    const int8_t stop = next(t.head);
    int32_t w[2] = {fastsin(t.heading), fastsin(t.heading + 0x4000)};
    Normalise2(w);
    w[0] = (int32_t)(((int64_t)w[0] * t.width) >> 12);
    w[1] = (int32_t)(((int64_t)w[1] * t.width) >> 12);
    int texture;
    uint16_t rect[4];
    if (!Assets_EffectSprite(20, texture, rect)) return;
    int tw = 256, th = 256;
    Assets_TextureSize(texture, &tw, &th);
    const float u0 = rect[0] / (float)tw, u1 = (rect[0] + rect[2]) / (float)tw;
    float v = rect[1] / (float)th;
    const float step = (float)(((uint32_t)rect[3] << 3) / 12) / 8.f / (float)th;
    const float r = ((t.colour & 0x1F) << 3) / 255.f, g = ((t.colour >> 5 & 0x1F) << 3) / 255.f,
                b = ((t.colour >> 10 & 0x1F) << 3) / 255.f;
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, Assets_Texture(texture));
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, t.additive ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glColor4f(r, g, b, 1.f);
    glBegin(GL_TRIANGLE_STRIP);
    int8_t k = t.tail;
    for (int n = 0; n < t.segments; ++n) {   // from the newest point back, at most "segments" of them
        k = (int8_t)(k - 1 < 0 ? 11 : k - 1);
        int32_t* p = t.pts[(int)k];
        for (int c = 0; c < 3; ++c) p[c] += delta[c];   // (only the points drawn are moved to the new origin)
        const float x = (at[0] + (p[0] << 3)) / 4096.f, y = (at[1] + (p[1] << 3)) / 4096.f;
        const float z = (at[2] + (p[2] << 3)) / 4096.f;
        glTexCoord2f(u0, v); glVertex3f(x + w[0] / 4096.f, y + w[1] / 4096.f, z);
        glTexCoord2f(u1, v); glVertex3f(x - w[0] / 4096.f, y - w[1] / 4096.f, z);
        if (t.taper) { w[0] >>= 1; w[1] >>= 1; }
        v += step;
        if (k == stop) break;
    }
    glEnd();
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_TRUE);
}
