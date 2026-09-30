// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "lookups.h"
#include <cstring>
#include "os/datafile.h"
#include <sstream>

namespace {
bool read(std::istream& file, void* target, size_t size) {
    return !!file.read(static_cast<char*>(target), static_cast<std::streamsize>(size));
}
bool magic(std::istream& file, const char* expected) {
    char header[8];
    return read(file, header, sizeof header) && std::memcmp(header, expected, sizeof header) == 0;
}
bool end(std::istream& file) { return file.peek() == std::istream::traits_type::eof(); }
std::istringstream openTable(const std::string& path) {
    std::vector<uint8_t> bytes;
    Data_ReadAll(path, bytes);   // a missing file gives an empty stream, which fails the magic check
    return std::istringstream(std::string(bytes.begin(), bytes.end()), std::ios::binary);
}
}

// The supported host is little-endian x64. Assert the disk record layout used by setup.
static_assert(sizeof(ZoneSetup) == 40 && sizeof(EventInfo) == 16 && sizeof(GearSound) == 48);

bool PopulationTables::load(const std::string& path) {
    zones.clear();
    profiles.clear();
    std::istringstream file = openTable(path);
    uint32_t zoneCount, profileCount;
    if (!magic(file, "CTWZONE1") || !read(file, &zoneCount, 4) || !read(file, &profileCount, 4) ||
            !zoneCount || zoneCount > 4096 || !profileCount || profileCount > 256) return false;
    PopulationTables next;
    next.zones.resize(zoneCount);
    next.profiles.resize(profileCount);
    if (!read(file, next.zones.data(), next.zones.size() * sizeof(ZoneSetup)) ||
            !read(file, next.profiles.data(), next.profiles.size() * sizeof(next.profiles[0])) || !end(file)) return false;
    for (const ZoneSetup& zone : next.zones)
        if (!zone.name[0] || zone.night < 0 || zone.night > 1 || zone.profile < 0 ||
                zone.profile >= static_cast<int32_t>(profileCount) || zone.pedMakeup < 0 || zone.vehMakeup < 0 ||
                zone.vehMakeup >= 8 || zone.pedDensity < 0 || zone.pedDensity > 255 ||
                zone.carDensity < 0 || zone.carDensity > 255) return false;
    for (const auto& profile : next.profiles)
        for (int32_t weight : profile)
            if (weight < 0 || weight > 255) return false;
    zones.swap(next.zones);
    profiles.swap(next.profiles);
    return true;
}

bool SoundTables::load(const std::string& path) {
    *this = SoundTables{};
    std::istringstream file = openTable(path);
    SoundTables next;
    if (!magic(file, "CTWSND1") || !read(file, next.events, sizeof next.events) ||
            !read(file, next.gears, sizeof next.gears) || !read(file, next.collisionLow, sizeof next.collisionLow) ||
            !read(file, next.collisionMed, sizeof next.collisionMed) || !read(file, next.collisionHigh, sizeof next.collisionHigh) ||
            !read(file, next.revAfterShift, sizeof next.revAfterShift) || !end(file)) return false;
    for (const EventInfo& event : next.events)
        if (event.bank < 0 || event.bank > 3 || (event.mode != 1 && event.mode != 2) ||
                event.pan < -1 || event.pan > 127) return false;
    for (const GearSound& gear : next.gears)
        if (gear.bank < 0 || gear.bank > 24 || gear.gears > 6 || gear.volume > 127) return false;
    for (const int32_t* group : {next.collisionLow, next.collisionMed, next.collisionHigh})
        for (int i = 0; i < 3; ++i)
            if (group[i] < 0 || group[i] >= 156) return false;
    *this = next;
    return true;
}

bool GameplayTables::load(const std::string& path) {
    *this = GameplayTables{};
    std::istringstream file = openTable(path);
    GameplayTables next;
    if (!magic(file, "CTWGAME1") || !read(file, next.gearRatio, sizeof next.gearRatio) ||
            !read(file, next.topRatio, sizeof next.topRatio) || !read(file, next.laneRow, sizeof next.laneRow) ||
            !read(file, next.laneCol, sizeof next.laneCol) || !read(file, next.laneFlag5, sizeof next.laneFlag5) ||
            !read(file, next.lanePlain, sizeof next.lanePlain) || !read(file, next.doorMax, sizeof next.doorMax) ||
            !read(file, next.spawnOffset, sizeof next.spawnOffset) || !end(file)) return false;
    for (int64_t ratio : next.gearRatio)
        if (ratio < -(8LL << 32) || ratio > (8LL << 32)) return false;
    for (int32_t ratio : next.topRatio) if (ratio <= 0 || ratio > (8 << 12)) return false;
    for (uint16_t limit : next.doorMax) if (limit > 4096) return false;
    *this = next;
    return true;
}

namespace { GameplayTables g_gameplayTables; }
const GameplayTables& TheGameplayTables() { return g_gameplayTables; }
bool LoadGameplayTables(const std::string& dataDir) { return g_gameplayTables.load(dataDir + "/gameplay_tables.bin"); }
