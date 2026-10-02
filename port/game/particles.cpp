// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "particles.h"
#include "random.h"
#include "gfx/assets.h"
#include "world/worldrenderer.h"
#include <glad/gl.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>

static Particles g_particles;
Particles& TheParticles() { return g_particles; }

static uint32_t randNC(uint32_t n) { return Rand32NonCritical(n); }

GarbageEmitter::GarbageEmitter(const int32_t at[3], int16_t size, bool attached)
    : Emitter(at, attached ? 12 : 8, 0x4000, 19, false), size_(size), attached_(attached) {
    if (!attached) dying = true;
}

void GarbageEmitter::create(uint32_t hours) {
    // brightness: 0.4 from 22:00 to 05:00, rising to 1.0 by 09:00, falling again from 18:00
    uint32_t bright = 0x1000;
    if (hours - 0x16001u < 0xFFFEEFFFu) bright = 0x666;
    else if (hours > 0x12000) bright = 0x1000 - (uint32_t)((((hours - 0x12000) >> 2) * 0x999ull) >> 12);
    else if ((hours >> 12) <= 8) bright = (uint32_t)((((hours - 0x5000) >> 2) * 0x999ull) >> 12) + 0x666;
    static const uint8_t colours[6][3] = {{80, 115, 135}, {90, 90, 90}, {150, 130, 130}, {100, 90, 10}, {85, 85, 0}, {43, 60, 0}};
    Particle p{};
    p.angle = (uint16_t)Rand16NonCritical(0xFFFF);
    const uint8_t* c = colours[Rand16NonCritical(6)];
    p.colour = (uint16_t)(((c[0] * bright) >> 15) | (((c[1] * bright) >> 10) & 0xFFE0) | (((c[2] * bright) >> 5) & 0xFC00));
    p.v[0] = (int16_t)(randNC(0x1EA) - 0xF5);
    p.v[1] = (int16_t)(randNC(0x1EA) - 0xF5);
    p.p[0] = (int16_t)(randNC(0x800) - 0x400);
    p.p[1] = (int16_t)(randNC(0x800) - 0x400);
    p.p[2] = 0x800;
    p.v[2] = (int16_t)(randNC(0xA4) + 0x199);
    p.size = (int16_t)(size_ + (int)randNC(0x198) - 0xCC);
    p.spin = 3000;
    p.grow = -8;
    p.life = 30;
    p.alpha = 31;
    addFromData(p);
}

void GarbageEmitter::process(uint32_t hours) {   // cParticleEmitterGarbage::Process
    if (attached_) {
        if (frames_ == 0) for (int i = 0; i < 4; ++i) create(hours);
        else if (!(frames_ & 1) && speed2_ >= 0x4000001) create(hours);
    } else if (frames_ == 0) for (int i = 0; i < 8; ++i) create(hours);
    Emitter::process(hours);
    if (frames_ < 0xFF35) ++frames_;
}

void GarbageEmitter::updateParticle(Particle& p) {
    Emitter::updateParticle(p);
    p.v[2] = (int16_t)std::max(-0x800, (int)p.v[2] - 0x51);
    if (p.p[2] <= -0x3801) p.p[2] = (int16_t)0xC800;
}

static uint16_t propColour(uint32_t colour, bool tint, uint32_t ambient) {
    uint16_t rgb555 = 0;
    for (int i = 0; i < 3; ++i) {
        const int base = (colour >> (i*8)) & 0xF8;
        const int light = (ambient >> (i*8)) & 0xFF;
        const int value = tint ? base + (int)(((int64_t)(light-base)*0x999000) >> 24) : base;
        rgb555 |= (uint16_t)(((value >> 3) & 31) << (i*5));
    }
    return rgb555;
}

PaperEmitter::PaperEmitter(const int32_t at[3], int count, int16_t size, uint32_t colourA, uint32_t colourB,
                           bool textured, uint32_t ambient) : Emitter(at, count, 0x4000, textured ? 9 : 255, false),
    colours_{propColour(colourA,true,ambient),propColour(colourB,true,ambient)}, count_(count), size_(size) {
    dying = true;
}

void PaperEmitter::process(uint32_t hours) {
    if (!emitted_) for (int i = 0; i < count_; ++i) {
        Particle p{};
        p.angle = (uint16_t)Rand16NonCritical(0xFFFF); p.colour = colours_[Rand16NonCritical(2)];
        p.v[0] = (int16_t)(randNC(0x332)-0x199); p.v[1] = (int16_t)(randNC(0x332)-0x199);
        p.p[0] = (int16_t)(randNC(0x800)-0x400); p.p[1] = (int16_t)(randNC(0x800)-0x400);
        p.p[2] = 0x800; p.v[2] = (int16_t)(randNC(0xCD)+0x266);
        p.spin = 0x7D0; p.size = size_; p.life = 0x78; p.alpha = sprite_ == 9 ? 31 : 15;
        addFromData(p);
    }
    emitted_ = true;
    Emitter::process(hours);
}

void PaperEmitter::updateParticle(Particle& p) {
    Emitter::updateParticle(p);
    p.v[2] = (int16_t)std::max((int)p.v[2]-0x51,-0x199);
}

WoodEmitter::WoodEmitter(const int32_t at[3], int16_t vx, int16_t vy, int32_t strength,
                         uint32_t colourA, uint32_t colourB, bool tint, uint32_t ambient)
    : Emitter(at,12,0x4000,19,false), moving_(vx != 0 || vy != 0) {
    const uint16_t colours[2] = {propColour(colourA,tint,ambient),propColour(colourB,tint,ambient)};
    const int count = std::clamp(strength >> 10,0,12);
    const int spread = (int)(((int64_t)strength*0xA3) >> 12);
    for (int i = 0; i < count; ++i) {
        Particle p{};
        p.colour = colours[Rand16NonCritical(2)]; p.angle = (uint16_t)Rand16NonCritical(0xFFFF);
        p.v[0] = (int16_t)(vx + (int)randNC(spread*2) - spread);
        p.v[1] = (int16_t)(vy + (int)randNC(spread*2) - spread);
        p.p[0] = (int16_t)(randNC(0x800)-0x400); p.p[1] = (int16_t)(randNC(0x800)-0x400);
        p.p[2] = 0x800; p.v[2] = (int16_t)(randNC(0xA4)+0x199);
        p.spin = 3000; p.size = 0x4CC; p.grow = -40; p.life = 120; p.alpha = 31;
        addFromData(p);
    }
    dying = true;
}

void WoodEmitter::updateParticle(Particle& p) {
    Emitter::updateParticle(p);
    p.v[2] = (int16_t)std::max((int)p.v[2]-40,-0x800);
    p.p[2] = (int16_t)std::max((int)p.p[2],-0x7000);
    p.size = (int16_t)std::max((int)p.size,0);
    if (moving_) for (int k = 0; k < 2; ++k) p.v[k] = (int16_t)((p.v[k]*0xF33) >> 12);
}

void GlassEmitter::process(uint32_t hours) {
    if (!emitted_) {
        // The reference has a separate dawn/dusk brightness curve, independent of ambient tint.
        int light = 0x1000;
        if (hours < (5u << 12) || hours > (22u << 12)) light = 0x666;
        else if (hours > (18u << 12)) light -= (int)((((hours-(18u << 12)) >> 2)*0x999u) >> 12);
        else if ((hours >> 12) <= 8) light = 0x666 + (int)((((hours-(5u << 12)) >> 2)*0x999u) >> 12);
        const uint16_t colour = (uint16_t)(((170*light >> 15) & 31) |
            (((212*light >> 15) & 31) << 5) | (((255*light >> 15) & 31) << 10));
        for (int i = 0; i < 2; ++i) {
            Particle p{};
            p.spin = (int16_t)(Rand16NonCritical(0xFA0)+0x7D0); p.angle = (uint16_t)Rand16NonCritical(0xFFFF);
            p.colour = colour; p.alpha = 22;
            p.v[0] = (int16_t)(randNC(0x1EA)-0xF5); p.v[1] = (int16_t)(randNC(0x1EA)-0xF5);
            p.p[0] = (int16_t)(randNC(0x800)-0x400); p.p[1] = (int16_t)(randNC(0x800)-0x400);
            p.p[2] = 0x999; p.v[2] = (int16_t)(randNC(0x52)+0xA3);
            p.size = 0x4CC; p.grow = -40; p.life = 60;
            addFromData(p);
        }
        emitted_ = true;
    }
    Emitter::process(hours);
}

void GlassEmitter::updateParticle(Particle& p) {
    Emitter::updateParticle(p);
    p.v[2] -= 40;
}

ExplosionFlash::ExplosionFlash(const int32_t at[3]) : Emitter(at, 2, 0x4000, 5, true) {
    Particle p{};
    p.colour = 0x37F; p.alpha = 15; p.alphaStep = -2; p.life = 24;
    p.size = 0x333 / 4; p.grow = 0x5000 / 4;
    p.angle = (uint16_t)randNC(0xFFFF); p.spin = 0x1000;
    p.p[2] = 0x800 / 4; p.v[0] = 0x1000;
    addFromData(p);
    p.p[2] = 0; p.v[0] = 0; p.grow = -p.grow;
    addFromData(p);
    dying = true;
}

void ExplosionFlash::updateParticle(Particle& p) {
    p.angle = (uint16_t)(p.angle + p.spin);
    p.size = (int16_t)(p.size + p.grow);
    p.alpha = (uint8_t)std::clamp((int)p.alpha + p.alphaStep, 1, 31);
    p.life = p.alpha > 1 && p.size > 0 ? 4 : 0;
    if (p.v[0] == 0x1000 && p.grow > 0) p.grow = (int16_t)((p.grow * 0xE66) >> 12);
}

ExplosionCloud::ExplosionCloud(const int32_t at[3]) : Emitter(at, 16, 0x4000, 7, true) {
    Particle p{};
    p.colour = 0x7FFF; p.alpha = 28; p.life = 60; p.alphaStep = -1;
    p.size = 0x6000 / 4; p.grow = 0x400 / 4; p.v[2] = 0x1000 / 4;
    addFromData(p);
    for (int i = 0; i < 7; ++i) {
        float a = i * 6.2831853f / 7;
        p.v[0] = (int16_t)(std::sin(a) * 0x800 / 4);
        p.v[1] = (int16_t)(std::cos(a) * 0x800 / 4);
        p.v[2] = 0xA66 / 4;
        p.size = (int16_t)((0x3800 + randNC(0x800)) / 4);
        p.angle = (uint16_t)randNC(0xFFFF);
        addFromData(p);
    }
    dying = true;
}

void ExplosionCloud::updateParticle(Particle& p) {
    for (int k = 0; k < 3; ++k) {
        p.p[k] = (int16_t)(p.p[k] + p.v[k]);
        p.v[k] = (int16_t)((p.v[k] * 0xE66) >> 12);
    }
    p.life = (uint16_t)(p.life - 2); p.size = (int16_t)(p.size + p.grow);
    if (p.life < 5) { p.alpha = (uint8_t)std::max(1, (int)p.alpha - 5); p.life = p.alpha > 1 ? 4 : 0; }
    uint16_t colour = 0;
    for (int shift = 0; shift <= 10; shift += 5) colour |= std::max(1, ((p.colour >> shift) & 31) - 1) << shift;
    p.colour = colour;
}

ExplosionDebris::ExplosionDebris(const int32_t at[3]) : Emitter(at, 5, 0x4000, 19, true) {
    for (int i = 0; i < 5; ++i) {
        Particle p{};
        int shade = (int)randNC(25);
        p.colour = (uint16_t)((shade + randNC(5)) | ((shade + randNC(5)) << 5) | ((shade + randNC(5)) << 10));
        float a = randNC(0xFFFF) * 9.587378e-05f;
        p.v[0] = (int16_t)((std::sin(a) - std::cos(a)) * 0x2000 / 4);
        p.v[1] = (int16_t)((std::cos(a) + std::sin(a)) * 0x2000 / 4);
        p.v[2] = 0x2000 / 4; p.p[2] = 0x1000 / 4;
        p.size = (int16_t)((0x800 + randNC(0x800)) / 4);
        p.spin = (int16_t)randNC(0x1000); p.life = 31; p.alpha = 31;
        addFromData(p);
    }
    dying = true;
}

void ExplosionDebris::updateParticle(Particle& p) {
    if (p.p[2] > 0) p.v[2] -= 0x199;
    Emitter::updateParticle(p);
    for (int k = 0; k < 3; ++k) p.v[k] = (int16_t)((p.v[k] * 0xE66) >> 12);
    if (p.size < 1 || p.life <= 10) p.life = 0;
}

Emitter::Emitter(const int32_t p[3], int count, int32_t range, uint8_t sprite, bool billboard)
    : range_(range), inv_(range ? (int32_t)((0x100000000000LL / range) >> 20) : 0), sprite_(sprite), billboard_(billboard) {
    pos[0] = p[0]; pos[1] = p[1]; pos[2] = p[2];
    parts_.assign((size_t)std::max(count, 1), Particle{});
}

void Emitter::setPos(const int32_t p[3]) {   // small moves: remember the offset so the particles stay put
    int32_t d[3] = {p[0] - pos[0], p[1] - pos[1], p[2] - pos[2]};
    moved_[0] = d[0]; moved_[1] = d[1]; moved_[2] = d[2];
    if (std::abs(d[0]) < 0x7001 && std::abs(d[1]) < 0x7001 && std::abs(d[2]) < 0x7001) return;
    pos[0] = p[0]; pos[1] = p[1]; pos[2] = p[2];
    for (Particle& q : parts_) q.active = 0;   // (a teleport: the game resets its particles)
    alive_ = 0;
    moved_[0] = moved_[1] = moved_[2] = 0;
}

void Emitter::addFromData(const Particle& d) {   // cParticleEmitterBase::AddParticleFromData
    int n = (int)parts_.size();
    if (next_ == 0xFF) next_ = 0;
    int i = next_;
    if (parts_[i].active) {
        int j = i;
        do { j = j + 1 < n ? j + 1 : 0; } while (parts_[j].active && j != i);
        if (parts_[j].active) { next_ = (uint8_t)(i + 1 < n ? i + 1 : 0); return; }
        i = j;
    }
    parts_[i] = d;
    parts_[i].active = 1;
    ++alive_;
    next_ = (uint8_t)(i + 1 < n ? i + 1 : 0);
}

void Emitter::updateParticle(Particle& q) {   // cParticleEmitterBase::UpdateParticle
    for (int k = 0; k < 3; ++k) q.p[k] = (int16_t)(q.p[k] + q.v[k]);
    uint16_t life = (uint16_t)(q.life - 2);
    q.angle = (uint16_t)(q.angle + q.spin);
    q.life = life;
    q.size = (int16_t)(q.size + q.grow);
    if (q.alphaStep) {
        int a = std::clamp((int)(int8_t)q.alpha + q.alphaStep, 1, 31);
        q.alpha = (uint8_t)a;
        if (life < 3 && a > 1) q.life = life | 4;
    }
}

void Emitter::process(uint32_t) {   // Process -> ParticleUpdateLoop(true)
    if (moved_[0] || moved_[1] || moved_[2]) {
        for (int k = 0; k < 3; ++k) pos[k] += moved_[k];
    }
    for (Particle& q : parts_) {
        if (!q.active) continue;
        for (int k = 0; k < 3; ++k) q.p[k] = (int16_t)(q.p[k] - (int16_t)((uint32_t)(moved_[k] * inv_) >> 12));
        bool out = false;
        for (int k = 0; k < 3; ++k) out |= std::abs((q.size + q.p[k]) >> 12) >= 7;
        if (out || q.life <= 2) { q.life = 0; q.active = 0; --alive_; continue; }
        updateParticle(q);
    }
    moved_[0] = moved_[1] = moved_[2] = 0;
}

void Emitter::render(const WorldCamera& cam) const {   // cParticleEmitterBase::ManagedRender
    if (!alive_) return;
    int texture = 0; uint16_t r[4] = {0,0,1,1};
    if (sprite_ != 255 && !Assets_EffectSprite(sprite_, texture, r)) return;
    GLuint tex = sprite_ == 255 ? 0 : Assets_Texture(texture);
    int tw = 256, th = 256;
    if (sprite_ != 255) Assets_TextureSize(texture, &tw, &th);
    float u0 = r[0] / (float)tw, v0 = r[1] / (float)th, u1 = (r[0] + r[2]) / (float)tw, v1 = (r[1] + r[3]) / (float)th;
    if (tex) { glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, tex); } else glDisable(GL_TEXTURE_2D);
    float R = range_ / 4096.f;
    glBegin(GL_QUADS);
    for (const Particle& q : parts_) {
        if (!q.active) continue;
        float s = q.size / 4096.f * R;
        float a = q.angle * 9.587378e-05f;
        float sn = sinf(a) * s, cs = cosf(a) * s;
        float e1[3], e2[3];
        if (billboard_) {
            for (int k = 0; k < 3; ++k) {
                e1[k] = sn * cam.right[k] - cs * cam.up[k];
                e2[k] = cs * cam.right[k] + sn * cam.up[k];
            }
        } else {
            e1[0] = sn; e1[1] = -cs; e1[2] = 0;
            e2[0] = cs; e2[1] = sn; e2[2] = 0;
        }
        if ((q.flags | 2) == 3) for (float& v : e1) v = -v;
        if ((q.flags & 0xFE) == 2) for (float& v : e2) v = -v;
        float c[3];
        for (int k = 0; k < 3; ++k) c[k] = pos[k] / 4096.f + q.p[k] / 4096.f * R - e1[k] - e2[k];
        uint16_t col = q.colour;
        glColor4ub((GLubyte)((col & 0x1F) << 3), (GLubyte)((col >> 5 & 0x1F) << 3), (GLubyte)((col >> 10 & 0x1F) << 3),
                   (GLubyte)(q.alpha * 255 / 31));
        glTexCoord2f(u0, v0); glVertex3f(c[0], c[1], c[2]);
        glTexCoord2f(u0, v1); glVertex3f(c[0] + 2 * e1[0], c[1] + 2 * e1[1], c[2] + 2 * e1[2]);
        glTexCoord2f(u1, v1); glVertex3f(c[0] + 2 * e1[0] + 2 * e2[0], c[1] + 2 * e1[1] + 2 * e2[1], c[2] + 2 * e1[2] + 2 * e2[2]);
        glTexCoord2f(u1, v0); glVertex3f(c[0] + 2 * e2[0], c[1] + 2 * e2[1], c[2] + 2 * e2[2]);
    }
    glEnd();
}

// ============================================================================================== smoke
void SmokeEmitter::addParticle(const int32_t vel[3], int colour) {   // cParticleEmitterSmoke::AddParticle(tv3d, colour)
    if (!init_) {
        tmpl_ = Particle{};
        tmpl_.life = 0x32;
        tmpl_.alpha = 0; tmpl_.alphaStep = 2;
        tmpl_.grow = (int16_t)((uint32_t)(inv_ * 0x333) >> 12);
        init_ = true;
    }
    tmpl_.p[0] = (int16_t)((uint32_t)(inv_ * ((int)randNC(0x1000) - 0x800)) >> 12);
    tmpl_.p[1] = (int16_t)((uint32_t)(((int)randNC(0x1000) - 0x800) * inv_) >> 12);
    tmpl_.p[2] = 0;
    tmpl_.colour = 0x7FFF;
    tmpl_.size = (int16_t)(((int64_t)inv_ * (int64_t)(randNC(0x19A) + 0x599)) >> 12);
    tmpl_.grow = (int16_t)((uint32_t)(inv_ * 0x333) >> 12);
    switch (colour) {   // eVehicleSmokeColour
        case 0: tmpl_.alphaStep = 0xA; break;
        case 1: tmpl_.alphaStep = 0x10; tmpl_.colour = 0x5294; break;
        case 2: tmpl_.alphaStep = 0x16; tmpl_.colour = 0x3DEF; break;
        case 3: tmpl_.alphaStep = 0x19; tmpl_.colour = 0x294A; break;
    }
    int32_t v[3];
    for (int k = 0; k < 3; ++k) v[k] = (int32_t)(((int64_t)(int32_t)(((int64_t)vel[k] * 0x66600000LL) >> 32) * inv_) >> 12);
    for (int k = 0; k < 3; ++k)   // DoesV3dOverflowV3d16
        if (std::abs((v[k] + tmpl_.size) >> 12) > 6) return;
    tmpl_.v[0] = (int16_t)v[0];
    tmpl_.v[1] = (int16_t)v[1];
    tmpl_.v[2] = (int16_t)((uint32_t)(inv_ * 0x4CD) >> 12);
    addFromData(tmpl_);
}

void SmokeEmitter::updateParticle(Particle& q) {   // cParticleEmitterSmoke::UpdateParticle
    for (int k = 0; k < 3; ++k) q.p[k] = (int16_t)(q.p[k] + q.v[k]);
    uint16_t oldLife = q.life;
    uint16_t life = (uint16_t)(oldLife - 2);
    q.angle = (uint16_t)(q.angle + q.spin);
    q.life = life;
    q.size = (int16_t)(q.size + q.grow);
    int a = (int8_t)q.alpha;
    int na;
    if (life < 0x14) na = a - 1;
    else na = a < q.alphaStep ? a + 2 : a;
    int c = std::clamp(na, 0, 31);
    q.alpha = (uint8_t)c;
    if (life < 5 && c > 1) { q.life = (uint16_t)(oldLife + 2); return; }
    if (na < 1) q.life = 0;
}

// ============================================================================================== steam
void SteamEmitter::addParticle() {   // cParticleEmitterSteam::AddParticle(const cSimpleMover*) with no mover
    if (!init_) { tmpl_.spin = 0; tmpl_.colour = 0x7FFF; tmpl_.alphaStep = 0; init_ = true; }
    tmpl_.alpha = 0x14;
    auto scaled = [&](int32_t v) { return (int16_t)((uint32_t)(v * inv_) >> 12); };   // world units -> the range
    tmpl_.p[0] = tmpl_.p[1] = tmpl_.p[2] = 0;
    tmpl_.v[0] = tmpl_.v[1] = 0;
    tmpl_.v[2] = scaled((int32_t)randNC(0xCD) + 0xCC);        // up 0.05 .. 0.1 a frame
    tmpl_.size = scaled((int32_t)randNC(0x333) + 0x800);      // 0.5 .. 0.7
    tmpl_.grow = scaled((int32_t)randNC(0x7B) + 0x7A);
    tmpl_.life = 0xC8;                                        // SetStandardDataLifeTime(200)
    tmpl_.angle = (uint16_t)(((uint32_t)randNC(0x8000000) + 0xC000000) >> 12);
    addFromData(tmpl_);
}

void SteamEmitter::tick(uint32_t frame) {   // cParticleEmitterSteam::Process
    if (on_ && (frame & 7) == 0) addParticle();
}

void SteamEmitter::updateParticle(Particle& q) {   // base update, then fade by one every 8 life steps
    Emitter::updateParticle(q);
    if ((int8_t)q.alpha >= 2 && (q.life & 7) == 0) q.alpha = (uint8_t)(q.alpha - 1);
}

void FireEmitter::addParticle() {
    Particle p{};
    auto scaled = [&](int32_t v) { return (int16_t)(((int64_t)v * inv_) >> 12); };
    p.life = 0x1F; p.colour = 0x7FFF; p.alpha = 22; p.alphaStep = -1;
    p.p[0] = scaled((int32_t)randNC(0x2000) - 0x1000);
    p.p[1] = scaled((int32_t)randNC(0x2000) - 0x1000);
    p.p[2] = scaled(0x1800);
    p.v[2] = scaled((int32_t)randNC(0xC00) + 0xC00);
    p.size = scaled((int32_t)randNC(0x1800) + 0x1800);
    p.grow = (int16_t)(-p.size / 10);
    p.spin = randNC(2) ? 0xE39 : -0xE39;
    p.flags = randNC(2) ? 2 : 0;
    addFromData(p);
}

void FireEmitter::updateParticle(Particle& p) {
    Emitter::updateParticle(p);
    if ((p.colour & 0x1F) > 2) p.colour -= 2;
    if ((p.colour & 0x3E0) > 0x80) p.colour -= 0x80;
    if ((p.colour & 0x7C00) > 0x1400) p.colour -= 0x1400;
    p.v[0] = (int16_t)(((int32_t)p.v[0] * 0xF33) >> 12);
    p.v[1] = (int16_t)(((int32_t)p.v[1] * 0xF33) >> 12);
}

// ============================================================================================== all emitters
void Particles::remove(const Emitter* e) {
    for (auto& p : emitters_)
        if (p.get() == e) p->dying = true;
}

void Particles::update(uint32_t frame, uint32_t hours) {
    for (auto& e : emitters_) {
        if (!e->dying)
            if (auto* steam = dynamic_cast<SteamEmitter*>(e.get())) steam->tick(frame);
        e->process(hours);
    }
    emitters_.erase(std::remove_if(emitters_.begin(), emitters_.end(), [](const std::unique_ptr<Emitter>& e) { return e->finished(); }),
                    emitters_.end());
}

void RainEmitter::addDrops(unsigned n, uint16_t cameraYaw) {   // cParticleEmitterRain::AddParticle(uint)
    if (!init_) {
        init_ = true;
        tmpl_ = Particle{};
        tmpl_.life = 30;                     // SetStandardDataLifeTime(0x1E)
        tmpl_.spin = 0;
        tmpl_.colour = 0x7FFF;
        tmpl_.grow = 0;
        tmpl_.alpha = 15;
        tmpl_.size = (int16_t)(4 * inv_);
    }
    tmpl_.v[2] = (int16_t)((n < 5 ? -2 : -3) * inv_);
    for (; n; --n) {
        const uint32_t x = Rand32NonCritical(0x22000) + 0x0FFEF000u, y = Rand32NonCritical(0x22000);
        tmpl_.p[0] = (int16_t)((uint32_t)(inv_ * x) >> 12);
        tmpl_.p[1] = (int16_t)((uint32_t)(inv_ * (y + 0x0FFEF000u)) >> 12);
        tmpl_.angle = (uint16_t)-cameraYaw;
        tmpl_.p[2] = (int16_t)(25 * inv_);
        addFromData(tmpl_);
    }
}

void RainEmitter::updateParticle(Particle& q) {   // cParticleEmitterRain::UpdateParticle
    Emitter::updateParticle(q);
    for (int k = 0; k < 2; ++k) {
        if (q.p[k] > 14336) q.p[k] = (int16_t)(q.p[k] - 28672);
        else if (q.p[k] <= -14337) q.p[k] = (int16_t)(q.p[k] + 28672);
    }
    q.life = q.p[2] > 0 ? 6 : 0;
}

void RainEmitter::render(const WorldCamera&) const {   // cParticleEmitterRain::ManagedRender
    if (!alive_) return;
    int texture = 0;
    uint16_t r[4] = {0, 0, 1, 1};
    if (!Assets_EffectSprite(sprite_, texture, r)) return;
    const GLuint tex = Assets_Texture(texture);
    if (!tex) return;
    int tw = 256, th = 256;
    Assets_TextureSize(texture, &tw, &th);
    const float u = (r[0] + r[2] / 2) / (float)tw, vTop = (r[1] + r[3]) / (float)th, vBottom = r[1] / (float)th;
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, tex);
    glColor4ub((GLubyte)tint, (GLubyte)(tint >> 8), (GLubyte)(tint >> 16), 0x70);
    const float R = range_ / 4096.f, w = 327.f / 4096.f;
    glBegin(GL_QUADS);
    for (const Particle& q : parts_) {
        if (!q.active) continue;
        const float a = q.angle * 9.587378e-05f, c = cosf(a) * w, s = sinf(a) * w;
        const float x = pos[0] / 4096.f + q.p[0] / 4096.f * R, y = pos[1] / 4096.f + q.p[1] / 4096.f * R;
        const float top = pos[2] / 4096.f + q.p[2] / 4096.f * R, bottom = pos[2] / 4096.f + (q.p[2] - q.size) / 4096.f * R;
        glTexCoord2f(u, vTop); glVertex3f(x + c, y + s, top);
        glTexCoord2f(u, vTop); glVertex3f(x - c, y - s, top);
        glTexCoord2f(u, vBottom); glVertex3f(x - c, y - s, bottom);
        glTexCoord2f(u, vBottom); glVertex3f(x + c, y + s, bottom);
    }
    glEnd();
}

void Particles::render(const WorldCamera& cam) const {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glDisable(GL_LIGHTING);
    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    for (const auto& e : emitters_) e->render(cam);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glDisable(GL_TEXTURE_2D);
}

void DrawSheetSprite(int sprite, uint32_t c, const float p[3], const float ax[3], const float ay[3], float sx, float sy) {
    int texture; uint16_t r[4];
    if (!Assets_EffectSprite(sprite, texture, r)) return;
    GLuint tex = Assets_Texture(texture);
    int tw = 256, th = 256;
    Assets_TextureSize(texture, &tw, &th);
    float u0 = r[0] / (float)tw, v0 = r[1] / (float)th, u1 = (r[0] + r[2]) / (float)tw, v1 = (r[1] + r[3]) / (float)th;
    if (tex) { glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, tex); } else glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glDisable(GL_LIGHTING);
    glDisable(GL_CULL_FACE);
    glColor4ub((GLubyte)(c & 0xFF), (GLubyte)(c >> 8 & 0xFF), (GLubyte)(c >> 16 & 0xFF), (GLubyte)(c >> 24));
    glBegin(GL_QUADS);
    glTexCoord2f(u0, v0); glVertex3f(p[0] - ax[0] * sx - ay[0] * sy, p[1] - ax[1] * sx - ay[1] * sy, p[2] - ax[2] * sx - ay[2] * sy);
    glTexCoord2f(u1, v0); glVertex3f(p[0] + ax[0] * sx - ay[0] * sy, p[1] + ax[1] * sx - ay[1] * sy, p[2] + ax[2] * sx - ay[2] * sy);
    glTexCoord2f(u1, v1); glVertex3f(p[0] + ax[0] * sx + ay[0] * sy, p[1] + ax[1] * sx + ay[1] * sy, p[2] + ax[2] * sx + ay[2] * sy);
    glTexCoord2f(u0, v1); glVertex3f(p[0] - ax[0] * sx + ay[0] * sy, p[1] - ax[1] * sx + ay[1] * sy, p[2] - ax[2] * sx + ay[2] * sy);
    glEnd();
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}
