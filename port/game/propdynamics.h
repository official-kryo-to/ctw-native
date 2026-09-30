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
//   - smashed props fall over about their base away from the vehicle, uprooted props fly off it and tumble;
//     both settle lying on the ground, and a dust puff stands in for the debris particles;
//   - knocked props are put back upright once the player is far away (as the game does when sectors reload).
#pragma once
#include "world/collision.h"
#include <vector>

class Game;
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
        bool resting = false;
    };
    void knock(Game& g, int cx, int cy, int index, const int32_t vel[3], bool smash);
    std::vector<Loose> loose_;
};
