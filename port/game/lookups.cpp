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
static_assert(sizeof(ZoneSetup) == 40 && sizeof(EventInfo) == 16 && sizeof(GearSound) == 48 && sizeof(SoundTables::PropSfx) == 4);

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

bool PedCommentTables::load(const std::string& path) {
    *this={};PedCommentTables next;auto file=openTable(path);char header[8];
    if(!read(file,header,8) || std::memcmp(header,"CTWPEDV1",8) ||
       !read(file,next.samples,sizeof next.samples) || !read(file,next.timing,sizeof next.timing) ||
       !read(file,next.recentSmall,sizeof next.recentSmall) || !read(file,next.recentLarge,sizeof next.recentLarge) || !end(file))return false;
    for(const auto& gender:next.samples)for(const auto& range:gender)
        if(range.first<0 || range.count<0 || range.first>65535 || range.count>65536-range.first)return false;
    next.loaded=true;*this=next;return true;
}

bool SoundTables::load(const std::string& path) {
    *this = SoundTables{};
    std::istringstream file = openTable(path);
    SoundTables next;
    char header[8];
    if (!read(file, header, sizeof header)) return false;
    const bool v4 = std::memcmp(header, "CTWSND4", 8) == 0;
    const bool v3 = v4 || std::memcmp(header, "CTWSND3", 8) == 0;
    if ((!v3 && std::memcmp(header, "CTWSND2", 8) != 0) || !read(file, next.events, sizeof next.events) ||
            !read(file, next.gears, sizeof next.gears) || !read(file, next.collisionLow, sizeof next.collisionLow) ||
            !read(file, next.collisionMed, sizeof next.collisionMed) || !read(file, next.collisionHigh, sizeof next.collisionHigh) ||
            !read(file, next.revAfterShift, sizeof next.revAfterShift) ||
            !read(file, next.propSfx, sizeof next.propSfx)) return false;
    if (v3 && (!read(file, next.hornPatterns, sizeof next.hornPatterns) || !read(file, next.hornFade, sizeof next.hornFade)))
        return false;
    if (v4 && !read(file, next.grunts, sizeof next.grunts)) return false;
    if (!end(file)) return false;
    next.horns = v3;
    next.hasGrunts = v4;
    for (const auto& prop : next.propSfx)
        if (prop.volume > 127) return false;
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

bool RadioTables::load(const std::string& path) {
    *this = RadioTables{};
    auto file = openTable(path);
    RadioTables next;
    uint32_t stationCount = 0, streamCount = 0;
    if (!magic(file, "CTWRAD2") || !read(file, &stationCount, 4) || !read(file, &streamCount, 4) ||
        !stationCount || stationCount > 16 || !streamCount || streamCount > 64 ||
        !read(file, next.volumeSprites, 10)) return false;
    next.stations.resize(stationCount);
    if (!read(file, next.stations.data(), stationCount * sizeof(Station))) return false;
    for (const auto& station : next.stations)
        if (station.icon < 0 || station.icon >= 32 || station.stream < 0 || station.stream >= (int)streamCount ||
            station.label < 0 || station.label > 4095 || station.name < 0 || station.name > 4095) return false;
    for (uint8_t sprite : next.volumeSprites) if (sprite >= 32) return false;
    for (uint32_t i = 0; i < streamCount; ++i) {
        char name[40];
        if (!read(file, name, sizeof name) || !std::memchr(name, 0, sizeof name)) return false;
        std::string value(name);
        if (value.find_first_of("/\\:") != std::string::npos || value == "..") return false;
        next.streams.push_back(value);
    }
    if (!end(file)) return false;
    *this = std::move(next);
    return true;
}

bool WantedTables::load(const std::string& path) {
    auto file = openTable(path);
    WantedTables next;
    if (!magic(file, "CTWWANT1") || !read(file, next.levels, sizeof next.levels) ||
        !read(file, next.crimes, sizeof next.crimes) || !read(file, next.copCarCrimes, sizeof next.copCarCrimes))
        return false;
    for (int32_t* table : {next.takeDownsPerCopCar, next.ratingToSubtractOnReachingMaxLevel, next.takeDownHealth,
                           next.copMaxCarsInCar, next.copMaxCarsOnFoot, next.copMultiplierInCar,
                           next.copMultiplierOnFoot, next.ramBlockPercent, next.flipPercent, next.targetSpeedMult,
                           next.probationaryHidePercent, next.arrivalOffsetMultiplier})
        if (!read(file, table, 7 * sizeof(int32_t))) return false;
    if (!read(file, &next.failedProbationFramesPerMinusTakedown, 4) || !end(file)) return false;
    for (int l = 1; l < 8; ++l) if (next.levels[l].threshold <= next.levels[l - 1].threshold) return false;
    for (int32_t c : next.copCarCrimes) if (c < 0 || c >= 26) return false;
    for (int32_t t : next.takeDownsPerCopCar) if (t < 0 || t > 50) return false;
    *this = next;
    return true;
}

bool ShardTables::load(const std::string& path) {
    auto file = openTable(path);
    ShardTables next;
    if (!magic(file, "CTWSHRD1")) return false;
    for (std::vector<Shard>& set : next.sets) {
        uint32_t count = 0;
        if (!read(file, &count, 4) || count == 0 || count > 64) return false;
        set.resize(count);
        for (Shard& s : set) {
            uint32_t vertices = 0;
            if (!read(file, &vertices, 4) || vertices == 0 || vertices > 16 ||
                !read(file, s.start, vertices * 8) || !read(file, s.end, vertices * 8))
                return false;
            s.count = (int)vertices;
        }
    }
    if (!end(file)) return false;
    next.loaded = true;
    *this = next;
    return true;
}

bool WeatherTables::load(const std::string& path) {
    auto file = openTable(path);
    WeatherTables next;
    if (!magic(file, "CTWWTHR1") || !read(file, next.chance, sizeof next.chance) ||
        !read(file, next.thunder, sizeof next.thunder) || !end(file)) return false;
    for (const auto& row : next.chance) {
        int total = 0;
        for (uint8_t c : row) total += c;
        if (total > 100) return false;
    }
    for (uint32_t s : next.thunder) if (s > 0xFFFF) return false;
    *this = next;
    return true;
}

bool WeaponTables::load(const std::string& path) {
    auto file = openTable(path);
    WeaponTables next;
    if (!magic(file, "CTWWPN01") || !read(file, next.info, sizeof next.info) ||
        !read(file, next.defaultAmmo, sizeof next.defaultAmmo) || !read(file, next.traceLife, sizeof next.traceLife) ||
        !end(file)) return false;
    for (int32_t life : next.traceLife) if (life < 1 || life > 127) return false;
    for (const WeaponInfo& w : next.info)
        if (w.slot < 0 || w.slot > 10 || w.clip < 0 || w.clip > 255 || w.fireDelay < 0 || w.reloadTime < 0) return false;
    *this = next;
    return true;
}

bool RestartTables::load(const std::string& path) {
    hospitals.clear();
    police.clear();
    auto file = openTable(path);
    uint32_t count = 0, policeCount = 0;
    char header[8];
    RestartTables next;
    if (!read(file, header, 8) || (std::memcmp(header, "CTWRESP1", 8) && std::memcmp(header, "CTWRESP2", 8)) ||
        !read(file, &count, 4) || !count || count > 64) return false;
    if (!std::memcmp(header, "CTWRESP2", 8) && (!read(file, &policeCount, 4) || !policeCount || policeCount > 64)) return false;
    next.hospitals.resize(count);
    next.police.resize(policeCount);
    if (!read(file, next.hospitals.data(), count * sizeof(Point)) ||
        (policeCount && !read(file, next.police.data(), policeCount * sizeof(Point))) || !end(file)) return false;
    auto valid = [](const Point& p) {
        for (int32_t coordinate : p.pos) if (coordinate < -10000 * 4096 || coordinate > 10000 * 4096) return false;
        if (p.heading < -360 || p.heading > 360) return false;
        return true;
    };
    for (const auto& p : next.hospitals) if (!valid(p)) return false;
    for (const auto& p : next.police) if (!valid(p)) return false;
    hospitals.swap(next.hospitals);
    police.swap(next.police);
    return true;
}
