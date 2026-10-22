// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#pragma once
#include "lookups.h"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

// Setup extracts these definitions from the user's supported game binary. Geometry remains in the game WAD.
struct NavigationSetup {
    char name[8];
    uint16_t text;
    uint8_t affluence, noPopulationBleed;
};
struct IslandSetup { char name[8]; uint32_t island; };
struct NavigationTables {
    std::vector<NavigationSetup> navigation;
    std::vector<IslandSetup> islands;
    bool load(const std::string& path);
};

class ZoneManager {
public:
    struct PopulationInfo {
        int fields[20]{};
        int pedMakeup = 0, vehMakeup = 0, pedDensity = 0, carDensity = 0, sex = 0;
        bool flag = false;   // native sZoneCycleInfo +0x1B; retained without assuming a consumer's meaning
    };
    struct NavigationInfo { uint16_t text = 143; uint8_t affluence = 0; bool noPopulationBleed = false; };
    bool init(const std::string& dataDir);
    void clear();
    const PopulationInfo* populationAt(int32_t x, int32_t y, bool night) const;
    const PopulationInfo& populationOrDefault(int32_t x, int32_t y, bool night) const;
    std::string populationName(int32_t x, int32_t y) const;
    const NavigationInfo* navigationAt(int32_t x, int32_t y) const;
    uint16_t textKey(int32_t x, int32_t y) const;
    int affluenceAt(int32_t x, int32_t y) const;
    bool populationBleedDisallowedAt(int32_t x, int32_t y) const;
    int islandAt(int32_t x, int32_t y) const;
private:
    struct Rectangle { std::string name; int16_t x0, y0, x1, y1; int info = -1; };
    struct Geometry {
        std::vector<Rectangle> rectangles;
        mutable std::array<int, 8> recent{{-1,-1,-1,-1,-1,-1,-1,-1}};
        mutable int next = 2;   // native slots 0 and 1 are reserved for the two players
        bool load(const std::vector<uint8_t>& bytes);
        int at(int32_t x, int32_t y) const;
    } navigation_, islands_, population_;
    std::vector<NavigationInfo> navigationInfo_;
    std::vector<uint8_t> islandInfo_;
    std::vector<std::array<int, 20>> profiles_;
    std::vector<PopulationInfo> populationInfo_[2];
    void setupDefaultZones(const NavigationTables& tables);
    void definePopulationProfiles(const PopulationTables& tables);
    void setupDefaultPopulation(const PopulationTables& tables);
    static const PopulationInfo& defaultPopulation();
};
