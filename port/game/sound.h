// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// Sound effects: a port of the parts of cAudioManager / cSoundEvents / cAudioBaseOAL the vehicles use.
//
// Data (ROM.WAD): resbnk.bin - the resident bank, u32 count + count x {u32 offset, u32 size, u32 rate} and the
// samples; each sample = 32-byte header (+4 length, +0x14 rate) + 8-bit unsigned mono PCM. carbnkN.bin - a car's
// own engine bank (same layout), loaded into RAM bank 1 for the car the player drives (gGears[type].bank).
//
// Every entity has 5 sound slots (sEventItem). A sound event (gEventInfo: bank, loop / one shot, sample, priority,
// fixed pan) is added to a slot with a volume (0..127) and a radius (squared units); looping events must be added
// again every frame or they stop. ProcessStandardSfx: volume = full within 0.2 x radius, then falls off with the
// square of the distance; pan from the camera's point of view; the pitch of a slot is 1 + 20 x (rate - value) / rate
// for the value the vehicle code puts in the slot.
#pragma once
#include "lookups.h"
#include <cstdint>
#include <map>
#include <string>
#include <vector>

class Game;
class Vehicle;

class Sound {
public:
    bool init(const std::string& dataDir);
    void update(Game& g);   // once per game frame, after the vehicles moved
    void doorEvent(const Vehicle& v, bool open);                 // cVehicle::OpenDoor / SetDoorClosed
    void collision(const Vehicle& v, int strength);              // cAudioManager::AddCollision
    int explosion(Game& game, const int32_t pos[3]);              // cExplosionBigVehicle::PlayScriptSfx
    void propSmash(const int32_t pos[3], int effect);              // cAudioManager::AddPropCollision
    void propSmash(const int32_t pos[3], int effect, int radius);
    bool horn = false;                                           // the player holds the horn
    bool ok() const { return !res_.data.empty(); }

private:
    struct Bank {
        std::vector<uint8_t> data;
        struct Entry { uint32_t offset, size, rate; };
        std::vector<Entry> entries;
        bool load(const std::string& dataDir, const char* name);
        bool sample(int i, const uint8_t*& pcm, uint32_t& len, uint32_t& rate) const;
    };
    Bank res_, car_;
    SoundTables tables_;
    int carBankEnum_ = -1;

    struct Slot { int event = 0x9C; int volume = 0, radius = 0, sfx = -1; int32_t freq = 0, lastFreq = 0;
                  bool active = false; int voice = 0; uint32_t rate = 22050; };
    struct Entity { Slot s[5]; int32_t pos[3] = {0, 0, 0}; bool seen = false; };
    std::map<uint32_t, Entity> ents_;
    uint32_t ticks_ = 0;
    uint32_t lastCollision_ = 0;

    int addEvent(Entity& e, int event, int volume, int radius, int sfx);   // cAudioManager::AddSoundEvent
    void processEntity(Game& g, Entity& e, bool persistent);              // ProcessStandardSfx
    void carEngine(Game& g, Vehicle& v, Entity& e);                       // ProcessEntityTypeCar
    void playerCar(Game& g, Vehicle& v, Entity& e);                       // ProcessEntityTypePlayerCar
    void playerPed(Game& g);                                            // unarmed ProcessEntityTypePlayerPed
    Entity ped_;
    Entity script_[8];   // original PlayScriptSfx pool: eight positional one-shots
    int lastWalkFrame_ = -1;
    bool firstFoot_ = true;

    // cSoundEvents state for the player's car (+0x18 .. +0x66)
    int state_ = 0, revs_ = 0, volA_ = 0, volB_ = 0, gear_ = 0, lastRpm_ = 0;
    int16_t damageClank_ = 0;
    int8_t skidTimer_ = 0, skidCount_ = 0;
    int8_t hornHeld_ = -1;
    uint32_t lastMs_ = 0;
    uint32_t playerUid_ = 0;
    void stopSlots(Entity& e, bool bankOnly = false);
    void stopLoops(Entity& e);
    friend struct SoundTestAccess;
};

Sound& TheSound();
