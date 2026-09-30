// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// Peds (people) are layered, animated, flat sprites seen from above (cSpriteFrameManager / cPedSprite).
//
// Animation table = gGameDir[7] (resource 2101): u16 offsets (in 4-byte units) at +2 + anim*2; per animation
//   u16 frames, i16 rate (24.8, frames per step), u16 angle adjust (45° steps), u8 flags (bit 0 one-shot,
//   bits 4-7 body type), u8 components; then frames x components x 12-byte layers:
//   u16 texture, u8 sideOffset, u8 forwardOffset ((b - 32) / 16 units), u16 u, v, w, h (texels, 16 per unit).
// 550 animations = 2 body sets (0, 1 = male) x 275; each ped draws two sprites: the legs (odd ids, 3 layers)
// and the upper body (even ids, 6 layers) - cPed::AnimateWalkRunCycle picks the pair from the movement state:
//   stand 0/1, walk 2/3, run 4/5, player sprint 0xA0/0xA1 (+ 275 for body set 1).
// Colours: palettes = gGameDir[8] (2395), 16 RGB555 colours per palette; a layer's colour slot comes from a
// per-body-type table (DAT_00586a8c). cPed::SetColour(a, b): upper body uses palette a, legs palette b (the
// first layer of the legs uses a). The player uses palettes 4 and 5 (4 is never given to other peds).
// Heights (cPedSprite::mZPositions, pose 0 = on foot): upper body 2.0 (first layer) / 1.5, legs 1.0 / 0.5 above
// the feet, each layer lowered by sSpriteOffset (-204/4096) times its index.
// Daylight shading (cPedSprite::DrawPedPrim): from 07:00 to 20:00 each corner of a layer is pulled from its
// palette colour towards the ambient colour (cTimeCycle::Colour(0xD)) by dot(corner direction from the quad
// centre, light direction) x the day factor (0 -> 1 over 07:00-08:00, 1 until 19:00, back to 0 by 20:00).
#pragma once
#include <cstdint>
#include <vector>

class TimeCycle;

struct PedLight {
    static PedLight fromTimeCycle(const TimeCycle& cycle);
    void cornerColour(const float corner[3], const float centre[3], uint32_t base, uint8_t out[4]) const;
    static bool shadesLayer(int body, int component) { return (body & 1) || component != 0; }
    float dir[3] = {0, 0, 1};   // TimeCycle::sunDirection
    float day = 0;              // mDayColourFactor, 0..1
    uint32_t ambient = 0;       // cTimeCycle::Colour(0xD), 0xAABBGGRR
    bool on = false;            // time between 07:00 and 20:00
};

class PedSprites {
public:
    bool init();   // loads the animation table and palettes (needs Assets_Open)
    bool ok() const { return !anims_.empty(); }

    int numFrames(int anim) const;
    int rate(int anim) const;               // 24.8
    bool oneShot(int anim) const;

    // cPedSprite::Animate: frame (24.8) advanced by rate * step >> 8, wrapping (or holding for one-shots).
    int advance(int anim, int frame, int step) const;

    // Draws one sprite (all layers of `anim` at `frame` (24.8)) at the feet position, facing heading
    // (radians, 0 = +y), with palettes `palA` (first layers, higher) and `palB` (the rest).
    void draw(const float pos[3], float heading, int anim, int frame, int palA, int palB, float zA, float zB,
              const PedLight* light = nullptr, bool flip = false) const;
    // cPedSprite::AnimateHelper for one-shot animations: true once the last frame has been reached
    bool advanceOneShot(int anim, int& frame, int step) const;

private:
    const uint8_t* animData(int anim) const;
    std::vector<uint8_t> anims_, palettes_;
};
