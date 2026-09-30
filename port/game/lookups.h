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
    bool load(const std::string& path);
};

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

struct RestartTables {
    struct Point { int32_t pos[3], heading; };
    std::vector<Point> hospitals;
    bool load(const std::string& path);
};
