// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// cTrailManager (gSimpleTrails) / cSimpleTrail: twelve trails of up to twelve points each, owned by an emitter
// (a weak pointer: a trail whose owner is gone is free again). Points are kept in eighths of the coordinates
// relative to the trail's origin, which Render moves to the owner each time it draws. A trail draws the points from
// the newest back (the oldest slot is never drawn, and at most "segments" of them) as a strip of effect sprite 20
// "width" wide across its heading, optionally halving the width each point; the colour is RGB555 at full alpha,
// blended additively or ordinarily. Drifting trails spread their points out and lift them while they last; a
// stopped trail counts down its fade.
#pragma once
#include <cstdint>

class Emitter;
struct WorldCamera;

class Trails {
public:
    int getFree(const Emitter* owner);   // GetFreeTrail: the first free slot, owned by owner, or -1
    // InitTrail(id, pos, dir, colour, width, a, taper, drift, d, segments, additive)
    void init(int id, const int32_t pos[3], const int32_t dir[3], uint16_t colour, int32_t width, bool a, bool taper,
              bool drift, int d, int segments, bool additive);
    void addPoint(int id, const int32_t pos[3]);   // AddPointToTrail
    void release(int id);                          // ReleaseTrail
    void stop(int id);                             // StopTrail
    void forget(const Emitter* owner);             // the weak pointers of an emitter being deleted
    void process();                                // Process
    void render();                                 // Render (it moves each trail's origin to its owner)
    void reset();                                  // Reset
    static int32_t fallingEpsilon;                 // sFallingEpsilon
private:
    struct Trail {
        int32_t origin[3] = {0, 0, 0};   // +0x00: eighths
        int32_t pts[12][3] = {};         // +0x0C: eighths, relative to the origin
        int16_t heading = 0;             // +0x9C
        int16_t width = 0;               // +0x9E
        uint16_t colour = 0;             // +0xA0
        int8_t head = 0, tail = 0;       // +0xA2 / +0xA3: oldest slot, next slot to write
        uint8_t fade = 0;                // +0xA4
        bool stopped = false;            // +0xA5
        bool a = false, taper = false, drift = false;   // +0xA6..+0xA8
        uint8_t d = 0, segments = 0;     // +0xA9, +0xAA
        bool additive = false;           // +0xAB
    };
    void clear(int id);
    void renderTrail(Trail& t, const int32_t at[3]) const;   // cSimpleTrail::Render
    Trail trails_[12];
    const Emitter* owners_[12] = {};
    friend struct TrailsTestAccess;
};

Trails& TheTrails();
