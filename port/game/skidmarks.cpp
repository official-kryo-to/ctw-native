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
    if (n == 0) { m.origin[0] = p[0] * 16; m.origin[1] = p[1] * 16; m.origin[2] = p[2] * 16; }
    int64_t dx = (int64_t)p[0] - m.origin[0] / 16, dy = (int64_t)p[1] - m.origin[1] / 16, dz = (int64_t)p[2] - m.origin[2] / 16;
    if (dx < -0x8000 || dx >= 0x8000 || dy < -0x8000 || dy >= 0x8000 || dz < -0x84CC || dz >= 0x7B34) {
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

void Skidmarks::setShared(bool shared) {
    if (shared_ == shared) return;
    for (Mark& m : marks_) m = Mark{};
    ended_.clear();
    shared_ = shared;
}

bool Skidmarks::addPoint(uint32_t id, const int32_t p[3], const int32_t dir[3], int hold, uint32_t* strip, uint32_t* sequence) {
    Mark* m = nullptr;
    const int limit = shared_ ? 256 : 16;
    for (int i = 0; i < limit; ++i) if (marks_[i].active && !marks_[i].remote && marks_[i].id == id) m = &marks_[i];
    if (!m) {
        for (int i = 0; i < limit; ++i) if (!marks_[i].id) m = &marks_[i];
        if (!m) return false;
        *m = Mark{}; m->active = 1; m->id = id;
        if (++nextStrip_ == 0) ++nextStrip_;
        m->strip = nextStrip_;
    }
    const int previous = m->count;
    addToMark(*m, p, dir, hold);
    if (m->count == previous) return false;
    ++m->sequence;
    if (strip) *strip = m->strip;
    if (sequence) *sequence = m->sequence;
    return true;
}

bool Skidmarks::addRemote(uint32_t id, uint32_t wheel, uint32_t owner, uint32_t strip, uint32_t sequence, const int32_t p[3], const int32_t dir[3], int hold) {
    if (!shared_ || !id || !strip || !sequence) return false;
    Mark* m = nullptr;
    for (Mark& k : marks_) if (k.remote && k.id == id && k.wheel == wheel && k.owner == owner && k.strip == strip) { m = &k; break; }
    if (m && sequence <= m->sequence) return false;
    if (!m) {
        for (Mark& k : marks_) if (k.remote && k.id == id && k.wheel == wheel && k.active) k.active = 0;
        for (Mark& k : marks_) if (!k.id) { m = &k; break; }
        if (!m) return false;
        *m = Mark{}; m->id = id; m->wheel = wheel; m->owner = owner; m->strip = strip; m->remote = true;
    }
    if (!m->active && m->sequence) return false;
    m->active = 1; m->hold = 0;
    addToMark(*m, p, dir, hold);
    m->sequence = sequence;
    // Network points arrive less often than render frames. Keep a strip open across packet cadence.
    if (m->active) m->hold = 120;
    return true;
}

void Skidmarks::recordEnd(Mark& m) {
    if (!shared_ || m.remote || m.ended || !m.sequence) return;
    m.ended = true;
    if (ended_.size() >= 256) ended_.pop_front();
    ended_.push_back({m.id, m.strip, m.sequence + 1});
}
bool Skidmarks::takeEnded(Ended& out) {
    if (ended_.empty()) return false;
    out = ended_.front(); ended_.pop_front(); return true;
}
bool Skidmarks::endRemote(uint32_t id, uint32_t wheel, uint32_t owner, uint32_t strip, uint32_t sequence) {
    for (Mark& m : marks_) if (m.remote && m.id == id && m.wheel == wheel && m.owner == owner && m.strip == strip) {
        if (sequence != m.sequence + 1 || !m.active) return false;
        m.sequence = sequence; m.active = 0; m.hold = 0; return true;
    }
    return false;
}

void Skidmarks::process() {   // cSkidmarkManager::Process
    for (Mark& m : marks_) {
        if (!m.id) continue;
        if (m.active) {
            if (m.hold) --m.hold;
            else { m.active = 0; recordEnd(m); }
            continue;
        }
        recordEnd(m);
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
