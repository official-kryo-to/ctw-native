// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "skidmarks.h"
#include <glad/gl.h>
#include <cstring>

static Skidmarks g_skid;
Skidmarks& TheSkidmarks() { return g_skid; }

void Skidmarks::addToMark(Mark& m, const int32_t p[3], const int32_t dir[3], int hold) {   // cSkidmark::AddPoint
    if (m.hold >= 2) return;
    int n = m.count;
    if (n == 0) { m.origin[0] = p[0] << 4; m.origin[1] = p[1] << 4; m.origin[2] = p[2] << 4; }
    int32_t dx = (p[0] * 16 - m.origin[0]) >> 4, dy = (p[1] * 16 - m.origin[1]) >> 4, dz = (p[2] * 16 - m.origin[2]) >> 4;
    if ((uint32_t)(dx + 0x8000) >= 0x10000 || (uint32_t)(dy + 0x8000) >= 0x10000 || ((uint32_t)(dz + 0x84CC) >> 16) != 0) {
        m.active = 0;
        return;
    }
    dz += 0x4CC;
    m.hold = (uint8_t)hold;
    int32_t wx = dir[0] * 0x19A0 >> 16, wy = dir[1] * 0x19A0 >> 16, wz = dir[2] * 0x19A0 >> 16;
    if (n + 1 >= 250) { m.active = 0; return; }
    int32_t* a = m.pts[n];
    int32_t* b = m.pts[n + 1];
    a[0] = (dx - wx) * 16; a[1] = (dy - wy) * 16; a[2] = (dz - wz) * 16;
    b[0] = (dx + wx) * 16; b[1] = (dy + wy) * 16; b[2] = (dz + wz) * 16;
    bool same = n != 0 && a[0] == m.pts[n - 2][0] && a[1] == m.pts[n - 2][1] && a[2] == m.pts[n - 2][2];
    if (!same) n += 2;
    m.count = (uint8_t)n;
    if (n >= 0xFA) m.active = 0;
}

void Skidmarks::addPoint(uint32_t id, const int32_t p[3], const int32_t dir[3], int hold) {   // cSkidmarkManager::AddSkidPoint
    Mark* m = nullptr;
    for (Mark& k : marks_)
        if (k.active && k.id == id) m = &k;
    if (!m) {
        for (Mark& k : marks_)
            if (k.id == 0) m = &k;
        if (!m) return;
        std::memset(m->pts, 0, sizeof m->pts);   // cSkidmark::Start
        m->count = 0; m->wait = 0x78; m->active = 1; m->hold = 0; m->alpha = 0xC; m->fade = 1;
        m->id = id;
    }
    addToMark(*m, p, dir, hold);
}

void Skidmarks::process() {   // cSkidmarkManager::Process
    for (Mark& m : marks_) {
        if (!m.id) continue;
        if (m.active) {
            if (m.hold) --m.hold;
            else m.active = 0;
            continue;
        }
        if ((int8_t)m.wait >= 1) { if (m.fade) m.wait = (uint8_t)(m.wait - 2); continue; }
        if (m.alpha > 1) { --m.alpha; continue; }
        m = Mark{};   // freed
        m.id = 0;
    }
}

void Skidmarks::render() const {   // cSkidmark::Render: black, alpha / 31, one triangle strip per mark
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_LIGHTING);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    for (const Mark& m : marks_) {
        if (!m.id || m.count <= 2) continue;
        glColor4f(0, 0, 0, m.alpha / 31.f);
        glBegin(GL_TRIANGLE_STRIP);
        for (int i = 0; i < m.count; ++i)
            glVertex3f((m.origin[0] + m.pts[i][0]) / 65536.f, (m.origin[1] + m.pts[i][1]) / 65536.f, (m.origin[2] + m.pts[i][2]) / 65536.f);
        glEnd();
    }
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}
