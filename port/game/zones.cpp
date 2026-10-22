// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "zones.h"
#include "os/datafile.h"
#include "os/gamefs.h"
#include <algorithm>
#include <cstring>

namespace {
std::string nameOf(const char* name) { return std::string(name, strnlen(name, 8)); }
}

static_assert(sizeof(NavigationSetup) == 12 && sizeof(IslandSetup) == 12);

bool NavigationTables::load(const std::string& path) {
    navigation.clear(); islands.clear();
    std::vector<uint8_t> bytes;
    uint32_t navCount = 0, islandCount = 0;
    if (!Data_ReadAll(path, bytes) || bytes.size() < 16 || std::memcmp(bytes.data(), "CTWNAV1", 8)) return false;
    std::memcpy(&navCount, bytes.data() + 8, 4); std::memcpy(&islandCount, bytes.data() + 12, 4);
    if (!navCount || navCount > 4096 || !islandCount || islandCount > 256 ||
        bytes.size() != 16 + (size_t)navCount * 12 + (size_t)islandCount * 12) return false;
    NavigationTables next;
    next.navigation.resize(navCount); next.islands.resize(islandCount);
    std::memcpy(next.navigation.data(), bytes.data() + 16, (size_t)navCount * 12);
    std::memcpy(next.islands.data(), bytes.data() + 16 + (size_t)navCount * 12, (size_t)islandCount * 12);
    for (const auto& row : next.navigation)
        if (!row.name[0] || row.affluence > 3 || row.noPopulationBleed > 1) return false;
    for (const auto& row : next.islands) if (!row.name[0] || row.island > 255) return false;
    *this = std::move(next);
    return true;
}

bool ZoneManager::Geometry::load(const std::vector<uint8_t>& bytes) {
    *this = Geometry{};
    const size_t count = bytes.size() / 16;   // cZoneMgr::Init ignores a trailing partial record
    if (count > 32767) return false;
    for (size_t i = 0; i < count; ++i) {
        const uint8_t* record = bytes.data() + i * 16;
        Rectangle r{}; r.name = nameOf((const char*)record);
        std::memcpy(&r.x0, record + 8, 2); std::memcpy(&r.y0, record + 10, 2);
        std::memcpy(&r.x1, record + 12, 2); std::memcpy(&r.y1, record + 14, 2);
        if (r.name.empty() || r.x0 > r.x1 || r.y0 > r.y1) { rectangles.clear(); return false; }
        rectangles.push_back(std::move(r));
    }
    return true;
}

int ZoneManager::Geometry::at(int32_t x, int32_t y) const {   // cZoneMgr::GetZoneForPoint
    // Native arithmetic: floor fixed-point to an integer, then signed division by five truncates towards zero.
    const int X = (x >> 12) / 5, Y = (y >> 12) / 5;
    const auto contains = [&](int index) {
        const auto& r = rectangles[index];
        return r.x0 < X && X <= r.x1 && r.y0 < Y && Y <= r.y1;
    };
    // The six-entry query history matters in overlaps: a recent rectangle wins over file order.
    for (int slot = next ? next - 1 : 7; slot != next; slot = slot ? slot - 1 : 7)
        if (recent[slot] >= 0 && contains(recent[slot])) return recent[slot];
    for (size_t i = 0; i < rectangles.size(); ++i) if (contains((int)i)) {
        recent[next] = (int)i;
        next = next < 7 ? next + 1 : 2;
        return (int)i;
    }
    return -1;
}

void ZoneManager::clear() { *this = ZoneManager{}; }

bool ZoneManager::init(const std::string& dataDir) {
    clear();
    NavigationTables navigation;
    PopulationTables population;
    GameFs fs;
    std::vector<uint8_t> nav, islands, info;
    ZoneManager next;
    if (!navigation.load(dataDir + "/navigation_tables.bin") || !population.load(dataDir + "/population_tables.bin") ||
        !fs.open(dataDir) || !fs.read("navzones.bin", nav) || !fs.read("islzones.bin", islands) || !fs.read("infozones.bin", info) ||
        !next.navigation_.load(nav) || !next.islands_.load(islands) || !next.population_.load(info)) return false;
    next.definePopulationProfiles(population);
    // Original zone startup applies default navigation/island definitions before population. The following
    // script player's OnZonesLoaded notification still needs the mission-script host; this manager does not fake it.
    next.setupDefaultZones(navigation);
    next.setupDefaultPopulation(population);
    *this = std::move(next);
    return true;
}

void ZoneManager::setupDefaultZones(const NavigationTables& tables) {   // cZoneManager::SetupDefaultZones
    navigationInfo_.assign(navigation_.rectangles.size(), NavigationInfo{});
    // Native navigation init sets text 143; affluence/bleed bytes are uninitialized until configured. The port
    // uses zero for those unknown bytes so an unmatched definition cannot expose uninitialized memory.
    for (const auto& s : tables.navigation) for (size_t i = 0; i < navigation_.rectangles.size(); ++i)
        if (navigation_.rectangles[i].name == nameOf(s.name))
            navigationInfo_[i] = {s.text, s.affluence, s.noPopulationBleed != 0};
    islandInfo_.assign(islands_.rectangles.size(), 0);
    for (const auto& s : tables.islands) for (size_t i = 0; i < islands_.rectangles.size(); ++i)
        if (islands_.rectangles[i].name == nameOf(s.name)) islandInfo_[i] = (uint8_t)s.island;
}

void ZoneManager::definePopulationProfiles(const PopulationTables& tables) {   // cZoneManager::DefinePopProfiles
    // PopulationProfile's twelve constructor arguments become these seven-bit sPopStats fields. The arguments
    // come from every constructor call in the user's binary; no profile or zone lookup data is embedded here.
    profiles_.assign(tables.profiles.size(), {});
    const int fields[12] = {1,10,0,3,5,19,7,11,12,8,15,18};
    for (size_t i = 0; i < profiles_.size(); ++i)
        for (int p = 0; p < 12; ++p) profiles_[i][fields[p]] = tables.profiles[i][p] & 127;
}

const ZoneManager::PopulationInfo& ZoneManager::defaultPopulation() {
    static const PopulationInfo info = [] {
        PopulationInfo d; d.fields[1] = 10; d.vehMakeup = 5; d.pedDensity = d.carDensity = 25; return d;
    }();
    return info;
}

void ZoneManager::setupDefaultPopulation(const PopulationTables& tables) {   // cZoneManager::SetupDefaultPopulation
    // cPopulationZones shares a day/night info pair across every geometry record with the same name.
    std::vector<std::string> names;
    for (auto& r : population_.rectangles) {
        const auto it = std::find(names.begin(), names.end(), r.name);
        r.info = (int)(it - names.begin());
        if (it == names.end()) names.push_back(r.name);
    }
    for (auto& info : populationInfo_) info.assign(names.size(), defaultPopulation());
    for (const auto& s : tables.zones) {
        const auto it = std::find(names.begin(), names.end(), nameOf(s.name));
        if (it == names.end()) continue;
        auto& info = populationInfo_[s.night][it - names.begin()];
        std::copy(profiles_[s.profile].begin(), profiles_[s.profile].end(), info.fields);
        info.pedMakeup = s.pedMakeup; info.vehMakeup = s.vehMakeup;
        info.pedDensity = std::min(s.pedDensity, 100); info.carDensity = std::min(s.carDensity, 100);
        info.sex = s.sex;
        info.flag = (s.flag & 1) != 0;
    }
}

const ZoneManager::PopulationInfo* ZoneManager::populationAt(int32_t x, int32_t y, bool night) const {
    const int index = population_.at(x, y);
    return index < 0 ? nullptr : &populationInfo_[night ? 1 : 0][population_.rectangles[index].info];
}

const ZoneManager::PopulationInfo& ZoneManager::populationOrDefault(int32_t x, int32_t y, bool night) const {
    const auto* info = populationAt(x, y, night);
    return info ? *info : defaultPopulation();
}

std::string ZoneManager::populationName(int32_t x, int32_t y) const {
    const int index = population_.at(x, y);
    return index < 0 ? std::string() : population_.rectangles[index].name;
}

const ZoneManager::NavigationInfo* ZoneManager::navigationAt(int32_t x, int32_t y) const {
    const int index = navigation_.at(x, y);
    return index < 0 ? nullptr : &navigationInfo_[index];
}

uint16_t ZoneManager::textKey(int32_t x, int32_t y) const {
    const auto* info = navigationAt(x, y); return info ? info->text : 0;
}
int ZoneManager::affluenceAt(int32_t x, int32_t y) const {
    const auto* info = navigationAt(x, y); return info ? info->affluence : 1;
}
bool ZoneManager::populationBleedDisallowedAt(int32_t x, int32_t y) const {
    const auto* info = navigationAt(x, y); return info ? info->noPopulationBleed : true;
}
int ZoneManager::islandAt(int32_t x, int32_t y) const {
    const int index = islands_.at(x, y); return index < 0 ? -1 : islandInfo_[index];
}
