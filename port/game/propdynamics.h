// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#pragma once
#include "rigidbody.h"
#include "watercannon.h"
#include <optional>
#include <map>
#include <vector>

class Game;
class Vehicle;
struct Model;

// cDynamicProp::ApplyWorldForce / UpRoot / Smash / ProcessAlways, with cPhysical's full integrator.
class PropDynamics {
public:
    void checkImpacts(Game& game);
    void explosion(Game& game, const struct ExplosionBlast& blast);
    // cFountainStream::OnCollision for knocked-over props in the water: ApplyWorldForce(base, 4 x jet, type 1)
    void waterJet(Game& game, const Fountain& fountain);
    void update(Game& game);
    void render(Game& game) const;   // between the last two ticks when game.interpolate is on
    int looseCount() const { return (int)loose_.size(); }   // the debug overlay
    // PC option (mods: set_prop_physics), off by default. The original keeps many props in place when hit: lamps
    // and signs bend to a fixed angle, hydrants tilt about their base, benches and kiosks only tilt or break. With
    // full physics every breakable prop that is hit hard enough to smash comes loose and tumbles instead (props
    // with infinite mass get kFullPhysicsMass); it still smashes, sprays and burns as the original does.
    void setFullPhysics(bool on) { fullPhysics_ = on; }
    bool fullPhysics() const { return fullPhysics_; }
    static constexpr int32_t kFullPhysicsMass = 0x2000;
    // a prop's identity for the things attached to it (fires), and where it is now
    static uint32_t key(int cx, int cy, int index) { return (uint32_t)(cx * 100 + cy) << 12 | (uint32_t)index; }
    bool position(uint32_t key, int32_t out[3]) const;   // false: not a moving body (it has not moved)
private:
    struct Loose {
        int cx, cy, index;
        Collision::Prop prop;
        RigidBody body;
        bool broken = false, uprooted = false, collidable = true;
        int lampTimer = 0, sleepCounter = 15;
        class GarbageEmitter* garbage = nullptr;   // a smashed bin spilling rubbish (effect 35)
        std::optional<Fountain> fountain; // hydrants emit at their fixed world base, not the tilted nozzle
        float previous[16]{};          // the drawn matrix at the start of the tick (render interpolation)
        bool hasPrevious = false;
    };
    Loose makeBody(Game& game, int cx, int cy, int index) const;
    void applyForce(Game& game, Loose& prop, const int32_t point[3], const int32_t force[3],
                    const int32_t* sourcePos = nullptr, const int32_t* sourceVelocity = nullptr, bool vehicleImpact = false,
                    int forceType = 8);
    // strength: Smash's uint, the force's magnitude (ApplyWorldForce) or the damage beyond the health (Damage)
    void smash(Game& game, Loose& prop, const int32_t point[3], const int32_t force[3],
               const int32_t* sourcePos, const int32_t* sourceVelocity, bool vehicleImpact, uint32_t strength);
    void setOnFire(Game& game, Loose& prop);   // cDynamicProp::SetOnFire
    void damage(Game& game, Loose& prop, int amount, int kind, const int32_t direction[3]);
    int health(const Game& game, const Loose& prop) const;
    void smashParticles(Game& game, Loose& prop, const int32_t point[3], const int32_t force[3], int effect,
                        const int32_t* sourcePos, const int32_t* sourceVelocity, bool vehicleImpact,
                        uint32_t strength);
    bool hit(Game& game, Vehicle& car, Loose& prop);
    bool hit(Game& game, Loose& a, Loose& b);
    template<class A, class B>
    static bool contactForces(A& a, B& b, const int32_t sphereA[4], const int32_t sphereB[4],
                              int32_t point[3], int32_t forceA[3], int32_t forceB[3]);
    static const Model* drawnModel(Game& game, const Loose& prop);
    std::vector<Loose> loose_;
    bool fullPhysics_ = false;
    struct DamageState { int8_t health; int32_t pos[3]; };
    // Partial damage to standing furniture must not replace its static shapes or create a second body.
    std::map<std::pair<int,int>, DamageState> damaged_;
    friend struct PropDynamicsTestAccess;
};
