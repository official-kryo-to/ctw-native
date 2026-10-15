// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "taser.h"
#include "fire.h"
#include "fixedmath.h"
#include "game.h"
#include "particles.h"
#include "random.h"
#include "sound.h"
#include <glad/gl.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace {
Pedestrians::Ped* findPed(Game& g, uint32_t uid) {
    if (!uid) return nullptr;
    for (Pedestrians::Ped& p : g.peds.peds)
        if (p.uid == uid) return &p;
    return nullptr;
}

// the length of the ped's shock animation (0x10), in frames, from the ped sprite table
int shockLength(const Game& g) {
    if (!g.pedSprites.ok()) return 15;
    const int frames = g.pedSprites.numFrames(0x10), rate = g.pedSprites.rate(0x10);
    if (frames <= 0 || rate <= 0) return 15;
    return std::max(1, (frames << 8) / rate);
}

Weapon* liveTazer(Game& g) {   // the shooter's current weapon must still be the taser (OnCollision)
    Weapon* w = g.playerWeapons.current();
    return w && w->type == 19 ? w : nullptr;
}
}   // namespace

void Tazer_AffectCharge(Weapon& w, int n) {   // cTazer::AffectCharge
    int c = (int)w.tazer.charge + n;
    if (c < 2) c = 1;
    if (c > 100) c = 0x65;
    w.tazer.charge = (int8_t)c;
}

void Tazer_Fire(Game& g, Weapon& w, bool) {   // cTazer::Fire
    TazerState& t = w.tazer;
    if (w.state != 0 || (t.flags & 7) || t.charge <= 1) return;
    // CreateBullet: FireSfx, then the dart from 2 units up at 2.5 units a frame (straight at a locked target)
    TheSound().playerEvent(100, 0x3C, 200);
    int32_t dir[3] = {g.player.fireDir[0], g.player.fireDir[1], g.player.fireDir[2]};
    int32_t at[3];
    int health;
    if (g.targeting.targeted && g.targeting.targetInfo(g, at, health)) {
        int32_t d[3] = {at[0] - g.player.pos[0], at[1] - g.player.pos[1], at[2] - g.player.pos[2]};
        if (std::abs(d[0]) + std::abs(d[1]) > 0xCC) {
            Normalise3(d);
            std::copy(d, d + 3, dir);
        }
    }
    const int32_t from[3] = {g.player.pos[0], g.player.pos[1], g.player.pos[2] + 0x2000};
    const int32_t launch[3] = {(int32_t)(((int64_t)dir[0] * 0x2800) >> 12), (int32_t)(((int64_t)dir[1] * 0x2800) >> 12),
                               (int32_t)(((int64_t)dir[2] * 0x2800) >> 12)};
    t.cartridge = g.projectiles.fireTazer(g, from, launch);
    t.flags = (uint8_t)((t.flags & 0x80) | 1);
    w.setState(1, 0);
}

bool Tazer_CartridgeHitPed(Game& g, uint32_t uid, const int32_t velocity[3]) {
    Weapon* w = liveTazer(g);
    Pedestrians::Ped* p = findPed(g, uid);
    if (!w || !p || g.player.dead || p->vehicle || !p->body.onGround()) return false;   // SetIsStanding, Vehicle
    w->tazer.ped = uid;
    // the first jolt: type 3, the weapon's damage, along the dart's velocity (effect response 6, effect 0x13)
    (void)velocity;
    g.peds.fireHit(g, *p, Weapons_Info(19).damage, g.player.pos);
    w->tazer.shockFrames = shockLength(g);
    return true;
}

void Tazer_Update(Game& g, Weapon& w, bool held) {   // cTazer::Update
    TazerState& t = w.tazer;
    uint8_t f = t.flags & 0x7F;
    t.flags = f;
    Pedestrians::Ped* ped = findPed(g, t.ped);
    const bool inWater = false;   // (cPed +0x210)
    if ((f & 7) == 1) {
        if (!ped) {
            if (!g.projectiles.alive(t.cartridge) || !held || inWater) {
                f &= 0x78;
                t.ped = 0;
                g.projectiles.expire(t.cartridge);   // cTazerCartridge +0x110 = 0
                t.cartridge = 0;
            } else f |= 0x80;
        } else f = (uint8_t)((f & 0x78) | 2);
        t.flags = f;
    }
    if ((f & 7) == 2) {
        bool keep = false;
        if (ped) {
            if (t.shockFrames > 0) {   // the victim is still in his shock animation: the wire crackles
                --t.shockFrames;
                TheSound().objectEvent(0x40000000u | ped->uid, ped->body.pos, 0x67, 0x7F, 200);
                t.flags |= 0x80;
                const int16_t sx = (int16_t)((int16_t)Rand32NonCritical(0x1000) - 0x800);
                const int16_t sy = (int16_t)((int16_t)Rand32NonCritical(0x1000) - 0x800);
                const int16_t dir[3] = {sx, sy, 0x800};
                g.addSpark(ped->body.pos, dir, 0x10, false, 0, true, 0x11F, 0x2E0, 0x1000);
                if (((t.flags >> 3 & 0xF) - 1u) < 5 && Rand16Critical(3) == 0)
                    g.addSteamCloud(ped->body.pos, 0x294A, 0x1000);
                keep = true;
            } else {
                const WeaponInfo& info = Weapons_Info(19);
                const int32_t r = info.range + 0x1000;
                const int64_t dx = ped->body.pos[0] - g.player.pos[0], dy = ped->body.pos[1] - g.player.pos[1],
                              dz = ped->body.pos[2] - g.player.pos[2];
                if (held && !inWater && t.charge > 1 &&
                    dx * dx + dy * dy + dz * dz <= ((((int64_t)r * r) << 20) >> 20 & ~0xFFFLL)) {
                    g.peds.fireHit(g, *ped, info.damage, g.player.pos);   // type 3, effect 0x13
                    t.shockFrames = shockLength(g);
                    if ((t.flags & 0x78) < 0x38) t.flags = (uint8_t)(((t.flags + 8) & 0x78) | (t.flags & 0x87));
                    if ((t.flags & 0x78) == 0x30) Fires_SetPedOnFire(g, ped->uid);   // the sixth shock
                    t.flags |= 0x80;
                    keep = true;
                } else {   // let go: a zero-damage type-3 hit releases him
                    g.peds.fireHit(g, *ped, 0, g.player.pos);
                }
            }
        }
        if (!keep) {
            t.flags &= 0xF8;
            t.ped = 0;
            t.cartridge = 0;
            t.shockFrames = 0;
        }
    }
    if ((t.flags & 7) == 0) Tazer_AffectCharge(w, 1);
}

void Tazer_Wire(Game& g, Weapon& w) {   // cTazer::Render: the geometry (and the charge it costs)
    TazerState& t = w.tazer;
    t.wireReady = false;
    if (!(t.flags & 0x80) || g.playerCar >= 0) return;
    int32_t sx = g.player.pos[0], sy = g.player.pos[1];
    const int32_t sz = g.player.pos[2] + 0x2000;
    int32_t dx, dy, dz;
    bool shocking = (t.flags & 7) == 2;
    if (shocking) {
        Tazer_AffectCharge(w, -1);
        const Pedestrians::Ped* ped = findPed(g, t.ped);
        if (!ped) { t.flags &= 0x7F; return; }
        dx = ped->body.pos[0] - sx; dy = ped->body.pos[1] - sy; dz = ped->body.pos[2] - sz;
        int32_t n[3] = {dx, dy, dz};
        Normalise3(n);
        const int32_t px = n[1], py = -n[0];   // across the wire
        int32_t r = (int32_t)Rand32NonCritical(0x666) - 0x333;
        sx += (int32_t)(((int64_t)px * r) >> 12);
        sy += (int32_t)(((int64_t)r * py) >> 12);
        r = (int32_t)Rand32NonCritical(0x666) - 0x333;
        dx += (int32_t)(((int64_t)px * r) >> 12);
        dy += (int32_t)(((int64_t)r * py) >> 12);
    } else {
        const Projectiles::Projectile* dart = g.projectiles.find(t.cartridge);
        if (!dart) { t.flags &= 0x7F; return; }
        dx = dart->pos[0] - sx; dy = dart->pos[1] - sy; dz = dart->pos[2] - sz;
    }
    t.wire[0][0] = sx / 4096.f; t.wire[0][1] = sy / 4096.f; t.wire[0][2] = sz / 4096.f;
    t.wire[1][0] = (sx + dx) / 4096.f; t.wire[1][1] = (sy + dy) / 4096.f; t.wire[1][2] = (sz + dz) / 4096.f;
    t.arcCount = 0;
    if (shocking) {   // the arc: odd points jitter across the wire, even points repeat the previous one
        int32_t n[3] = {dx, dy, dz};
        Normalise3(n);
        const int extra = (int)(Rand16NonCritical(0x14) & 0xFFFF);
        const int count = extra + 10;
        const uint32_t frame = g.frame;
        t.arcColour = frame % 3 == 0 ? 0x7DEF : frame % 3 == 1 ? 0x7FFF : 0x3FFF;
        std::copy(t.wire[0], t.wire[0] + 3, t.arc[0]);
        std::copy(t.wire[1], t.wire[1] + 3, t.arc[count - 1]);
        for (int i = 1; i < count - 1; ++i) {
            if (!(i & 1)) { std::copy(t.arc[i - 1], t.arc[i - 1] + 3, t.arc[i]); continue; }
            const float frac = (float)i / (float)count;
            const int step = (int)((frac >= 0.f ? 0.5f : -0.5f) + frac * 4096.f);
            const int32_t j = (int32_t)Rand32NonCritical(0x1000) - 0x800;
            t.arc[i][2] = (float)((sz + (int32_t)(((int64_t)dz * step) >> 12)) * 16) / 65536.f;
            t.arc[i][0] = (float)(int32_t)((((uint32_t)(((int64_t)n[1] * j) >> 8)) & 0xFFFFFFF0u) +
                                           (uint32_t)((sx + (int32_t)(((int64_t)dx * step) >> 12)) * 16)) / 65536.f;
            t.arc[i][1] = (float)(int32_t)((((uint32_t)(((int64_t)-n[0] * j) >> 8)) & 0xFFFFFFF0u) +
                                           (uint32_t)((sy + (int32_t)(((int64_t)dy * step) >> 12)) * 16)) / 65536.f;
        }
        t.arcCount = count;
    }
    t.flags &= 0x7F;
    t.wireReady = true;
}

void Tazer_RenderWire(const Weapon& w) {
    const TazerState& t = w.tazer;
    if (!t.wireReady) return;
    auto colour = [](uint16_t c, float a) {
        glColor4f((c & 31) / 31.f, (c >> 5 & 31) / 31.f, (c >> 10 & 31) / 31.f, a);
    };
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_LIGHTING);
    glEnable(GL_DEPTH_TEST);
    colour(0x294A, 1.f);   // cGlInterface::Colour(0x294A, 0x1F)
    glBegin(GL_LINE_STRIP);
    glVertex3fv(t.wire[0]);
    glVertex3fv(t.wire[1]);
    glEnd();
    if (t.arcCount) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_COLOR, GL_ONE);
        colour(t.arcColour, 1.f);
        glBegin(GL_LINE_STRIP);
        for (int i = 0; i < t.arcCount; ++i) glVertex3fv(t.arc[i]);
        glEnd();
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
    glColor4f(1.f, 1.f, 1.f, 1.f);
}
