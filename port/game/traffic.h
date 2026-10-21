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
// physics car: cWanderRoads gives up and CreateWanderRoads hands it a cWanderRoadsUnderPhysics, which rejoins the
// road and then drives at each next spline target in a straight line (cGotoTargetInVehicleStraightLine) at the
// road's speed, moving on when it is within half its speed (2.5..10 units) of it. It goes back on rails only once
// the car is off screen with a straight road ahead (cAISpline::IsJourneyStraightLine), so it is never seen to snap.
// A driver who reacts to damage by driving away (cVehicle::HandleReaction, reaction 3) keeps the speed value 0x78
// and a target of his own, so he stays under physics.
#pragma once
#include "aispline.h"
#include "driving.h"
#include "trafficavoidance.h"
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
    void afterPhysics(Game& g);
    Vehicle::Controls controls(uint32_t uid) const;
    bool pedIntercept(Game& g, uint32_t uid, TrafficAvoidance::Intercept& out) {
        return avoidance_.interceptPed(g,uid,out);
    }

    struct ZoneInfo { int fields[20]; int pedMakeup, vehMakeup, pedDensity, carDensity, sex = 0; };   // sex: 1 male, 2 female
    const ZoneInfo* zoneAt(int32_t x, int32_t y, bool night) const;
    std::string zoneName(int32_t x, int32_t y) const;
    int maxCars = 0;          // the cap of the last population update (HUD / tests)
    int count() const { return (int)drivers_.size(); }
    float densityScale = 1.f; // PC mod API: scales the target moving-car population; original default is 1
    void forget(uint32_t uid) { drivers_.erase(uid); }
    // cVehicle::HandleReaction for reaction 3: SetToPhysics, speed value 0x78, cWanderRoadsUnderPhysics
    bool driveAway(Game& g, Vehicle& v);
    bool drivingAway(uint32_t uid) const { const auto it = drivers_.find(uid); return it != drivers_.end() && it->second.underPhysics; }
    const DrivingProps& drivingProps() const { return drivingProps_; }
    // the vehicle part of cPopulationManager::Update (Game::updatePopulation): room = below the cap
    void generateRandom(Game& g, const ZoneInfo& z, bool room);
    // cPopulationManager::GetCopSubType: the cop unit by wanted level (0 police; 3, 2 and 1 from three stars on)
    static int copSubType(const Game& g);
    // cPopulationManager::GetCopsVehicleId (the helicopter is taken as loaded: the port cannot spawn it, so a roll
    // for it spawns nothing)
    int copsVehicleId(const Game& g, int subtype, int limit);
    // cPopulationManager::WantedModifier for the vehicle network: more cops with the wanted level (fewer while a
    // star is dimmed), capped per level, the rest of the cap shared out; the result is what is still missing of
    // each type. current: the population by type (Game::carPopulationByType).
    void wantedModifier(const Game& g, int fields[20], const int current[20], int cap) const;
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
        bool rejoining = false, aimValid = false, rejoinFailed = false;
        int32_t aim[3]{};
        uint8_t speedValue = 6;              // the vehicle's +0x955 (cVehicle constructor default)
        bool underPhysics = false;           // cWanderRoadsUnderPhysics
        bool reacting = false;               // ... from HandleReaction: its speed fixed (flag bit 29), a target
        bool following = false;              // ... past its rejoin: driving at the spline's targets
        Vehicle::Controls controls;
        DrivingRecovery recovery;
        int32_t beforePhysics[3]{}, stuckOrigin[2]{};
        uint16_t stuckFrames = 0;
        bool trackingStuck = false, stuck = false, movementRequested = false;
        int16_t previousTargetCheck[2] = {-1,-1};
        uint8_t completionFrame = 0;
        bool cachedCompletion = false;
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
    uint8_t spawnFallback_ = 0; // ordinary population spawn retry state (+0x5A)
    bool heliActive_ = false;   // +0x58: a police helicopter exists (never in the port)
    uint32_t heliTimer_ = 0;    // +0x5C: frames before another helicopter roll
    DrivingProps drivingProps_;
    TrafficAvoidance avoidance_;

    int vehicleFromMakeup(const Game& g, int makeup, int limit) const;   // cPopInfoManager::GetVehicleId
    bool createPos(Game& g, int network, int& a, int& b, int32_t pos[3], int& lane, int32_t& ratio);
    void generate(Game& g, int pedType, const ZoneInfo& z);
    friend struct WantedPopulationTestAccess;
    void proximity(Game& g);
    void wander(Game& g, Vehicle& v, Driver& d);
    void forwardSpeed(Game& g, Vehicle& v, Driver& d);
    bool copyRoadPosition(Game& g, const Vehicle& v, Driver& d, int32_t& distance);
    bool computeAim(Game& g, const Vehicle& v, Driver& d);
    bool completedAim(const Game& g, const Vehicle& v, Driver& d);
    void rejoin(Game& g, Vehicle& v, Driver& d);
    void wanderUnderPhysics(Game& g, Vehicle& v, Driver& d);   // cWanderRoadsUnderPhysics::Process
    int32_t roadSpeed(Game& g, const Vehicle& v, const Driver& d) const;   // iAITask::GetRoadSpeed
    friend struct TrafficTestAccess;
};
