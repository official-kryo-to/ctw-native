// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// Street furniture: lamp posts, bins, benches, hydrants, signs ... (cDynamicPropManager, cLightManager::AddPropLights).
//
// Placement: section 6 of each world.bin collision cell (cWorldSector +0x120), u32 count then 20-byte
//   sPackedPropData {u16 prop, u16 kind, i16 heading, u16 state (runtime), i32 x, y, z}.
// Definitions: gGameDir[16] (cDynamicPropManager::LoadPropsData), one per prop until 0xDEADBEEF:
//   u16 model (resource id), u16 broken model (0xFFFF = none), u16 0xDEAD, u8 flags, u8 shape count, then shapes
//   {u32 type, ...}: 1 box {offset x y z (bottom centre), size x y z}, 2 cylinder {offset (base), radius, height},
//   3 sphere {offset (centre), radius}, 4 mesh header, 5 mesh vertex, 6 mesh triangle (16 bytes each).
// Lights: gGameDir[19] (cLightManager::LoadDynamicLightData), 28 bytes each {u16 prop, u8 type, u8,
//   offset x y z, u32 size, u32 RGB555 colour, u32}; AddPropLights adds every entry whose prop matches.
// Kinds (ePropDef, the `kind` of a placement): gGameDir[17], 16 bytes each until 0xDEAD at +14 {u8 smash type
//   (which of cDynamicProp::Smash1..4), u8, u8 health, u8 x5 flags, i16, i16 smash force, i16 uproot force, 0xDEAD};
//   forces are 20.12, negative = never. cDynamicProp::ApplyWorldForce: f = |force| * 0x111 >> 12; the prop is
//   uprooted when f >= uproot force and smashed when f >= smash force.
// All values are 20.12 fixed point; a prop is rotated about z by its heading (65536 = full turn).
#pragma once
#include "gfx/model.h"
#include "world/collision.h"
#include "world/worldmap.h"
#include <cstdint>
#include <map>
#include <memory>
#include <vector>

class PropLibrary {
public:
    bool load();                                           // needs Assets_Open
    int count() const { return (int)defs_.size(); }
    const Model* model(int prop);                          // loaded on first use; nullptr if it has none
    // The prop's collision shapes in world space (boxes and cylinders; spheres become short cylinders).
    void shapes(const Collision::Prop& p, std::vector<Collision::Box>& boxes, std::vector<Collision::Cyl>& cyls) const;
    void lights(const Collision::Prop& p, std::vector<WorldLight>& out) const;
    void draw(const Collision::Prop& p);                   // with the current GL matrices and lights
    static void drawModel(const Model& m, const float matrix[16]);   // any model at a column-major matrix
    float radius(int prop);                                // for visibility tests, world units
    const Model* brokenModel(int prop);                    // the model cDynamicProp::SwapModel uses, or nullptr
    struct Kind { uint8_t smashType, health; float smashForce, uprootForce; };   // forces < 0: never
    const Kind* kind(int k) const { return k >= 0 && k < (int)kinds_.size() ? &kinds_[k] : nullptr; }
    // Footprint for impact tests: radius around the position and height, world units (0 = not solid).
    void footprint(int prop, float& radius, float& height) const;
private:
    struct Shape { uint32_t type; int32_t v[6]; };
    struct Def { uint16_t model, broken; uint8_t flags; std::vector<Shape> shapes; };
    struct Light { uint16_t prop; uint8_t type; int32_t offset[3]; int32_t size; uint16_t colour; };
    std::vector<Def> defs_;
    std::vector<Light> lights_;
    std::vector<Kind> kinds_;
    std::map<int, std::unique_ptr<Model>> models_;
};

// Rotates a local offset (20.12) by the prop's heading and adds its position.
void PropToWorld(const Collision::Prop& p, const int32_t local[3], int32_t out[3]);
