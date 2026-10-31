// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#pragma once
#include "vehicle.h"
#include <cstdint>
#include <string>

// User-local acceleration, reverse recovery and police target-plan tables, extracted by setup.
class DrivingProps {
public:
    bool load(const std::string& path);
    bool ok() const { return loaded_; }
    bool accelDecel(int vehicle, int gear, int32_t& acceleration, int32_t& deceleration) const;
    // cWheeledVehicle::GetMaxAccelerationFromGear: the table's acceleration for the gear (Q12, no fallback)
    int32_t maxAcceleration(int vehicle, int gear) const;
    bool reverseTimes(int attempt, uint16_t& first, uint16_t& second, uint16_t& pattern) const;
    int32_t reverseSteer(uint16_t pattern, bool secondPhase) const;
    bool policeData() const { return policeLoaded_; }
    bool pursuitOffset(int index, int32_t out[2]) const;
    int pursuitNext(int index) const;
    int pursuitRemap(int index) const;
    int speedValue(int index) const;
private:
    int8_t values_[103][5][2]{};
    uint16_t reverseFirst_[4]{}, reverseSecond_[4]{};
    int32_t reverseSteering_[10][2]{};
    bool loaded_ = false;
    int32_t pursuitOffsets_[15][2]{}, pursuitNext_[30]{}, speedValues_[5]{};
    int64_t pursuitRemap_[8]{};
    bool policeLoaded_ = false;
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
                                    const int32_t target[3], int32_t speedLimit,
                                    const int32_t* targetMotion = nullptr);
};

// cDirectVehicleControl: a script's order that works the vehicle's controls itself for a while (Vehicle::SetStop(false)
// on thread 0, Vehicle::SetTempAction on thread 2, above whatever the vehicle was doing). It keeps where the vehicle
// was (+0x18) and its heading (+0x28) when told. Ported: the actions kena01 gives.
class DirectVehicleControl {
public:
    enum Action {
        HandbrakeTurn = 4,   // full throttle on the handbrake, wheels hard left, until it has turned (+0x2A) a quarter
        Stop = 11,           // brake to a standstill where it was told, holding the heading (Vehicle::SetStop)
        BrakeStop = 12,      // brake at one and a half, nudging the heading straight every 8th frame
    };
    DirectVehicleControl(Action action, uint32_t frames, const Vehicle& vehicle);
    // Process: this frame's yoke (into a fresh one); true when the order is done (that frame still drives)
    bool process(const Vehicle& vehicle, uint32_t frameCounter, Vehicle::Controls& yoke);
private:
    int32_t at_[2];        // +0x18
    Action action_;        // +0x20
    uint32_t timer_;       // +0x24: frames left
    uint16_t heading_;     // +0x28
    int16_t turn_;         // +0x2A: 0x4000 for the turns, else 0
    uint8_t state_ = 0;    // +0x2D: the turn's counter-steer frames
};
