// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// Water jets: CWaterCannon (the stream both fountains and the fire truck use) and cFountainStream.
//
// A cannon is a ring of 16 points {position, velocity (units per frame), age}. Every frame the owner puts a new
// point at the head (Update_NewInput); then Update_OncePerFrame advances the head and moves the older points:
// a new point's velocity grows by one unit along itself once (flag 2), older ones are damped x0.9, fall with
// gravity 0x10B a frame (flag 4) and die below z -10; ages count up to 25. Render draws a triangle strip from
// the newest point back, as wide as the point is old (age / 16 units), with sprite 20 of the effects sheet in
// the cannon's colour mixed 60 % toward the time cycle's ambient colour.
//
// PutOutFires walks the points from the newest back; a fire within half the point's age (at most five units, 2D)
// of one loses water health, gets a steam cloud every sixteenth frame and cuts the drawn stream off there.
#pragma once
#include <cstdint>
#include "particles.h"
#include <optional>

struct WorldCamera;
class Game;

class WaterCannon {
public:
    uint16_t flags = 0xDE;         // +0x1A4: 1 wobble up, 2 first-frame boost, 4 gravity, 8 splash, 0x10 highlights, 0x20 see-through
    uint16_t colour = 0x7F39;      // +0x1A6, RGB555
    uint8_t maxPoints = 0x11;      // +0x1A2
    void input(const int32_t pos[3], const int32_t vel[3], uint32_t frame);   // Update_NewInput
    void step();                                                              // Update_OncePerFrame
    void render(const WorldCamera& cam, uint32_t ambientRGB) const;           // Render
    void putOutFires(Game& g, bool strong);                                   // PutOutFires
private:
    int32_t pos_[16][3] = {}, vel_[16][3] = {};
    uint8_t age_[16] = {};
    int16_t head_ = 0;
    std::optional<WaterSplashEmitter> splash_;
    uint32_t frame_ = 0, audioId_ = 0;
    mutable uint8_t renderedVertices_ = 0;
    friend struct WaterTestAccess;
};

// cFountainStream: a cannon at a fixed spot fed every frame with a jet that wobbles with the frame counter (city
// emitters of type 1: straight up at 2.0; smashed hydrants: 1.5 up and away from the hit). It is also an entity
// with a 1 x 1 x 10 collision box at its base (SetCollisionPrimitiveBox, collides against peds, vehicles and
// props) that pushes whatever stands in the water (OnCollision).
class Fountain {
public:
    Fountain(const int32_t pos[3], const int16_t velocity412[3]);
    // cFountainStream::Process (the jet and PutOutFires), its OnCollision contacts, then the cannon's
    // Update_OncePerFrame
    void process(Game& g);
    void tick(uint32_t frame);    // the jet alone (no world)
    static constexpr int32_t kBoxHalf[3] = {0x1000, 0x1000, 0xA000};
    // whether a sphere (centre, radius) touches the collision box
    bool touches(const int32_t centre[3], int32_t radius) const;
    // the force (vehicles x8, props x4) or the velocity (people) the jet gives on contact: its velocity x4
    void jet(int32_t out[3], int scale) const;
    void base(int32_t out[3]) const { for (int k = 0; k < 3; ++k) out[k] = pos_[k]; }
    void render(const WorldCamera& cam, uint32_t ambientRGB) const { jet_.render(cam, ambientRGB); }
private:
    int32_t pos_[3];
    int16_t vel_[3];
    WaterCannon jet_;
    friend struct WaterTestAccess;
};
