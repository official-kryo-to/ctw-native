// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "driving.h"
#include "os/datafile.h"
#include "random.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace {
int32_t divideQ12(int32_t numerator, int32_t denominator) {
    return denominator ? (int32_t)(((int64_t)numerator * 4294967296LL / denominator) >> 20) : 0;
}
int32_t length3(const int32_t v[3]) {
    return (int32_t)std::sqrt((double)((int64_t)v[0]*v[0] + (int64_t)v[1]*v[1] + (int64_t)v[2]*v[2]));
}
// Angle result at 0x6dfc94 (ATan uses the original float conversion and truncation).
int16_t targetAngle(int32_t dp, bool negative) {
    dp = std::clamp(dp, -4096, 4096);
    int angle = 0x4000;
    if (dp) {
        const int32_t ratio = divideQ12((int32_t)std::sqrt((double)(0x1000000LL - (int64_t)dp*dp)), std::abs(dp));
        const int a = std::clamp((int)(atanf((float)ratio * 0.000244140625f) * 10430.f), -32768, 32767);
        angle = dp < 0 ? -32768 - a : a;
    }
    return (int16_t)(negative ? -angle : angle);
}
}

bool DrivingProps::load(const std::string& path) {
    loaded_ = false;
    std::vector<uint8_t> data;
    const size_t size=sizeof values_+sizeof reverseFirst_+sizeof reverseSecond_+sizeof reverseSteering_;
    if (!Data_ReadAll(path, data) || data.size() != 8 + size || std::memcmp(data.data(), "CTWDRIV2", 8)) return false;
    std::memcpy(values_, data.data() + 8, sizeof values_);
    size_t offset=8+sizeof values_;
    std::memcpy(reverseFirst_,data.data()+offset,sizeof reverseFirst_);offset+=sizeof reverseFirst_;
    std::memcpy(reverseSecond_,data.data()+offset,sizeof reverseSecond_);offset+=sizeof reverseSecond_;
    std::memcpy(reverseSteering_,data.data()+offset,sizeof reverseSteering_);
    loaded_ = true;
    return true;
}

bool DrivingProps::reverseTimes(int attempt,uint16_t& first,uint16_t& second,uint16_t& pattern) const {
    if (!loaded_ || attempt<0) return false;
    // cGotoTargetInVehicleStraightLine::SetReverseTime1
    if (attempt>1) {
        second=10000;
        first=(uint16_t)(Rand16Critical(100)+10000);
        pattern=(uint16_t)(Rand16Critical(7)+2);
    } else {
        pattern=(uint16_t)attempt;
        first=reverseFirst_[attempt];second=reverseSecond_[attempt];
    }
    return true;
}

int32_t DrivingProps::reverseSteer(uint16_t pattern,bool secondPhase) const { // cGotoTargetInVehicleStraightLine::ReverseSteer
    return loaded_ && pattern<10 ? reverseSteering_[pattern][secondPhase ? 1 : 0] : 0;
}

bool DrivingProps::accelDecel(int vehicle, int gear, int32_t& acceleration, int32_t& deceleration) const {
    if (!loaded_ || vehicle < 0 || vehicle >= 103 || gear < 1 || gear > 5) return false;
    int a = values_[vehicle][gear-1][0], d = values_[vehicle][gear-1][1];
    if (!a) {
        // ComputeDrivingIK tries the preceding pair once, then uses its explicit 1/1 default.
        if (gear > 1) { a = values_[vehicle][gear-2][0]; d = values_[vehicle][gear-2][1]; }
        else if (vehicle > 0) { a = values_[vehicle-1][4][0]; d = values_[vehicle-1][4][1]; }
        if (!a) { acceleration = deceleration = 4096; return true; }
    }
    acceleration = a * 4096;
    deceleration = -d * 4096;
    return acceleration > 0 && deceleration > 0;
}

// Fixed-position target branch of the native driving-data constructor, with ComputeDrivingIK's planar flag.
DrivingData::DrivingData(const Vehicle& vehicle, const int32_t target[3], const int32_t* targetMotion) {
    if (targetMotion) std::copy(targetMotion,targetMotion+3,targetVelocity);
    int32_t delta[3], direction[3];
    for (int i=0;i<3;++i) delta[i] = target[i] - vehicle.pos[i];
    delta[2]=0;
    distance = length3(delta);
    for (int i=0;i<3;++i) direction[i] = distance ? divideQ12(delta[i], distance) : vehicle.fwd[i];
    int64_t f=0, r=0, speedDot=0;
    const int32_t speed=length3(vehicle.vel);
    for (int i=0;i<3;++i) {
        f += (int64_t)vehicle.fwd[i]*direction[i];
        r += (int64_t)vehicle.right[i]*direction[i];
        const int32_t motion=speed ? divideQ12(vehicle.vel[i],speed) : vehicle.fwd[i];
        speedDot += (int64_t)vehicle.fwd[i]*motion;
    }
    angle = targetAngle((int32_t)(f>>12), r < 0);
    side=std::clamp((int32_t)(r>>12),-4096,4096);
    forwardSpeed = (int32_t)(((speedDot>>12)*speed)>>12);
}

bool DrivingRecovery::update(const Vehicle& vehicle,const DrivingProps& props,const DrivingData& data,
                             bool stuck,Vehicle::Controls& controls) {
    if (stuck && !active_) {
        if (!props.reverseTimes(attempts_,remaining_,switchAt_,pattern_)) return false;
        turnPositive_=data.side<0;
        active_=true;
        resetAttempts_=800;
        ++attempts_;
    }
    if (remaining_) --remaining_;
    if (active_) {
        if (remaining_>=61) {
            const int32_t steering=props.reverseSteer(pattern_,remaining_<=switchAt_);
            controls.steer=turnPositive_ ? steering : -steering;
            controls.throttle=(int64_t)vehicle.vel[0]*vehicle.vel[0]+(int64_t)vehicle.vel[1]*vehicle.vel[1]+(int64_t)vehicle.vel[2]*vehicle.vel[2] > 3011704640LL ? 0 : -4096;
            controls.handbrake=false;
            return true;
        }
        if (remaining_) {
            controls.throttle=4096;controls.steer=0;controls.handbrake=true;
            return true;
        }
        active_=false;
    } else if (resetAttempts_ && !--resetAttempts_) attempts_=0;
    return false;
}

int32_t DrivingData::steering(const DrivingConstraints& c) const { // sDrivingData::GetSteering
    const int16_t adjusted = angle < -50 ? (int16_t)(angle+50) : angle > 50 ? (int16_t)(angle-50) : 0;
    return std::clamp(divideQ12(adjusted,c.steeringLock), -4096,4096);
}

int32_t DrivingData::timeToTurn(const DrivingConstraints& c) const { // sDrivingData::GetTimeToTurnThroughAngle
    return c.turnRate ? divideQ12(std::abs((int)angle),c.turnRate) : 0;
}

int32_t DrivingData::speedForSteering(const DrivingConstraints& c) const { // sDrivingData::GetSpeedAllowingForSteering
    if (!c.turnRate) return 0x80000;
    const int32_t halfTime = (int32_t)(((int64_t)std::abs((int)angle) * 4294967296LL / c.turnRate) >> 21);
    return halfTime < 41 ? 0x80000 : std::clamp(divideQ12(distance,halfTime),0x8000,0x80000);
}

int32_t DrivingData::speedForDeceleration(const DrivingConstraints& c) const { // sDrivingData::GetSpeedAllowingForDeceleration
    return (int32_t)std::sqrt((double)(uint64_t)((int64_t)length3(targetVelocity)*4096 + 2LL*distance*c.deceleration));
}

int32_t DrivingData::gas(const DrivingConstraints& c, int32_t desiredSpeed, int32_t maxGas) const { // sDrivingData::GetGasToAcquireSpeed
    const int32_t change = (int32_t)(((uint32_t)desiredSpeed-(uint32_t)forwardSpeed) * 30u);
    if (change < -204800) return std::max(divideQ12((int32_t)((uint32_t)change-204800u),c.deceleration),-4096);
    if (change <= 40960) return 0;
    return std::min(divideQ12(change-40960,c.acceleration),maxGas);
}

bool AIDriving::constraints(const Vehicle& v, const DrivingProps& props, DrivingConstraints& out) {
    // Angle limits from 0x8ef860, followed by the gear override at 0x8f44f4. The unused engine-force field is omitted.
    out.steeringLock = (int16_t)((int32_t)(((int64_t)v.steerLock_ * 0x28BE630000000LL) >> 32) >> 17);
    int32_t lock;
    if (v.gear_ < 0) out.turnRate = 10010;
    else {
        const int64_t t = ((int64_t)v.speed() * 0x399900000LL) & (int64_t)0xFFFFFFFF00000000ULL;
        lock = std::max((int32_t)(((int64_t)(v.steerLock_/2)*4096 - ((t/6590)>>20)*v.steerSpeedRed_)>>12),0);
        out.turnRate = (int16_t)(((uint64_t)(uint32_t)lock * 42722864) >> 24);
    }
    if (v.gear_ > 0) return props.accelDecel(v.infoId,v.gear_,out.acceleration,out.deceleration);
    // sVehicleDrivingInfo::PatchUp: reverse/neutral keep the vehicle-type branches instead of the gear table.
    switch (v.infoId) {
    case 4: out.acceleration=50761;out.deceleration=58425;break;
    case 7: out.acceleration=14848;out.deceleration=28520;break;
    case 8: out.acceleration=85348;out.deceleration=75415;break;
    case 9: out.acceleration=54587;out.deceleration=101470;break;
    case 12: out.acceleration=66232;out.deceleration=159858;break;
    case 13: out.acceleration=119713;out.deceleration=99983;break;
    case 14: out.acceleration=156004;out.deceleration=157667;break;
    case 19: out.acceleration=42258;out.deceleration=39346;break;
    case 21: out.acceleration=110895;out.deceleration=93200;break;
    case 30: out.acceleration=123629;out.deceleration=101814;break;
    case 34: out.acceleration=115396;out.deceleration=94306;break;
    case 40: out.acceleration=27648;out.deceleration=38547;break;
    default:out.acceleration=143892;out.deceleration=86413;break;
    }
    return true;
}

Vehicle::Controls AIDriving::towards(const Vehicle& vehicle, const DrivingProps& props,
                                   const int32_t target[3], int32_t speedLimit) {
    Vehicle::Controls result;
    DrivingConstraints c;
    if (!constraints(vehicle,props,c)) return result;
    const DrivingData data(vehicle,target);
    result.steer = data.steering(c);
    const int32_t desired = std::max(0,std::min({speedLimit,data.speedForSteering(c),data.speedForDeceleration(c)}));
    result.throttle = data.gas(c,desired);
    return result;
}
