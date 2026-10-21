// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#pragma once
#include "vehicle.h"
#include <cstdint>
#include <string>

// User-local acceleration and reverse-recovery tables, extracted by setup.
class DrivingProps {
public:
    bool load(const std::string& path);
    bool ok() const { return loaded_; }
    bool accelDecel(int vehicle, int gear, int32_t& acceleration, int32_t& deceleration) const;
    // cWheeledVehicle::GetMaxAccelerationFromGear: the table's acceleration for the gear (Q12, no fallback)
    int32_t maxAcceleration(int vehicle, int gear) const;
    bool reverseTimes(int attempt, uint16_t& first, uint16_t& second, uint16_t& pattern) const;
    int32_t reverseSteer(uint16_t pattern, bool secondPhase) const;
private:
    int8_t values_[103][5][2]{};
    uint16_t reverseFirst_[4]{}, reverseSecond_[4]{};
    int32_t reverseSteering_[10][2]{};
    bool loaded_ = false;
};

struct DrivingConstraints {
    int16_t steeringLock = 0, turnRate = 0; // angles, 65536 per turn
    int32_t acceleration = 0, deceleration = 0;
};

struct DrivingData {
    int32_t distance = 0, forwardSpeed = 0;
    int32_t targetVelocity[3]{};
    int16_t angle = 0;
    int32_t side = 0;
    DrivingData(const Vehicle& vehicle, const int32_t target[3], const int32_t* targetMotion = nullptr);
    int32_t steering(const DrivingConstraints& constraints) const;
    int32_t timeToTurn(const DrivingConstraints& constraints) const;
    int32_t speedForSteering(const DrivingConstraints& constraints) const;
    int32_t speedForDeceleration(const DrivingConstraints& constraints) const;
    int32_t gas(const DrivingConstraints& constraints, int32_t desiredSpeed, int32_t maxGas = 4096) const;
};

class DrivingRecovery {
public:
    // ComputeDrivingIK's reverse/handbrake recovery. Returns true while it owns the vehicle controls.
    bool update(const Vehicle& vehicle, const DrivingProps& props, const DrivingData& data,
                bool stuck, Vehicle::Controls& controls);
    bool active() const { return active_; }
private:
    uint16_t remaining_ = 0, switchAt_ = 0, pattern_ = 0, resetAttempts_ = 0;
    uint8_t attempts_ = 0;
    bool active_ = false, turnPositive_ = false;
};

class AIDriving {
public:
    static bool constraints(const Vehicle& vehicle, const DrivingProps& props, DrivingConstraints& out);
    static Vehicle::Controls towards(const Vehicle& vehicle, const DrivingProps& props,
                                    const int32_t target[3], int32_t speedLimit);
};
