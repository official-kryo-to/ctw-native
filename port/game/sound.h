// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// Sound effects: a port of the parts of cAudioManager / cSoundEvents / cAudioBaseOAL the vehicles use.
//
// Data (ROM.WAD): resbnk.bin - the resident bank, u32 count + count x {u32 offset, u32 size, u32 rate} and the
// samples; each sample = 32-byte header (+4 length, +0x14 rate) + 8-bit unsigned mono PCM. carbnkN.bin - a car's
// own engine bank (same layout). The port caches these at startup, selecting the driver's bank without file IO.
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
class GameFs;

class Sound {
public:
    bool init(const std::string& dataDir);
    void update(Game& g);   // once per game frame, after the vehicles moved
    void doorEvent(const Vehicle& v, bool open);                 // cVehicle::OpenDoor / SetDoorClosed
    void collision(const Vehicle& v, int strength);              // cAudioManager::AddCollision
    int explosion(Game& game, const int32_t pos[3]);              // cExplosionBigVehicle::PlayScriptSfx
    void propSmash(const int32_t pos[3], int effect);              // cAudioManager::AddPropCollision
    void propSmash(const int32_t pos[3], int effect, int radius);
    void pedDeath(const int32_t pos[3], bool male, bool inWater);   // cAudioManager::PlayDeathSound
    void playResident(int sample, int volume);                     // cAudioManager::PlaySfx: not positional
    void playHud(int sample, int volume);                          // resident HUD channel, independent of world pause
    void conversation(Game& game, uint32_t uid);                    // native comment type 5, on-foot civilian speech
    void waterStream(uint32_t id, const int32_t pos[3], int volume); // cannon event 0x6C, refreshed each tick
    void playerEvent(int event, int volume, int radius);           // AddSoundEvent on the player ped (weapons)
    void scriptSfx(const int32_t pos[3], int sample, int volume, int radius);   // cAudioManager::PlayScriptSfx
    // cAudioManager::PlayLoopingScriptSfx: event 0x2E on a free script slot; the slot (or -1). scriptLoop keeps it
    // playing, moves it and sets its volume each frame; stopScriptSfx frees it (StopLoopingScriptSfx).
    int loopingScriptSfx(const int32_t pos[3], int sample, int volume, int radius);
    void scriptLoop(int handle, const int32_t pos[3], int volume);
    void stopScriptSfx(int handle);
    // AddSoundEvent on a short-lived object (projectiles): objectTick keeps it alive and moves it each frame
    void objectEvent(uint32_t id, const int32_t pos[3], int event, int volume, int radius);
    void objectTick(uint32_t id, const int32_t pos[3]);
    bool horn = false;                                           // the player holds the horn
    // cAudioManager::SoundHorn -> cHorn::SoundHorn: a traffic car honks (a pattern of beeps and blasts in one of
    // three channels); a police car with no wanted level gives its event 0x81 instead, at most every 5-10 s
    void soundHorn(Game& g, const Vehicle& v);
    // cAudioManager::DoFightGrunt: a ped grunts (event 0x71, radius 400) with one of ten samples for its sex, never
    // the same one twice running, at most once every 0.5 to 1.5 s unless forced; loud: full volume, else 30..109
    void fightGrunt(uint32_t uid, const int32_t pos[3], bool male, bool force, bool loud);
    void startBurnout(const Vehicle& v);   // cAudioManager::StartBurnout: event 0x93 as the burnout launches
    bool ok() const { return !res_.data.empty(); }

private:
    struct Bank {
        std::vector<uint8_t> data;
        struct Entry { uint32_t offset, size, rate; };
        std::vector<Entry> entries;
        bool load(GameFs& fs, const char* name);
        bool sample(int i, const uint8_t*& pcm, uint32_t& len, uint32_t& rate) const;
    };
    Bank res_, car_;
    std::map<std::string, Bank> carBanks_;
    const Bank* activeCarBank_ = nullptr;
    SoundTables tables_;
    int carBankEnum_ = -1;

    struct Slot { int event = 0x9C; int volume = 0, radius = 0, sfx = -1; int32_t freq = 0, lastFreq = 0;
                  bool active = false; int voice = 0; uint32_t rate = 22050; };
    struct Entity { Slot s[5]; int32_t pos[3] = {0, 0, 0}; bool seen = false; };
    std::map<uint32_t, Entity> ents_;
    std::map<uint32_t, Entity> water_;
    std::map<uint32_t, Entity> people_; // independent namespace from vehicle sound IDs
    PedCommentTables commentTables_;
    struct CommentVoice { bool active=false; uint32_t uid=0; int slot=-1,sample=-1; uint64_t deadline=0; } comments_[2];
    struct CommentHistory { int type=-1; uint64_t time=0; };
    std::map<uint32_t,CommentHistory> commentHistory_;
    int smallCommentCursor_=0,largeCommentCursor_=0;
    bool conversationAt(Game& game,uint32_t uid,uint32_t now);
    void updatePedComments(Game& game);
    uint32_t ticks_ = 0;
    uint32_t lastCollision_ = 0;

    int addEvent(Entity& e, int event, int volume, int radius, int sfx);   // cAudioManager::AddSoundEvent
    struct HornChannel {             // cHorn: three of 0x1C bytes
        uint32_t delay = 0;          // +0x00: honks to let pass before this channel takes another
        int8_t step = -1;            // +0x04: the pattern step, -1 free
        uint8_t pattern = 0;         // +0x05
        int phase = 0;               // +0x08: 0 start the step, 1 playing it
        uint32_t uid = 0;            // +0x0C: the vehicle (0 none; the original's 9999)
        uint32_t frames = 0;         // +0x10: the step's frames left
        bool beep = false;           // +0x14: the step is a beep (event 0x46), not a blast (the event 0x47 loop)
    } horns_[3];
    uint32_t policeHornMs_ = 0;      // cHorn +0x54
    uint64_t nextGruntMs_ = 0;       // cAudioManager +0x2F48
    int16_t lastGrunt_ = 0;          // +0x2F50
    void processHorns(Game& g);      // cHorn::Process
    void processEntity(Game& g, Entity& e, bool persistent);              // ProcessStandardSfx
    void carEngine(Game& g, Vehicle& v, Entity& e);                       // ProcessEntityTypeCar
    void playerCar(Game& g, Vehicle& v, Entity& e);                       // ProcessEntityTypePlayerCar
    void playerPed(Game& g);                                            // walking ProcessEntityTypePlayerPed
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
