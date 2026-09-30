// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "vehicle.h"
#include "lookups.h"
#include "gfx/assets.h"
#include "gfx/model.h"
#include "os/pak.h"
#include "world/collision.h"
#include "particles.h"
#include "sound.h"
#include "skidmarks.h"
#include "world/worldrenderer.h"
#include "cargens.h"
#include <glad/gl.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cstdio>

static int fastsin(int a) { return (int)(sinf((float)a * 9.587378e-05f) * 4096.f); }
static inline int32_t mulq(int64_t a, int64_t b) { return (int32_t)((a * b) >> 12); }
static inline int64_t isqrt(int64_t v) { return v <= 0 ? 0 : (int64_t)std::sqrt((double)v); }
static int16_t atan2q(int32_t y, int32_t x) { return (int16_t)std::clamp((int)(atan2f((float)y, (float)x) * 10430.f), -0x8000, 0x7FFF); }
static void normalise(int32_t v[3]) {
    double l2 = (double)v[0] * v[0] + (double)v[1] * v[1] + (double)v[2] * v[2];
    if (l2 <= 0) return;
    float inv = 1.f / std::sqrt((float)l2);
    for (int i = 0; i < 3; ++i) { float f = inv * (float)v[i]; v[i] = (int32_t)((f >= 0 ? 0.5f : -0.5f) + f * 4096.f); }
}

static const int32_t kGravityZ = -0x1D000;   // GravityVector (cPhysical static init)
static bool dbg() { static int d = getenv("CTW_CARDBG") ? 1 : 0; return d; }
#define DBG(...) do { if (dbg()) printf(__VA_ARGS__); } while (0)

std::string VehicleInfo::name() const {
    const char* p = (const char*)raw + 8;
    return std::string(p, strnlen(p, 0x20));
}

bool LoadVehicleInfos(std::vector<VehicleInfo>& out) {
    std::vector<uint8_t> dir, d;
    uint16_t gd[28];
    if (!Assets_Pak().read(0, dir) || dir.size() < sizeof gd) return false;
    memcpy(gd, dir.data(), sizeof gd);
    if (!Assets_Pak().read(gd[4], d) || d.size() < 4) return false;   // gGameDir[4] = 1994
    uint32_t n;
    memcpy(&n, d.data(), 4);
    if (n > (d.size() - 4) / 0x138) return false;
    out.resize(n);
    for (uint32_t i = 0; i < n; ++i) memcpy(out[i].raw, &d[4 + i * 0x138], 0x138);
    return true;
}

// speed (units/s, 20.12) -> the engine code's km/h-like value (x 3.6, / 51.4)
static int32_t speedUnits(int32_t speed) {
    int64_t t = ((int64_t)speed * 0x399900000LL) & (int64_t)0xFFFFFFFF00000000ULL;
    __int128 m = (__int128)t * 0x4F8EE0D84C734C1LL;
    int64_t u = (int64_t)(m >> 64);
    return (int32_t)(((u >> 7) - (u >> 63)) >> 20);
}

// ============================================================================================== setup
void Vehicle::init(const VehicleInfo& info, int id, const int32_t p[3], int16_t h, int pal) {
    infoId = id;
    bike_ = info.type() == 1;   // cPopulationManager: type 1 -> cBike
    palette = pal;
    // cWheeledVehicle::SetPropertiesFromVehicleInfo
    hx = (info.s16(0x28) * 0x2332 >> 12) >> 1;
    hy = (info.s16(0x2A) * 0x2332 >> 12) >> 1;
    hz = (info.s16(0x2C) * 2) >> 1;
    mass_ = info.s32(0x50);
    int16_t cgx = info.s16(0x2E), cgy = info.s16(0x30), cgz = info.s16(0x32);
    if (cgx == 0 && cgy == 0 && cgz == 0) { cgLocal_[0] = 0; cgLocal_[1] = 0x1000; cgLocal_[2] = (int32_t)(((int64_t)info.s16(0x2C) * 0x332) >> 12); }
    else { cgLocal_[0] = cgx; cgLocal_[1] = cgy; cgLocal_[2] = cgz; }
    collOffset_[0] = 0; collOffset_[1] = 0; collOffset_[2] = info.s16(0x2C);
    driveSplit_ = info.s16(0x34);
    brakeSplit_ = info.s16(0x36);
    wheelRadius_ = info.s16(0x38);
    drag_ = info.s16(0x3E);
    brakeForce_ = info.s32(0x54);
    auto deg = [](int32_t v) { return (int32_t)((int64_t)v * 0x3244 / 0xB4000); };   // degrees -> radians (x pi/180)
    steerLock_ = deg(info.s32(0x60));
    steerRate_ = deg(info.s32(0x64));
    steerSpeedRed_ = deg(info.s32(0x68));
    for (int i = 0; i < 2; ++i) {
        Tyre& t = tyre_[i];
        t = Tyre{};
        t.front = i == 0;
        t.axleY = info.s16(i == 0 ? 0x3A : 0x3C);
        t.k0 = info.s16(i == 0 ? 0x40 : 0x44);
        t.k4 = info.s16(i == 0 ? 0x42 : 0x46);
        t.gripMax = t.grip = info.s16(0x48);
        t.k18 = info.s32(i == 0 ? 0x58 : 0x5C);
        t.onGround = true;   // spawned resting on its wheels
    }
    gripBase_ = info.s16(0x48);
    for (int i = 0; i < 6; ++i) swayK_[i] = info.s32(0x74 + i * 4);
    for (int i = 0; i < 6; ++i) info_[i] = info.s32(0xC4 + i * 4);   // +0xC4 rear lamps, +0xD0 headlamps
    for (int i = 0; i < 4; ++i) {
        doorNode_[i] = (int8_t)info.raw[0x94 + i];
        doorOff_[i][0] = info.s32(0x98 + i * 8); doorOff_[i][1] = info.s32(0x9C + i * 8);
        for (int k = 0; k < 3; ++k) seatOff_[i][k] = info.s32(0xDC + i * 12 + k * 4);
    }
    doorNode_[4] = (int8_t)info.raw[0x110];
    infoFlags8e_ = (uint16_t)info.s16(0x8E);
    numSeats_ = info.s32(0x10C);
    for (Door& d : doors_) d = Door{};
    doorOpenBits_ = 0;
    for (int& u : seatUser) u = -1;
    maxRpm_ = info.s32(0x70);
    maxTorque_ = info.s32(0x6C);
    engK10_ = info.s16(0x4A);
    numGears_ = info.raw[0x4C];
    topSpeed_ = (int32_t)((uint32_t)(info.raw[0x4E] | info.raw[0x4F] << 8) << 12);
    // final drive ratio (the top gear reaches the top speed at max rpm)
    int32_t ratio = numGears_ >= 1 && numGears_ <= 6 ? TheGameplayTables().topRatio[numGears_ - 1] : 0;
    finalDrive_ = 0;
    if (ratio && topSpeed_) {
        int64_t a = ((int64_t)maxRpm_ << 32) / ratio;
        int32_t b = (int32_t)(a >> 20) / 30;
        int32_t w = (int32_t)(((int64_t)b * 0x324400000LL) >> 32);
        int64_t c = ((int64_t)wheelRadius_ * w * 0x100000) >> 32;
        int64_t d = (c * 0x399900000LL) & (int64_t)0xFFFFFFFF00000000ULL;
        __int128 m = (__int128)d * 0x4F8EE0D84C734C1LL;
        int64_t u = (int64_t)(m >> 64);
        int64_t e = (((u >> 7) - (u >> 63)) & 0xFFFFFFFF00000LL) << 12;
        finalDrive_ = (int32_t)((e / topSpeed_) >> 20);
    }
    int32_t fa = std::abs(tyre_[0].axleY - cgLocal_[1]), ra = std::abs(tyre_[1].axleY - cgLocal_[1]);
    weightFront_ = 0x1000 - (fa + ra ? (int32_t)((((int64_t)fa << 32) / (fa + ra)) >> 20) : 0);
    // cPhysical::LockPhysicalProperites: inverse mass and the inverse inertia of a box (cars: x2)
    invMass_ = mass_ ? (int32_t)((0x100000000000LL / mass_) >> 20) : 0;
    int64_t m12 = ((int64_t)mass_ << 32) / 0x6000 >> 20;
    int32_t xx = (int32_t)(((int64_t)(hx * 2) * (hx * 2)) >> 12), yy = (int32_t)(((int64_t)(hy * 2) * (hy * 2)) >> 12),
            zz = (int32_t)(((int64_t)(hz * 2) * (hz * 2)) >> 12);
    // (the x2 is only for class 0x2E - bikes - not cars)
    int sh = bike_ ? 1 : 0;
    int64_t ix = (int32_t)((((yy + zz) * m12) >> 12) << sh), iy = (int32_t)((((xx + zz) * m12) >> 12) << sh), iz = (int32_t)((((xx + yy) * m12) >> 12) << sh);
    invInertia_[0] = ix ? (int32_t)((0x100000000000LL / ix) >> 20) : 0;
    invInertia_[1] = iy ? (int32_t)((0x100000000000LL / iy) >> 20) : 0;
    invInertia_[2] = iz ? (int32_t)((0x100000000000LL / iz) >> 20) : 0;
    for (int i = 0; i < 3; ++i) if (invInertia_[i] > 0x2000) invInertia_[i] = 0x1FD7;
    // cPhysical::CalcSpheres (vehicles: spheres of radius hx along the length)
    int32_t r = std::max(hx, 0x800);
    sphereR_ = r;
    int32_t den = (int32_t)(((uint64_t)(uint32_t)r * 0x1A6700000ULL) >> 32);
    int32_t cnt = den ? (int32_t)(((int64_t)r * -0x599 + (int64_t)hy * 0x2000) / den) : 0;
    int n = (cnt + 0x1000) >> 12;
    n = std::clamp(n, 2, 6);
    sphereCount_ = n;
    sphereStep_[0] = 0; sphereStep_[1] = (int32_t)((r - hy) - (hy - r)) / (n - 1); sphereStep_[2] = 0;
    sphereFirst_[0] = collOffset_[0];
    sphereFirst_[1] = hy - r + collOffset_[1];
    sphereFirst_[2] = r - collOffset_[2] + collOffset_[2];
    placeUpright(p, h);
    physics_ = false;
    simple_ = false;
    gear_ = 1;
}

void Vehicle::placeUpright(const int32_t p[3], int16_t h) {
    int s = fastsin(h), c = fastsin(h + 0x4000);
    right[0] = (int16_t)c; right[1] = (int16_t)-s; right[2] = 0;
    fwd[0] = (int16_t)s; fwd[1] = (int16_t)c; fwd[2] = 0;
    up[0] = 0; up[1] = 0; up[2] = 0x1000;
    pos[0] = p[0]; pos[1] = p[1]; pos[2] = p[2];
    float hh = h * 9.587378e-05f;   // quaternion: rotation about z by -h (headings are clockwise)
    q_[0] = 0; q_[1] = 0; q_[2] = sinf(-hh * 0.5f); q_[3] = cosf(-hh * 0.5f);
    syncFromIntegrator();
}

void Vehicle::placeOnRail(int32_t x, int32_t y, int32_t z, const int32_t dir[2], int32_t sp) {
    int32_t p[3] = {x, y, z};
    placeUpright(p, atan2q(dir[0], dir[1]));   // heading = -ATan2(-dx, dy)
    vel[0] = mulq(sp, dir[0]); vel[1] = mulq(sp, dir[1]); vel[2] = 0;
    angVel[0] = angVel[1] = angVel[2] = 0;
}

int16_t Vehicle::heading() const { return (int16_t)-atan2q(-fwd[0], fwd[1]); }
int32_t Vehicle::speed() const { return (int32_t)isqrt((int64_t)vel[0] * vel[0] + (int64_t)vel[1] * vel[1] + (int64_t)vel[2] * vel[2]); }

// ============================================================================================== cPhysical
void Vehicle::syncFromIntegrator() {
    // Quat::ToMatrix + cEntity::SetMatrixFromIntegrator: rows of the entity matrix = local axes in world
    float x = q_[0], y = q_[1], z = q_[2], w = q_[3];
    float R[3][3] = {{1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)},
                     {2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)},
                     {2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)}};
    for (int k = 0; k < 3; ++k) {   // columns of R = local axes
        right[k] = (int16_t)lroundf(R[k][0] * 4096.f);
        fwd[k] = (int16_t)lroundf(R[k][1] * 4096.f);
        up[k] = (int16_t)lroundf(R[k][2] * 4096.f);
        rot_[0][k] = R[k][0]; rot_[1][k] = R[k][1]; rot_[2][k] = R[k][2];
    }
    // invIWorld = R diag R^T
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            float s = 0;
            for (int k = 0; k < 3; ++k) s += R[i][k] * (invInertia_[k] / 4096.f) * R[j][k];
            invIWorld_[i][j] = s;
        }
    if (!physics_) {   // cgWorld follows the entity
        for (int k = 0; k < 3; ++k) cgWorld_[k] = pos[k] + mulq(cgLocal_[0], right[k]) + mulq(cgLocal_[1], fwd[k]) + mulq(cgLocal_[2], up[k]);
    } else {
        for (int k = 0; k < 3; ++k) pos[k] = cgWorld_[k] - (mulq(cgLocal_[0], right[k]) + mulq(cgLocal_[1], fwd[k]) + mulq(cgLocal_[2], up[k]));
    }
}

void Vehicle::worldCG(int32_t o[3]) const {
    if (physics_) { o[0] = cgWorld_[0]; o[1] = cgWorld_[1]; o[2] = cgWorld_[2]; return; }
    for (int k = 0; k < 3; ++k) o[k] = pos[k] + mulq(cgLocal_[0], right[k]) + mulq(cgLocal_[1], fwd[k]) + mulq(cgLocal_[2], up[k]);
}

void Vehicle::velocityAt(const int32_t p[3], int32_t o[3]) const {   // cPhysical::GetVelocityAtWorldPos
    int32_t cg[3];
    worldCG(cg);
    int32_t r[3] = {p[0] - cg[0], p[1] - cg[1], p[2] - cg[2]};
    o[0] = vel[0] + (int32_t)(((int64_t)angVel[1] * r[2] - (int64_t)angVel[2] * r[1]) >> 12);
    o[1] = vel[1] + (int32_t)(((int64_t)angVel[2] * r[0] - (int64_t)angVel[0] * r[2]) >> 12);
    o[2] = vel[2] + (int32_t)(((int64_t)angVel[0] * r[1] - (int64_t)angVel[1] * r[0]) >> 12);
}

void Vehicle::setToPhysics(bool on) {   // cPhysical::SetToPhysics
    if (on && !physics_) {
        physics_ = true;
        for (int k = 0; k < 3; ++k) cgWorld_[k] = pos[k] + mulq(cgLocal_[0], right[k]) + mulq(cgLocal_[1], fwd[k]) + mulq(cgLocal_[2], up[k]);
    } else if (!on && physics_) {
        for (int k = 0; k < 3; ++k) { vel[k] = angVel[k] = force_[k] = torque_[k] = 0; }
        physics_ = false;
    }
}

void Vehicle::setToSimple(bool on) { simple_ = on; }   // cVehicle::SetToSimplePhysics

bool Vehicle::velocityBelow(int32_t tol) const {   // cPhysical::IsVelocityBelowTolerance
    int64_t t2 = ((int64_t)tol * tol) >> 12;
    int64_t s2 = (int64_t)vel[0] * vel[0] + (int64_t)vel[1] * vel[1] + (int64_t)vel[2] * vel[2];
    int32_t ry = hz - cgLocal_[2], rx = hx - cgLocal_[0], rz = hy - cgLocal_[1];
    int32_t v1 = vel[1] + (int32_t)(((int64_t)angVel[0] * ry - (int64_t)angVel[2] * rx) >> 12);
    int32_t v0 = vel[0] + (int32_t)(((int64_t)angVel[2] * rz - (int64_t)angVel[1] * ry) >> 12);
    int32_t v2 = vel[2] + (int32_t)(((int64_t)angVel[1] * rx - (int64_t)angVel[0] * rz) >> 12);
    return (((int64_t)v1 * v1 + (int64_t)v0 * v0 + (int64_t)v2 * v2) >> 12) <= t2 && s2 <= t2 * 0x1000;
}

void Vehicle::applyWorldForce(const int32_t pIn[3], const int32_t fIn[3], int type) {
    // cWheeledVehicle::ApplyWorldForce: in simple physics, tyre/impact forces are flat and act at the CG height
    int32_t p[3] = {pIn[0], pIn[1], pIn[2]}, f[3] = {fIn[0], fIn[1], fIn[2]};
    if (simple_) {
        if (type == 2 || type == 4 || type == 8) {
            f[2] = 0;
            int32_t cg[3];
            worldCG(cg);
            p[2] = cg[2];
        } else setToSimple(false);
    }
    // cVehicle::ApplyWorldForce -> cPhysical::ApplyWorldForce
    setToPhysics(true);
    if (!mass_) return;
    int32_t cg[3];
    worldCG(cg);
    int32_t r[3] = {p[0] - cg[0], p[1] - cg[1], p[2] - cg[2]};
    if (r[0] || r[1] || r[2]) {
        // (the game keeps torque and angular velocity negated - F x r and v + r x w; this port uses the usual
        // r x F and v + w x r throughout)
        torque_[0] += (int32_t)(((int64_t)r[1] * f[2] - (int64_t)r[2] * f[1]) >> 12);
        torque_[1] += (int32_t)(((int64_t)r[2] * f[0] - (int64_t)r[0] * f[2]) >> 12);
        torque_[2] += (int32_t)(((int64_t)r[0] * f[1] - (int64_t)r[1] * f[0]) >> 12);
    }
    for (int k = 0; k < 3; ++k) force_[k] += f[k];
}

void Vehicle::recalcKinematics() {   // cPhysical::RecalcKinematics
    if (!mass_) return;
    if (torque_[0] || torque_[1] || torque_[2]) {
        for (int i = 0; i < 3; ++i) {
            float a = invIWorld_[i][0] * torque_[0] + invIWorld_[i][1] * torque_[1] + invIWorld_[i][2] * torque_[2];
            angVel[i] += (int32_t)(((int64_t)lroundf(a) * 0x11) >> 9);
        }
        torque_[0] = torque_[1] = torque_[2] = 0;
    }
    if (force_[0] || force_[1] || force_[2]) {
        for (int i = 0; i < 3; ++i) {
            int64_t a = (int64_t)invMass_ * force_[i];
            int32_t ai = (int32_t)(a >> 12);
            accel_[i] = ai;
            vel[i] += (int32_t)((((a * 0x100000) >> 32) + (int64_t)ai * 0x10) >> 9);
            force_[i] = 0;
        }
    }
}

void Vehicle::integrateStep(int32_t dt) {   // cPhysical::Integrate
    float h = (float)((dt * 0x11) >> 9) / 4096.f;
    float wx = angVel[0] / 4096.f, wy = angVel[1] / 4096.f, wz = angVel[2] / 4096.f;
    float x = q_[0], y = q_[1], z = q_[2], w = q_[3];
    // q += 0.5 * (0, omega) * q * dt
    q_[0] += 0.5f * h * (wx * w + wy * z - wz * y);
    q_[1] += 0.5f * h * (wy * w + wz * x - wx * z);
    q_[2] += 0.5f * h * (wz * w + wx * y - wy * x);
    q_[3] += 0.5f * h * (-wx * x - wy * y - wz * z);
    int32_t k = (int32_t)(((int64_t)dt * 0x88) >> 12);
    for (int i = 0; i < 3; ++i) cgWorld_[i] += mulq(k, vel[i]);
    if (simple_) { q_[0] = 0; q_[1] = 0; }
    float l = std::sqrt(q_[0] * q_[0] + q_[1] * q_[1] + q_[2] * q_[2] + q_[3] * q_[3]);
    if (l > 0) for (float& v : q_) v /= l;
    syncFromIntegrator();
}

void Vehicle::spheres(int32_t (*out)[4], int& n) const {   // cPhysical::CalcSpheres(SSphere*)
    n = sphereCount_;
    int32_t p[3], st[3];
    for (int k = 0; k < 3; ++k) {
        p[k] = pos[k] + mulq(sphereFirst_[0], right[k]) + mulq(sphereFirst_[1], fwd[k]) + mulq(sphereFirst_[2], up[k]);
        st[k] = mulq(sphereStep_[0], right[k]) + mulq(sphereStep_[1], fwd[k]) + mulq(sphereStep_[2], up[k]);
    }
    for (int i = 0; i < n; ++i) {
        out[i][0] = p[0]; out[i][1] = p[1]; out[i][2] = p[2]; out[i][3] = sphereR_;
        for (int k = 0; k < 3; ++k) p[k] += st[k];
    }
}

void Vehicle::bboxVerts(int32_t o[8][3]) const {   // cPhysical::GetWorldBBoxVertices
    int32_t c[3];
    for (int k = 0; k < 3; ++k) c[k] = pos[k] + mulq(collOffset_[0], right[k]) + mulq(collOffset_[1], fwd[k]) + mulq(collOffset_[2], up[k]);
    static const int sg[8][3] = {{1, 1, 1}, {-1, 1, 1}, {-1, -1, 1}, {1, -1, 1}, {-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1}};
    for (int i = 0; i < 8; ++i)
        for (int k = 0; k < 3; ++k)
            o[i][k] = c[k] + sg[i][0] * mulq(hx, right[k]) + sg[i][1] * mulq(hy, fwd[k]) + sg[i][2] * mulq(hz, up[k]);
}

int32_t Vehicle::impactTerm(const int32_t n[3], const int32_t r[3]) const {   // cPhysical::CalcImpactTerm
    if (!mass_) return 0;
    float rn[3] = {((int64_t)r[1] * n[2] - (int64_t)r[2] * n[1]) / 4096.f, ((int64_t)n[0] * r[2] - (int64_t)r[0] * n[2]) / 4096.f,
                   ((int64_t)r[0] * n[1] - (int64_t)n[0] * r[1]) / 4096.f};
    float a[3];
    for (int i = 0; i < 3; ++i) a[i] = invIWorld_[i][0] * rn[0] + invIWorld_[i][1] * rn[1] + invIWorld_[i][2] * rn[2];
    float c[3] = {(a[1] * r[2] - a[2] * r[1]) / 4096.f, (a[2] * r[0] - a[0] * r[2]) / 4096.f, (a[0] * r[1] - a[1] * r[0]) / 4096.f};
    return (int32_t)((c[0] * n[0] + c[1] * n[1] + c[2] * n[2]) / 4096.f);
}

// ============================================================================================== driving
void Vehicle::act(const Controls& yIn, bool playerDriving, Collision* col) {
    playerDriving_ = playerDriving;
    Controls y = dead_ ? Controls{} : yIn;   // a wreck does not drive
    b64_ &= 0xFFF9;   // this frame's brake / reverse
    if (physicsActive()) { railBraking = false; indicators = 0; }   // (a simulated car is not on rails any more)
    // cWheeledVehicle::Act (the driver's controls)
    b62_ = 0;
    steerInputAbs_ = std::abs(y.steer);
    throttleAbs_ = std::abs(y.throttle);
    if (y.steer < 0) b62_ = 4;
    else if (y.steer > 0) b62_ = 8;
    if (y.handbrake) {
        ++handbrakeTimerHb_;
        if (std::abs(y.steer) < 0x801) b62_ |= 0x40;
        else { b62_ |= 0x10; traction_ = 0x1000; }
    } else handbrakeTimerHb_ = 0;
    int32_t th = y.throttle;
    if (gear_ < 1) {
        if (th < 1) {
            if (th < 0 && !(b64_ & 1)) { b64_ |= 4; b62_ |= 1; }   // reverse gear: "back" drives
        } else b62_ |= 2;
    } else if (th < 1) {
        if (th < 0) {
            b64_ |= 2; b62_ |= 2;
            if (bike_ && playerDriving) {   // a bike's rider braking: 0x80, and less traction when steering too
                b62_ |= 0x80;
                if (std::abs(y.steer) > 0x800) traction_ = 0xB33;
            }
        }
    } else b62_ |= 1;
    if (!tyre_[0].onGround && !tyre_[1].onGround) b62_ &= 0xFE;   // no gas in the air
    // cWheeledVehicle::Process: the low-gear steering grip (0xB48) and the reverse-handbrake timer (0xB4E)
    if ((int8_t)gear_ < 2 && !(b62_ & 0x90) && (b62_ & 0xC)) lowGearGrip_ = 0;
    else lowGearGrip_ = std::min(lowGearGrip_ + 0x200, 0x1000);
    if (gear_ == -1 && (b62_ & 0xD) == 1) handbrakeTimer_ = (int16_t)std::min(handbrakeTimer_ + 10, 300);
    else handbrakeTimer_ = (int16_t)std::max(handbrakeTimer_ - 6, 0);
    if (!physics_ && b62_ == 0) {   // parked and nobody touches it: nothing to do
        for (Tyre& t : tyre_) { t.skid = t.spinning = t.slide = false; }
    } else updatePhysics(col);
    if (bike_) bikeAct(y);
}

// ============================================================================================== bikes
bool Vehicle::bikeLeaning() const {   // cBike::IsLeaning
    return speed() < 0x4000 && !bikeThrottle_ && seatUser[0] != -1 && bikeMounted;
}

void Vehicle::bikeAct(const Controls& y) {   // cBike::Act (after cWheeledVehicle::Act)
    int32_t sp = speed();
    int32_t sp20 = (int32_t)((((int64_t)sp << 32) / 0x14000) >> 20);   // speed / 20
    // the foot-down lean while stopped
    if (bikeLeaning()) { if (bikeKick_ < 0x800) bikeKick_ += 0xF5; }
    else if (bikeKick_ > 0) bikeKick_ -= 0xF5;
    bikeThrottle_ = y.throttle != 0;
    // lean into the turn: steer x 1.3 x min(speed / 20, 1) on the ground, eased at 0.2 a frame
    int64_t want = collided_ ? ((int64_t)y.steer * 0x14CC00000LL) >> 32 : 0;
    int64_t f = sp20 < 0x1001 ? ((int64_t)sp20 << 32) >> 12 : 0x100000000LL;
    bikeLean_ += (int32_t)(((int64_t)(int32_t)((f * want - ((int64_t)(uint32_t)bikeLean_ << 32)) >> 32) * 0x333) >> 12);
    if (gear_ == -1) {
        if (tyre_[0].spin < -0x18000) tyre_[0].spin = -0x18000;
        if (tyre_[1].spin < -0x18000) tyre_[1].spin = -0x18000;
    }
    // wheelies / stoppies: a pitch spring pushed by the forward acceleration
    int32_t dv[3] = {vel[0] - bikePrevVel_[0], vel[1] - bikePrevVel_[1], vel[2] - bikePrevVel_[2]};
    for (int k = 0; k < 3; ++k) bikePrevVel_[k] = vel[k];
    bikePitchPrev_ = bikePitch_;
    int32_t acc = (int32_t)(((int64_t)fwd[0] * dv[0] + (int64_t)fwd[1] * dv[1] + (int64_t)fwd[2] * dv[2]) >> 12);
    auto toAngle = [](int32_t rad) { return (int)((int32_t)(((int64_t)rad * 0x28BE630) >> 8) >> 16); };
    auto push = [](int64_t t) { int64_t l = (t << 37) - (t << 33); return (int32_t)(((l >> 28) + (l >> 32)) >> 9); };
    int32_t accTerm = (int32_t)(((int64_t)-acc * 0x1F00000) >> 32);
    bool straight = std::abs(bikeLean_) < 0x801 && std::abs(y.steer) <= 0x800;
    if (bikePitch_ < 1) {
        int a = toAngle(bikePitch_ + 0x1000);   // (+0x1666 / +0x99A while leaning back / forward: no such input here)
        int32_t c = fastsin(a + 0x4000), sn = fastsin(a);
        int64_t t = straight && collided_ ? (((int64_t)sn * 0x37) >> 12) + (((int64_t)accTerm * c) >> 12) : 0x7A;
        bikePitchV_ += push(t);
        int32_t p = bikePitchV_ + bikePitch_;
        bikePitch_ = p;
        if (p >= 1) {
            if (bikePitchV_ >= 1) { bikePitchV_ = 0; bikePitch_ = 0; p = 0; }
            else p = bikePitch_;
        }
        if (p < -0xE66) { bikePitch_ = -0xE66; if (bikePitchV_ < 0) bikePitchV_ = 0; }
        else if (p > 0) {
            if ((uint32_t)p < 0x11E) { bikePitch_ = 0; bikePitchV_ = 0; }
            else bikePitch_ = p - 0x11E;
        }
    } else {
        int a = toAngle(bikePitch_ - 0xB64);
        int32_t c = fastsin(a + 0x4000), sn = fastsin(a);
        int64_t t = straight ? ((((int64_t)sn * 0x3700000) >> 21) >> 12) + (((int64_t)accTerm * c) >> 12) : -0x7A;
        bikePitchV_ += push(t);
        int32_t p = bikePitch_ + bikePitchV_ - 0xCC;
        bikePitch_ = p;
        if (p < 1) { bikePitch_ = 0; if (bikePitchV_ < 0) bikePitchV_ = 0; }
        else if ((uint32_t)p > 0xB64) { bikePitch_ = 0xB64; if (bikePitchV_ > 0) bikePitchV_ = 0; }
    }
    bool abandoned = seatUser[0] == -1;
    // no throttle and slow: the rider stops it (an empty bike still standing is left alone)
    if (!bikeThrottle_ && sp < 0x4199) {
        bool skip = false;
        if (abandoned) {
            if (up[2] > 0x146) skip = true;
            else setToPhysics(false);
        }
        if (!skip) for (int k = 0; k < 3; ++k) { vel[k] = 0; angVel[k] = 0; }
    }
    if (!abandoned) {
        bikeAbandoned_ = false;
        // keep it upright: roll back against the tilt (slow, or on the ground in full physics)
        if ((sp < 0x2000 || (!simple_ && collided_)) && up[2] < 0xFD7) {
            setToPhysics(true);
            if (std::abs(fwd[2]) <= 0x198) {
                int32_t k3 = right[2] * 3;
                for (int k = 0; k < 3; ++k) angVel[k] += (int32_t)(((int64_t)fwd[k] * k3) >> 12);   // (the game's -=: its spin is negated)
            }
        }
    } else {
        // bailed out of at speed: once it slows down it falls over to the side it leans to
        if (!bikeAbandoned_) { bikeAbandoned_ = true; bikeFastAbandoned_ = sp > 0x4000; }
        int64_t sp2 = (int64_t)vel[0] * vel[0] + (int64_t)vel[1] * vel[1] + (int64_t)vel[2] * vel[2];
        if (physics_ && collided_ && bikeFastAbandoned_ && (sp2 >> 12) <= 0x3FFFFFF && std::abs(fwd[2]) <= 0x7FF) {
            simple_ = false;
            int32_t t = right[2] < 0 ? 0xB33 : -0xB33;
            int32_t k2 = up[2] < 0x334 ? t : (t * 0x2249) >> 12;
            for (int k = 0; k < 3; ++k) angVel[k] += (int32_t)(((int64_t)fwd[k] * k2) >> 12);   // (negated spin)
        }
    }
}

void Vehicle::riderRenderPos(int32_t upper[3], int32_t legs[3]) const {   // cBike::GetPedRenderPos (model space x 1.3)
    float M[16];
    modelMatrix(M);
    int32_t sy = seatOff_[0][1];
    const float u[3] = {0, sy / 4096.f, 0x1333 / 4096.f}, l[3] = {0, (sy + 0xE00) / 4096.f, 0xC00 / 4096.f};
    for (int k = 0; k < 3; ++k) {
        upper[k] = (int32_t)((M[k] * u[0] + M[4 + k] * u[1] + M[8 + k] * u[2] + M[12 + k]) * 4096.f);
        legs[k] = (int32_t)((M[k] * l[0] + M[4 + k] * l[1] + M[8 + k] * l[2] + M[12 + k]) * 4096.f);
    }
}

void Vehicle::updatePhysics(Collision* col) {   // cWheeledVehicle::UpdatePhysics (on land)
    updateSteering();
    updateEngine();
    updateTyres(col);
    calcForces();
    if (physics_ && !(b62_ & 3) && simple_) {
        bool resting = tyre_[0].onGround && tyre_[1].onGround && !(b62_ & 3);
        if (!velocityBelow(0x1666) || std::abs(tyre_[1].spin) > 0x3000 || std::abs(tyre_[0].spin) > 0x3000 || !resting) {
            if (velocityBelow(0x3000)) {
                for (int k = 0; k < 3; ++k) { vel[k] = mulq(vel[k], 0xF33); angVel[k] = mulq(angVel[k], 0xF33); }
                tyre_[0].spin = mulq(tyre_[0].spin, 0xF33);
                tyre_[1].spin = mulq(tyre_[1].spin, 0xF33);
            }
        } else setToPhysics(false);
    }
    handleSettling(col);
}

void Vehicle::updateSteering() {   // cWheeledVehicle::UpdateSteering
    int32_t rate = tyre_[1].spin == 0 ? steerLock_ : mulq(steerRate_, steerInputAbs_ ? steerInputAbs_ : 0x1000);
    int32_t a;
    if (b62_ & 4) a = std::min(steerAngle_, 0) - rate;
    else if (b62_ & 8) a = std::max(steerAngle_, 0) + rate;
    else a = 0;
    int32_t lim;
    if (gear_ < 0) lim = 0xF5B;
    else {
        int32_t lock = steerLock_ < 0 ? steerLock_ + 1 : steerLock_;
        int64_t t = ((int64_t)speed() * 0x399900000LL) & (int64_t)0xFFFFFFFF00000000ULL;
        int64_t red = (t / 0x19BE) >> 20;
        lim = (int32_t)(((int64_t)(lock >> 1) * 0x1000 - red * steerSpeedRed_) >> 12);
        lim = std::max(lim, 0);
    }
    if (tyre_[0].burst) lim >>= 1;
    steerAngle_ = std::clamp(a, -lim, lim);
}

int32_t Vehicle::finalDriveRatio() const {   // CEngine::CalculateFinalDriveRatio (no overdrive)
    int32_t f = finalDrive_;
    if (tyre_[0].burst || tyre_[1].burst) f <<= 1;
    if (b64_ & 0x100) f = mulq(f, 0xE66);
    return f;
}

static int32_t torqueCurve(int64_t r) {   // CEngine: ((1.8 r - 4.5) r + 2.2) r + 0.5, r = rpm / max rpm
    int64_t r20 = r * 0x100000;
    int64_t a = (r * 0x1CCD00000LL - 0x480000000000LL) >> 32;
    int64_t b = (a * r20 + 0x233300000000LL) >> 32;
    return (int32_t)((b * r20 + 0x80000000000LL) >> 32);
}

void Vehicle::updateEngine() {   // CEngine::Update
    driveForce_ = 0;
    if (shiftDelay_) { --shiftDelay_; return; }
    int64_t gr = gear_ + 1 >= 0 && gear_ + 1 < 8 ? TheGameplayTables().gearRatio[gear_ + 1] : 0;
    int32_t fd = finalDriveRatio();
    int64_t ratio = (gr * fd) >> 32;
    int64_t spin = mulq(tyre_[1].spin, 0x98C9) + mulq(tyre_[0].spin, 0x98C9);
    int32_t rpm = (int32_t)((ratio * spin) >> 13);
    rpm_ = rpm;
    if (maxRpm_ < rpm) rpm_ = maxRpm_;
    else if (rpm < 0x3E8000) rpm_ = 0x3E8000;
    int64_t r = maxRpm_ ? ((((int64_t)(uint32_t)rpm_ << 32) / maxRpm_) << 12) >> 32 : 0;
    if (b62_ & 1) {   // gas
        int64_t t = (int64_t)maxTorque_ * torqueCurve(r);
        int64_t t44 = (t & 0x80000000000LL) ? (int64_t)(((uint64_t)t & 0xFFFFFFFF000ULL) << 8) | (int64_t)0xFFF0000000000000ULL
                                             : (int64_t)(((uint64_t)t & 0xFFFFFFFF000ULL) << 8);
        int32_t g = (int32_t)((t44 * ratio) >> 32);
        int64_t f = (int64_t)(int32_t)(((int64_t)throttleAbs_ * g * 0x100000) >> 32) * 0x1000;   // x 0xB38 (1.0)
        driveForce_ = (int32_t)(f >> 12);
        if (b64_ & 0x100) driveForce_ <<= 2;
    } else if (!(b62_ & 2)) {   // coasting: engine braking
        int64_t c = ((int64_t)maxTorque_ * torqueCurve(r)) >> 12;
        int64_t v = (-0x5100000000LL - (c << 32)) >> 12;
        driveForce_ = (int32_t)(((int64_t)(int32_t)((v * ratio) >> 32) * engK10_) >> 12);
    }
    handleGearChange();
}

void Vehicle::handleGearChange() {   // CEngine::HandleGearChange
    if (shiftDelay_) return;
    bool slowBraking = (b62_ & 2) && speedUnits(speed()) < 0x1C000;
    int32_t half = maxRpm_ >> 1;
    if (gear_ < 1) {
        if (rpm_ < half && slowBraking) {
            int8_t g = (int8_t)(gear_ + 1);
            if ((g & 0xFE) == 0) g = 1;
            gear_ = g;
            shiftDelay_ = 2;
        }
        return;
    }
    if (numGears_ != gear_ && mulq(maxRpm_, 0xE66) <= rpm_) { ++gear_; shiftDelay_ = 2; }
    if (half < rpm_) return;
    if (gear_ == 1) {
        if (!slowBraking || (b64_ & 1)) return;
        gear_ = -1;
    } else --gear_;
    shiftDelay_ = 2;
}

void Vehicle::updateTyres(Collision* col) {   // cWheeledVehicle::UpdateTyres
    if (!(b62_ & 0x90)) traction_ = std::max(traction_ - 0x333, 0);
    for (Tyre& t : tyre_) {
        tyreGroundContact(t, col);
        tyreForces(t);
        t.grip = std::min(t.gripMax, t.grip + (t.gripMax >> 4));
        tyreVelocity(t);
    }
}

void Vehicle::tyreGroundContact(Tyre& t, Collision* col) {   // CTyre::CalcGroundContact
    int32_t w[3];
    for (int k = 0; k < 3; ++k) w[k] = pos[k] + mulq(t.axleY, fwd[k]);
    if (simple_ && t.onGround && ((groundSkip_ + frameCounter_) & 3) != 0) {
        t.contact[0] = w[0]; t.contact[1] = w[1]; t.contact[2] = w[2];
        t.normal[0] = 0; t.normal[1] = 0; t.normal[2] = 0x1000;
        return;
    }
    Collision::Ground g{0, 0, {0, 0, 1}};
    if (col && col->ok()) g = col->ground(w[0] / 4096.f, w[1] / 4096.f, (w[2] + 0x1000) / 4096.f);
    int32_t gz = (int32_t)lroundf(g.z * 4096.f);
    int32_t d = w[2] - gz;
    t.contact[0] = w[0]; t.contact[1] = w[1]; t.contact[2] = gz;
    for (int k = 0; k < 3; ++k) t.normal[k] = (int32_t)lroundf(g.normal[k] * 4096.f);
    bool adjust;
    if (d < 1) { t.onGround = false; adjust = true; }
    else { t.onGround = d < 0x4CC; adjust = d >= 0x4CC; }
    if (adjust) {
        int32_t tilt = (int32_t)(((int64_t)hx * right[2]) >> 12);
        t.onGround = d + tilt < 0x4CC;
        if (d + tilt >= 0x4CC) t.onGround = d - tilt < 0x4CC;
    }
    int32_t upN = (int32_t)lroundf(rot_[2][0] * 4096.f) * t.normal[0] + (int32_t)lroundf(rot_[2][1] * 4096.f) * t.normal[1] +
                  (int32_t)lroundf(rot_[2][2] * 4096.f) * t.normal[2];
    if (upN < 0x800000) t.onGround = false;
}

void Vehicle::tyreForces(Tyre& t) {   // CTyre::CalcForces
    t.skid = t.spinning = t.slide = false;
    if (!t.onGround) return;
    int32_t F[3] = {fwd[0], fwd[1], fwd[2]}, R[3] = {right[0], right[1], right[2]};
    if (t.front && steerAngle_ != 0) {
        int a = (int)(((int64_t)steerAngle_ * 0x28BE630) >> 8) >> 16;
        int c = fastsin(a + 0x4000), s = fastsin(a);
        for (int k = 0; k < 3; ++k) {
            int16_t f = (int16_t)((int16_t)((s * R[k]) >> 12) + (int16_t)((c * F[k]) >> 12));
            int16_t r = (int16_t)((int16_t)((c * R[k]) >> 12) - (int16_t)((s * F[k]) >> 12));
            F[k] = f; R[k] = r;
        }
    }
    int32_t v[3];
    velocityAt(t.contact, v);
    int32_t slip[3];
    for (int k = 0; k < 3; ++k) slip[k] = (int32_t)(((int64_t)(int32_t)(((int64_t)t.spin * F[k] * 0x100000) >> 32) * wheelRadius_) >> 12) - v[k];
    int32_t share = t.front ? weightFront_ : 0x1000 - weightFront_;
    int32_t load = (int32_t)(((int64_t)mass_ * share * 0x100000) >> 32);
    int64_t maxF = ((int64_t)load * t.grip) >> 12;
    int32_t mag = (int32_t)isqrt((int64_t)slip[0] * slip[0] + (int64_t)slip[1] * slip[1] + (int64_t)slip[2] * slip[2]);
    int32_t limit = (int32_t)(((int64_t)(int32_t)(((maxF << 37) - (maxF << 33)) >> 32) * sticky_) >> 12);
    int64_t f[3];
    if (limit < mag) {   // sliding: the force is capped
        for (int k = 0; k < 3; ++k) f[k] = (int64_t)(int32_t)((((int64_t)slip[k] << 32) / mag) >> 20) * limit;
        t.slide = true;
    } else for (int k = 0; k < 3; ++k) f[k] = (int64_t)load * slip[k];
    int32_t fx = (int32_t)(f[0] >> 12) >> 4, fy = (int32_t)(f[1] >> 12) >> 4, fz = (int32_t)(f[2] >> 12) >> 4;
    if (!t.front && 0x8000 < mag) {
        if (!(b64_ & 0x100)) { if (0x10000 < mag) t.spinning = true; }
        else if (sticky_ * 16 < mag) t.spinning = true;
    }
    int32_t along = (int32_t)(((int64_t)F[0] * fx + (int64_t)F[1] * fy + (int64_t)F[2] * fz) >> 12);
    int32_t side = (int32_t)(((int64_t)fy * R[1] + (int64_t)fx * R[0] + (int64_t)fz * R[2]) >> 12);
    if (traction_ != 0) {
        int64_t tq = (int64_t)traction_ * 0x100000;
        along = mulq((int32_t)((tq * (t.k0 - 0x1000) + 0x100000000000LL) >> 32), along);
        side = mulq((int32_t)((tq * (t.k4 - 0x1000) + 0x100000000000LL) >> 32), side);
    }
    int32_t s1 = (int32_t)(((int64_t)(handbrakeTimer_ * (lowGearGrip_ - 0x1000)) * 0xD00000 + 0x100000000000LL) >> 32);
    int32_t sideScale = (int32_t)(((int64_t)s1 * (t.k18 - 0x1000) * 0x100000 + 0x100000000000LL) >> 32);
    int32_t sf[3];
    for (int k = 0; k < 3; ++k) sf[k] = mulq(sideScale, (int32_t)(((int64_t)R[k] * side * 0x100000) >> 32));
    if (gear_ == -1 && lowGearGrip_ < 0x400 && handbrakeTimer_ > 0x32 && std::abs(along * 2) < std::abs(side))
        for (int k = 0; k < 3; ++k) sf[k] = mulq(sf[k], 0x66);
    for (int k = 0; k < 3; ++k) t.force[k] = sf[k] + mulq(along, F[k]);
    if (t.burst) for (int k = 0; k < 3; ++k) t.force[k] = mulq(t.force[k], 0xE66);
    t.torqueFb = wheelRadius_ ? (int32_t)(((int64_t)along * 30) / wheelRadius_) << 12 : 0;
}

void Vehicle::tyreVelocity(Tyre& t) {   // CTyre::ProcessTyreVelocity
    if ((b62_ & 0xD0) && t.front && wheelRadius_) {   // handbrake: the front wheels follow the ground
        int64_t v = ((int64_t)vel[0] * fwd[0] + (int64_t)vel[1] * fwd[1] + (int64_t)vel[2] * fwd[2]) * 0x100000;
        tyre_[0].spin = (int32_t)(((v & (int64_t)0xFFFFFFFF00000000ULL) / wheelRadius_) >> 20);
    }
    int64_t brakeT = 0;
    if (b62_ & 2) {
        int32_t split = t.front ? brakeSplit_ : 0x1000 - brakeSplit_;
        int32_t b = mulq(throttleAbs_, (int32_t)(((int64_t)brakeForce_ * split * 0x100000) >> 32));
        brakeT = gear_ >= 0 ? b * 5 : b * -5;
    }
    int32_t dsplit = t.front ? driveSplit_ : 0x1000 - driveSplit_;
    int64_t torque = ((int64_t)driveForce_ * dsplit + brakeT * -0x1000) >> 12;
    if (t.onGround) torque = (int32_t)torque - t.torqueFb;
    int32_t share = t.front ? weightFront_ : 0x1000 - weightFront_;
    int64_t inertia = ((int64_t)mass_ * share * 0x100000) >> 32;
    int64_t dw = inertia ? (int64_t)(((uint64_t)torque << 32)) / inertia : 0;
    int32_t s = t.spin + (int32_t)((((int64_t)(dw << 12) >> 32) + (int64_t)(int32_t)(dw >> 20) * 0x10) >> 9);
    s = gear_ < 1 ? std::min(s, 0) : std::max(s, 0);
    if (!t.front && (b62_ & 0xD0)) s = 0;   // handbrake locks the rear wheels
    t.spin = s;
}

void Vehicle::applyTyreForce(Tyre& t) {   // cWheeledVehicle::ApplyTyreForce
    if (!t.onGround || (!t.force[0] && !t.force[1] && !t.force[2])) return;
    int32_t d = (int32_t)(((int64_t)t.normal[0] * t.force[0] + (int64_t)t.normal[1] * t.force[1] + (int64_t)t.normal[2] * t.force[2]) * 0x100000 >> 32);
    int32_t f[3];
    for (int k = 0; k < 3; ++k) f[k] = (t.force[k] - mulq(d, t.normal[k])) * 30;
    applyWorldForce(t.contact, f, 2);
}

void Vehicle::calcForces() {   // cWheeledVehicle::CalcForces
    int32_t cg[3];
    worldCG(cg);
    if (tyre_[0].onGround && tyre_[1].onGround) ++groundFrames_;
    else groundFrames_ = 0;
    if (!simple_) {
        if (groundFrames_ > 5 && std::abs(up[2] - 0x1000) < 0x28 &&
            (!meshNear_ || (b62_ != 0 && (int64_t)vel[0] * vel[0] + (int64_t)vel[1] * vel[1] + (int64_t)vel[2] * vel[2] < 0x4000001))) {
            setToSimple(true);
            vel[2] = 0;
            angVel[0] = angVel[1] = angVel[2] = 0;
        }
    } else if (!tyre_[0].onGround || !tyre_[1].onGround) setToSimple(false);
    applyTyreForce(tyre_[0]);
    applyTyreForce(tyre_[1]);
    int64_t dm = (int64_t)drag_ * -speed();
    int64_t d44 = (dm & 0x80000000000LL) ? (int64_t)(((uint64_t)dm & 0xFFFFFFFF000ULL) << 8) | (int64_t)0xFFF0000000000000ULL
                                          : (int64_t)(((uint64_t)dm & 0xFFFFFFFF000ULL) << 8);
    int32_t k = (int32_t)((d44 * 0x88) >> 32);
    int32_t df[3] = {mulq(k, vel[0]), mulq(k, vel[1]), mulq(k, vel[2])};
    applyWorldForce(cg, df, 2);
    if (!simple_) {
        int32_t gf[3] = {0, 0, mulq(kGravityZ, mass_)};
        applyWorldForce(cg, gf, 1);
    }
    collidedPrev_ = collided_;
}

void Vehicle::handleSettling(Collision* col) {   // cWheeledVehicle::HandleSettling (resting on a side or the roof)
    if (!physics_ || simple_ || !velocityBelow(0x6000) || !col || !col->ok()) { settleFrames_ = 0; return; }
    Collision::Ground g = col->ground(pos[0] / 4096.f, pos[1] / 4096.f, pos[2] / 4096.f);
    int32_t gn[3] = {(int32_t)lroundf(g.normal[0] * 4096.f), (int32_t)lroundf(g.normal[1] * 4096.f), (int32_t)lroundf(g.normal[2] * 4096.f)};
    int32_t gz = (int32_t)lroundf(g.z * 4096.f);
    const int16_t* axis[3] = {right, right, up};
    const int sgn[3] = {1, -1, -1};
    const int32_t size[3] = {hx * 2, hx * 2, hz * 2};
    for (int i = 0; i < 3; ++i) {
        int32_t d[3] = {sgn[i] * axis[i][0], sgn[i] * axis[i][1], sgn[i] * axis[i][2]};
        if ((int64_t)d[0] * gn[0] + (int64_t)d[1] * gn[1] + (int64_t)d[2] * gn[2] <= 0xF32FFF) continue;
        int32_t z = pos[2] + mulq(d[2], size[i]);
        if (std::abs(z - gz) < 0x333) {
            if (++settleFrames_ == 6) setToPhysics(false);
            return;
        }
    }
    settleFrames_ = 0;
}

// ============================================================================================== integrator
void Vehicle::integrate(Collision* col) {   // cPhysicalIntegrator::Process for this car
    ++frameCounter_;
    if (!physics_) return;
    DBG("integrate: force %d %d %d torque %d %d %d vel %.2f %.2f %.2f\n", force_[0], force_[1], force_[2], torque_[0], torque_[1], torque_[2], vel[0] / 4096.f, vel[1] / 4096.f, vel[2] / 4096.f);
    recalcKinematics();
    DBG("  after kin: vel %.2f %.2f %.2f ang %d %d %d  tyres %d %d drive %d gear %d rpm %d\n", vel[0] / 4096.f, vel[1] / 4096.f, vel[2] / 4096.f, angVel[0], angVel[1], angVel[2], tyre_[0].onGround, tyre_[1].onGround, driveForce_, gear_, rpm_ >> 12);
    if (!col || !col->ok()) { integrateStep(0x1000); return; }
    int32_t r = speed() / 0x19 + (int32_t)(((int64_t)hy * 3) >> 1);
    meshNear_ = col->meshNear(pos, speed() / 0x19 + hy * 2);
    if (!meshNear_ && simple_) {
        collided_ = true;
        simpleCollision(col);
        simpleSprings(col);
    } else {
        collided_ = false;
        fullCollision(col, meshNear_);
        fullSprings(col);
        if (simple_) collided_ = true;
    }
    (void)r;
}

void Vehicle::simpleCollision(Collision* col) {   // cPhysicalIntegrator::SimpleCollision
    Collision::Candidates cand;
    int32_t R = speed() / 0x19 + (int32_t)(((int64_t)hy * 3) >> 1);
    col->candidates(pos, R, false, cand);
    int32_t frac = 0x1000;
    int iters = 8;
    std::vector<Rec> recs;
    while (iters) {
        int32_t sv[3] = {cgWorld_[0], cgWorld_[1], cgWorld_[2]}, sVel[3] = {vel[0], vel[1], vel[2]}, sAng[3] = {angVel[0], angVel[1], angVel[2]};
        float sq[4] = {q_[0], q_[1], q_[2], q_[3]};
        int32_t before[6][4], after[6][4];
        int n;
        spheres(before, n);
        integrateStep(frac);
        spheres(after, n);
        int32_t best = 0x64000;
        recs.clear();
        auto add = [&](const Rec& rc) {
            if (rc.t > best) return;
            if (rc.t < best) recs.clear();
            recs.push_back(rc);
            best = rc.t;
        };
        for (const Collision::Cyl* c : cand.cyls) {
            if (playerDriving_ && c->pad != 0) continue;
            for (int i = 0; i < n; ++i) {
                int32_t a[3] = {before[i][0], before[i][1], pos[2]};
                Rec rc{};
                if (Collision::sweptSphereVCylinder(a, after[i], after[i][3], *c, rc.p, rc.n, rc.t)) add(rc);
            }
        }
        for (const Collision::Box* b : cand.boxes) {
            if (playerDriving_ && (b->flags >> 3 & 1)) continue;
            for (int i = 0; i < n; ++i) {
                int32_t a[3] = {before[i][0], before[i][1], pos[2]};
                Rec rc{};
                if (!Collision::sweptCircleVBox(a, after[i], after[i][3], hz * 2, *b, rc.p, rc.t) || rc.t < 0) continue;
                for (int k = 0; k < 3; ++k) rc.n[k] = (a[k] - rc.p[k]) + mulq(after[i][k] - a[k], rc.t);
                if (!rc.n[0] && !rc.n[1] && !rc.n[2]) continue;
                normalise(rc.n);
                add(rc);
            }
        }
        if (recs.empty()) break;
        for (int k = 0; k < 3; ++k) { cgWorld_[k] = sv[k]; vel[k] = sVel[k]; angVel[k] = sAng[k]; }
        memcpy(q_, sq, sizeof q_);
        syncFromIntegrator();
        int64_t tt = (iters <= 3 && best <= 3) ? 4 : best;
        int32_t step = (int32_t)((tt * frac) >> 12);
        integrateStep(step);
        for (const Rec& rc : recs) calcImpactEnv(rc, iters > 3 || best > 3);
        --iters;
        frac -= step;
    }
    if (iters == 0) b64_ |= 0x80;   // IntegratorFrozen
}

void Vehicle::fullCollision(Collision* col, bool) {   // cPhysicalIntegrator::FullCollision
    Collision::Candidates cand;
    int32_t R = speed() / 0x19 + (int32_t)(((int64_t)hy * 3) >> 1);
    col->candidates(pos, R, false, cand);
    int32_t frac = 0x1000;
    int iters = 8;
    std::vector<Rec> recs;
    bool first = true;
    while (true) {
        int32_t sv[3] = {cgWorld_[0], cgWorld_[1], cgWorld_[2]}, sVel[3] = {vel[0], vel[1], vel[2]}, sAng[3] = {angVel[0], angVel[1], angVel[2]};
        float sq[4] = {q_[0], q_[1], q_[2], q_[3]};
        int32_t before[6][4], after[6][4], vb[8][3], va[8][3];
        int n;
        spheres(before, n);
        bboxVerts(vb);
        integrateStep(frac);
        (void)first;
        spheres(after, n);
        bboxVerts(va);
        int32_t best = 0x64000;
        recs.clear();
        auto add = [&](const Rec& rc) {
            if (rc.t > best) return;
            if (rc.t < best) recs.clear();
            recs.push_back(rc);
            best = rc.t;
        };
        for (const Collision::Cyl* c : cand.cyls) {
            if (playerDriving_ && c->pad != 0) continue;
            for (int i = 0; i < n; ++i) {
                Rec rc{};
                if (Collision::sweptSphereVCylinder(before[i], after[i], after[i][3], *c, rc.p, rc.n, rc.t)) add(rc);
            }
        }
        for (const Collision::Box* b : cand.boxes) {
            if (!(playerDriving_ && (b->flags >> 3 & 1))) {
                for (int i = 0; i < n; ++i) {
                    Rec rc{};
                    if (!Collision::sweptSphereVBox(before[i], after[i], after[i][3], *b, rc.p, rc.t) || rc.t < 0) continue;
                    for (int k = 0; k < 3; ++k) rc.n[k] = (before[i][k] - rc.p[k]) + mulq(after[i][k] - before[i][k], rc.t);
                    if (!rc.n[0] && !rc.n[1] && !rc.n[2]) continue;
                    normalise(rc.n);
                    add(rc);
                }
            }
            for (int v = 0; v < 8; ++v) {   // the box corners (0x1E3)
                Rec rc{};
                if (!Collision::sweptVertVBox(vb[v], va[v], *b, rc.p, rc.n, rc.t) || rc.t < 0 || rc.n[2] <= -0xF34) continue;
                if ((int64_t)rc.n[2] * (rc.p[2] - pos[2]) + (int64_t)rc.n[0] * (rc.p[0] - pos[0]) + (int64_t)rc.n[1] * (rc.p[1] - pos[1]) >= 1) continue;
                add(rc);
            }
        }
        for (const Collision::TriRef& tr : cand.tris) {
            for (int i = 0; i < n; ++i) {
                Rec rc{};
                if (Collision::sweptSphereVTri(before[i], after[i], after[i][3], tr, rc.p, rc.n, rc.t) && rc.t >= 0) add(rc);
            }
        }
        if (!simple_) {   // the ground plane under the car (GetGroundSimple): corners going through it
            int32_t radius = (int32_t)isqrt((int64_t)hx * hx + (int64_t)hy * hy + (int64_t)hz * hz);
            Collision::Ground g = col->ground(pos[0] / 4096.f, pos[1] / 4096.f, (pos[2] + radius) / 4096.f);
            int32_t gz = (int32_t)lroundf(g.z * 4096.f);
            {
                for (int v = 0; v < 8; ++v) {
                    int32_t zb = vb[v][2], za = va[v][2];
                    if (za >= zb) continue;
                    int32_t h = zb - za, d = zb - gz;
                    if (h - d == 0 || d > h) continue;
                    Rec rc{};
                    rc.t = std::max((int32_t)((((int64_t)(uint32_t)d << 32) / h) >> 20), 0);
                    for (int k = 0; k < 3; ++k) rc.p[k] = vb[v][k] + mulq(rc.t, va[v][k] - vb[v][k]);
                    rc.n[0] = 0; rc.n[1] = 0; rc.n[2] = 0x1000;
                    rc.depth = h - d;
                    rc.surface = g.surface;
                    add(rc);
                }
            }
        }
        if (recs.empty()) break;
        if (simple_) setToSimple(false);
        for (int k = 0; k < 3; ++k) { cgWorld_[k] = sv[k]; vel[k] = sVel[k]; angVel[k] = sAng[k]; }
        memcpy(q_, sq, sizeof q_);
        syncFromIntegrator();
        int64_t tt = (iters <= 3 && best <= 3) ? 4 : best;
        int32_t step = (int32_t)((tt * frac) >> 12);
        integrateStep(step);
        for (const Rec& rc : recs) calcImpactEnv(rc, iters > 3 || best > 3);
        --iters;
        frac -= step;
        if (iters == 0) {
            if (frac > 0x800) { vel[0] = vel[1] = 0; angVel[0] = 0; }
            b64_ |= 0x80;
            return;
        }
        first = false;
    }
}

void Vehicle::calcImpactEnv(const Rec& rc, bool friction) {   // cPhysicalIntegrator::CalcImpactEnv + CalcImpact2
    int32_t cg[3];
    worldCG(cg);
    int32_t v[3];
    velocityAt(rc.p, v);
    int32_t vn = (int32_t)(((int64_t)rc.n[0] * v[0] + (int64_t)rc.n[1] * v[1] + (int64_t)v[2] * rc.n[2]) >> 12);
    int32_t vnc = vn < 0 ? vn : -4;
    int32_t r[3] = {rc.p[0] - cg[0], rc.p[1] - cg[1], rc.p[2] - cg[2]};
    if (simple_) r[2] = 0;
    int32_t den = impactTerm(rc.n, r) + invMass_;
    if (den <= 0) return;
    uint32_t u = (uint32_t)(-vnc - 0x1000);
    uint32_t u3 = u > 0xDFFF ? 0xE000 : u;
    uint32_t e = (int32_t)u >= 0 ? (uint32_t)(((uint64_t)(u3 * 0xE3E) * 0x12492493ULL) >> 32) & 0x1FFF000 : 0;
    uint64_t j = ((uint64_t)e * (uint64_t)-0x100000LL + 0x1FD700000000ULL) / (uint64_t)(uint32_t)den;
    int32_t impulse = (int32_t)((int64_t)((-j) >> 20) * vnc >> 12);
    bool noFriction = false;
    if (rc.surface != 2) {
        int64_t upn = (int64_t)lroundf(rot_[2][0] * 4096.f) * rc.n[0] + (int64_t)lroundf(rot_[2][1] * 4096.f) * rc.n[1] + (int64_t)lroundf(rot_[2][2] * 4096.f) * rc.n[2];
        if (0x800000 < upn && mass_) noFriction = true;   // the car standing on it: no friction (the tyres do that)
    }
    int32_t coef = 0xCC;
    if (std::abs(rc.n[2]) < 0x199) {   // a wall: the wheels lose grip (LoseTraction(0, 0xB33))
        tyre_[0].grip = 0;
        tyre_[1].grip = mulq(tyre_[1].grip, 0xB33);
        coef = 0;
    }
    int32_t fr[3] = {0, 0, 0};
    if (friction) {
        int32_t vt[3] = {v[0] - mulq(vnc, rc.n[0]), v[1] - mulq(vnc, rc.n[1]), v[2] - mulq(vnc, rc.n[2])};
        if (!vt[0] && !vt[1] && !vt[2]) noFriction = true;
        if (!noFriction) {
            int32_t k = (int32_t)(((int64_t)coef * -mass_) >> 12);
            for (int i = 0; i < 3; ++i) fr[i] = mulq(k, vt[i]);
        }
    }
    collided_ = true;
    int32_t j30 = impulse * 30;
    int32_t f[3] = {fr[0] + mulq(j30, rc.n[0]), fr[1] + mulq(j30, rc.n[1]), fr[2] + mulq(j30, rc.n[2])};
    DBG("  impact p %.2f %.2f %.2f n %d %d %d vn %.2f imp %.2f den %d\n", rc.p[0] / 4096.f, rc.p[1] / 4096.f, rc.p[2] / 4096.f, rc.n[0], rc.n[1], rc.n[2], vn / 4096.f, impulse / 4096.f, den);
    applyWorldForce(rc.p, f, 4);
    recalcKinematics();
    if (wheelRadius_) {   // cWheeledVehicle::OnCollisionPost: the wheels match the ground speed
        int64_t along = ((int64_t)vel[0] * fwd[0] + (int64_t)vel[1] * fwd[1] + (int64_t)vel[2] * fwd[2]) * 0x100000;
        tyre_[0].spin = (int32_t)(((along & (int64_t)0xFFFFFFFF00000000ULL) / wheelRadius_) >> 20);
    }
    if (!noFriction) onCollision(f, nullptr);   // (not reported while resting on the surface)
}

void Vehicle::springImpact(const Rec& rc) {   // cPhysicalIntegrator::SpringImpact
    int32_t v[3];
    velocityAt(rc.p, v);
    int32_t depth = std::min(rc.depth, 0x800);
    int32_t vn = (int32_t)(((int64_t)rc.n[0] * v[0] + (int64_t)rc.n[1] * v[1] + (int64_t)rc.n[2] * v[2]) >> 12);
    int64_t damp = (int64_t)vn * -0x64000;
    int32_t F = (int32_t)((damp < 0 ? damp : 0) >> 12) + depth * 0x80;
    if (F < 1) return;
    int32_t imp[3] = {mulq(F, rc.n[0]), mulq(F, rc.n[1]), mulq(F, rc.n[2])};
    int64_t upn = (int64_t)lroundf(rot_[2][0] * 4096.f) * rc.n[0] + (int64_t)lroundf(rot_[2][1] * 4096.f) * rc.n[1] + (int64_t)lroundf(rot_[2][2] * 4096.f) * rc.n[2];
    if (upn < 0x800001) {   // not under the car: friction along the surface
        int32_t vt[3] = {v[0] - mulq(vn, rc.n[0]), v[1] - mulq(vn, rc.n[1]), v[2] - mulq(vn, rc.n[2])};
        int32_t m = (int32_t)isqrt((int64_t)vt[0] * vt[0] + (int64_t)vt[1] * vt[1] + (int64_t)vt[2] * vt[2]);
        if (m > 0) normalise(vt);
        int32_t lim = (int32_t)(((int64_t)depth * 0x1C000) >> 12);
        m = std::min(m, lim);
        for (int k = 0; k < 3; ++k) imp[k] -= mulq(vt[k], m);
    }
    collided_ = true;
    DBG("  spring p %.2f %.2f %.2f n %d %d %d depth %.3f F %.2f vn %.2f\n", rc.p[0] / 4096.f, rc.p[1] / 4096.f, rc.p[2] / 4096.f, rc.n[0], rc.n[1], rc.n[2], depth / 4096.f, F / 4096.f, vn / 4096.f);
    int32_t f[3] = {mulq(imp[0], mass_), mulq(imp[1], mass_), mulq(imp[2], mass_)};
    applyWorldForce(rc.p, f, 4);
    recalcKinematics();
}

void Vehicle::fullSprings(Collision* col) {   // cPhysicalIntegrator::FullSpringCollision
    Collision::Candidates cand;
    int32_t R = speed() / 0x19 + (int32_t)(((int64_t)hy * 3) >> 1);
    col->candidates(pos, R, false, cand);
    int32_t sp[6][4], vb[8][3];
    int n;
    spheres(sp, n);
    bboxVerts(vb);
    for (const Collision::Box* b : cand.boxes)
        for (int i = 0; i < n; ++i) {
            Rec rc{};
            if (simple_) {
                int32_t c[3] = {sp[i][0], sp[i][1], pos[2]};
                if (Collision::circleVBox(c, sp[i][3] + 0x199, hz * 2, *b, rc.p, rc.n, rc.depth)) springImpact(rc);
            } else if (Collision::sphereVBox(sp[i], sp[i][3], *b, rc.p, rc.n, rc.depth)) springImpact(rc);
        }
    for (const Collision::TriRef& tr : cand.tris) {
        for (int i = 0; i < n; ++i) {
            Rec rc{};
            if (Collision::sphereVTri(sp[i], sp[i][3], tr, rc.p, rc.n, rc.depth)) {
                if (simple_) setToSimple(false);
                springImpact(rc);
            }
        }
        if (std::abs(tr.tri->n[2]) > 0x199)
            for (int v = 0; v < 8; ++v) {   // corners against walkable slopes
                int32_t a[3] = {vb[v][0] + (tr.tri->n[0] >> 1), vb[v][1] + (tr.tri->n[1] >> 1), vb[v][2] - 0x4CC + (tr.tri->n[2] >> 1)};
                int32_t b[3] = {vb[v][0], vb[v][1], vb[v][2] - 0x4CC};
                Rec rc{};
                if (!Collision::sweptVertVTri(a, b, tr, rc.p, rc.n, rc.t)) continue;
                const int32_t* V0 = tr.verts + tr.tri->v[0] * 3;
                rc.depth = (int32_t)((((int64_t)V0[0] * rc.n[0] + (int64_t)V0[1] * rc.n[1] + (int64_t)V0[2] * rc.n[2]) -
                                      ((int64_t)rc.n[0] * b[0] + (int64_t)rc.n[1] * b[1] + (int64_t)rc.n[2] * b[2])) >> 12);
                if (rc.depth > 0) {
                    if (simple_) setToSimple(false);
                    springImpact(rc);
                }
            }
    }
    for (const Collision::Cyl* c : cand.cyls)
        for (int i = 0; i < n; ++i) {
            int32_t d[3] = {sp[i][0] - c->x, sp[i][1] - c->y, (sp[i][2] - c->z) - c->h};
            if (d[2] == 0 || sp[i][2] - c->z < c->h) d[2] = 0;
            int64_t d2 = ((int64_t)d[0] * d[0] + (int64_t)d[1] * d[1] + (int64_t)d[2] * d[2]) & ~0xFFFLL;
            int64_t rr = (int64_t)sp[i][3] + c->r;
            if (d2 >= rr * rr) continue;
            normalise(d);
            Rec rc{};
            rc.n[0] = d[0]; rc.n[1] = d[1]; rc.n[2] = d[2];
            rc.depth = (sphereR_ - (int32_t)isqrt(d2)) + c->r;
            int32_t k = c->r - rc.depth;
            rc.p[0] = c->x + mulq(d[0], k); rc.p[1] = c->y + mulq(d[1], k); rc.p[2] = simple_ ? pos[2] : c->z + mulq(d[2], k);
            if (c->pad == 0) springImpact(rc);
        }
    {   // the corners float 0.3 above the ground (UseSpringCollision)
        for (int v = 0; v < 8; ++v) {
            Collision::Ground g = col->ground(vb[v][0] / 4096.f, vb[v][1] / 4096.f, (vb[v][2] + 0x1000) / 4096.f);
            int32_t gz = (int32_t)lroundf(g.z * 4096.f);
            if (gz >= 1) continue;   // (the game only springs off ground at or below 0)
            if (-0x4CC < gz - vb[v][2]) {
                Rec rc{};
                rc.depth = (gz - vb[v][2]) + 0x4CC;
                rc.n[2] = 0x1000;
                rc.p[0] = vb[v][0]; rc.p[1] = vb[v][1]; rc.p[2] = vb[v][2];
                springImpact(rc);
            }
        }
    }
}

void Vehicle::simpleSprings(Collision* col) {   // cPhysicalIntegrator::SimpleSpringCollision
    Collision::Candidates cand;
    int32_t R = speed() / 0x19 + (int32_t)(((int64_t)hy * 3) >> 1);
    col->candidates(pos, R, false, cand);
    int32_t sp[6][4];
    int n;
    spheres(sp, n);
    for (const Collision::Box* b : cand.boxes)
        for (int i = 0; i < n; ++i) {
            int32_t c[3] = {sp[i][0], sp[i][1], pos[2]};
            Rec rc{};
            if (Collision::circleVBox(c, sp[i][3] + 0x199, hz * 2, *b, rc.p, rc.n, rc.depth) && !(b->flags >> 3 & 1)) springImpact(rc);
        }
    for (const Collision::Cyl* c : cand.cyls)
        for (int i = 0; i < n; ++i) {
            int32_t d[3] = {sp[i][0] - c->x, sp[i][1] - c->y, 0};
            int64_t d2 = (((int64_t)d[0] * d[0] + (int64_t)d[1] * d[1]) * 0x100000 >> 20) & ~0xFFFLL;
            int64_t rr = (int64_t)sp[i][3] + c->r + 0x199;
            if (d2 >= rr * rr) continue;
            normalise(d);
            Rec rc{};
            rc.n[0] = d[0]; rc.n[1] = d[1];
            rc.depth = (sphereR_ - (int32_t)isqrt(d2)) + c->r;
            int32_t k = c->r - rc.depth;
            rc.p[0] = c->x + mulq(d[0], k); rc.p[1] = c->y + mulq(d[1], k); rc.p[2] = pos[2];
            if (c->pad == 0) springImpact(rc);
        }
}

// ============================================================================================== car against car
int32_t Vehicle::boundRadius() const { return (int32_t)isqrt((int64_t)hx * hx + (int64_t)hy * hy + (int64_t)hz * hz); }

void Vehicle::collisionCentre(int32_t o[3]) const {
    for (int k = 0; k < 3; ++k) o[k] = pos[k] + mulq(collOffset_[0], right[k]) + mulq(collOffset_[1], fwd[k]) + mulq(collOffset_[2], up[k]);
}

static bool distanceLessThan(const int32_t a[3], const int32_t b[3], int32_t d) {
    int64_t x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
    return x * x + y * y + z * z < (int64_t)d * d;
}

void Vehicle::collideCars(std::vector<Vehicle>& cars, int playerCar, uint32_t frame) {
    // PhysicalToPhysicalProcess: each physics-active car (in order) against the cars not handled yet this frame
    std::vector<uint8_t> done(cars.size(), 0);
    for (size_t i = 0; i < cars.size(); ++i) {
        Vehicle& a = cars[i];
        if (!a.physics_) continue;
        done[i] = 1;
        a.recalcKinematics();
        // cVehicle::DueForCollision: the player's car every frame, others every other frame (cSimpleMover)
        if ((int)i != playerCar && ((frame + (uint32_t)i) & 1)) continue;
        int32_t ca[3];
        a.collisionCentre(ca);
        for (size_t j = 0; j < cars.size(); ++j) {
            if (j == i || done[j]) continue;
            Vehicle& b = cars[j];
            int32_t cb[3];
            b.collisionCentre(cb);
            if (distanceLessThan(ca, cb, a.boundRadius() + b.boundRadius())) resolvePair(a, b);
        }
    }
}

bool Vehicle::resolvePair(Vehicle& a, Vehicle& b) {   // cPhysicalIntegrator::ResolvePhysicalCollision (both have mass)
    int32_t sa[6][4], sb[6][4];
    int na, nb;
    a.spheres(sa, na);
    b.spheres(sb, nb);
    bool first = true;
    for (int i = 0; i < na; ++i) {
        bool hitPrev = false;
        for (int j = 0; j < nb; ++j) {
            int32_t rr = sa[i][3] + sb[j][3];
            if (!distanceLessThan(sa[i], sb[j], rr)) {
                if (hitPrev) break;
                continue;
            }
            hitPrev = true;
            int32_t dx = sb[j][0] - sa[i][0], dy = sb[j][1] - sa[i][1], dz = sb[j][2] - sa[i][2];
            uint64_t d2 = (uint64_t)((int64_t)dx * dx + (int64_t)dy * dy + (int64_t)dz * dz);
            if (!(d2 & 0xFFFFFFFF000ULL)) continue;
            int32_t dist = (int32_t)isqrt((int64_t)(d2 & ~0xFFFULL));
            int64_t inv = dist ? (0x100000000000LL / dist) >> 20 : 0;
            Rec rc{};
            rc.n[0] = (int16_t)((inv * -dx) >> 12);
            rc.n[1] = (int16_t)((inv * -dy) >> 12);
            rc.n[2] = (int16_t)((inv * -dz) >> 12);
            for (int k = 0; k < 3; ++k) rc.p[k] = sa[i][k] - mulq(sa[i][3], rc.n[k]);   // on a's sphere, towards b
            rc.depth = rr - dist;
            if (calcImpactCar(a, b, rc, first)) first = false;
        }
    }
    return !first;
}

bool Vehicle::calcImpactCar(Vehicle& a, Vehicle& b, const Rec& rc, bool first) {   // cPhysicalIntegrator::CalcImpactCar
    int32_t va[3], vb[3], cga[3], cgb[3];
    a.velocityAt(rc.p, va);
    b.velocityAt(rc.p, vb);
    int32_t vr[3] = {va[0] - vb[0], va[1] - vb[1], va[2] - vb[2]};
    int32_t vn = (int32_t)(((int64_t)rc.n[0] * vr[0] + (int64_t)rc.n[1] * vr[1] + (int64_t)rc.n[2] * vr[2]) >> 12);
    if (vn >= 0) return false;   // moving apart
    a.worldCG(cga);
    b.worldCG(cgb);
    int32_t ra[3] = {rc.p[0] - cga[0], rc.p[1] - cga[1], rc.p[2] - cga[2]}, rb[3] = {rc.p[0] - cgb[0], rc.p[1] - cgb[1], rc.p[2] - cgb[2]};
    uint32_t u = (uint32_t)(-vn - 0x1000);
    uint32_t u3 = u > 0xDFFF ? 0xE000 : u;
    uint32_t e = (int32_t)u >= 0 ? (uint32_t)(((uint64_t)(u3 * 0xE90) * 0x12492493ULL) >> 32) & 0x1FFF000 : 0;
    int32_t terms = a.impactTerm(rc.n, ra) + b.impactTerm(rc.n, rb);
    int64_t den = (int64_t)a.invMass_ + b.invMass_ + std::max(terms, 0);
    int64_t q = den ? ((int64_t)e * 0x100000 - 0x1FD700000000LL) / den : 0;
    int32_t imp = (int32_t)(((q >> 20) * vn) >> 12);
    int32_t mf = a.mass_ > 0x7FF ? 0x4000 : a.mass_ << 3;   // the overlap is pushed out too
    if (b.mass_ < 0x800) mf = mulq(b.mass_ << 1, mf);
    imp += mulq(rc.depth, mf);
    if (vn < -0x2000) {   // a hard hit: cars nobody drives lose their grip (SetWheelFriction(0, 0))
        for (Vehicle* v : {&a, &b})
            if (!v->playerDriving_) { v->tyre_[0].grip = 0; v->tyre_[1].grip = 0; }
    }
    int32_t fa[3] = {0, 0, 0}, fb[3] = {0, 0, 0};
    int32_t vt[3] = {vr[0] - mulq(vn, rc.n[0]), vr[1] - mulq(vn, rc.n[1]), vr[2] - mulq(vn, rc.n[2])};
    if (vt[0] || vt[1] || vt[2]) {   // rubbing: friction 0.45 x mass
        int32_t ka = (int32_t)(((int64_t)-a.mass_ * 0x73300000LL) >> 32), kb = (int32_t)(((int64_t)b.mass_ * 0x73300000LL) >> 32);
        for (int k = 0; k < 3; ++k) { fa[k] = mulq(ka, vt[k]); fb[k] = mulq(kb, vt[k]); }
    }
    // CalcImpact2: the impulse (x30 per frame) along the normal, equal and opposite
    int32_t j30 = imp * 30;
    for (int k = 0; k < 3; ++k) { fa[k] += mulq(j30, rc.n[k]); fb[k] -= mulq(j30, rc.n[k]); }
    DBG("  car impact p %.2f %.2f %.2f n %d %d %d vn %.2f imp %.2f depth %.3f\n", rc.p[0] / 4096.f, rc.p[1] / 4096.f, rc.p[2] / 4096.f, rc.n[0], rc.n[1], rc.n[2], vn / 4096.f, imp / 4096.f, rc.depth / 4096.f);
    a.collided_ = b.collided_ = true;
    a.applyWorldForce(rc.p, fa, 8);
    b.applyWorldForce(rc.p, fb, 8);
    a.recalcKinematics();
    b.recalcKinematics();
    if (first) { a.onCollision(fa, &b); b.onCollision(fb, &a); }
    if (first)   // cWheeledVehicle::OnCollisionPost: the driven wheels match the ground speed
        for (Vehicle* v : {&a, &b})
            if (v->wheelRadius_) {
                int64_t along = ((int64_t)v->vel[0] * v->fwd[0] + (int64_t)v->vel[1] * v->fwd[1] + (int64_t)v->vel[2] * v->fwd[2]) * 0x100000;
                v->tyre_[0].spin = (int32_t)(((along & (int64_t)0xFFFFFFFF00000000ULL) / v->wheelRadius_) >> 20);
            }
    return true;
}

// ============================================================================================== doors
void Vehicle::openDoor(int seat) {   // cVehicle::OpenDoor
    if (seat < 0 || seat > 3 || doorNode_[seat] <= 0) return;
    TheSound().doorEvent(*this, true);
    doors_[seat].mode = 2;
    doorOpenBits_ |= 1 << seat;
}

void Vehicle::closeDoor(int seat) {   // cVehicle::CloseDoor
    if (seat < 0 || seat > 3 || doorNode_[seat] <= 0) return;
    doors_[seat].mode = 1;
}

void Vehicle::setDoorClosed(int seat) {   // cVehicle::SetDoorClosed
    TheSound().doorEvent(*this, false);
    doors_[seat].angle = 0;
    doors_[seat].speed = 0;
    doorOpenBits_ &= ~(1 << seat);
}

void Vehicle::updateDoors() {   // cVehicle::UpdateDoorMatrices -> cVehicleDoor::Update (max angles DAT_0058718e)
    for (int i = 0; i < 5; ++i) {
        Door& d = doors_[i];
        uint16_t max = TheGameplayTables().doorMax[i];
        if (d.mode == 3) {
            if (d.angle < max && d.speed > 9) { if (d.speed != 10) d.speed -= 5; }
            else { d.speed = 0; d.mode = 1; }
        } else if (d.mode == 2) {   // opening: 10 per frame up to the stop
            if (d.angle < max) d.speed = 10;
            else d.mode = 0;
        } else if (d.mode == 1) {   // closing: swings shut, faster each frame (-5, down to -40)
            if (d.angle > 9) {
                if (d.speed > 0) d.speed = 0;
                if (d.speed >= -0x27) d.speed -= 5;
            } else {
                if (i < 4) setDoorClosed(i);
                d.mode = 0;
            }
        }
        if (!d.speed) continue;
        uint32_t a = (uint32_t)d.angle + d.speed;
        a &= 0xFFFF;
        d.angle = (uint16_t)a;
        if (d.speed < 0) {
            if (a > max) { d.angle = 0; d.speed = 0; }
        } else if (a > max) {   // hits the stop and bounces back a little
            d.angle = max;
            d.speed = (int8_t)-((d.speed & 0xFE) >> 1);
        }
    }
}

void Vehicle::doorSpawnPoint(int seat, int32_t o[3]) const {
    o[0] = doorOff_[seat][0] + TheGameplayTables().spawnOffset[seat & 1][0];
    o[1] = doorOff_[seat][1] + (!(infoFlags8e_ >> 2 & 1) ? TheGameplayTables().spawnOffset[seat & 1][1] : 0);
    o[2] = 0;
}

void Vehicle::seatOffset(int seat, int32_t o[3]) const { for (int k = 0; k < 3; ++k) o[k] = seatOff_[seat][k]; }

void Vehicle::localToWorld(const int32_t l[3], int32_t o[3]) const {
    for (int k = 0; k < 3; ++k) o[k] = pos[k] + mulq(l[0], right[k]) + mulq(l[1], fwd[k]) + mulq(l[2], up[k]);
}

// ============================================================================================== body sway
void Vehicle::processAlways() { updateSuspension(); updateDoors(); addSkidmarks(); }

void Vehicle::addSkidmarks() const {   // cWheeledVehicle::AddSkidmarks
    int32_t lat = (int32_t)(((int64_t)(hx << 1) * 0x66600000LL) >> 32);   // 0.4 x the width either side
    int32_t L[3] = {mulq(lat, right[0]), mulq(lat, right[1]), mulq(lat, right[2])};
    if (tyre_[1].spinning) {   // the back wheels
        int32_t a = tyre_[1].axleY * 16;
        int32_t c[3] = {pos[0] + (a * fwd[0] >> 16), pos[1] + (a * fwd[1] >> 16), pos[2] + (a * fwd[2] >> 16)};
        int32_t r[3] = {c[0] + L[0], c[1] + L[1], c[2] + L[2]}, l[3] = {c[0] - L[0], c[1] - L[1], c[2] - L[2]};
        if (tyre_[1].onGround) {
            int32_t d[3] = {r[0] - l[0], r[1] - l[1], r[2] - l[2]};
            normalise(d);
            TheSkidmarks().addPoint(uid * 4, r, d, 1);
            TheSkidmarks().addPoint(uid * 4 + 1, l, d, 1);
        }
    }
    if (tyre_[0].spinning) {   // the front wheels
        int32_t a = tyre_[0].axleY * 16;
        int32_t c[3] = {pos[0] + (a * fwd[0] >> 16), pos[1] + (a * fwd[1] >> 16), pos[2] + (a * fwd[2] >> 16)};
        int32_t r[3] = {c[0] + L[0], c[1] + L[1], c[2] + L[2]}, l[3] = {c[0] - L[0], c[1] - L[1], c[2] - L[2]};
        int32_t d[3] = {right[0], right[1], right[2]};
        TheSkidmarks().addPoint(uid * 4 + 2, r, d, 1);
        TheSkidmarks().addPoint(uid * 4 + 3, l, d, 1);
    }
}

void Vehicle::updateSuspension() {   // cWheeledVehicle::UpdateSuspension (every frame)
    int32_t inRoll = swayRollV_, inPitch = swayPitchV_;
    if (simple_) {   // on the road: the body leans against this frame's change of velocity
        int32_t dv[3] = {mulq(accel_[0], 0x88), mulq(accel_[1], 0x88), mulq(accel_[2], 0x88)};
        int32_t r = (int32_t)(((int64_t)dv[0] * right[0] + (int64_t)dv[1] * right[1] + (int64_t)dv[2] * right[2]) >> 12);
        int32_t f = (int32_t)(((int64_t)dv[0] * fwd[0] + (int64_t)dv[1] * fwd[1] + (int64_t)dv[2] * fwd[2]) >> 12);
        inRoll += mulq(r, 0xCCC);
        inPitch += mulq(f, 0xCCC);
    }
    int32_t w = wobble_;
    int64_t acc = (int64_t)wobbleV_ - (int32_t)(((int64_t)(int32_t)(((int64_t)invMass_ * 3) >> 3) * w) >> 12);
    swayRollV_ = mulq(swayK_[2] >> 1, inRoll - mulq(mulq(swayK_[0], swayRoll_), 0x999));
    swayRoll_ += swayRollV_;
    int32_t damp = mulq(w, 0xCC);
    if ((int64_t)(int32_t)((acc * 0xE6600000LL) >> 32) * w < 1) damp = 0;
    swayPitchV_ = mulq(inPitch - mulq(mulq(swayK_[1], swayPitch_), 0x999), swayK_[3] >> 1);
    swayPitch_ += swayPitchV_;
    wobbleV_ = (int32_t)((acc * 0xE66) >> 12) - damp;
    wobble_ = w + wobbleV_;
    tiltV_ = (tiltV_ - mulq(tilt_, 0x2CC)) >> 1;
    tilt_ += tiltV_;
}

// cWheeledVehicle::UpdateModelMatrix: the drawn body = sway rotation (about the length, then across) x entity matrix
void Vehicle::modelMatrix(float M[16]) const {
    float E[4][3] = {{right[0] / 4096.f, right[1] / 4096.f, right[2] / 4096.f}, {fwd[0] / 4096.f, fwd[1] / 4096.f, fwd[2] / 4096.f},
                     {up[0] / 4096.f, up[1] / 4096.f, up[2] / 4096.f}, {pos[0] / 4096.f, pos[1] / 4096.f, pos[2] / 4096.f}};
    if (bike_) {   // cBike::UpdateModelMatrix: RotY(lean + wobble - foot-down lean) x RotX(pitch + tilt) about a wheel, x1.3
        int32_t ay = bikeLean_ + wobble_ - bikeKick_, ax = bikePitch_ + tilt_;
        if (ay || ax) {
            auto angle = [](int32_t rad) { return (int)((int32_t)(((int64_t)rad * 0x28BE630) >> 8) >> 16); };
            int a = angle(ay), b = angle(ax);
            float cy = fastsin(a + 0x4000) / 4096.f, sy = fastsin(a) / 4096.f, cx = fastsin(b + 0x4000) / 4096.f, sx = fastsin(b) / 4096.f;
            float Y[3][3] = {{cy, 0, -sy}, {0, 1, 0}, {sy, 0, cy}}, X[3][3] = {{1, 0, 0}, {0, cx, -sx}, {0, sx, cx}};
            float S[4][3] = {};
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j) S[i][j] = Y[i][0] * X[0][j] + Y[i][1] * X[1][j] + Y[i][2] * X[2][j];
            if (bikePitch_ != 0) {   // pivot on the back wheel (wheelie) or the front wheel (stoppie)
                int16_t sa = (int16_t)(((int64_t)bikePitch_ * 0x28BE630) >> 24);
                int32_t axle = bikePitch_ > 0 ? tyre_[0].axleY : tyre_[1].axleY;
                int32_t sn = fastsin(sa), c = fastsin((int16_t)(sa + 0x4000));
                S[3][1] = (int32_t)(((int64_t)(int32_t)(((0x1000 - (int64_t)c) * axle) >> 12) * 0x14CC) >> 12) / 4096.f;
                S[3][2] = (int32_t)(((int64_t)(int32_t)(((int64_t)axle * sn) >> 12) * 0x14CC) >> 12) / 4096.f;
            }
            float T[4][3];
            for (int i = 0; i < 4; ++i)
                for (int j = 0; j < 3; ++j) T[i][j] = S[i][0] * E[0][j] + S[i][1] * E[1][j] + S[i][2] * E[2][j] + (i == 3 ? E[3][j] : 0);
            memcpy(E, T, sizeof T);
        }
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) E[i][j] *= 0x14CC / 4096.f;   // cBike::VehicleScale
        float out[16] = {E[0][0], E[0][1], E[0][2], 0, E[1][0], E[1][1], E[1][2], 0, E[2][0], E[2][1], E[2][2], 0, E[3][0], E[3][1], E[3][2], 1};
        memcpy(M, out, sizeof out);
        return;
    }
    int32_t ay = (int32_t)(((int64_t)wobble_ * 0x1000 - (int64_t)swayRoll_ * swayK_[4]) >> 12);
    int32_t ax = (int32_t)(((int64_t)tilt_ * 0x1000 - (int64_t)swayPitch_ * swayK_[5]) >> 12);
    auto angle = [](int32_t rad) { return (int)((int32_t)(((int64_t)rad * 0x28BE630) >> 8) >> 16); };   // radians -> 0x10000
    if (ay || ax) {
        int a = angle(ay), b = angle(ax);
        float cy = fastsin(a + 0x4000) / 4096.f, sy = fastsin(a) / 4096.f, cx = fastsin(b + 0x4000) / 4096.f, sx = fastsin(b) / 4096.f;
        float Y[3][3] = {{cy, 0, -sy}, {0, 1, 0}, {sy, 0, cy}}, X[3][3] = {{1, 0, 0}, {0, cx, -sx}, {0, sx, cx}};
        float S[4][3] = {};
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) S[i][j] = Y[i][0] * X[0][j] + Y[i][1] * X[1][j] + Y[i][2] * X[2][j];
        auto mul = [](const float A[4][3], const float B[4][3], float O[4][3]) {   // row vectors: O = A x B
            float T[4][3];
            for (int i = 0; i < 4; ++i)
                for (int j = 0; j < 3; ++j) T[i][j] = A[i][0] * B[0][j] + A[i][1] * B[1][j] + A[i][2] * B[2][j] + (i == 3 ? B[3][j] : 0);
            memcpy(O, T, sizeof T);
        };
        float R[4][3];
        mul(S, E, R);
        if (ax < 0) {   // nose up: pivot about the back (the game applies the sway a second time here)
            int32_t hl = hy, t = fastsin(b), c = fastsin(b + 0x4000);
            S[3][0] = 0;
            S[3][1] = (mulq(hl, c) - hl) / 4096.f;
            S[3][2] = (int32_t)(((int64_t)t * -hl * 0x100000) >> 32) / 4096.f;
            mul(S, R, R);
        }
        memcpy(E, R, sizeof R);
    }
    float out[16] = {E[0][0], E[0][1], E[0][2], 0, E[1][0], E[1][1], E[1][2], 0, E[2][0], E[2][1], E[2][2], 0, E[3][0], E[3][1], E[3][2], 1};
    memcpy(M, out, sizeof out);
}

// ============================================================================================== damage
void Vehicle::onCollision(const int32_t F[3], const Vehicle* other) {
    // cVehicle::OnCollision: the crash sound (strength 50..80 from the closing speed, above 2 units/s)
    int64_t v[3] = {vel[0], vel[1], vel[2]};
    if (other) for (int k = 0; k < 3; ++k) v[k] += other->vel[k];
    uint64_t sp2 = (uint64_t)(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (sp2 >= 0x4000001) {
        if (sp2 > 0x9C3FFFFFFULL) sp2 = 0x9C4000000ULL;
        TheSound().collision(*this, (int)(((sp2 >> 24) * 0x32) >> 12) + 0x32);
    }
    // cVehicle::DoOnCollisionWorking
    bool otherIsVehicle = other != nullptr;
    if (dead_) return;
    double f5[3] = {(double)(((int64_t)F[0] * 5) >> 9), (double)(((int64_t)F[1] * 5) >> 9), (double)(((int64_t)F[2] * 5) >> 9)};
    double s = f5[0] * f5[0] + f5[1] * f5[1] + f5[2] * f5[2];
    int64_t k = (int64_t)((double)invMass_ * invMass_ * s / 16777216.0 / 4096.0);   // (dv)^2
    int64_t dmg = otherIsVehicle ? k - 0x1000 : ((k - 0xA000) * 0x333) >> 12;
    if (dmg <= 0) return;
    damage((int)(dmg >> 12));
}

void Vehicle::damage(int amount) {   // cVehicle::Damage (collision damage, 100 %)
    if (dead_ || amount <= 0) return;
    int d = amount < 2 ? 1 : amount;
    if ((int)health_ <= d) {
        if (playerDriving_) health_ = 0x1E;   // the player's car does not blow up at once: it catches fire
        else { health_ = 0; setDead(); return; }
    } else {
        health_ = (uint8_t)(health_ - d);
        if (health_ > 0x1E) burnTimer_ = 0x1E0;   // cVehicle::SetHealth
    }
    if (health_ < 0x1E && playerDriving_) health_ = 0x1E;
}

Vehicle::SoundState Vehicle::soundState() const {
    SoundState s{};
    s.frontSpin = tyre_[0].spinning; s.rearSpin = tyre_[1].spinning;
    s.frontGround = tyre_[0].onGround; s.rearGround = tyre_[1].onGround;
    s.burst = tyre_[0].burst || tyre_[1].burst;
    s.rpm = rpm_; s.maxRpm = maxRpm_;
    s.gas = (b62_ & 1) != 0;
    for (int k = 0; k < 3; ++k) s.accel[k] = accel_[k];
    return s;
}

void Vehicle::releaseEffects() {
    if (smoke_) { TheParticles().remove(smoke_); smoke_ = nullptr; }
}

void Vehicle::setDead() {   // cVehicle::SetDead: the burnt wreck, black smoke and fire for 12 seconds
    dead_ = true;
    justDied = true;
    palette = 25;
    burnTimer_ = 0x2D0;
    if (smoke_) { TheParticles().remove(smoke_); smoke_ = nullptr; }
    smoke_ = TheParticles().add<SmokeEmitter>(pos, 10);
    smokeLife_ = burnTimer_;
    smokeColour_ = 3;
    smokeOffset_[0] = 0; smokeOffset_[1] = 0; smokeOffset_[2] = 0x400;
    smokeVel_[0] = smokeVel_[1] = smokeVel_[2] = 0;
}

void Vehicle::processDamage(uint32_t frame) {   // the damage part of cVehicle::Process (+ cSmoke::Process)
    if (!dead_) {
        if (health_ < 0xBE) {   // smoke from the engine: 3/4 of the way to the front, 1 unit up
            if (!smoke_) {
                smokeOffset_[0] = 0; smokeOffset_[1] = (int32_t)(((int64_t)hy * 3) >> 2); smokeOffset_[2] = 0x1000;
                int32_t p[3];
                localToWorld(smokeOffset_, p);
                smoke_ = TheParticles().add<SmokeEmitter>(p, 10);
                smokeLife_ = -1;
            }
            if ((frame & 7) == 0) {
                int32_t v[3] = {mulq(vel[0], 0xF5), mulq(vel[1], 0xF5), mulq(vel[2], 0xF5)};
                if (isqrt((int64_t)v[0] * v[0] + (int64_t)v[1] * v[1] + (int64_t)v[2] * v[2]) < 0x6001) {
                    int32_t lx = mulq(hx, -0x199), ly = mulq(hy, -0x199);
                    smokeVel_[0] = (int16_t)(Rand32Critical((uint32_t)(mulq(hx, 0x199) - lx)) + lx + v[0]);
                    smokeVel_[1] = (int16_t)(Rand32Critical((uint32_t)(mulq(hy, 0x199) - ly)) + ly + v[1]);
                    smokeVel_[2] = (int16_t)v[2];
                    smokeColour_ = health_ < 0x1F ? 3 : health_ < 0x50 ? 2 : health_ < 0x82 ? 1 : 0;
                }
            }
        } else if (smoke_) { TheParticles().remove(smoke_); smoke_ = nullptr; }
        if (health_ < 0x1F) {   // burning: blows up when the timer runs out (8 seconds)
            if (burnTimer_ != 0) burnTimer_ = (int16_t)(burnTimer_ - 2);
            else { health_ = 0; setDead(); }
        }
    } else if (burnTimer_ > 0) burnTimer_ = (int16_t)(burnTimer_ - 2);
    if (smoke_) {   // cSmoke::Process: follows its car, a puff every 8 frames, gone when its time is up
        int32_t p[3];
        localToWorld(smokeOffset_, p);
        smoke_->setPos(p);
        if (smokeLife_ >= 0) {
            smokeLife_ = (int16_t)(smokeLife_ - 2);
            if (smokeLife_ <= 0) { TheParticles().remove(smoke_); smoke_ = nullptr; return; }
        }
        if ((frame & 7) == 0) smoke_->addParticle(smokeVel_, smokeColour_);
    }
}

// ============================================================================================== lights
void Vehicle::renderLights(const WorldCamera& cam, uint32_t frame, bool night, Collision* col) const {
    if (dead_) return;
    bool driver = engineOn;
    auto world = [&](int32_t x, int32_t y, int32_t z, float out[3]) {
        int32_t l[3] = {x, y, z}, w[3];
        localToWorld(l, w);
        out[0] = w[0] / 4096.f; out[1] = w[1] / 4096.f; out[2] = w[2] / 4096.f;
    };
    // rear lights at info +0xC4 (x, y, z), mirrored in x; brake lights 0.2 in and 0.2 up
    int32_t rx = info_[0], ry = info_[1], rz = info_[2];
    bool braking = (b64_ & 6) != 0 || railBraking;
    if (bike_) {   // cBike::RenderBrakeLights: one lamp in the middle, lying in the bike's plane, only while braking
        if (driver && ((b64_ & 2) || railBraking)) {
            uint32_t colour = (b64_ & 4) ? 0xB4F8F8F8u : 0xB40000F8u;
            float p[3], ax[3] = {right[0] / 4096.f, right[1] / 4096.f, right[2] / 4096.f}, ay[3] = {fwd[0] / 4096.f, fwd[1] / 4096.f, fwd[2] / 4096.f};
            world(0, ry - 0x333, rz, p);
            DrawSheetSprite(0xE, colour, p, ax, ay, 0.7f, 0.7f);
        }
    } else if (driver && braking) {
        uint32_t colour = (b64_ & 4) ? 0xB4F8F8F8u : 0xB40000F8u;   // reversing: white
        float p[3];
        world(-rx, ry - 0x333, rz + 0x333, p);
        DrawSheetSprite(0xE, colour, p, cam.right, cam.up, 0.7f, 0.7f);
        world(rx, ry - 0x333, rz + 0x333, p);
        DrawSheetSprite(0xE, colour, p, cam.right, cam.up, 0.7f, 0.7f);
    }
    if ((driver && indicators) || (!driver && indicators == 3)) {   // blinking, 8 frames on / 8 off
        if (8 < ((frame + uid) & 0xF)) {
            float p[3];
            if (indicators & 1) {
                world(-0x199 - rx, ry - 0x333, rz + 0x333, p);
                DrawSheetSprite(0xE, 0xB478B0F8u, p, cam.right, cam.up, 0.7f, 0.7f);
                world(-0x199 - rx, 0x333 - ry, rz + 0x333, p);
                DrawSheetSprite(0xE, 0xB478B0F8u, p, cam.right, cam.up, 0.7f, 0.7f);
            }
            if (indicators & 2) {
                world(rx + 0x199, ry - 0x333, rz + 0x333, p);
                DrawSheetSprite(0xE, 0xB478B0F8u, p, cam.right, cam.up, 0.7f, 0.7f);
                world(rx + 0x199, 0x333 - ry, rz + 0x333, p);
                DrawSheetSprite(0xE, 0xB478B0F8u, p, cam.right, cam.up, 0.7f, 0.7f);
            }
        }
    }
    // headlights at night (UpdateHeadLights / HeadLightsOn: info +0x8E bit 8): a pool of light where a ray from each
    // lamp, 3 units ahead and 1.2 (1.0) down, meets the ground
    if (driver && night && (infoFlags8e_ >> 8 & 1) && col) {
        for (int side = 0; side < (bike_ ? 1 : 2); ++side) {   // (cBike::RenderHeadlights: the one lamp)
            int32_t l[3] = {info_[3] + side * 0x2000, info_[4], info_[5]}, a[3];
            localToWorld(l, a);
            int32_t drop = side == 0 ? 0x1333 : 0x1000;
            int32_t b[3] = {a[0] + fwd[0] * 3, a[1] + fwd[1] * 3, a[2] + fwd[2] * 3 - drop};
            Collision::Ground gnd = col->ground(b[0] / 4096.f, b[1] / 4096.f, a[2] / 4096.f);
            float gz = gnd.z;
            float az = a[2] / 4096.f, bz = b[2] / 4096.f;
            if (bz > gz || az <= gz) continue;   // (the game's GetLineCollision; here the ground only)
            float t = (az - gz) / (az - bz);
            float hit[3] = {a[0] / 4096.f + (b[0] - a[0]) / 4096.f * t, a[1] / 4096.f + (b[1] - a[1]) / 4096.f * t, gz + 0.25f};   // lifted a quarter of the (flat) normal
            float d = std::sqrt((hit[0] - a[0] / 4096.f) * (hit[0] - a[0] / 4096.f) + (hit[1] - a[1] / 4096.f) * (hit[1] - a[1] / 4096.f) +
                                (hit[2] - az) * (hit[2] - az));
            float u = std::min(1.f, d / 3.f);   // (dist << 32) / 6 >> 31
            float size = 1.f + u * 3.5f;   // (u * 0x3800 + 0x1000000) >> 12, half extent
            uint32_t alpha = (uint32_t)((1.f - u) * 5.f / 256.f * 4096.f);
            if (bike_) { size = 2.f + u * 2.f; alpha = (uint32_t)((1.f - u) * 120.f); }   // u x 2 + 2, (1 - u) x 0x78000
            static const float ax[3] = {1, 0, 0}, ay[3] = {0, 1, 0};   // HELPERGetRayCollision: matrix from the ground normal
            DrawSheetSprite(0xE, alpha << 24 | 0xA9F5FFu, hit, ax, ay, size, size);
        }
    }
}

// ============================================================================================== render
void Vehicle::render(const Model* m) const {
    if (!m || m->verts.empty()) return;
    // cVehicleModelInstance: the model at the entity matrix (rows right / forward / up), batches of this palette
    float M[16];
    modelMatrix(M);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glMultMatrixf(M);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glCullFace(GL_BACK);
    glEnable(GL_NORMALIZE);
    const float s = m->scale;
    uint32_t inst = 1u << palette;
    // cVehicle::UpdateDoorMatrix: an open door's node = RotZ(angle x 64) x its initial matrix (right-hand doors
    // turn the other way); the hierarchy is resolved again like cModelInstance::RefreshMatrices
    std::vector<NodeMatrix> world;
    const std::vector<NodeMatrix>* W = &m->world;
    bool anyDoor = false;
    for (int i = 0; i < 4; ++i) anyDoor |= doors_[i].angle != 0 && doorNode_[i] > 0 && doorNode_[i] < (int)m->nodes.size();
    if (anyDoor) {
        std::vector<NodeMatrix> local = m->nodes;
        for (int i = 0; i < 4; ++i) {
            int n = doorNode_[i];
            if (!doors_[i].angle || n <= 0 || n >= (int)local.size()) continue;
            int a = ((i & 1) ? -doors_[i].angle : doors_[i].angle) * 64;
            float c = fastsin((int16_t)a + 0x4000) / 4096.f, sn = fastsin((int16_t)a) / 4096.f;
            // (the game's row-vector RotZ; this port applies node matrices as columns, hence the transpose)
            float Rz[3][3] = {{c, sn, 0}, {-sn, c, 0}, {0, 0, 1}};
            NodeMatrix L = m->nodes[n];
            for (int r = 0; r < 3; ++r)
                for (int k = 0; k < 3; ++k) local[n].r[r][k] = Rz[r][0] * L.r[0][k] + Rz[r][1] * L.r[1][k] + Rz[r][2] * L.r[2][k];
        }
        world.resize(local.size());
        for (size_t k = 0; k < local.size(); ++k) {
            const NodeMatrix& L = local[k];
            int p = L.parent;
            if (p <= 0 || (size_t)(p - 1) >= k) { world[k] = L; continue; }
            const NodeMatrix& P = world[p - 1];
            NodeMatrix Wm = L;
            for (int r = 0; r < 3; ++r) {
                for (int c2 = 0; c2 < 3; ++c2) Wm.r[r][c2] = P.r[r][0] * L.r[0][c2] + P.r[r][1] * L.r[1][c2] + P.r[r][2] * L.r[2][c2];
                Wm.t[r] = P.r[r][0] * L.t[0] + P.r[r][1] * L.t[1] + P.r[r][2] * L.t[2] + P.t[r];
            }
            world[k] = Wm;
        }
        W = &world;
    }
    for (int pass = 0; pass < 2; ++pass)
        for (const ModelBatch& b : m->batches) {
            if (!(b.mask & inst)) continue;
            if (((b.flags & 0x08) != 0) != (pass == 1)) continue;
            if (b.flags & 0x10) glDisable(GL_CULL_FACE); else glEnable(GL_CULL_FACE);
            if (b.flags & 0x07) glEnable(GL_LIGHTING); else glDisable(GL_LIGHTING);
            glDepthMask((b.flags & 0x20) ? GL_FALSE : GL_TRUE);
            if (pass == 1) { glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glEnable(GL_ALPHA_TEST); }
            else { glDisable(GL_BLEND); glDisable(GL_ALPHA_TEST); }
            GLuint tex = Assets_Texture(b.texture);
            if (tex) { glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, tex); } else glDisable(GL_TEXTURE_2D);
            float alpha = (float)((b.alpha + b.alpha * 31) >> 8) / 31.f;
            glColor4f(1, 1, 1, alpha);
            const NodeMatrix& nm = (*W)[b.node < W->size() ? b.node : 0];
            glBegin(GL_TRIANGLES);
            for (uint32_t i = 2; i < b.count; ++i) {
                const ModelVertex* v[3] = {&m->verts[b.firstVertex + i - 2], &m->verts[b.firstVertex + i - 1], &m->verts[b.firstVertex + i]};
                if (i & 1) std::swap(v[1], v[2]);
                if (v[0]->x == v[1]->x && v[0]->y == v[1]->y && v[0]->z == v[1]->z) continue;
                if (v[1]->x == v[2]->x && v[1]->y == v[2]->y && v[1]->z == v[2]->z) continue;
                if (v[0]->x == v[2]->x && v[0]->y == v[2]->y && v[0]->z == v[2]->z) continue;
                for (int k = 0; k < 3; ++k) {
                    float p[3] = {v[k]->x * s, v[k]->y * s, v[k]->z * s};
                    float nn0[3] = {v[k]->nx / 32767.f, v[k]->ny / 32767.f, v[k]->nz / 32767.f};
                    float pp[3], nn[3];
                    for (int r = 0; r < 3; ++r) {
                        pp[r] = nm.r[r][0] * p[0] + nm.r[r][1] * p[1] + nm.r[r][2] * p[2] + nm.t[r];
                        nn[r] = nm.r[r][0] * nn0[0] + nm.r[r][1] * nn0[1] + nm.r[r][2] * nn0[2];
                    }
                    glTexCoord2f(v[k]->u / 2048.f, v[k]->v / 2048.f);
                    glNormal3fv(nn);
                    glVertex3fv(pp);
                }
            }
            glEnd();
        }
    glPopMatrix();
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_CULL_FACE);
    glDepthMask(GL_TRUE);
}
