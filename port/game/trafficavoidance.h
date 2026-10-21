// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#pragma once
#include <cstdint>
#include <cstddef>
#include <map>
#include <array>
#include <vector>

class Game;
class Vehicle;

// The entity-cache/interception part of the player's local-area knowledge, used by physical AI drivers.
// References use the port's stable entity IDs so vector relocation/removal cannot leave dangling pointers.
class TrafficAvoidance {
public:
    struct Prediction {
        int32_t time = 0, duration = 0;
        int32_t source[3]{}, obstacle[3]{};
    };
    struct Intercept : Prediction {
        uint32_t uid = 0;
        bool pedestrian = false, player = false;
        int32_t sourceRadius = 0, obstacleRadius = 0;
    };
    void reset();
    void update(Game& game); // after LocalArea::update, before physical drivers query their surroundings
    void setTarget(uint32_t vehicle, const int32_t target[3]);
    bool intercept(Game& game, const Vehicle& vehicle, Intercept& out,
                   bool vehicles = true, bool pedestrians = true);
    bool interceptPed(Game& game, uint32_t uid, Intercept& out);
    bool queue(Game& game, const Vehicle& vehicle, int32_t target[3]);

    static bool predict(const int32_t source[3], const int32_t velocity[3], int32_t radius,
                        const int32_t obstacle[3], const int32_t obstacleVelocity[3], int32_t obstacleRadius,
                        int32_t maxTime, Prediction& out);
private:
    enum class Kind { None, Vehicle, Pedestrian, Player };
    struct Ref {
        Kind kind = Kind::None;
        uint32_t uid = 0;
        bool operator==(const Ref& other) const { return kind == other.kind && uid == other.uid; }
    };
    struct Entity;
    struct Entry {
        Ref ref, obstacle;
        int64_t distance = 0;
        bool found = false, computed = false, force = true, velocityCached = false;
        int32_t velocity[3]{};
        Intercept hit;
    };
    std::vector<Entry> cars_, peds_;
    std::map<uint32_t, std::array<int32_t, 3>> targets_;
    static Entity resolve(Game& game, Ref ref);
    static void add(std::vector<Entry>& entries, size_t capacity, Ref ref, int64_t squaredDistance);
    static void resetEntries(Game& game, std::vector<Entry>& entries);
    static void dimensions(const Vehicle& vehicle, const int32_t direction[3], int32_t out[3]);
    void avoidanceVelocity(Entry& entry, const Entity& entity);
    void compute(Game& game, Entry& entry, const Entity& source, const std::vector<Entry>& candidates);
    bool query(Game& game, Entry& entry, Intercept& out, bool vehicles, bool pedestrians);
    friend struct TrafficAvoidanceTestAccess;
};
