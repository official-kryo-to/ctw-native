// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#pragma once
#include "world/collision.h"
#include "world/props.h"
#include <cstdint>

// cPhysical and the full cPhysicalIntegrator path for dynamic props. Positions, velocities, quaternion,
// inertia and forces use the original Q12 arithmetic, including the game's negated angular convention.
class RigidBody {
public:
    struct Rec { int32_t p[3], n[3], t = 0, depth = 0; int surface = 0; };
    void init(const Collision::Prop& prop, const PropLibrary::Physics& primitive, int32_t mass, bool planar);
    void applyWorldForce(const int32_t point[3], const int32_t force[3], int type = 8);
    void recalcKinematics();
    void integrateStep(int32_t fraction);
    void process(Collision& collision);
    void damp();
    void matrix(float out[16]) const;
    void spheres(int32_t out[6][4], int& count) const;
    void bboxVerts(int32_t out[8][3]) const;
    void worldCG(int32_t out[3]) const;
    void worldPosition(const int32_t local[3], int32_t out[3]) const;
    void velocityAt(const int32_t point[3], int32_t out[3]) const;
    int32_t impactTerm(const int32_t normal[3], const int32_t arm[3]) const;
    int32_t speed() const;
    void rotate(const int32_t axis[3], int16_t angle);
    void setToPhysics(bool active);
    void setLocalCG(const int32_t centre[3]);
    bool active() const { return physics_; }
    bool contact() const { return collided_; }
    int32_t pos[3]{}, vel[3]{}, angVel[3]{};
    int32_t mass_ = 0, invMass_ = 0;
    int32_t hx = 0, hy = 0, hz = 0;
    int32_t cgLocal_[3]{}, collOffset_[3]{};
private:
    void syncFromIntegrator();
    void fullCollision(Collision* collision, bool meshNear);
    void fullSprings(Collision* collision);
    void calcImpactEnv(const Rec& record, bool friction);
    void springImpact(const Rec& record);
    int32_t q_[4] = {0,0,0,4096};
    int32_t cgWorld_[3]{}, invInertia_[3]{}, invIWorld_[3][3]{};
    int16_t right[3] = {4096,0,0}, fwd[3] = {0,4096,0}, up[3] = {0,0,4096};
    int32_t force_[3]{}, torque_[3]{};
    int32_t sphereFirst_[3]{}, sphereStep_[3]{}, sphereR_ = 2048;
    int sphereCount_ = 2;
    bool physics_ = false, planar_ = false, collided_ = false, frozen_ = false;
    friend struct PropDynamicsTestAccess;
};
