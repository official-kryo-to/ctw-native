// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "flamethrower.h"
#include "fire.h"
#include "fixedmath.h"
#include "game.h"
#include "projectiles.h"
#include "random.h"
#include "sound.h"
#include "gfx/assets.h"
#include <glad/gl.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace {
int fastsin(int a) { return (int)(sinf((float)a * 9.587378e-05f) * 4096.f); }
int16_t atan2q(int32_t x, int32_t y) {   // ATan2: atan2f(x, y) x 10430.38, clamped to int16
    return (int16_t)std::clamp((int)(atan2f((float)x, (float)y) * 10430.378f), -32768, 32767);
}
int32_t isqrt(int64_t v) { return v > 0 ? (int32_t)(uint32_t)std::sqrt((double)(uint64_t)v) : 0; }

// cBulletBasedWeapon::TweakBulletDirection: with a locked target (cPlayer +0x1120) at least 0.05 units away
// (|dx| + |dy|), the direction points straight at it.
void tweakDirection(const Game& g, int32_t dir[3]) {
    int32_t at[3];
    int health;
    if (!g.targeting.targeted || !g.targeting.targetInfo(g, at, health)) return;
    int32_t d[3] = {at[0] - g.player.pos[0], at[1] - g.player.pos[1], at[2] - g.player.pos[2]};
    if (std::abs(d[0]) + std::abs(d[1]) < 0xCD) return;
    Normalise3(d);
    std::copy(d, d + 3, dir);
}

// cBulletBasedWeapon::TweakBulletOrigin: 1.5 units along the normalised direction
void tweakOrigin(int32_t origin[3], const int32_t dir[3]) {
    int32_t n[3] = {dir[0], dir[1], dir[2]};
    Normalise3(n);
    for (int k = 0; k < 3; ++k) origin[k] += (int32_t)((3LL * n[k]) >> 1);
}

// cParticleEmitterFlameThrower::SetEntitiesOnFire: the first entity (peds, then vehicles) within the radius and
// 30 units of height whose bearing from the emitter is within 0xE39 (20 degrees) of the fire direction catches
// fire; a vehicle also takes cVehicle::Damage type 5 with the flamethrower's damage. dist: the wall distance >> 5
// (negative: no wall, use the weapon range; above 0xFFF: nothing).
void setEntitiesOnFire(Game& g, const FlameEmitter& e, const int16_t dir[3], int32_t dist) {
    const int16_t facing = atan2q(-dir[0], dir[1]);
    const WeaponInfo& info = Weapons_Info(16);
    int32_t radius;
    if (dist < 0) radius = info.range;
    else if (dist > 0xFFF) return;
    else radius = dist << 5;
    const int32_t* at = e.pos;
    const int64_t r2 = ((int64_t)radius * radius) >> 12;
    auto inCone = [&](const int32_t p[3]) {
        const int32_t dx = p[0] - at[0], dy = p[1] - at[1], dz = p[2] - at[2];
        if (std::abs(dz) >= 0x1E000) return false;
        if ((int32_t)(((int64_t)dx * dx + (int64_t)dy * dy + (int64_t)dz * dz) >> 12) >= (int32_t)r2) return false;
        const int bearing = -(int)atan2q(-dx, dy);
        const int diff = std::abs(bearing - -(int)facing);
        return diff < 0xE39;
    };
    for (size_t i = 0; i < g.peds.peds.size(); ++i) {
        Pedestrians::Ped& p = g.peds.peds[i];
        if (p.vehicle || p.deadFrames >= 0 || p.body.dead || Fires_PedOnFire(g, p.uid)) continue;   // CanBeHit
        if (!inCone(p.body.pos)) continue;
        Fires_SetPedOnFire(g, p.uid);   // cPed::SetOnFire(shooter)
        return;
    }
    for (Vehicle& c : g.cars) {
        if (c.dead() || &c == (g.playerCar >= 0 ? &g.cars[g.playerCar] : nullptr) || Fires_VehicleOnFire(g, c.uid)) continue;
        if (!inCone(c.pos)) continue;
        Fires_SetVehicleOnFire(g, c);   // cVehicle::SetOnFire
        c.damage(info.damage);         // cVehicle::Damage, sDamageInfo type 5
        g.vehicleReaction(c, 5, info.damage, 0, g.player.pos);
        return;
    }
}

FlameEmitter* liveEmitter(FlameThrowerState& s) {
    if (s.emitter && !TheParticles().contains(s.emitter)) s.emitter = nullptr;
    return s.emitter;
}

// cFlameThrower::CreateBullet
void createBullet(Game& g, Weapon& w) {
    FlameThrowerState& s = w.flame;
    int32_t dir[3] = {g.player.fireDir[0], g.player.fireDir[1], 0};
    tweakDirection(g, dir);
    int32_t off[3] = {dir[0] * 2, dir[1] * 2, 0x1000};
    tweakOrigin(off, dir);
    const int32_t start[3] = {g.player.pos[0], g.player.pos[1], g.player.pos[2] + 0x2000};
    const int32_t end[3] = {off[0] + start[0] + dir[0] * 10, off[1] + start[1] + dir[1] * 10,
                            off[2] + g.player.pos[2] + dir[2] * 10};
    s.flags &= 0x7F;
    int32_t contact[3], n[3], wall[3] = {0, 0, 0};
    const bool hit = Projectiles_SphereStatics(g.collision, start, end, 0x333, contact, n);
    if (hit) {
        s.flags |= 0x80;
        const int32_t h[3] = {contact[0] - start[0], contact[1] - start[1], contact[2] - start[2]};
        const int64_t d2 = (int64_t)h[0] * h[0] + (int64_t)h[1] * h[1] + (int64_t)h[2] * h[2];
        s.wall = (int16_t)((uint32_t)isqrt(d2) >> 5);
        if (d2 < (int64_t)off[0] * off[0] + (int64_t)off[1] * off[1] + (int64_t)off[2] * off[2])
            std::copy(h, h + 3, off);
        for (int k = 0; k < 3; ++k) wall[k] = h[k] - off[k];
    }
    if (FlameEmitter* e = liveEmitter(s)) {
        e->setPos(start);
        e->addParticle(dir, off, 0x1000, hit, wall);
        const int16_t fire[3] = {g.player.fireDir[0], g.player.fireDir[1], g.player.fireDir[2]};
        setEntitiesOnFire(g, *e, fire, (s.flags & 0x80) ? s.wall : -0x1000);
    } else {
        s.emitter = TheParticles().add<FlameEmitter>(g.player.pos);
        s.emitter->setPos(start);
        s.emitter->addParticle(dir, off, 0x1000, hit, wall);
        s.emitter->dying = true;   // +0x104 |= 0x20: removed once its last flame is gone
    }
}
}   // namespace

// ============================================================================================ the emitter

FlameEmitter::FlameEmitter(const int32_t pos[3]) : Emitter(pos, 20, 0x14000, 11, false) {
    additive_ = true;   // cEffectRenderable +0x11
}

void FlameEmitter::addParticle(const int32_t dir[3], const int32_t off[3], int16_t scale, bool hitWall,
                               const int32_t wall[3]) {
    if (!init_) {
        tmpl_ = Particle{};
        tmpl_.life = 0x32;   // SetStandardDataLifeTime
        tmpl_.colour = 0x7FFF;
        tmpl_.size = (int16_t)((scale * inv_) >> 12);
        tmpl_.alpha = 1;
        tmpl_.alphaStep = 0x0A;
        tmpl_.grow = (int16_t)(((int64_t)inv_ * scale * 0x4CD) >> 24);
        init_ = true;
    }
    tmpl_.alphaStep = 0;   // reused as the life at which the flame reaches the wall and stops
    int32_t v[3];
    for (int k = 0; k < 3; ++k) v[k] = (int32_t)(((int64_t)dir[k] * 0x999) >> 12);
    if (hitWall) {
        const int64_t speed = isqrt((int64_t)v[0] * v[0] + (int64_t)v[1] * v[1] + (int64_t)v[2] * v[2]);
        int8_t frames = 0;
        if (speed) {
            const uint64_t toWall = (uint32_t)isqrt((int64_t)wall[0] * wall[0] + (int64_t)wall[1] * wall[1] +
                                                    (int64_t)wall[2] * wall[2]);
            frames = (int8_t)((int64_t)(toWall << 32) / speed >> 32);
        }
        tmpl_.alphaStep = (int8_t)((int8_t)tmpl_.life - frames);
    }
    for (int k = 0; k < 3; ++k) tmpl_.v[k] = (int16_t)((v[k] * inv_) >> 12);
    const int32_t r = (int32_t)Rand32NonCritical(0x2000) - 0x1000;
    tmpl_.p[0] = (int16_t)(((off[0] + (int32_t)(((int64_t)r * -off[1]) >> 14)) * inv_) >> 12);
    tmpl_.p[1] = (int16_t)(((off[1] + (int32_t)(((int64_t)off[0] * r) >> 14)) * inv_) >> 12);
    tmpl_.p[2] = (int16_t)((inv_ * off[2]) >> 12);
    const int16_t a = (int16_t)Rand16NonCritical(0x8000);
    if ((int16_t)tmpl_.angle < 1) {   // the spin alternates with the previous flame's angle
        tmpl_.angle = (uint16_t)a;
        tmpl_.spin = (int16_t)Rand16NonCritical(0x4000);
    } else {
        tmpl_.angle = (uint16_t)-a;
        tmpl_.spin = (int16_t)-(int16_t)Rand16NonCritical(0x1555);
    }
    addFromData(tmpl_);
}

void FlameEmitter::updateParticle(Particle& q) {   // cParticleEmitterFlameThrower::UpdateParticle
    for (int k = 0; k < 3; ++k) q.p[k] = (int16_t)(q.p[k] + q.v[k]);
    q.angle = (uint16_t)(q.angle + q.spin);
    q.life = (uint16_t)(q.life - 2);
    q.size = (int16_t)(q.size + q.grow);
    int alpha;
    if (q.life < 0x17) alpha = (int8_t)(q.alpha - 5);
    else {
        alpha = (int8_t)(q.alpha + 10);
        // white towards orange: red -2, green -4, blue -6 a frame, down to 2 / 4 / 6
        const unsigned c = q.colour;
        const unsigned r = (c & 0x1F) < 3 ? c : c - 2;
        const unsigned gr = (r & 0x3E0) < 0x81 ? r : r - 0x80;
        if ((c & 0x1F) > 2 || (r & 0x3E0) > 0x80 || (gr & 0x7C00) > 0x1800)
            q.colour = (uint16_t)((gr & 0x7C00) < 0x1801 ? gr : gr + 0xE800);
    }
    q.alpha = (uint8_t)std::clamp(alpha, 0, 31);
    if (alpha < 1) {
        q.life = 0;
        q.active = 0;
        --alive_;
    } else if (q.life < 2) q.life |= 4;
    // one in four flames leaves smoke as it fades in (cGeneralParticleController::AddSmoke, colour 2)
    int32_t rel[3];
    for (int k = 0; k < 3; ++k) rel[k] = (int32_t)(((int64_t)inv_ * (pos[k] - smokeOrigin_[k])) >> 12);
    if (q.life == 0x18) {
        bool overflow = false;
        for (int k = 0; k < 3; ++k) overflow |= std::abs(rel[k] >> 12) > 6;
        if (!overflow && Rand16NonCritical(4) == 0) {
            Particle s{};
            for (int k = 0; k < 3; ++k) s.p[k] = (int16_t)(q.p[k] + rel[k]);
            s.p[2] = (int16_t)(s.p[2] + ((inv_ * 0x2266) >> 12));
            s.v[0] = (int16_t)(q.v[0] >> 1);
            s.v[1] = (int16_t)(q.v[1] >> 1);
            s.v[2] = (int16_t)((inv_ * 0x666) >> 12);
            s.angle = q.angle;
            s.colour = 0x3DEF;   // cParticleEmitterSmoke::AddParticle(sParticle, 2)
            s.size = q.size;
            s.grow = (int16_t)((inv_ * 0x333) >> 12);
            s.life = 0x14;
            s.active = q.active;
            s.alpha = 10;
            s.alphaStep = 0x16;
            s.flags = q.flags;
            smoke_.push_back(s);
            q.alphaStep = -5;
        }
    }
    if ((int)q.life <= (int)q.alphaStep) q.v[0] = q.v[1] = 0;   // reached the wall
}

void FlameEmitter::process(uint32_t hours) {
    smoke_.clear();
    Emitter::process(hours);
    if (owner_ && !smoke_.empty()) {
        auto* smoke = owner_->add<SmokeEmitter>(smokeOrigin_, (int)smoke_.size());
        smoke->followPlayer = true;
        smoke->dying = true;
        for (const Particle& p : smoke_) smoke->addParticleFromData(p);
    }
}

// ============================================================================================ the weapon

void FlameThrower_Fire(Game& g, Weapon& w) {
    if (w.state == 0) {
        if (w.ammoCount() == 1) TheSound().playerEvent(99, 35, 1000);   // FireEmptySfx with the last of the fuel
        if (w.ammoCount() == 0) {
            w.setState(5, 0);
            return;
        }
        // FireSfx: the looping flame sound rises by 16 (it falls by 8 a frame in Update)
        w.flame.volume = (int8_t)(std::min<int>(w.flame.volume, 0x6F) + 0x10);
        TheSound().playerEvent(0x24, w.flame.volume, 200);
        createBullet(g, w);
        w.reduceAmmo(1);
    }
    w.flame.flags |= 0x20;
}

void FlameThrower_Update(Game&, Weapon& w) {
    FlameThrowerState& s = w.flame;
    // the jet keeps bending only while firing: an idle frame restarts it straight
    s.flags &= (s.flags & 0x20) ? 0xDF : 0xCF;
    if (s.volume > 0) {
        s.volume = (int8_t)(s.volume > 7 ? s.volume - 8 : 0);
        TheSound().playerEvent(0x24, s.volume, 200);
    }
}

void FlameThrower_Jet(Game& g, Weapon& w) {   // CreateGasJet
    FlameThrowerState& s = w.flame;
    s.jetReady = false;
    if (w.ammoCount() == 0 || g.playerCar >= 0 || g.player.dead || g.player.hidden) return;
    int32_t dir[3] = {g.player.fireDir[0], g.player.fireDir[1], g.player.fireDir[2]};
    tweakDirection(g, dir);
    const int16_t angle = (int16_t)-atan2q(-dir[0], dir[1]);
    int32_t origin[3] = {g.player.pos[0], g.player.pos[1], g.player.pos[2] + 0x2000};
    dir[2] = 0;
    tweakOrigin(origin, dir);
    const int c = fastsin(angle + 0x4000), sn = fastsin(angle);
    const int32_t tx = (int32_t)(((int64_t)sn * 0x2800) >> 12), ty = (int32_t)(((int64_t)c * 5) >> 1);
    int32_t ox, oy;   // the old tip (the stored angle's)
    unsigned f = s.flags;
    bool bend;
    if (!(f & 0x10)) {
        f |= 0x10;
        s.angle = angle;
        s.flags = (uint8_t)f;
        ox = tx; oy = ty;
        bend = (f & 0xF) != 0;
    } else {
        oy = (int32_t)(((int64_t)fastsin(s.angle + 0x4000) * 0x2800) >> 12);
        ox = (int32_t)(((int64_t)fastsin(s.angle) * 0x2800) >> 12);
        bend = (f & 0xF) != 0;
    }
    int32_t p1x, p1y, p2x, p2y, p3x, p3y, p4x = ox, p4y = oy;
    if (!bend && (s.angle < angle - 0x38E || s.angle > angle + 0x38E)) {   // turned by more than five degrees
        f |= 5;
        bend = true;
    }
    s.angle = angle;
    if (bend) {   // the spine curves from the new direction to the old tip; the countdown runs down
        p2x = (ox + tx) >> 2; p2y = (oy + ty) >> 2;
        p1x = (p2x + tx) >> 3; p1y = (p2y + ty) >> 3;
        p3x = (p2x + ox) >> 1; p3y = (p2y + oy) >> 1;
        f = (f & 0xF0) | ((f - 1) & 0xF);
    } else {
        p1x = tx >> 2; p1y = ty >> 2;
        p2x = tx >> 1; p2y = ty >> 1;
        p3x = (int32_t)((3LL * tx) >> 2); p3y = (int32_t)((3LL * ty) >> 2);
    }
    s.flags = (uint8_t)f;
    if (f & 0x80) {   // the spine stops at the wall CreateBullet hit
        const int64_t q = ((int64_t)s.wall << 37) / 0x2800;
        const int64_t scaleQ = (int64_t)((uint64_t)q * 0x1000) >> 32;
        const int32_t reach = (int32_t)((uint64_t)q >> 20);
        const int32_t cx = (int32_t)((scaleQ * ((((int64_t)tx << 32) / 0x2800) >> 20)) >> 12);
        const int32_t cy = (int32_t)((scaleQ * ((((int64_t)ty << 32) / 0x2800) >> 20)) >> 12);
        auto clamp = [&](int32_t& x, int32_t& y) {
            if (reach <= isqrt((int64_t)x * x + (int64_t)y * y)) { x = cx; y = cy; }
        };
        clamp(p1x, p1y); clamp(p2x, p2y); clamp(p3x, p3y); clamp(p4x, p4y);
    }
    // the widths: a five-frame delay line of Rand32Critical(0x1000) - 0x800
    const int32_t r0 = (int32_t)Rand32Critical(0x1000) - 0x800;
    const int32_t j0 = s.jitter[0], j1 = s.jitter[1], j2 = s.jitter[2], j3 = s.jitter[3];
    s.jitter[4] = j3; s.jitter[3] = j2; s.jitter[2] = j1; s.jitter[1] = j0; s.jitter[0] = r0;
    const int64_t px = ((int64_t)c * 0x266) >> 12, py = ((int64_t)-sn * 0x266) >> 12;   // 0.15 across
    auto width = [](int64_t base, int64_t mul, int32_t j) {   // mul x base x (1 + j)
        const int64_t m = (base * mul) >> 12;
        return (int16_t)(((m * j) >> 12) + m);
    };
    const int16_t w0x = (int16_t)(3 * (int16_t)px + (int16_t)((2 * px * r0) >> 12));
    const int16_t w0y = (int16_t)(3 * (int16_t)py + (int16_t)((2 * py * r0) >> 12));
    const int16_t w1x = width(px, 0x2000, j0), w1y = width(py, 0x2000, j0);
    const int16_t w2x = width(px, 0x3000, j1), w2y = width(py, 0x3000, j1);
    const int16_t w3x = width(px, 0x3800, j2), w3y = width(py, 0x3800, j2);
    const int16_t w4x = width(px, 0x5000, j3), w4y = width(py, 0x5000, j3);
    const int16_t spine[5][2] = {{0, 0}, {(int16_t)p1x, (int16_t)p1y}, {(int16_t)p2x, (int16_t)p2y},
                                 {(int16_t)p3x, (int16_t)p3y}, {(int16_t)p4x, (int16_t)p4y}};
    const int16_t half[5][2] = {{w0x, w0y}, {w1x, w1y}, {w2x, w2y}, {w3x, w3y}, {w4x, w4y}};
    // immsprite3d::Begin(origin, 0x2000): a unit is 0x2000 / 2^24 world units
    const float k = 8192.f / 16777216.f;
    int texture = 0;
    uint16_t rect[4] = {0, 0, 1, 1};
    int tw = 256, th = 256;
    if (Assets_EffectSprite(2, texture, rect)) Assets_TextureSize(texture, &tw, &th);
    for (int i = 0; i < 5; ++i)
        for (int side = 0; side < 2; ++side) {
            const int sign = side ? 1 : -1;
            float* v = s.jet[i * 2 + side];
            v[0] = origin[0] / 4096.f + (int16_t)(spine[i][0] + sign * half[i][0]) * k;
            v[1] = origin[1] / 4096.f + (int16_t)(spine[i][1] + sign * half[i][1]) * k;
            v[2] = origin[2] / 4096.f;
            s.jetUV[i * 2 + side][0] = (rect[0] + (side ? rect[2] : 0)) / (float)tw;
            s.jetUV[i * 2 + side][1] = (rect[1] + rect[3] * i / 4.f) / (float)th;
        }
    s.flags |= 0x10;
    s.jetReady = true;
}

void FlameThrower_RenderJet(const Weapon& w) {
    const FlameThrowerState& s = w.flame;
    if (!s.jetReady) return;
    int texture = 0;
    uint16_t rect[4];
    GLuint tex = Assets_EffectSprite(2, texture, rect) ? Assets_Texture(texture) : 0;
    if (tex) { glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, tex); } else glDisable(GL_TEXTURE_2D);
    glDisable(GL_CULL_FACE);
    glDisable(GL_LIGHTING);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glColor4f(1.f, 1.f, 1.f, 0.7f);
    glBegin(GL_TRIANGLE_STRIP);
    for (int i = 0; i < 10; ++i) {
        glTexCoord2f(s.jetUV[i][0], s.jetUV[i][1]);
        glVertex3fv(s.jet[i]);
    }
    glEnd();
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
    glColor4f(1.f, 1.f, 1.f, 1.f);
}
