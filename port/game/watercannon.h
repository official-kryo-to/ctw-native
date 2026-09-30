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
#pragma once
#include <cstdint>

struct WorldCamera;

class WaterCannon {
public:
    uint16_t flags = 0xDE;         // +0x1A4: 1 wobble up, 2 first-frame boost, 4 gravity, 8 splash, 0x10 highlights, 0x20 see-through
    uint16_t colour = 0x7F39;      // +0x1A6, RGB555
    uint8_t maxPoints = 0x11;      // +0x1A2
    void input(const int32_t pos[3], const int32_t vel[3], uint32_t frame);   // Update_NewInput
    void step();                                                              // Update_OncePerFrame
    void render(const WorldCamera& cam, uint32_t ambientRGB) const;           // Render
private:
    int32_t pos_[16][3] = {}, vel_[16][3] = {};
    uint8_t age_[16] = {};
    int16_t head_ = 0;
};

// cFountainStream: a cannon at a fixed spot fed every frame with an upward jet (2.0 units a frame) that wobbles
// with the frame counter (city emitters of type 1).
class Fountain {
public:
    Fountain(const int32_t pos[3], const int16_t velocity412[3]);
    void tick(uint32_t frame);    // cFountainStream::Process, then the cannon's Update_OncePerFrame
    void render(const WorldCamera& cam, uint32_t ambientRGB) const { jet_.render(cam, ambientRGB); }
private:
    int32_t pos_[3];
    int16_t vel_[3];
    WaterCannon jet_;
};
