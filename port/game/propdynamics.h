// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#pragma once
#include "rigidbody.h"
#include <vector>

class Game;
class Vehicle;
struct Model;

// cDynamicProp::ApplyWorldForce / UpRoot / Smash / ProcessAlways, with cPhysical's full integrator.
class PropDynamics {
public:
    void checkImpacts(Game& game);
    void update(Game& game);
    void render(Game& game) const;
private:
    struct Loose {
        int cx, cy, index;
        Collision::Prop prop;
        RigidBody body;
        bool broken = false, uprooted = false, collidable = true;
        int lampTimer = 0, sleepCounter = 15;
    };
    Loose makeBody(Game& game, int cx, int cy, int index) const;
    void applyForce(Game& game, Loose& prop, const int32_t point[3], const int32_t force[3]);
    bool hit(Game& game, Vehicle& car, Loose& prop);
    bool hit(Game& game, Loose& a, Loose& b);
    template<class A, class B>
    static bool contactForces(A& a, B& b, const int32_t sphereA[4], const int32_t sphereB[4],
                              int32_t point[3], int32_t forceA[3], int32_t forceB[3]);
    static const Model* drawnModel(Game& game, const Loose& prop);
    std::vector<Loose> loose_;
    friend struct PropDynamicsTestAccess;
};
