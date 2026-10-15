// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// cFire: a burning point, attached to a ped or vehicle or standing in the world (at most 30 at once).
//
// Each fire draws three camera-facing flames of particle sprite 14 (additive) that grow, rise and fade on their own
// cycles, and may own a cParticleEmitterFire that gets a flame on one frame in three. Its lifetime counts down by 2 a
// frame; below 3 it goes out. A burning ped (cPed::SetOnFire: 480, or 0x1E0 when set on fire by someone) takes one
// point of type-5 damage (effect 0x1D) every second frame, the player every fourth, until the fire dies or the ped
// is in water. cVehicle::SetOnFire attaches a style-2 fire (style 4 for vehicle types 4 and 5) for 300 frames,
// 0.45 x the half length towards the engine and 0.25 up.
//
// Loose fires (molotovs) slide with their velocity, damped to 90% above 0.1 units a second; falling ones get gravity.
// Their first Process removes them over water. Whatever touches one (2 units plus its own radius) catches fire.
//
// Not ported yet: the fire sound (cAudioManager::AddFireSfx), wall collisions of sliding fires, water hits, the
// burnt colours and scripted fire styles 3, 5 and 6.
#pragma once
#include <cstdint>
#include <vector>

class Game;
class FireEmitter;
class PetrolFlameEmitter;
struct WorldCamera;
class Vehicle;

class Fires {
public:
    enum Attach { None = 0, Ped = 1, Player = 2, Car = 3 };
    struct Flame { int16_t off[3], w, h; uint32_t abgr; int16_t age, rate; };   // cFire +0x100, stride 0x14
    struct Fire {
        int32_t pos[3];
        Attach attach = None;
        uint32_t uid = 0;                // the burning ped's or vehicle's uid
        int32_t offset[3] = {0, 0, 0};   // AttachToWithOwner: in the vehicle's frame
        bool byPlayer = false;           // +0x148: the player started it (the damage source)
        int16_t life = 0;                // +0x13C
        int16_t fade = 0;                // +0x13E
        int16_t sx = 0, sy = 0;          // +0x140 / +0x142: half sizes
        uint8_t style = 0;               // +0x150 low nibble (eFireStyle)
        bool emitterWanted = false;      // +0x150 bit 4
        FireEmitter* emitter = nullptr;  // +0xF0 (weak)
        bool petrol = false;             // +0x151
        PetrolFlameEmitter* petrolEmitter = nullptr;   // +0xF8 (weak)
        int32_t vel[3] = {0, 0, 0};      // cSimpleMover velocity, units a second
        bool falling = false;            // +0x150 bit 7: gravity
        bool groundChecked = false;      // +0x150 bit 6 cleared by the first Process
        Flame flames[3] = {};
        bool out = false;
    };

    // cFire::cFire(pos, owner, life, style, emitter, petrol, ...); false when 30 fires already burn
    Fire* start(Game& g, const int32_t pos[3], Attach attach, uint32_t uid, int16_t life, int style, bool emitter,
                bool byPlayer, const int32_t offset[3] = nullptr, bool petrol = false);
    // cExplosionMolotov::Process, state 1: three small fires flung outward and a central one with a petrol flame
    void molotov(Game& g, const int32_t pos[3], bool byPlayer);
    void process(Game& g);   // cFire::Process for each fire, then the burning peds' damage (cPed::Process)
    void render(const WorldCamera& cam) const;   // cFire::ManagedRender
    bool burning(Attach attach, uint32_t uid) const;   // cPed / cVehicle::IsOnFire
    void extinguish(Attach attach, uint32_t uid);      // cPed::ExtingushFire / cVehicle::ExtinguishFire
    void clear();
    const std::vector<Fire>& list() const { return fires_; }
private:
    void processFire(Game& g, Fire& f);
    void ignite(Game& g, const Fire& f);   // cFire::OnCollision: whatever touches a loose fire catches it
    std::vector<Fire> fires_;
    friend struct FiresTestAccess;
};

// cPed / cVehicle::IsOnFire and SetOnFire for the player's weapons (the source is the player)
bool Fires_PedOnFire(const Game& g, uint32_t pedUid);
bool Fires_VehicleOnFire(const Game& g, uint32_t carUid);
void Fires_SetPedOnFire(Game& g, uint32_t pedUid);
void Fires_SetVehicleOnFire(Game& g, Vehicle& car);
