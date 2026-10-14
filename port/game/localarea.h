// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// What the population and the pedestrians' path finding know about the player's surroundings:
//
// FrameScheduler (cFrameTimerScheduler / cFrameSchedule): 64 frame bins. A schedule of period p fires when
// (frame + start) & (p - 1) == p - 1; creating or re-timing one unregisters it, takes the least loaded start
// (LowestBin: the middle of the longest run of least loaded bins) and registers it in every p-th bin. The player
// creates the ped (64) and vehicle (32) population schedules, then the local area schedule (16).
//
// CameraView (cBaseCam::RecalculateMatrix / CanSee): the camera matrix RotX(-0x4000 - pitch) x RotZ(yaw) in Q12 and
// the integer frustum test of the original (no near or far plane; a sphere within r / cos(fov) of the side planes).
//
// WorldSectors (cWorld, cPlayer::UpdateCriticalArea, cWorldSector::AddCriticalArea): the world is 140 x 100 sectors
// of 50 units. The player keeps the 3 x 3 sectors loaded around the point where the camera's view ray meets
// z = -25, at most 32 units from the player (the "critical area"); cWorld::GarbageCollection unloads the others and
// deletes their unreferenced entities. Sectors load in request order (cWorld's list).
//
// LocalArea (cLocalAreaKnowledge, pavement network): every 16 frames it alternately rebuilds the road links (not
// ported: traffic has its own) and the pavement links: up to 48 links of the loaded sectors' pavement nodes within
// 141.5 units that cross the 100-unit circle around the player (AddPathingNodes); when full, a longer chord replaces
// the shortest. GetCreatePosition walks them round-robin for a spawn point; GetNearestNodePosition finds the
// nearest point on one.
#pragma once
#include "peds.h"
#include <cstdint>
#include <vector>

class Game;

class FrameScheduler {
public:
    struct Schedule { uint8_t start = 0, period = 1; };
    void reset();
    void create(Schedule& s, int period);      // cFrameSchedule::cFrameSchedule
    void setTiming(Schedule& s, int period);   // cFrameSchedule::SetTiming
    static bool fired(const Schedule& s, uint32_t frame) { return ((s.period - 1u) & ~(frame + s.start)) == 0; }
    int lowestBin(int period) const;           // cFrameTimerScheduler::LowestBin
private:
    void add(const Schedule& s, int delta);
    int8_t bins_[64] = {};
};

struct CameraView {
    int32_t pos[3] = {0, 0, 0};
    int16_t m[9] = {4096, 0, 0, 0, 4096, 0, 0, 0, 4096};   // rows: right, up, back (the camera looks along -row 2)
    int16_t tanH = 0, tanV = 0, invCos = 0;               // cBaseCam +0xC4, +0xC8, +0xC6 / +0xCA
    void set(const int32_t eye[3], uint16_t yaw, uint16_t pitch, int screenWidth, int screenHeight);
    bool canSee(const int32_t p[3], int32_t radius) const;   // cBaseCam::CanSee
};

class WorldSectors {
public:
    static constexpr int kColumns = 140, kRows = 100;
    static void sectorOf(int32_t x, int32_t y, int& sx, int& sy);   // wv2d::Set
    void reset();
    void updateCriticalArea(const int32_t player[3], const CameraView& cam);   // cPlayer::UpdateCriticalArea
    // cEntity::DoSectorTransition for an entity that holds its sector (cEntity +0x38: the player, and through
    // cPed::SetVehicle his vehicle): it references the sector it is in, which cWorld::ForceLoad loads if needed.
    void hold(int32_t x, int32_t y);
    void collect();                                 // cWorld::GarbageCollection (the caller deletes the entities)
    bool loaded(int sx, int sy) const;
    bool loadedAt(int32_t x, int32_t y) const;
    // cPlayer::GetPopulation: is the sector of (x, y) loaded and within the cWorldIterator::SetRadius box of
    // `radius` around `centre`?
    static bool inBox(const int32_t centre[3], int32_t radius, int32_t x, int32_t y);
    const std::vector<uint16_t>& list() const { return list_; }   // sx * 100 + sy, in load order
private:
    void area(int sx, int sy, int delta);
    uint8_t refs_[kColumns * kRows] = {};
    bool loaded_[kColumns * kRows] = {};
    std::vector<uint16_t> list_;
    uint16_t critical_ = 0;                        // cPlayer +0x392: the area's centre (byte 0 sy, byte 1 sx), 0 none
    int held_ = -1;                                // the held sector (sx * 100 + sy)
    void load(int i);
};

class LocalArea {
public:
    using NodeRef = Pedestrians::NodeRef;
    struct Link { NodeRef a, b; int32_t length = 0; uint16_t t0 = 0, t1 = 0; };   // sNodeData
    void reset(FrameScheduler& scheduler);
    void update(Game& g);                           // cLocalAreaKnowledge::Update (from cPlayer::Think)
    // GetCreatePosition with GenerateRandomPed's constraints: off screen (radius 1), in a loaded sector, where a
    // link crosses the circle of `radius` around `centre`. a / b: the link, walked from a to b.
    bool createPosition(Game& g, const int32_t centre[3], int32_t radius, int32_t out[3], NodeRef& a, NodeRef& b);
    // GetNearestNodePosition (pavement, no constraints): the nearest point within `range` on a link; t = Q12 ratio.
    bool nearestNodePosition(Game& g, const int32_t p[3], int32_t range, NodeRef& a, NodeRef& b, int32_t point[3],
                             int32_t& t) const;
    const std::vector<Link>& pavement() const { return links_; }
private:
    void addPavement(Game& g, const int32_t player[3]);   // AddPathingNodes, the pavement half
    void dropInvalid(Game& g);                       // ResetCache: links whose sector unloaded
    void add(const Link& l);                         // AddToArray(network 1)
    FrameScheduler::Schedule schedule_;
    std::vector<Link> links_;
    uint16_t cursor_ = 0;                            // +0x306: GetCreatePosition's round robin
    bool roads_ = false;                             // +0xC3B: the next fired update rebuilds the road links
};

// maths::GetChordCircle: where the segment a -> b crosses the circle (centre c, radius r): t0 <= t1 in 0..4096 along
// it, len = |b - a|. False if it misses, is shorter than 1 unit, or lies wholly outside.
bool ChordCircle(const int32_t a[2], const int32_t b[2], const int32_t c[2], int32_t r, uint16_t& t0, uint16_t& t1,
                 int32_t& len);
