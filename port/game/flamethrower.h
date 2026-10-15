// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// cFlameThrower (weapon 16) and cParticleEmitterFlameThrower.
//
// While the fire button is held the weapon fires every frame (cFlameThrower::Fire never leaves state 0): FireSfx
// raises the looping sound event 0x24 by 16 (Update lets it fall by 8 a frame), and CreateBullet sweeps a 0.2-unit
// sphere 10 units along the fire direction from 2 units up, then adds one flame to the shooter's emitter. Flames are
// 20 flat, additive quads of particle sprite 11 that turn from white to orange, stop where the sweep hit a wall,
// and sometimes leave smoke. From the second shot on, SetEntitiesOnFire sets the first entity inside a 20-degree cone
// on fire (vehicles also take type-5 damage).
//
// The pilot light (CreateGasJet, drawn whenever the flamethrower is held with ammo) is a ten-vertex strip of particle
// sprite 2 along five spine points. While firing, a turn of more than five degrees bends the spine towards the old
// direction for five frames; the spine stops at a hit wall; the widths jitter with a five-frame Rand32Critical history.
// The original rolls that random number while drawing. The port renders several frames per 30 Hz tick, so the jet is
// built once per tick and drawn from that.
#pragma once
#include "particles.h"
#include <cstdint>

class Game;
class Weapon;

class FlameEmitter : public Emitter {   // cParticleEmitterFlameThrower
public:
    explicit FlameEmitter(const int32_t pos[3]);
    // AddParticle(dir, offset, scale, hitWall, toWall): one flame at offset from the emitter, moving 0.6 x dir
    void addParticle(const int32_t dir[3], const int32_t offset[3], int16_t scale, bool hitWall, const int32_t wall[3]);
    void process(uint32_t hours = 12u << 12) override;
protected:
    void updateParticle(Particle& p) override;
private:
    Particle tmpl_{};
    bool init_ = false;
    std::vector<Particle> smoke_;
    friend struct FlameThrowerTestAccess;
};

struct FlameThrowerState {   // cFlameThrower +0x18..+0x39
    FlameEmitter* emitter = nullptr;   // +0x18: weak pointer (checked against the live emitters before use)
    int32_t jitter[5] = {0, 0, 0, 0, 0};   // +0x20..+0x30
    int16_t angle = 0;                  // +0x34
    int16_t wall = 0;                   // +0x36: distance to the hit wall >> 5
    int8_t volume = 0;                  // +0x38
    uint8_t flags = 0;                  // +0x39: 0x80 hit a wall, 0x20 fired, 0x10 jet drawn, low nibble bend frames
    // the jet built this tick (CreateGasJet): ten vertices, world units
    float jet[10][3] = {};
    float jetUV[10][2] = {};
    bool jetReady = false;
};

void FlameThrower_Fire(Game& g, Weapon& w);     // cFlameThrower::Fire
void FlameThrower_Update(Game& g, Weapon& w);   // cFlameThrower::Update (before cWeapon::Update)
void FlameThrower_Jet(Game& g, Weapon& w);      // cFlameThrower::Render -> CreateGasJet, once per tick
void FlameThrower_RenderJet(const Weapon& w);
