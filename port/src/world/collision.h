// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// World collision (CCollision + cWorld / cWorldSector), from world.bin in ROM.WAD.
//
// world.bin (cWorld::Init / UpdateStreaming): u32 per collision cell, index x * 100 + y, cells 50 x 50 units from
// (-3500, -2500) (wv2d cells). Entry bits 0-16 = data offset / 64, bits 17-23 = compressed size / 64,
// bits 24-30 = extra / 64 (uncompressed size = (compressed + extra) * 64). The data is a zlib stream
// (UnCompressSector). Uncompressed, a chain of sections {u32 byteCount, data} (cWorldSector::DataLoaded):
//   +0xF0 boxes      u32 count, count x 28 bytes {i32 cx, cy, cz, halfX, halfY, halfZ, i16 angle, i16 ?}
//   +0xF8 cylinders  u32 count, count x 24 bytes {i32 x, y, zBase, radius, height, ?}
//   +0x100 meshes    u32 count, meshes {i32 minX, minY, maxX, maxY, u16 nVerts, u16 nTris,
//                    nVerts x i32[3], nTris x 40-byte triangles} - triangle: i32[3] plane point, u8 v0 v1 v2,
//                    u8 radius, i16 normal[3], i16 edgeNormal[3][3]; point/radius/normals are computed at load.
//   +0x108 .. +0x118 (pickups, ... - not used yet)
//   +0x120 props: u32 count, count x 20 bytes sPackedPropData (see props.h); their collision shapes are added to
//          the cell when a PropLibrary is set
//   +0x128 car generators (cCarGenManager::SpawnAllCarGensInSector): u32 count, count x 20 bytes sPackedCarGenData
//          {i32 x, y, z, i16 heading, u8 vehicle (info index), u8 flags, u8 colour (25 = random), 3 x pad};
//          flags: 1 random vehicle, 2 random heading (+-0x38E), 4 random position (+-0.2), bits 3-5 chance,
//          0x40 only where no player can see it, 0x80 needs the script flag (save game +0x64 & 0x3000)
//   +0x130 city emitters (cCityEmitters::SpawnAllEmittersInSector): u32 count, count x 16 bytes {u8 type, 3 x pad,
//          i32 x, y, z}; type 0 = steam (cParticleEmitterSteam), 1 = fountain (cFountainStream)
//   +0x138 (attractors - not used yet)
//   +0x140 ground map: 40 x 40 x 2 bits (1.25-unit squares): 1 = land (z 0), 2 = lake (z -2.5), else sea (-7.5)
//   +0x148 (not used yet)
// All values are 20.12 fixed point; the queries below take and return floats in world units.
#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

class Collision {
public:
    bool init(const std::string& dataDir);
    bool ok() const { return !world_.empty(); }

    struct Ground { float z; int surface; float normal[3]; };   // surface: 0 = solid, 2 = water (CCollision)
    // CCollision::GetGround: highest surface at (x, y) that is not more than 0.5 above z.
    Ground ground(float x, float y, float z);

    // Pushes a vertical cylinder (feet at z, given radius and height) out of boxes, cylinders and steep triangles.
    // Returns true if anything was hit.
    bool pushOut(float& x, float& y, float z, float radius, float height);

    size_t cachedCells() const { return cells_.size(); }

    struct CarGen { int32_t x, y, z; int16_t heading; uint8_t vehicle, flags, colour, pad[3]; };   // sPackedCarGenData
    static_assert(sizeof(CarGen) == 20, "sPackedCarGenData");
    const std::vector<CarGen>* carGens(int cx, int cy);   // the car generators of a 50-unit cell (nullptr: none)
    static void cellOfPos(int32_t x, int32_t y, int& cx, int& cy);

    struct Prop { uint16_t prop, kind; int16_t heading; uint16_t state; int32_t x, y, z; };   // sPackedPropData; kind 1 uses the garage controller
    static_assert(sizeof(Prop) == 20, "sPackedPropData");
    const std::vector<Prop>* props(int cx, int cy);        // the props of a 50-unit cell (nullptr: none)
    struct CityEmitter { uint8_t type, pad[3]; int32_t x, y, z; };                     // sSectorEmitterData
    static_assert(sizeof(CityEmitter) == 16, "sSectorEmitterData");
    const std::vector<CityEmitter>* emitters(int cx, int cy);
    // cSectorNodeData (sector chunk 3): the pavement network peds wander on. Nodes use the cBaseNode layout of the
    // road network (positions in 1/8 units, z in 1/2 units, links in links[first .. first + count)). Bridge nodes
    // (flag bit 7) sit on the sector edge; bridges lists {node, neighbouring sector index (y * 140 + x)} and the
    // neighbour has a bridge node at the same 2D position (cSectorNodeData::ResolveBridgeNodeId).
    struct PathNode { uint16_t first, flags; int16_t x, y; int8_t z; uint8_t cover; };
    static_assert(sizeof(PathNode) == 10, "cBaseNode");
    struct PedPaths { std::vector<PathNode> nodes; std::vector<uint16_t> links; std::vector<std::pair<uint16_t, uint16_t>> bridges; };
    const PedPaths* pedPaths(int cx, int cy);   // nullptr: no pavement nodes in this 50-unit cell
    void setPropLibrary(const class PropLibrary* library) { propLibrary_ = library; }
    // Runtime state of a prop (0 = standing; the game sets others when one is knocked over or smashed). A prop that
    // is not solid keeps its shapes aside until it is made solid again.
    void setPropState(int cx, int cy, int index, uint16_t state);
    void setPropSolid(int cx, int cy, int index, bool solid);

    // Debug: wireframes of the boxes (yellow), cylinders (cyan) and mesh triangles (magenta) within `range`.
    void debugDraw(float x, float y, float range);

    // ---- the swept-sphere queries peds and cameras use (collisionsweep.cpp), all 20.12 fixed point ----------
    struct Box { int32_t cx, cy, cz, hx, hy, hz; int16_t angle; uint16_t flags; };   // CCollisionBox (28 bytes)
    struct Cyl { int32_t x, y, z, r, h, pad; };                                       // CCollisionCylinder
    struct Tri { int32_t p[3]; uint8_t v[3]; uint8_t radius; int16_t n[3]; int16_t en[3][3]; };   // SCollisionTriangle
    struct TriRef { const Tri* tri; const int32_t* verts; };                         // sTriangleWithVerts

    // Shapes within radius, including neighbouring placement cells. Dynamic props are not duplicated across
    // cells and must not be dropped by the original fixed-size candidate buffers. groundSlab adds CCollision::mBox:
    // a 200 x 200 x 1 box whose top is the ground under p (GetGround), used while the ped moves vertically.
    struct Candidates { std::vector<const Box*> boxes; std::vector<const Cyl*> cyls; std::vector<TriRef> tris; Box slab; };
    void candidates(const int32_t p[3], int32_t radius, bool groundSlab, Candidates& out);

    // Swept sphere a -> b (radius r) against one shape: t = fraction of the move (0..0x1000) at first contact,
    // hit = the contact point on the shape (CCollision::SweptSphereVBox / VCylinder / VTri).
    static bool sweptSphereVBox(const int32_t a[3], const int32_t b[3], int32_t r, const Box& box, int32_t hit[3], int32_t& t);
    static bool sweptSphereVCylinder(const int32_t a[3], const int32_t b[3], int32_t r, const Cyl& c, int32_t hit[3], int32_t n[3], int32_t& t);
    static bool sweptSphereVTri(const int32_t a[3], const int32_t b[3], int32_t r, const TriRef& tr, int32_t hit[3], int32_t n[3], int32_t& t);
    // Static overlap (CCollision::SphereVBox / SphereVTri): contact point, push-out normal (Q12) and depth.
    static bool sphereVBox(const int32_t c[3], int32_t r, const Box& box, int32_t contact[3], int32_t n[3], int32_t& depth);
    static bool sphereVTri(const int32_t c[3], int32_t r, const TriRef& tr, int32_t contact[3], int32_t n[3], int32_t& depth);

    // Vehicles (cPhysicalIntegrator):
    // CCollision::CircleVBox: a vertical circle at c (height h below c) against a box, in the box's plane.
    static bool circleVBox(const int32_t c[3], int32_t r, int32_t h, const Box& box, int32_t contact[3], int32_t n[3], int32_t& depth);
    // CCollision::SweptCircleVBox: the circle moving a -> b (2D), height h.
    static bool sweptCircleVBox(const int32_t a[3], const int32_t b[3], int32_t r, int32_t h, const Box& box, int32_t hit[3], int32_t& t);
    // CCollision::SweptVertVBox / SweptVertVTri: a point moving a -> b; hit point, surface normal, t.
    static bool sweptVertVBox(const int32_t a[3], const int32_t b[3], const Box& box, int32_t hit[3], int32_t n[3], int32_t& t);
    static bool sweptVertVTri(const int32_t a[3], const int32_t b[3], const TriRef& tr, int32_t hit[3], int32_t n[3], int32_t& t);
    // cPhysicalIntegrator::IsMeshNear: any collision mesh of the cell within r of (x, y).
    bool meshNear(const int32_t p[3], int32_t r);

    // Box-only boolean query; skipFlagged excludes box flag 2. Cameras use staticLine's original filter rules.
    bool lineHitsBoxes(const int32_t a[3], const int32_t b[3], bool skipFlagged);
    // GetLineIntersectWithStatics: boxes 0x200, cylinders 0x400, meshes 0x100, ground 0x800;
    // 0x1000 excludes box flag 4. 0x2000 gates each cell on an unflagged box hit,
    // ending the query if the gate fails (including any pending nearest result).
    // 0x80000000 stops at the first hit; 0x40000000 selects the nearest one. Without either,
    // the result retains the last visited hit, including the box HandleStuckCam inspects.
    struct LineHit { int32_t point[3]{}, normal[3]{}, fraction = 0; Box box{}; bool isBox = false; };
    bool staticLine(const int32_t a[3], const int32_t b[3], uint32_t flags, LineHit* hit = nullptr);
    // CCollision::GetSphereCollision against boxes (flags 0x40000200): the first box a sphere moving a -> b hits.
    bool sweptSphereHitsBoxes(const int32_t a[3], const int32_t b[3], int32_t r, int32_t contact[3], int32_t n[3]);

    struct Mesh { int32_t minX, minY, maxX, maxY; std::vector<int32_t> verts; std::vector<Tri> tris; };
    static void finishTriangle(Tri& t, const std::vector<int32_t>& verts);
private:
    struct Cell {
        std::vector<Box> boxes;
        std::vector<Cyl> cyls;
        std::vector<Mesh> meshes;
        std::vector<uint8_t> groundMap;   // 400 bytes or empty
        std::vector<CarGen> carGens;
        std::vector<Prop> props;
        struct PropShapes { uint32_t box0 = 0, boxes = 0, cyl0 = 0, cyls = 0, mesh0 = 0, meshes = 0;
                            std::vector<Box> savedBoxes; std::vector<Cyl> savedCyls; bool solid = true; };
        std::vector<PropShapes> propShapes;   // per prop: its shapes in boxes / cyls
        std::vector<CityEmitter> emitters;
        PedPaths pedPaths;
        bool loaded = false;              // false = no data (off-map)
    };
    const Cell* cell(int cx, int cy);
    Cell* mutableCell(int cx, int cy) { return const_cast<Cell*>(cell(cx, cy)); }

    std::vector<uint8_t> world_;
    std::map<int, std::unique_ptr<Cell>> cells_;
    const class PropLibrary* propLibrary_ = nullptr;
    friend struct CollisionTestAccess;
};
