// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "particles.h"
#include "trails.h"
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
static int fastsin(int a) { return (int)(sinf((float)a * 9.587378e-05f) * 4096.f); }

// cParticleEmitterWaterSplash, type 1 (CWaterCannon's nozzle spray).
WaterSplashEmitter::WaterSplashEmitter(const int32_t at[3], const int32_t velocity[3], uint16_t colour)
    : Emitter(at,20,0x4000,15,false) {
    pos[2]+=0x199;
    tmpl_.colour=colour; tmpl_.life=80; tmpl_.alpha=30;
    tmpl_.size=tmpl_.grow=(int16_t)(((0x999/2)*0x199)>>12);
    tmpl_.angle=(uint16_t)((Rand16NonCritical(0x2000)-0x39)&0xFF);
    direction(velocity);
    addParticle();
}

void WaterSplashEmitter::direction(const int32_t velocity[3]) {
    for(int k=0;k<3;++k) {
        int32_t v=(int16_t)(((int32_t)(int16_t)velocity[k]*0x1333)>>12);
        if(k==2) v+=0x800;
        tmpl_.v[k]=(int16_t)(((int64_t)v*inv_)>>12);
    }
}

void WaterSplashEmitter::updateParticle(Particle& p) {
    for(int k=0;k<3;++k) p.v[k]=(int16_t)(((int32_t)p.v[k]*0xE66)>>12);
    p.v[2]=(int16_t)(p.v[2]-((0x10B*inv_)>>12));
    if(p.life<=9 && p.alpha>=6) p.alpha-=5;
    else if(!(frame_&1) && p.alpha) --p.alpha;
    for(int k=0;k<3;++k) if(p.p[k] < -0x5000 || p.p[k]>0x5000) {
        p.life=0; p.active=0; --alive_; return;
    }
    Emitter::updateParticle(p);
}

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

SlatsEmitter::SlatsEmitter(const int32_t at[3], const int32_t velocity[3], const int32_t endA[3], const int32_t endB[3],
                           uint32_t colourA, uint32_t colourB, bool textured, bool slow, uint32_t ambient)
    : Emitter(at,6,0x70000,textured ? 24 : 0,false) {
    const uint16_t colours[2] = {propColour(colourA,true,ambient),propColour(colourB,true,ambient)};
    const int scale = slow ? 0x199 : 0x800;
    const int64_t speed2 = (int64_t)velocity[0]*velocity[0] +
        (int64_t)velocity[1]*velocity[1] + (int64_t)velocity[2]*velocity[2];
    int32_t launch[2]{};
    if (speed2) {
        const float reciprocal = 1.f/std::sqrt((float)speed2);
        for (int k=0;k<2;++k) {
            const float f = reciprocal*velocity[k];
            const int32_t n = (int32_t)std::fma(f,4096.f,f < 0 ? -.5f : .5f);
            launch[k] = slow ? (int32_t)((int64_t)n*scale >> 12) : n >> 1;
        }
    }
    struct Side { int32_t delta[3], half, quarter, velocity[2], spinSpread; } sides[2]{};
    for (int i=0;i<2;++i) {
        Side& side = sides[i]; const int32_t* end = i ? endB : endA;
        for (int k=0;k<3;++k) side.delta[k] = end[k]-at[k];
        side.half = (int32_t)std::sqrt((double)side.delta[0]*side.delta[0] +
            (double)side.delta[1]*side.delta[1] + (double)side.delta[2]*side.delta[2]) >> 1;
        side.quarter = side.half >> 2;
        side.spinSpread = std::abs((0x4000-side.half) >> 2);
        for (int k=0;k<2;++k) side.velocity[k] = launch[k] + (int32_t)((int64_t)scale*(side.delta[k] >> 8) >> 12);
    }
    const int jitter = (int)((int64_t)inv_*(scale << 4) >> 12);
    for (int i=0;i<6;++i) {
        Particle p{};
        p.colour = colours[Rand16NonCritical(2)]; p.alpha = 31; p.life = 300;
        const bool useA = sides[0].half >= 0x52 && !(i & 1);
        const Side& side = sides[useA ? 0 : 1];
        // Invalid endpoints leave uninitialised fields in the reference; skip those fragments here.
        if (side.half < 0x52) continue;
        int32_t vx = side.velocity[0], vy = side.velocity[1];
        if (jitter) { vx += (int32_t)Rand32NonCritical(jitter*2)-jitter; vy += (int32_t)Rand32NonCritical(jitter*2)-jitter; }
        const int size = (int)Rand32NonCritical(side.half-side.quarter)+side.quarter;
        // ATan2 uses atan2f and truncates to the signed 16-bit turn before the negation.
        const int angle = std::clamp((int)(std::atan2(-(float)side.delta[0],(float)side.delta[1])*10430.f),-32768,32767);
        p.angle = (uint16_t)-angle;
        const int spin = 0x71C + (int)((Rand16NonCritical(0xE39)*side.spinSpread) >> 12);
        p.spin = (int16_t)(useA ? -spin : spin);
        for (int k=0;k<2;++k) p.p[k] = (int16_t)((int64_t)inv_*side.delta[k] >> 12);
        p.p[2] = (int16_t)((int64_t)inv_*0x2000 >> 12);
        p.v[0] = (int16_t)((int64_t)inv_*vx >> 12); p.v[1] = (int16_t)((int64_t)inv_*vy >> 12);
        p.v[2] = (int16_t)((int64_t)inv_*-40 >> 12); p.size = (int16_t)((int64_t)inv_*size >> 12);
        addFromData(p);
    }
    dying = true;
}

void SlatsEmitter::updateParticle(Particle& p) {
    Emitter::updateParticle(p);
    const int32_t range = (int32_t)((0x100000000000LL/inv_) >> 20);
    if ((int64_t)range*p.p[2] + (int64_t)pos[2]*4096 < 0) {
        p.v[0] = p.v[1] = p.v[2] = p.spin = 0;
        p.p[2] = (int16_t)((inv_*0x199) >> 12);
    } else {
        for (int k=0;k<2;++k) p.v[k] = (int16_t)((p.v[k]*0xF33) >> 12);
        p.spin = (int16_t)((p.spin*0xE66) >> 12);
        p.v[2] = (int16_t)((p.v[2]*0x1199) >> 12);
    }
    if (p.life <= 15) p.alphaStep = -3;
}

void SlatsEmitter::vertices(const Particle& p, float out[4][3]) const {
    // ManagedRender reconstructs the range from its quantised reciprocal, and rounds both axes to Q12.
    const int32_t range = (int32_t)((0x100000000000LL/inv_) >> 20);
    const int32_t sn = (int32_t)(std::sin(p.angle*9.587378e-05f)*4096);
    const int32_t cs = (int32_t)(std::sin((uint16_t)(p.angle+0x4000)*9.587378e-05f)*4096);
    const int32_t along[2] = {(int32_t)((((int64_t)sn*p.size) >> 12)*range >> 12),
                              (int32_t)((((int64_t)-cs*p.size) >> 12)*range >> 12)};
    const int32_t across[2] = {(int32_t)((((int64_t)cs*(inv_ >> 2)) >> 12)*range >> 12),
                               (int32_t)((((int64_t)sn*(inv_ >> 2)) >> 12)*range >> 12)};
    for (int k=0;k<3;++k) {
        // Positions use Q16 at the renderer boundary, retaining four more bits than the axes.
        const int64_t centre = (int64_t)pos[k]*16 + (((int64_t)p.p[k]*range) >> 8);
        const int32_t a = k < 2 ? along[k]*16 : 0, b = k < 2 ? across[k]*16 : 0;
        out[0][k] = (centre-a-b)/65536.f;
        out[1][k] = (centre+a-b)/65536.f;
        out[2][k] = (centre+a+b)/65536.f;
        out[3][k] = (centre-a+b)/65536.f;
    }
}

void SlatsEmitter::render(const WorldCamera&) const {
    if (!alive_) return;
    int texture; uint16_t rect[4];
    if (!Assets_EffectSprite(sprite_,texture,rect)) return;
    int tw=256, th=256; Assets_TextureSize(texture,&tw,&th);
    glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D,Assets_Texture(texture));
    glBegin(GL_QUADS);
    for (const Particle& p : parts_) {
        if (!p.active) continue;
        float quad[4][3]; vertices(p,quad);
        float uv[4][2];
        if (sprite_) {
            // Textured strips sample a section proportional to their length; the far edge shifts one pixel.
            const int portion = (int16_t)((p.size*4096-(p.size < 0 ? 121 : 0))/122);
            const float u0=rect[0]/(float)tw, u1=(rect[0]+rect[2])/(float)tw;
            const float v0=rect[1]/(float)th, v1=(rect[1]+((portion*rect[3]) >> 12))/(float)th;
            uv[0][0]=uv[1][0]=u0; uv[2][0]=uv[3][0]=u1;
            uv[0][1]=v0+1.f/th; uv[1][1]=v1; uv[2][1]=v1+1.f/th; uv[3][1]=v0;
            glColor4ub(255,255,255,255); // the reference ignores particle tint and alpha on this branch
        } else {
            for (auto& v : uv) { v[0]=(rect[0]+(rect[2] >> 1))/(float)tw; v[1]=(rect[1]+(rect[3] >> 1))/(float)th; }
            glColor4ub((p.colour & 31) << 3,((p.colour >> 5) & 31) << 3,((p.colour >> 10) & 31) << 3,p.alpha*255/31);
        }
        for (int i=0;i<4;++i) { glTexCoord2f(uv[i][0],uv[i][1]); glVertex3fv(quad[i]); }
    }
    glEnd();
}

ExplosionFlash::ExplosionFlash(const int32_t at[3], int type, int32_t scale)
    : Emitter(at, 2, 0x4000, type == 5 ? 14 : 5, false) {
    additive_ = true;
    Particle p{};
    p.colour = type == 2 ? 0x7C00 : type == 5 ? 0x7FFF : 0x37F;
    p.alpha = type == 2 ? 10 : 15; p.alphaStep = type == 2 ? -1 : -2;
    p.life = type == 2 ? 48 : 24;
    p.size = (int16_t)((((int16_t)((int64_t)0x333*scale >> 12))*inv_) >> 12);
    p.grow = (int16_t)((((int16_t)((int64_t)0x5000*scale >> (type == 2 ? 13 : 12)))*inv_) >> 12);
    p.spin = 0x800; p.p[2] = (int16_t)(0x800*inv_ >> 12);
    if (type == 2) p.v[0] = 0x1000; // growth-damping sentinel, not a physical velocity
    addFromData(p);
    p.angle = 0x8000; p.spin = -0x800;
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

// cParticleEmitterComplexExplsn::cParticleEmitterComplexExplsn, with its AddMainCloudParticle and the
// conversion cParticleEmitterComplexExplsn::AddParticle applies to each birth
ExplosionCloud::ExplosionCloud(const int32_t at[3], int type, unsigned phase, int16_t scale,
                               int32_t vx, int32_t vy, unsigned count)
    : Emitter(at, (int)count, 0x4000, 7, true), scale_(scale) {
    additive_ = true;
    Particle p{};
    p.colour = 0x7FFF; p.alpha = 28; p.life = 60; p.alphaStep = -1;
    if (!phase) {
        p.size = (int16_t)(6*inv_);
        p.v[0] = (int16_t)(((int16_t)vx*inv_) >> 12);
        p.v[1] = (int16_t)(((int16_t)vy*inv_) >> 12);
        p.v[2] = (int16_t)(0x1000*inv_ >> 12);
        addFromData(p);
    } else if ((type & ~1) == 6) {
        // the big-vehicle ring: puffs spaced 0x4304 apart with a little jitter, half a unit out along the ring
        // plus the drift, rising 0.65 a frame and growing by (phase / 5) / 2 x 0.1
        const int32_t fifth = (int32_t)(((int64_t)phase << 12) / 5);
        const int32_t half = (int32_t)((uint32_t)(fifth >> 1) << 17) >> 17;   // SBFX #1, #15
        const int16_t grow = (int16_t)((uint32_t)(half * 0x199) >> 12);
        for (unsigned i = 0; i < count; ++i) {
            const int angle = (int)((i * 0x4304 + Rand16NonCritical(10) + 0x4304) & 0xFFFF);
            const int sn = fastsin(angle), cs = fastsin(angle + 0x4000);
            const int32_t ax = (int32_t)(int16_t)(sn - cs) * 2048 + vx * 4096;
            const int32_t ay = (int32_t)(int16_t)(cs + sn) * 2048 + vy * 4096;
            p.v[0] = (int16_t)(((int64_t)scale * ax) >> 24);
            p.v[1] = (int16_t)(((int64_t)scale * ay) >> 24);
            p.v[2] = 0xA66;
            p.size = (int16_t)((uint32_t)(scale * (int32_t)(randNC(0x800) + 0x3800)) >> 12);
            p.grow = grow;
            p.p[2] = (int16_t)(p.p[2]+(p.size >> 1));   // AddParticle, as for the bursts below
            for (int k = 0; k < 3; ++k) {
                p.p[k] = (int16_t)((p.p[k]*inv_) >> 12);
                p.v[k] = (int16_t)((p.v[k]*inv_) >> 12);
            }
            p.size = (int16_t)((p.size*inv_) >> 12); p.grow = (int16_t)((p.grow*inv_) >> 12);
            addFromData(p);
        }
    } else for (unsigned i = 0; i < count; ++i) {
        // AddParticle converts the standard data in place. Its height survives between births.
        const int spread = type == 0 ? 0x1998 : 0x1000, bias = type == 0 ? 0xCCC : 0x800;
        p.v[0] = (int16_t)(((int64_t)((int)randNC(spread)-bias)*scale) >> 12);
        p.v[1] = (int16_t)(((int64_t)((int)randNC(spread)-bias)*scale) >> 12);
        p.size = (int16_t)(((int64_t)(randNC(0x800)+0x3800)*scale) >> 12);
        p.v[2] = (int16_t)(((int64_t)(randNC(0x3AF)+0x51)*scale) >> 12);
        p.grow = (int16_t)(((int64_t)(randNC(0xA4)+0x28)*scale) >> 12);
        p.p[2] = (int16_t)(p.p[2]+(p.size >> 1));
        for (int k = 0; k < 3; ++k) {
            p.p[k] = (int16_t)((p.p[k]*inv_) >> 12);
            p.v[k] = (int16_t)((p.v[k]*inv_) >> 12);
        }
        p.size = (int16_t)((p.size*inv_) >> 12); p.grow = (int16_t)((p.grow*inv_) >> 12);
        addFromData(p);
    }
    dying = true;
}

void ExplosionCloud::process(uint32_t hours) {
    smoke_.clear();
    Emitter::process(hours);
    if (owner_ && !smoke_.empty()) {
        auto* smoke = owner_->add<SmokeEmitter>(smokeOrigin_,(int)smoke_.size());
        smoke->followPlayer = true; smoke->dying = true;
        for (const Particle& p : smoke_) smoke->addParticleFromData(p);
    }
}

void ExplosionCloud::updateParticle(Particle& p) {   // cParticleEmitterComplexExplsn::UpdateParticle
    for (int k = 0; k < 3; ++k) {
        p.p[k] = (int16_t)(p.p[k] + p.v[k]);
        p.v[k] = (int16_t)(p.v[k]-((p.v[k]*0x199) >> 12));
    }
    p.life = (uint16_t)(p.life - 2); p.size = (int16_t)(p.size + p.grow);
    if (!(frame_ & 1)) p.alpha = (uint8_t)std::clamp((int)p.alpha-(p.life < 5 ? 5 : 0),1,31);
    if (p.life <= 2 && p.alpha >= 2) p.life |= 4;
    for (int shift = 0; shift <= 10; shift += 5)
        if (((p.colour >> shift) & 31) > -p.alphaStep)
            p.colour = (uint16_t)(p.colour+p.alphaStep*(1 << shift));
    if (p.life != 24) return;
    Particle smoke = p;
    for (int k = 0; k < 3; ++k) {
        const int64_t relative = ((((int64_t)pos[k]-smokeOrigin_[k])*inv_ >> 12)*0x333) >> 12;
        if (relative < INT16_MIN || relative > INT16_MAX) return;
        smoke.p[k] = (int16_t)(relative+((p.p[k]*0x333) >> 12));
        smoke.v[k] = (int16_t)((p.v[k]*0x333) >> 13);
    }
    smoke.size = (int16_t)((p.size*0x333) >> 12);
    smoke.grow = (int16_t)((((int64_t)scale_*0x33300000 >> 32)*0x333) >> 13);
    smoke.alpha = 30; smoke.life = 20;
    smoke_.push_back(smoke);
}

int ExplosionDebris::alive_count_ = 0;

ExplosionDebris* ExplosionDebris::create(const int32_t at[3], int type) {
    if (alive_count_) return nullptr;
    alive_count_ = 1;
    ExplosionDebris* debris = TheParticles().add<ExplosionDebris>(at, type);
    debris->counted_ = true;
    return debris;
}

ExplosionDebris::ExplosionDebris(const int32_t at[3], int type) : Emitter(at,5,0x4000,19,false) {
    Particle p{};
    p.p[2] = 0x1000; p.life = 31; p.alpha = 31;
    for (int i = 0; i < 5; ++i) {
        p.alphaStep = 0;   // AddDebrisParticle keeps the trail handle here (-1: none)
        if (type == 2) p.colour = 0x1D8E;
        else {
            const int shade = (int)Rand16NonCritical(25);
            const int red = shade+Rand16NonCritical(5), green = shade+Rand16NonCritical(5), blue = shade+Rand16NonCritical(5);
            p.colour = (uint16_t)(red | (green << 5) | (blue << 10));
        }
        const uint16_t angle = (uint16_t)Rand16NonCritical(0xFFFF);
        const int sn = (int)(std::sin(angle*9.587378e-05f)*4096.f);
        const int cs = (int)(std::sin((uint16_t)(angle+0x4000)*9.587378e-05f)*4096.f);
        p.v[0] = (int16_t)((sn-cs)*2); p.v[1] = (int16_t)((cs+sn)*2); p.v[2] = 0x2000;
        p.angle = 0; p.spin = (int16_t)randNC(0x1000);
        p.size = (int16_t)(randNC(0x800)+0x800);
        // AddDebrisParticle mutates its standard data: the height is converted again for each birth.
        for (int k = 0; k < 3; ++k) {
            p.p[k] = (int16_t)((p.p[k]*inv_) >> 12); p.v[k] = (int16_t)((p.v[k]*inv_) >> 12);
        }
        p.size = (int16_t)((p.size*inv_) >> 12);
        // a smoke trail 0.3 wide in the fragment's colour (the molotov's 0x15F), halving each point, 12 at most
        const int trail = TheTrails().getFree(this);
        p.alphaStep = (int8_t)trail;
        if (trail >= 0) {
            const int32_t across[3] = {0x1000, 0, 0};   // the emitter's matrix row 0
            TheTrails().init(trail, pos, across, type == 2 ? 0x15F : p.colour, 0x4CC, true, true, false, 1, 12, false);
        }
        addFromData(p);
    }
    dying = true;
}

void ExplosionDebris::updateParticle(Particle& p) {
    // the trail point: the fragment's world position with one shared X/Y jitter (drawn even without a trail)
    const int32_t range = (int32_t)((0x100000000000LL / inv_) >> 20);
    const int32_t jitter = (int32_t)randNC(0x1000) - 0x800;
    const int32_t point[3] = {pos[0] + jitter + (int32_t)(((int64_t)range * p.p[0]) >> 12),
                              pos[1] + jitter + (int32_t)(((int64_t)range * p.p[1]) >> 12),
                              pos[2] + (int32_t)(((int64_t)range * p.p[2]) >> 12)};
    if (p.alphaStep != -1) TheTrails().addPoint(p.alphaStep, point);
    if (p.p[2] > 0) p.v[2] -= 0x199;
    for (int k = 0; k < 3; ++k) {
        p.p[k] = (int16_t)(p.p[k]+p.v[k]); p.v[k] = (int16_t)((p.v[k]*0xE66) >> 12);
    }
    p.angle = (uint16_t)(p.angle+p.spin); p.size = (int16_t)(p.size+p.grow); p.life -= 2;
    if (p.size < 1 || ((int8_t)p.alpha <= 0 && p.life <= 10)) {
        p.life = 0;
        TheTrails().release(p.alphaStep);
    } else if (p.life <= 3) TheTrails().release(p.alphaStep);   // (the handle is kept, as in the reference)
}

int32_t ExplosionTwirls::viewAxis_[2] = {0, 0};

void ExplosionTwirls::setViewAxis(int32_t x, int32_t y) { viewAxis_[0] = x; viewAxis_[1] = y; }

// cParticleEmitterExplosionTwirls::cParticleEmitterExplosionTwirls
ExplosionTwirls::ExplosionTwirls(const int32_t at[3], int count)
    : Emitter(at, 10, 0x4000, 0x11, true) {
    additive_ = true;
    pending_ = (uint8_t)((count << 1) | 1);
    addParticle();
    dying = true;
}

void ExplosionTwirls::addParticle() {   // cParticleEmitterExplosionTwirls::AddParticle
    if (!init_) {
        init_ = true;
        std_ = Particle{};
        std_.v[2] = 0xCCC; std_.angle = 0;
        std_.grow = (int16_t)0xF4CD;
        std_.alpha = 1; std_.alphaStep = 5;
        std_.colour = 0x03FF; std_.size = 0x7000;
        std_.life = 10;   // SetStandardDataLifeTime(10)
        std_.size = (int16_t)((std_.size * inv_) >> 12);
        std_.grow = (int16_t)((std_.grow * inv_) >> 12);
        for (int k = 0; k < 3; ++k) std_.v[k] = (int16_t)((std_.v[k] * inv_) >> 12);
    }
    const int16_t x = (int16_t)(randNC(0xC000) - 0x6000), y = (int16_t)(randNC(0xC000) - 0x6000);
    const int16_t z = (int16_t)(randNC(0xC000) - 0x6000);
    // each fleck heads back towards the centre at half its offset a frame
    std_.p[0] = (int16_t)((inv_ * x) >> 12);
    std_.p[1] = (int16_t)((inv_ * y) >> 12);
    std_.p[2] = (int16_t)((inv_ * z) >> 12);
    std_.v[0] = (int16_t)((inv_ * ((int16_t)-x >> 1)) >> 12);
    std_.v[1] = (int16_t)((inv_ * ((int16_t)-y >> 1)) >> 12);
    addFromData(std_);
    const int left = ((pending_ >> 1) - 1) & 0x7F;
    pending_ = left ? (uint8_t)((pending_ & 1) | (left << 1)) : 0;
}

void ExplosionTwirls::process(uint32_t hours) {   // cParticleEmitterExplosionTwirls::Process
    Emitter::process(hours);
    if ((pending_ & 1) && (frame_ & 7)) addParticle();
}

void ExplosionTwirls::updateParticle(Particle& p) {   // cParticleEmitterExplosionTwirls::UpdateParticle
    Emitter::updateParticle(p);
    if (p.life <= 4 && p.alphaStep >= 1) p.alphaStep = -4;
    const int32_t sum = (int32_t)((uint32_t)(p.p[0] * p.p[0]) + (uint32_t)(p.p[1] * p.p[1]) + (uint32_t)(p.p[2] * p.p[2]));
    const uint32_t distance = (uint32_t)std::sqrt((double)(uint64_t)((int64_t)sum << 24));
    p.v[0] = (int16_t)(p.v[0] >> 2);
    p.v[1] = (int16_t)(p.v[1] >> 2);
    const int32_t cross = (int32_t)(((int64_t)p.p[1] * viewAxis_[0] - (int64_t)p.p[0] * viewAxis_[1]) >> 12);
    const int32_t turn = (int32_t)(int16_t)(distance >> 12) * cross;
    std_.angle = (uint16_t)(std_.angle - (turn >> 15));
}

Emitter::~Emitter() { TheTrails().forget(this); }   // the trails' weak pointers to it

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

// ============================================================================================== skid smoke
// cParticleEmitterSkidSmoke::AddParticle(tv3d, tv3d<cFixed<4,12>>, bool)
void SkidSmokeEmitter::addParticle(const int32_t at[3], const int16_t velocity[3], bool dark) {
    Particle p{};
    p.alpha = 31;   // the base emitter's standard data
    p.life = 15;    // SetStandardDataLifeTime(15)
    p.colour = dark ? 0x5294 : 0x6739;
    p.size = (int16_t)((uint32_t)inv_ >> 1);
    p.grow = (int16_t)((uint32_t)(inv_ * 0x7B) >> 12);
    for (int k = 0; k < 3; ++k) {   // DoesV3dOverflowV3d16: a puff too far from the controller is dropped
        const int64_t relative = ((int64_t)at[k] - pos[k]) * inv_ >> 12;
        if (relative < INT16_MIN || relative > INT16_MAX) return;
        p.p[k] = (int16_t)relative;
        p.v[k] = (int16_t)((uint32_t)(inv_ * velocity[k]) >> 12);
    }
    addFromData(p);
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

bool Particles::contains(const Emitter* e) const {
    for (const auto& p : emitters_) if (p.get() == e) return true;
    return false;
}

void Particles::update(uint32_t frame, uint32_t hours, const int32_t* playerPos) {
    // Complex explosions can add smoke during Process. Heap objects stay stable if the vector grows.
    const size_t count = emitters_.size();
    for (size_t i = 0; i < count; ++i) {
        Emitter* e = emitters_[i].get();
        e->frame_ = frame;
        for (int k = 0; k < 3; ++k) e->smokeOrigin_[k] = playerPos ? playerPos[k] : e->pos[k];
        if (auto* smoke = dynamic_cast<SmokeEmitter*>(e); smoke && smoke->followPlayer && playerPos) smoke->setPos(playerPos);
        if (!e->dying)
            if (auto* steam = dynamic_cast<SteamEmitter*>(e)) steam->tick(frame);
        e->process(hours);
    }
    emitters_.erase(std::remove_if(emitters_.begin(), emitters_.end(), [](const std::unique_ptr<Emitter>& e) { return e->finished(); }),
                    emitters_.end());
    TheTrails().process();   // cTrailManager::Process
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
    glDepthMask(GL_FALSE);
    glDisable(GL_LIGHTING);
    glDisable(GL_CULL_FACE);
    glDisable(GL_ALPHA_TEST);
    glEnable(GL_DEPTH_TEST);
    TheTrails().render();   // cTrailManager::Render
    // cEffectManager::Render draws emissive effects first, then smoke and other ordinary alpha effects.
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    for (const auto& e : emitters_) if (e->additive_) e->render(cam);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    for (const auto& e : emitters_) if (!e->additive_) e->render(cam);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glDisable(GL_TEXTURE_2D);
}

// ============================================================================================== petrol flame

void PetrolFlameEmitter::addParticle() {   // cParticleEmitterPetrolFlame::AddParticle
    if (!init_) {
        tmpl_.alpha = 0;
        tmpl_.colour = 0x7D4A;
        init_ = true;
    }
    tmpl_.spin = 0;
    tmpl_.p[2] = (int16_t)(tmpl_.p[2] + 0x800);
    tmpl_.v[2] = (int16_t)(tmpl_.v[2] + 0x19A);
    tmpl_.angle = (uint16_t)Rand16NonCritical(0x10000);
    tmpl_.size = (int16_t)((size_ * inv_) >> 10);
    tmpl_.grow = (int16_t)(-((inv_ * 0x800) >> 12));
    tmpl_.life = (uint16_t)(Rand16NonCritical(10) + 0x19);   // SetStandardDataLifeTime
    // the template is scaled in place, so its position and climb shrink towards a fixed point
    for (int k = 0; k < 3; ++k) {
        tmpl_.p[k] = (int16_t)((tmpl_.p[k] * inv_) >> 12);
        tmpl_.v[k] = (int16_t)((tmpl_.v[k] * inv_) >> 12);
    }
    tmpl_.alphaStep = 3;
    addFromData(tmpl_);
}

void PetrolFlameEmitter::updateParticle(Particle& q) {
    Emitter::updateParticle(q);
    if (q.size < 1) {
        q.life = 0;
        q.active = 0;
        --alive_;
    }
    if ((int8_t)q.alpha > 7) q.alphaStep = -2;
}

// ============================================================================================== sparks and steam

void SparksEmitter::addParticle(const int32_t at[3], const int16_t dir[3], uint8_t life, bool useAngle, int16_t angle,
                                bool jitter, uint16_t colour, uint16_t colourRand, int16_t scale) {
    if (!init_) {   // cParticleEmitterSparks::AddParticle
        tmpl_.size = (int16_t)((uint32_t)(inv_ * 0x6E1) >> 12);
        init_ = true;
    }
    for (int k = 0; k < 3; ++k) tmpl_.v[k] = dir[k];
    if (jitter) {
        tmpl_.v[0] = (int16_t)(randNC(0x666) + tmpl_.v[0] - 0x333);
        tmpl_.v[1] = (int16_t)(randNC(0x666) + tmpl_.v[1] - 0x333);
    }
    const int16_t heading = (int16_t)std::clamp((int)(atan2f((float)-tmpl_.v[0], (float)tmpl_.v[1]) * 10430.378f), -32768, 32767);
    tmpl_.angle = (uint16_t)((uint16_t)(heading - (useAngle ? angle : 0)) ^ 0x8000);
    tmpl_.alpha = (uint8_t)(((uint32_t)(scale * 0x1E) >> 12) + 1);
    tmpl_.life = life;   // SetStandardDataLifeTime
    tmpl_.alphaStep = -2;
    tmpl_.colour = colour;
    tmpl_.colour |= (uint16_t)(randNC((colourRand & 0x1F) + 1u) & 0x1F);
    tmpl_.colour |= (uint16_t)((randNC((colourRand >> 5 & 0x1F) + 1u) & 0x1F) << 5);
    tmpl_.colour |= (uint16_t)((randNC((colourRand >> 10 & 0x1F) + 1u) & 0x1F) << 10);
    for (int k = 0; k < 3; ++k) tmpl_.v[k] = (int16_t)((uint32_t)(tmpl_.v[k] * inv_) >> 12);
    int32_t rel[3];
    for (int k = 0; k < 3; ++k) rel[k] = (int32_t)(((int64_t)inv_ * (at[k] - pos[k])) >> 12);
    for (int k = 0; k < 3; ++k)   // DoesV3dOverflowV3d16
        if (std::abs((rel[k] + tmpl_.size) >> 12) > 6) return;
    for (int k = 0; k < 3; ++k) tmpl_.p[k] = (int16_t)rel[k];
    addFromData(tmpl_);
}

void SparksEmitter::updateParticle(Particle& q) {   // cParticleEmitterSparks::UpdateParticle
    Emitter::updateParticle(q);
    unsigned c = q.colour;
    if ((~c & 0x1F) != 0) ++c;
    const unsigned g = (c & 0x3E0) == 0x3E0 ? c : c + 0x20;
    if ((~q.colour & 0x1F) || (c & 0x3E0) != 0x3E0 || (g & 0x7C00) != 0x7C00)
        q.colour = (uint16_t)((g & 0x7C00) == 0x7C00 ? g : g + 0x400);
}

void SteamEmitter::addCloud(const int32_t at[3], uint16_t colour, int16_t scale) {   // AddParticle(pos, colour, scale)
    if (!init_) { tmpl_.spin = 0; tmpl_.active = 1; tmpl_.alphaStep = 0; init_ = true; }
    tmpl_.colour = colour;
    tmpl_.alpha = 0x1E;
    tmpl_.size = (int16_t)((uint32_t)(scale * ((int32_t)randNC(0x333) + 0x800)) >> 12);
    tmpl_.grow = (int16_t)((uint32_t)(scale * ((int32_t)randNC(0x7B) + 0x7A)) >> 12);
    int32_t rel[3];
    for (int k = 0; k < 3; ++k) rel[k] = (int32_t)(((int64_t)inv_ * (at[k] - pos[k])) >> 12);
    for (int k = 0; k < 3; ++k)
        if (std::abs((rel[k] + tmpl_.size) >> 12) > 6) return;
    for (int k = 0; k < 3; ++k) tmpl_.p[k] = (int16_t)rel[k];
    tmpl_.v[0] = tmpl_.v[1] = 0;
    tmpl_.v[2] = (int16_t)((int32_t)randNC(0xCD) + 0xCC);
    tmpl_.life = 200;
    tmpl_.angle = (uint16_t)(((uint32_t)randNC(0x8000000) + 0xC000000u) >> 12);
    for (int k = 0; k < 3; ++k) tmpl_.v[k] = (int16_t)((uint32_t)(tmpl_.v[k] * inv_) >> 12);
    tmpl_.size = (int16_t)((uint32_t)(tmpl_.size * inv_) >> 12);
    tmpl_.grow = (int16_t)((uint32_t)(tmpl_.grow * inv_) >> 12);
    addFromData(tmpl_);
}

// ============================================================================================== blood

void BloodEmitter::addParticle(const int32_t victim[3]) {   // cParticleEmitterBlood::AddParticle(cSimpleMover*)
    if (!init_) {   // SetStandardDataLifeTime(0x14) and the rest of the template, once
        tmpl_ = Particle{};
        tmpl_.life = 0x14;
        tmpl_.grow = 0;
        tmpl_.spin = 0x800;
        tmpl_.colour = 0x040D;   // RGB555: dark red
        tmpl_.alpha = 0x1D;
        tmpl_.size = (int16_t)((uint32_t)(inv_ * 0x19A) >> 12);
        tmpl_.v[2] = (int16_t)((uint32_t)(inv_ * 0x666) >> 12);
        init_ = true;
    }
    int32_t rel[3];
    for (int k = 0; k < 3; ++k) rel[k] = (int32_t)((uint64_t)((int64_t)inv_ * (victim[k] - pos[k])) >> 12);
    for (int k = 0; k < 3; ++k)   // DoesV3dOverflowV3d16 with the unscaled size 0x333
        if (std::abs((rel[k] + 0x333) >> 12) > 6) return;
    for (int k = 0; k < 3; ++k) tmpl_.p[k] = (int16_t)rel[k];
    tmpl_.v[0] = (int16_t)(randNC(0x334) - 0x19A);
    tmpl_.v[1] = (int16_t)(randNC(0x334) - 0x19A);
    tmpl_.v[2] = (int16_t)(randNC(0xA4) + 0x52);
    const int16_t angle = (int16_t)(Rand16NonCritical(0x1000) - 0x800);
    tmpl_.angle = (uint16_t)angle;
    tmpl_.spin = angle;
    for (int k = 0; k < 3; ++k) tmpl_.v[k] = (int16_t)((uint32_t)(tmpl_.v[k] * inv_) >> 12);
    tmpl_.size = (int16_t)((uint32_t)(0x333 * inv_) >> 12);
    tmpl_.grow = (int16_t)((uint32_t)(tmpl_.grow * inv_) >> 12);
    addFromData(tmpl_);
}

void DrawSheetSprite(int sprite, uint32_t c, const float p[3], const float ax[3], const float ay[3], float sx, float sy,
                     bool additive) {
    int texture; uint16_t r[4];
    if (!Assets_EffectSprite(sprite, texture, r)) return;
    GLuint tex = Assets_Texture(texture);
    int tw = 256, th = 256;
    Assets_TextureSize(texture, &tw, &th);
    float u0 = r[0] / (float)tw, v0 = r[1] / (float)th, u1 = (r[0] + r[2]) / (float)tw, v1 = (r[1] + r[3]) / (float)th;
    if (tex) { glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, tex); } else glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, additive ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA);
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
