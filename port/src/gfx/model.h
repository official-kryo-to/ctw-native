// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// cModel resource (game.pak). Layout, verified against cModel::OnLoad/LoadTextures, cModelInstance::Render/AddToRenderList
// and cBucketManager::Render, and against all 40 vehicle models in the game data:
//   0x00  16-byte header: 'M','G', u16 A, u8 B, u8 C, u16 D, ...
//   0x10  block 0 (32 bytes): i32[3] bbox min, i32[3] bbox max (fixed point 1/4096), f32 scale (1/64), f32 1/scale
//   0x30  D vertices, 16 bytes each, all int16: x y z | nx ny nz | u v   (position = value * scale, normal = value / 32767)
//   ....  (B-1) node blocks, 32 bytes each: 3x3 rotation (i16, 1.0 = 4096), i8 parent, u8 ?, i32[3] translation (1/4096)
//         parent p: 0 = none, otherwise the node is relative to node p-1 (node 0 = model root);
//         world = local * world[p-1]   (cModelInstance::RefreshMatrices / ReparentNode)
//   ....  C draw records, 12 bytes each: u16 texture, u16 vertexCount, u8 flags, u8 node, u8 alpha, u8 ?, u32 visibilityMask
//         flags: 0x07 lit, 0x08 translucent, 0x10 no backface culling, 0x20 no depth write (cBucketManager::Render)
//   ....  A blocks of 192 bytes (not decoded)
// Batches consume the vertex array sequentially (their counts sum to D exactly); vertex data is stitched triangle strips.
// A batch is transformed by node matrix `node` (node 0 = the model itself; node k>0 = node block k-1: wheels, rotors, ...).
#pragma once
#include <cstdint>
#include <vector>

struct ModelVertex { int16_t x, y, z, nx, ny, nz, u, v; };

struct ModelBatch {
    uint16_t texture, count;
    uint8_t flags, node, alpha, unk;
    uint32_t mask;
    uint32_t firstVertex;
};

struct NodeMatrix {
    float r[3][3];   // rotation, 1.0 = identity
    float t[3];      // translation in model units
    int parent = 0;  // see above
};

struct Model {
    float bboxMin[3], bboxMax[3], scale;
    std::vector<ModelVertex> verts;
    std::vector<ModelBatch> batches;
    std::vector<NodeMatrix> nodes;   // local matrices: nodes[0] = identity (root), nodes[k] = node block k-1
    std::vector<NodeMatrix> world;   // nodes resolved through their parents (model space)
    bool parse(const std::vector<uint8_t>& raw);
};
