// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// Traffic: ports of cPopulationManager (vehicles), cPopulationZones / cZoneManager::SetupDefaultPopulation,
// cPopInfoManager, cWanderRoads and cPhysicalIntegrator::VehicleSimpleProximityProcess.
//
// Zones (infozones.bin, 16-byte records {char name[8], i16 minX, minY, maxX, maxY in 5-unit steps}) get their
// population from the game's startup table (day and night): a PopulationProfile (weights per ped type, 7 bits each),
// the car mix (a popinfo.bin "vehicle makeup": weighted lists of vehicle ids) and the car density. At most
// min(14, density x 100 x 16 / 7500) random cars live around the player. A spawn attempt fires every 1..64 frames
// (sooner when moving fast); it rolls a ped type from the profile (civilian cars from the zone's mix, cops drive
// police cars) and places the car where a road link that passes within 10 units of the player crosses the circle
// 69 units out, out of sight, with 5 units clear (cLocalAreaKnowledge::GetCreatePosition).
//
// A traffic car is "on rails" (not simulated): cWanderRoads moves it along its cAISpline at the task speed, which
// accelerates by 0x4CE a frame to the road speed (6 + a little, x2 / x2.5 on two-lane roads) and brakes with
// v^2 / d towards whatever VehicleSimpleProximityProcess found ahead (cars in a box 6..9 units + the car's radius
// in front, peds in its path) or a red light (1024-frame cycle). When something hits it, it becomes a normal
// physics car (the driver's rejoin task, cRejoinNetworkInVehicle, is not ported: it stays put).
#pragma once
#include "aispline.h"
#include <cstdint>
#include <map>
#include <string>
#include <vector>

class Game;
class Vehicle;

class Traffic {
public:
    bool init(const std::string& dataDir);
    bool ok() const { return !makeups_.empty(); }
    void update(Game& g);   // once per game frame, before the vehicles act

    struct ZoneInfo { int fields[20]; int pedMakeup, vehMakeup, pedDensity, carDensity, sex = 0; };   // sex: 1 male, 2 female
    const ZoneInfo* zoneAt(int32_t x, int32_t y, bool night) const;
    std::string zoneName(int32_t x, int32_t y) const;
    int maxCars = 0;          // for the HUD / tests
    int count() const { return (int)drivers_.size(); }
    float densityScale = 1.f; // PC mod API: scales the target moving-car population; original default is 1
    void forget(uint32_t uid) { drivers_.erase(uid); }
    bool firedThisFrame = false;   // the vehicle schedule fired: cPopulationManager::Update skips peds this frame
    const ZoneInfo& zoneOrDefault(int32_t x, int32_t y, bool night) const;   // cPopulationZones::Info
    // popinfo.bin ped makeups: weighted pedinfo.bin record indices (cPopInfoManager::GetSubType)
    const std::vector<std::pair<uint16_t, uint16_t>>* pedMakeup(int makeup) const {
        return makeup >= 0 && makeup < (int)pedMakeups_.size() ? &pedMakeups_[makeup] : nullptr;
    }

private:
    struct Driver {           // cWanderRoads
        AISpline spline;
        int32_t speed = 6 << 12;   // +0xE8
        uint8_t horn = 0, startDelay = 1, holdCounter = 0;   // +0xF5, +0xF7, +0xFB
        bool noNext = false;       // +0xF9
        bool cop = false;          // +0xF8
        uint32_t lastFrame = 0;    // +0xF0
    };
    std::map<uint32_t, Driver> drivers_;   // by Vehicle::uid

    struct Zone { std::string name; int16_t x0, y0, x1, y1; int info = -1; };
    std::vector<Zone> zones_;
    std::vector<ZoneInfo> infos_[2];       // day, night
    // popinfo.bin
    std::vector<std::vector<uint8_t>> lists_;
    struct Makeup { std::vector<std::pair<uint16_t, uint16_t>> entries; uint8_t sea = 0, land = 0; };
    std::vector<Makeup> makeups_;
    std::vector<std::vector<std::pair<uint16_t, uint16_t>>> pedMakeups_;
    uint32_t linkCursor_ = 0;

    int vehicleFromMakeup(const Game& g, int makeup, int limit) const;   // cPopInfoManager::GetVehicleId
    bool createPos(Game& g, int network, int& a, int& b, int32_t pos[3], int& lane, int32_t& ratio);
    void generate(Game& g, int pedType, const ZoneInfo& z);
    void proximity(Game& g);
    void wander(Game& g, Vehicle& v, Driver& d);
    void forwardSpeed(Game& g, Vehicle& v, Driver& d);
};
