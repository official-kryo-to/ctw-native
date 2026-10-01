// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// Street furniture that vehicles knock over (cDynamicProp::ApplyWorldForce -> UpRoot / Smash).
//
// From the game: the thresholds per prop kind (gGameDir[17], see world/props.h): a prop is uprooted when the force
// reaches its uproot force and smashed when it reaches its smash force; a smashed prop loses its lights, swaps to
// its broken model if it has one and stops colliding (cDynamicProp::Smash, SwapModel, RemovePropLights).
// Approximated here (the original moves loose props with the full rigid-body integrator):
//   - the force is taken from the vehicle's speed (f = speed in units/s x 0.3), not from the contact impulse;
//   - uprooted props fly off the vehicle and tumble, with their rotated model geometry supporting them on terrain;
//   - anchored smashed props keep their broken model at the placement, with the Smash1 bend angles and the
//     lamp's 60-frame delay before falling. Debris emitters and the full integrator are not ported yet;
//   - knocked props are put back upright once the player is far away (as the game does when sectors reload).
#pragma once
#include "world/collision.h"
#include <array>
#include <vector>

class Game;
struct Model;
struct WorldCamera;

class PropDynamics {
public:
    void checkImpacts(Game& g);            // after the vehicles have moved (each game frame)
    void update(Game& g);                  // the loose props (each game frame)
    void render(Game& g) const;            // inside the world pass
private:
    struct Loose {
        int cx, cy, index;                 // where the prop lives in the collision cells
        Collision::Prop prop;
        bool broken;                       // draw the broken model (if any)
        float pos[3], vel[3];              // world units, units/s
        float axis[2];                     // tip-over axis (horizontal)
        float tilt = 0, tiltVel = 0, maxTilt = 1.53f, heightHalf = 1.f;
        int fallDelay = 0;
        bool resting = false;
        std::vector<std::array<float, 3>> support;   // model vertices after their node transforms
    };
    void knock(Game& g, int cx, int cy, int index, const int32_t vel[3], bool smash, bool uproot);
    static const Model* drawnModel(Game& g, const Loose& l);
    static void matrix(const Loose& l, float out[16]);
    static void supportPoints(Loose& l, const Model& model);
    static bool meetGround(Game& g, Loose& l, float previousZ);
    std::vector<Loose> loose_;
    friend struct PropDynamicsTestAccess;
};
