// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// Local setup table formats. These declarations contain no extracted game data.
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

struct ZoneSetup {
    char name[8];
    int32_t night, profile, pedMakeup, vehMakeup, pedDensity, carDensity, sex, flag;
};
struct PopulationTables {
    std::vector<ZoneSetup> zones;
    std::vector<std::array<int32_t, 12>> profiles;
    bool load(const std::string& path);
};

struct EventInfo {
    int32_t bank, mode;
    uint16_t sfx;
    uint8_t keep, priority;
    int8_t pan;
    uint8_t radiusBoost, alt, pad;
};
struct GearSound {
    int16_t horn, horn2, drive, idle, start, pad;
    int32_t bank;
    uint8_t volume, gears, step[6];
    int32_t thr[5];
    int16_t pitch, vol;
};
struct SoundTables {
    EventInfo events[156]{};
    GearSound gears[20]{};
    int32_t collisionLow[3]{}, collisionMed[3]{}, collisionHigh[3]{}, revAfterShift[4]{};
    struct PropSfx { uint16_t sample; uint8_t volume, pad; };
    PropSfx propSfx[57]{};
    bool load(const std::string& path);
};

struct PedCommentTables {
    struct Range { int32_t first, count; } samples[2][26]{};
    struct Timing { uint32_t ownGap, randomGap, repeatGap, reserved, peerGap, reserved2; uint64_t nextAllowed; } timing[26]{};
    int32_t recentSmall[2]{}, recentLarge[4]{};
    bool loaded = false;
    bool load(const std::string& path);
};
static_assert(sizeof(PedCommentTables::Timing)==32,"native comment timing record");

struct GameplayTables {
    int64_t gearRatio[8]{};
    int32_t topRatio[6]{};
    int8_t laneRow[7]{}, laneCol[7]{};
    uint8_t laneFlag5[52]{}, lanePlain[52]{};
    uint16_t doorMax[5]{};
    int32_t spawnOffset[2][2]{};
    bool load(const std::string& path);
};
const GameplayTables& TheGameplayTables();
bool LoadGameplayTables(const std::string& dataDir);

struct RadioTables {
    struct Station { int32_t icon, stream, label, name; };
    std::vector<Station> stations;
    std::vector<std::string> streams;
    uint8_t volumeSprites[10]{};
    bool load(const std::string& path);
};

struct WeatherTables {
    uint8_t chance[8][8]{};         // cTimeCycle::msWeatherChanceTable: percentages of the next weather
    uint32_t thunder[5]{};          // UpdateLightning: resident-bank samples
    bool load(const std::string& path);
};

// cWeaponInfo::msWeaponInfos, indexed by eWeaponType (0 fists .. 24 thrown objects; 25..27 unused), and
// cWeaponManager::GetDefaultWeaponAmmo's table.
struct WeaponInfo {
    int32_t aimClass;          // +0x00
    int32_t maxAmmo;           // +0x04: -1 = infinite (melee)
    int32_t clip;              // +0x08: clip size (0: no clip)
    int32_t pickupAmmo;        // +0x0C
    int32_t damage;            // +0x10
    int32_t fireDelay;         // +0x14: cWeapon::SetState(1/2) timer, counted down by 2 a frame
    int32_t reloadTime;        // +0x18: SetState(4) timer
    int32_t range;             // +0x1C: 20.12 units (cPed::PrimaryWeaponRange)
    int16_t spread[4];         // +0x20: CalcAccuracyDir: base, then + walking / running / sprinting (AccuracyModifyer)
    int32_t textId;            // +0x28: e_main.gxt string
    int32_t slot;              // +0x2C: cWeaponManager slot (byte)
    int32_t effect;            // +0x30: sDamageInfo +0x24
    int16_t scale[2];          // +0x34/+0x36: movement factor without/with fire held (Q12); zero stops movement
    int32_t rank;              // +0x38
};
static_assert(sizeof(WeaponInfo) == 60, "cWeaponInfo");

struct WeaponTables {
    WeaponInfo info[28]{};
    uint16_t defaultAmmo[28]{};
    int32_t traceLife[3]{};         // cBulletTraces::AddTrace: frames per trace type
    bool load(const std::string& path);
};

struct RestartTables {
    struct Point { int32_t pos[3], heading; };
    std::vector<Point> hospitals;
    bool load(const std::string& path);
};
