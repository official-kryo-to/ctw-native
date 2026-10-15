// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// cTazer (weapon 19) and its cTazerCartridge dart.
//
// The taser has no ammo (cWeapon::Ammo is -1) but a charge of 1..101: firing needs more than 1, each frame of a
// shock costs 1 (cTazer::Render -> AffectCharge(-1)) and an idle frame gives 1 back (Update -> AffectCharge(1)).
// Fire shoots a dart (cTazerCartridge, a cBaseProjectile with lifetime 300 that collides with peds) from 2 units up
// at 2.5 units a frame along the fire direction, or straight at the locked target. Releasing the button, or the dart
// going 1.155 x the weapon range away, pulls it back at 64 units a second until it is within 2.5 units. A standing
// ped hit on foot is wired up: a type-3 hit (effect 0x13), then while the button is held, the charge lasts and the
// ped stays within range + 1, a type-3 hit of the weapon's damage each shock (the sixth sets him on fire) and a
// scream; between shocks the wire crackles (sound 0x67), throws sparks and sometimes a grey steam cloud. Letting go
// releases him with a zero-damage hit.
//
// The wire is a dark line from the shooter (2 units up) to the dart or ped; while shocking, both ends jitter and an
// additive zig-zag arc of 10..29 points flickers white / yellow / cyan with the frame. The original draws these in
// Render; the port builds them once per 30 Hz tick (they use Rand32NonCritical) and draws that.
//
// Approximation: the original shocks again whenever the ped is not playing animation 0x10 (the shock animation
// started by the hit's damage response, which the port does not have); the port waits that animation's length
// from the ped sprite table instead. The shock scream (AddPedComment 9) is not ported.
#pragma once
#include <cstdint>

class Game;
class Weapon;

struct TazerState {   // cTazer +0x18..+0x30
    uint32_t cartridge = 0;   // +0x18: the dart (projectile id), 0 if none
    uint32_t ped = 0;         // +0x28: the wired ped's uid
    uint8_t flags = 0;        // +0x20: bits 0-2 state (0 idle, 1 dart flying, 2 wired), 3-6 shocks, 7 draw the wire
    int8_t charge = 0x65;     // +0x30
    int shockFrames = 0;      // (the port's stand-in for the victim's shock animation)
    // the wire built this tick
    float wire[2][3] = {};
    float arc[30][3] = {};
    int arcCount = 0;
    uint16_t arcColour = 0x7FFF;
    bool wireReady = false;
};

void Tazer_Fire(Game& g, Weapon& w, bool held);     // cTazer::Fire
void Tazer_Update(Game& g, Weapon& w, bool held);   // cTazer::Update (before cWeapon::Update)
void Tazer_Wire(Game& g, Weapon& w);                // cTazer::Render's geometry and AffectCharge, once per tick
void Tazer_RenderWire(const Weapon& w);
void Tazer_AffectCharge(Weapon& w, int n);          // cTazer::AffectCharge: clamped to 1..101
// cTazerCartridge::OnCollision with a ped (the projectile code calls it); true if the ped was wired up
bool Tazer_CartridgeHitPed(Game& g, uint32_t pedUid, const int32_t velocity[3]);
