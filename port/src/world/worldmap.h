// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// The city: streaming blocks from game.pak, as loaded by cRenderWorld / cRenderWorldSector / cWorldModelInstance.
//
// World map header = gGameDir[0] (resource 1):
//   u16 seaWaterModel, u16 lakeWaterModel, ... ; at +8 a grid of u16 streaming-block resource ids, 59 per row
//   (row stride 0x76 bytes), 42 rows. Grid cell (c, r) covers sectors x = 2c..2c+1, y = 2r..2r+1 (0xFFFF = none).
//   Sectors are 60 units (0x3C000 in 1/4096) square; sector (x, y) starts at (60x - 3510, 60y - 2490).
// Streaming block (one resource, 2x2 sectors):
//   +0x00  CMatrix43 (9 x i16 rotation, 1.0 = 4096; pad; 3 x i32 translation, 1/4096). Always identity rotation and
//          translation (120c - 3480, 120r - 2460, 0) - the centre of the block's first sector. All of the block's
//          models are drawn with this matrix (cWorldModelInstance::Render passes sector+0x28 = the block).
//   +0x20  u16 offset[4] of the sub-sector records, index = (x & 1) | (y & 1) << 1
//   sub-sector record at `o`: +0x28 u8, u8, +0x2A u16 instanceCount, +0x2C u16 n20a, +0x2E u16 groupCount,
//          +0x30 u16 n20b, +0x32 u16 textureCount, then from +0x34:
//          groupCount x 16 bytes   {i16 bounds[6], u16 instances in group, u16 flags}
//          instanceCount x 16 bytes (cModelInstanceInfoSerialize) {u16 slot, u8 renderList, u8 swappable,
//                                   u32 modelId, u32 modelOffset (from block start), u32 runtime}
//          n20a x 20 bytes (not decoded yet), n20b x 20 bytes world lights (WorldLight),
//          textureCount x u16 texture resource ids
//   Models are ordinary cModel data embedded in the block; their bounding boxes are absolute world coordinates.
//   The same object can be listed by neighbouring blocks (same slot, same model and bounds): it is drawn once.
//   modelId == seaWaterModel / lakeWaterModel marks water: instead of the model, cWaterRenderBlock draws a
//   20 x 20 tile at the centre of its bounds (sea z -7.5, lake z -2.5).
#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

struct WorldVertex { float x, y, z, nx, ny, nz, u, v; };

struct WorldDraw {                 // one texture/material run inside a block mesh (GL_TRIANGLES)
    uint16_t texture;
    uint8_t flags;                 // cModel batch flags (0x07 lit, 0x08 translucent, 0x10 no cull, 0x20 no z-write)
    uint8_t list;                  // bucket render list: 0 = opaque, else from the instance (cBucketManager::Draw)
    float alpha;
    uint32_t first, count;
    uint32_t mask = 0xFFFFFFFFu;   // animated batches: drawn when (mask & anims[anim].current) != 0
    int anim = -1;                 // index into WorldBlockMesh::anims, -1 = static
};

// Texture/visibility animation of a model (its A blocks of 192 bytes: u32 mask[32], i16 frames[32], negative =
// unused). cWorldModelInstance::Render steps once per rendered frame (30 fps): when the counter runs out it moves
// to the next slot with a non-negative duration; the visible mask is the OR of every block's current slot.
struct WorldAnim {
    struct Track { uint32_t mask[32]; int16_t frames[32]; int16_t slot = 0, counter = 0; };
    std::vector<Track> tracks;
    uint32_t current = 0;
    void step();
};

struct WaterPatch { float cx, cy, z; bool sea; };   // one 20 x 20 water tile (cWaterRenderBlock::AddRenderPos)

// Fixed world light (sub-sector records of 20 bytes: i32 x, y, z, i32 size (20.12), u16 id, u16 RGB555 colour),
// added by cRenderWorldSector::GenerateLights as light type 0 with size * 0.8 (cLight::Initalise).
struct WorldLight { int32_t x, y, z; int16_t size; uint16_t colour, id; };

using WorldObjectKey = std::array<int32_t, 7>;   // slot + absolute bounds: identifies an object across blocks

struct WorldBlockMesh {
    std::vector<WorldVertex> verts;
    std::vector<WorldDraw> draws;
    std::vector<WaterPatch> water;
    std::vector<WorldAnim> anims;
    std::vector<WorldLight> lights;
    std::vector<WorldObjectKey> objects;   // every object this block lists (drawn or not)
    int instances = 0, drawn = 0;
};

class WorldMap {
public:
    static const int kCols = 59, kRows = 42;
    bool init();                                   // reads the map header from game.pak
    int blockId(int c, int r) const;               // resource id or -1
    static void blockCentre(int c, int r, float* x, float* y);   // centre of the 2x2-sector block
    // Builds the mesh of block (c, r). Objects whose key is in `skip` are left out (another block draws them).
    bool build(int c, int r, const std::map<WorldObjectKey, int>& skip, int self, WorldBlockMesh& out) const;
private:
    std::vector<uint16_t> grid_;
    uint16_t seaModel_ = 0, lakeModel_ = 0;
};
