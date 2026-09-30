// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// How AI cars follow the road network: ports of cTarget (link positions), cAISpline and
// cWorldNodeData::GetRandomNode, in the game's fixed point.
//
// A link target is a point on the one-way link A -> B: ratio (stored x10000 like the game) and a lane. Its world
// position (cTarget::GetWorldPos) is lerp(A, B, ratio) pushed sideways by
//   halfWidth x (2 lane - (lanes - 1)) + ((A + B + seed) & 15 - 7) x wobble  (+- the lane offset on 2-lane roads).
// The spline keeps four targets T0..T3. The car travels from T1 to T2 with parameter t (0..1): along the link when
// T1 and T2 share their start node, otherwise on a quadratic curve through the intersection of the incoming and
// outgoing lines (ComputeIntercept) - the turn across a junction. When t passes 1 the targets shuffle down and the
// next link is picked at random (GetRandomNode: forward-ish links preferred, lane 0 may turn, other lanes go straight).
#pragma once
#include <cstdint>

class RoadNetwork;

struct LinkTarget {   // cTarget set by SetLink (0x20 bytes in the game)
    int a = -1, b = -1;
    int16_t ratio = 0;    // +0x18: ratio x 10000 >> 12
    uint8_t lane = 0;     // flags bits 6-8
    bool laneInfo = false;// flags bit 4 (the 4-argument SetLink)
    bool metaB = false;   // flags bit 9: lane metadata from node B
    bool valid() const { return a >= 0; }
};

class AISpline {
public:
    // cAISpline::Set(from, to, ratio (Q12 along the link), lane, lanes, cop, seed)
    void set(const RoadNetwork& rn, int a, int b, int16_t ratio, int lane, int lanes, bool cop, int seed);
    void getPosAndDir(const RoadNetwork& rn, int32_t pos[2], int32_t dir[2]);   // dir: unit (Q12)
    bool inc(const RoadNetwork& rn, int32_t dist, bool cop);                     // advance; true if it shuffled
    // cAISpline::IsToSlowForHoldingPattern: a red light / dead end ahead -> distance to the stop point
    bool tooSlowForHoldingPattern(const RoadNetwork& rn, uint32_t frame, int32_t& dist, bool& deadEnd);
    int currentLinkA() const { return t_[1].a; }
    int currentLinkB() const { return t_[1].b; }
    int lane() const { return t_[1].lane; }
    bool indicateLeft() const { return left_; }
    bool indicateRight() const { return right_; }
    int16_t param() const { return param_; }
    int32_t length() const { return len_; }
    const LinkTarget& target(int i) const { return t_[i]; }

    static void worldPos(const RoadNetwork& rn, const LinkTarget& t, int seed, int32_t out[3]);   // cTarget::GetWorldPos
    static void setLink(const RoadNetwork& rn, LinkTarget& t, int a, int b, int32_t ratioQ12, int lane, bool laneInfo);
    static int laneConvert(int fromLanes, int lane, int toLanes, bool flag5);   // cTarget::LaneConvert
    // cWorldNodeData::GetRandomNode: the node after `cur` coming from `prev` (-1: none); false if there is none
    static bool randomNode(const RoadNetwork& rn, int& cur, int prev, int* lane, int metaNode, bool cop, bool restricted);

private:
    void fixUpTuplet(const RoadNetwork& rn, int a, int b, int c, int slot, int lane);
    void computeIntercept(const RoadNetwork& rn);
    void computeDistance(const RoadNetwork& rn);
    void shuffleDown(const RoadNetwork& rn, bool cop);
    bool sameLink(int i, int j) const { return t_[i].a == t_[j].a; }   // the game compares the start node ids

    LinkTarget t_[4];
    int32_t icpt_[2] = {0, 0};   // +0xA8
    int32_t len_ = 0;            // +0xB8
    int seed_ = 0;               // +0xB0
    int16_t param_ = 0;          // +0xC8 (Q12)
    bool dirty_ = true;          // +0xB4
    int32_t p1_[2] = {0, 0}, p2_[2] = {0, 0};   // cached T1 / T2 positions (+0x10, +0x18)
    bool left_ = false, right_ = false;          // +0xCB, +0xCC (indicators)
};
