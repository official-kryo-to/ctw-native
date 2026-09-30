// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "aispline.h"
#include "lookups.h"
#include "roads.h"
#include "cargens.h"   // Rand32Critical
#include <cmath>
#include <cstdlib>

static inline int32_t mulq(int64_t a, int64_t b) { return (int32_t)((a * b) >> 12); }
static int32_t isqrt64(int64_t v) { return v <= 0 ? 0 : (int32_t)std::sqrt((double)v); }
static void normalise2(int32_t v[2]) {   // Normalise(tv2d)
    double l = std::sqrt((double)v[0] * v[0] + (double)v[1] * v[1]);
    if (l <= 0) return;
    v[0] = (int32_t)std::lround(v[0] / l * 4096.0);
    v[1] = (int32_t)std::lround(v[1] / l * 4096.0);
}
static void normalise3(int32_t v[3]) {
    double l = std::sqrt((double)v[0] * v[0] + (double)v[1] * v[1] + (double)v[2] * v[2]);
    if (l <= 0) return;
    for (int i = 0; i < 3; ++i) v[i] = (int32_t)std::lround(v[i] / l * 4096.0);
}
static uint32_t rand16(uint32_t n) { return Rand32Critical(n); }

// ============================================================================================== cTarget
int AISpline::laneConvert(int from, int lane, int to, bool flag5) {   // DAT_00581b44 / 4b / 52 / 86
    const GameplayTables& tables = TheGameplayTables();
    if (from < 0 || from > 6 || to < 0 || to > 6) return 0;
    int idx = (tables.laneRow[from] + lane) * 4 + tables.laneCol[to];
    if (idx < 0 || idx >= 52) return 0;
    return flag5 ? tables.laneFlag5[idx] : tables.lanePlain[idx];
}

void AISpline::setLink(const RoadNetwork& rn, LinkTarget& t, int a, int b, int32_t ratioQ12, int lane, bool laneInfo) {
    t.a = a; t.b = b;
    t.ratio = (int16_t)((uint32_t)(ratioQ12 * 10000) >> 12);
    t.lane = (uint8_t)(lane & 7);
    t.laneInfo = laneInfo;
    // cTarget::ResolveBestNodeForLaneInfo: take the lane data from the node without flag 5, else the narrower one
    RoadMeta ma = rn.meta(a), mb = rn.meta(b);
    if (!ma.flag5 && mb.flag5) t.metaB = false;
    else if (ma.flag5 && !mb.flag5) t.metaB = true;
    else t.metaB = mb.width < ma.width;
}

void AISpline::worldPos(const RoadNetwork& rn, const LinkTarget& t, int seed, int32_t o[3]) {   // cTarget::GetWorldPos
    int32_t A[3], B[3];
    rn.pos(t.a, A);
    rn.pos(t.b, B);
    if (t.a == t.b) { o[0] = A[0]; o[1] = A[1]; o[2] = A[2]; return; }
    int32_t r = ((int32_t)t.ratio << 12) / 10000, ir = 0x1000 - r;
    o[0] = mulq(A[0], ir) + mulq(B[0], r);
    o[1] = mulq(A[1], ir) + mulq(B[1], r);
    o[2] = mulq(B[2], r) + mulq(A[2], ir);
    if (!t.laneInfo) return;
    int32_t d[2] = {B[0] - A[0], B[1] - A[1]};
    normalise2(d);
    RoadMeta m = rn.meta(t.metaB ? t.b : t.a);
    int32_t lat = mulq(m.halfWidth, (int32_t)(t.lane << 13) - m.lanesMinus1 * 0x1000);
    if (seed != -1) lat += ((((uint16_t)t.b + seed + (uint16_t)t.a) & 0xF) - 7) * m.wobble;
    if (m.lanes > 1) lat = lat < 1 ? lat - m.laneOffset : lat + m.laneOffset;
    o[0] += mulq(-d[1], lat);
    o[1] += mulq(d[0], lat);
}

// ============================================================================================== node choice
bool AISpline::randomNode(const RoadNetwork& rn, int& cur, int prev, int* lane, int metaNode, bool cop, bool restricted) {
    const RoadNetwork::Node& n = rn.node(cur);
    int count = rn.linkCount(cur);
    if (!count) return false;
    int32_t C[3], P[3] = {0, 0, 0};
    rn.pos(cur, C);
    auto usable = [&](int next, bool anyOffRef) {
        const RoadNetwork::Node& m = rn.node(next);
        if (restricted) return true;
        if (m.flags >> 11 & 1) return false;
        return (m.flags & 3) == 0 || anyOffRef;
    };
    if (prev >= 0 && lane && metaNode >= 0) {
        RoadMeta meta = rn.meta(metaNode);
        if (meta.lanes > 1 && !meta.flag5) {
            // GetRandomNodeRespectingLane: lane 0 may turn one way, the others go straight on
            rn.pos(prev, P);
            int32_t din[3] = {C[0] - P[0], C[1] - P[1], C[2] - P[2]};
            normalise3(din);
            bool firstLane = *lane == 0;
            int32_t s1 = firstLane ? -din[0] : din[0], s3 = firstLane ? din[1] : -din[1];
            int cand[8], nc = 0;
            for (int k = 0; k < count; ++k) {
                int next = rn.link(cur, k);
                if (next == prev) continue;
                const RoadNetwork::Node& m = rn.node(next);
                if (!(cop || restricted || (m.flags & 3) == 0)) continue;
                if (!restricted && (m.flags >> 11 & 1)) continue;
                int32_t N[3];
                rn.pos(next, N);
                int32_t dout[3] = {N[0] - C[0], N[1] - C[1], N[2] - C[2]};
                normalise3(dout);
                int32_t side = (int32_t)(((int64_t)dout[0] * s3 + (int64_t)dout[1] * s1) >> 12);
                bool ok = firstLane ? side > -0x334 : std::abs(side) < 0x333;
                if (ok) cand[nc++] = next;
            }
            if (nc) { cur = cand[rand16(nc)]; return true; }
            (void)n;
        }
    }
    int32_t din[3] = {0, 0, 0};
    if (prev >= 0) {
        rn.pos(prev, P);
        din[0] = C[0] - P[0]; din[1] = C[1] - P[1]; din[2] = C[2] - P[2];
        normalise3(din);
    }
    int all[8], fwd[8], na = 0, nf = 0;
    for (int k = 0; k < count; ++k) {
        int next = rn.link(cur, k);
        if (!usable(next, cop)) continue;
        if (prev >= 0) {
            int32_t N[3];
            rn.pos(next, N);
            int32_t dout[3] = {N[0] - C[0], N[1] - C[1], N[2] - C[2]};
            normalise3(dout);
            if ((int64_t)din[0] * dout[0] + (int64_t)din[1] * dout[1] + (int64_t)din[2] * dout[2] > -0xB4F000) fwd[nf++] = next;
        }
        all[na++] = next;
    }
    if (nf) { cur = fwd[rand16(nf)]; return true; }
    if (!na) return false;
    int i = (int)rand16(na);
    if (prev >= 0 && all[i] == prev) i = i + 1 < na ? i + 1 : 0;
    cur = all[i];
    return true;
}

// ============================================================================================== cAISpline
void AISpline::fixUpTuplet(const RoadNetwork& rn, int a, int b, int c, int slot, int lane) {
    RoadMeta mb = rn.meta(b);
    int32_t w = mb.width < 9 ? 8 : mb.width;
    int32_t A[2], B[2], C[2];
    rn.pos2d(a, A); rn.pos2d(b, B); rn.pos2d(c, C);
    int64_t l1 = isqrt64((int64_t)(B[0] - A[0]) * (B[0] - A[0]) + (int64_t)(B[1] - A[1]) * (B[1] - A[1]));
    int32_t r1 = l1 ? (int32_t)((((int64_t)(l1 - w * 0x1000)) * 4294967296LL / l1) >> 20) : 0;
    if (r1 < 0x801) r1 = 0x800;
    int64_t l2 = isqrt64((int64_t)(C[0] - B[0]) * (C[0] - B[0]) + (int64_t)(C[1] - B[1]) * (C[1] - B[1]));
    int32_t r2 = l2 ? (int32_t)((((int64_t)w << 44) / l2) >> 20) : 0x7FF;
    if (r2 > 0x7FE) r2 = 0x7FF;
    setLink(rn, t_[slot], a, b, r1, lane, true);
    setLink(rn, t_[slot + 1], b, c, r2, 0, true);
    RoadMeta m0 = rn.meta(t_[slot].metaB ? t_[slot].b : t_[slot].a), m1 = rn.meta(t_[slot + 1].metaB ? t_[slot + 1].b : t_[slot + 1].a);
    setLink(rn, t_[slot + 1], b, c, r2, laneConvert(m0.lanes, lane, m1.lanes, m1.flag5), true);
}

void AISpline::set(const RoadNetwork& rn, int a, int b, int16_t ratio, int lane, int lanes, bool cop, int seed) {
    seed_ = seed;
    dirty_ = true;
    param_ = ratio;
    setLink(rn, t_[0], a, b, 0, 0, false);
    RoadMeta m0 = rn.meta(t_[0].metaB ? t_[0].b : t_[0].a);
    int l = laneConvert(lanes, lane, m0.lanes, m0.flag5);
    int from = a, to = b;
    if (m0.flag5 && l >= m0.lanes / 2) {   // a two-way link: the upper lanes drive the other way
        l = m0.lanes - 1 - l;
        param_ = (int16_t)(0x1000 - param_);
        from = b; to = a;
    }
    setLink(rn, t_[1], from, to, 0, l, true);
    setLink(rn, t_[2], from, to, 0x1000, l, true);
    int lane2 = t_[2].lane;
    int next = to;
    if (!randomNode(rn, next, from, &lane2, t_[2].metaB ? t_[2].b : t_[2].a, cop, false)) next = from;
    setLink(rn, t_[3], to, next, 0, 0, true);
    RoadMeta m2 = rn.meta(t_[2].metaB ? t_[2].b : t_[2].a), m3 = rn.meta(t_[3].metaB ? t_[3].b : t_[3].a);
    setLink(rn, t_[3], to, next, 0, laneConvert(m2.lanes, lane2, m3.lanes, m3.flag5), true);
    fixUpTuplet(rn, from, to, next, 2, l);
    computeIntercept(rn);
    computeDistance(rn);
    dirty_ = true;
    // the start ratio along the link -> the parameter along T1..T2
    int32_t r2 = ((int32_t)t_[2].ratio << 12) / 10000;
    int32_t t = r2 ? (int32_t)(((int64_t)(uint16_t)param_ << 12) / r2) : 0x1000;
    while (t > 0x1000) {
        int32_t oldLen = len_;
        shuffleDown(rn, cop);
        if (!len_) { t = 0x1000; break; }
        t = (int32_t)(((int64_t)(t - 0x1000) * oldLen) / len_);
    }
    param_ = (int16_t)t;
}

void AISpline::computeIntercept(const RoadNetwork& rn) {   // cAISpline::ComputeIntercept
    int32_t P0[3], P1[3], P2[3], P3[3];
    int32_t cross = 0;
    if (sameLink(1, 2)) {
        worldPos(rn, t_[1], seed_, P1);
        worldPos(rn, t_[2], seed_, P2);
        icpt_[0] = (P1[0] + P2[0]) >> 1;
        icpt_[1] = (P1[1] + P2[1]) >> 1;
        int32_t N[2];
        rn.pos2d(t_[3].b, N);
        int32_t d1[2] = {P2[0] - P1[0], P2[1] - P1[1]};
        normalise2(d1);
        int32_t p[2] = {-d1[1], d1[0]};
        int32_t d2[2] = {N[0] - P2[0], N[1] - P2[1]};
        normalise2(d2);
        cross = (int32_t)(((int64_t)d2[0] * p[0] + (int64_t)d2[1] * p[1]) >> 12);
    } else {
        worldPos(rn, t_[0], seed_, P0);
        worldPos(rn, t_[1], seed_, P1);
        worldPos(rn, t_[2], seed_, P2);
        worldPos(rn, t_[3], seed_, P3);
        if (t_[1].b == t_[2].a && t_[2].b == t_[1].a) {   // turning back on the same road
            int32_t N[2];
            rn.pos2d(t_[1].b, N);
            icpt_[0] = N[0]; icpt_[1] = N[1];
            cross = -0x1000;
        } else {
            int32_t d1[2] = {P0[0] - P1[0], P0[1] - P1[1]};
            if (!d1[0] && !d1[1]) { d1[0] = P1[0] - P2[0]; d1[1] = P1[1] - P2[1]; }
            normalise2(d1);
            int32_t d2[2] = {P3[0] - P2[0], P3[1] - P2[1]};
            if (!d2[0] && !d2[1]) { d2[0] = P2[0] - P1[0]; d2[1] = P2[1] - P1[1]; }
            normalise2(d2);
            // maths::LineIntercept(P1 + d1, P1, P2, P2 + d2)
            int32_t a[2] = {P1[0] + d1[0], P1[1] + d1[1]}, b[2] = {P1[0], P1[1]}, c[2] = {P2[0], P2[1]}, e[2] = {P2[0] + d2[0], P2[1] + d2[1]};
            int32_t i6 = a[0] - b[0], i7 = b[1] - a[1];
            int64_t den = (int64_t)(e[1] - c[1]) * i6 + (int64_t)(e[0] - c[0]) * i7;
            bool hit = std::llabs(den) > 0x27FFF;
            if (hit) {
                // (the quotient is an integer number of units along d2, as in the game)
                int32_t k = den ? (int32_t)(((int64_t)i7 * (c[0] - a[0]) + (int64_t)i6 * (c[1] - a[1])) / den) : 0;
                icpt_[0] = c[0] - (int32_t)(((int64_t)(e[0] - c[0]) * ((int64_t)k << 12)) >> 12);
                icpt_[1] = c[1] - (int32_t)(((int64_t)(e[1] - c[1]) * ((int64_t)k << 12)) >> 12);
            } else {
                icpt_[0] = (P1[0] + P2[0]) >> 1;
                icpt_[1] = (P1[1] + P2[1]) >> 1;
            }
            if ((int64_t)d1[0] * (icpt_[0] - P1[0]) + (int64_t)d1[1] * (icpt_[1] - P1[1]) >= 0 ||
                (int64_t)d2[0] * (icpt_[0] - P2[0]) + (int64_t)d2[1] * (icpt_[1] - P2[1]) >= 0) {
                icpt_[0] = (P2[0] + P1[0]) >> 1;
                icpt_[1] = (P2[1] + P1[1]) >> 1;
            }
            cross = (int32_t)(((int64_t)d2[0] * d1[1] - (int64_t)d2[1] * d1[0]) >> 12);
        }
    }
    left_ = right_ = false;   // indicators at junctions (more than two links)
    if (rn.linkCount(t_[1].b) > 2) {
        if (cross >= 0x19A) right_ = true;
        else if (cross < -0x199) left_ = true;
    }
}

// maths::ComputeHomeBrewedCurvePoint: quadratic Bezier A -> (via C) -> B at t, and its unit tangent
static void curvePoint(const int32_t A[2], const int32_t C[2], const int32_t B[2], int32_t t, int32_t p[2], int32_t d[2]) {
    int32_t t2 = mulq(t, t) - t;              // t^2 - t
    int32_t tt = mulq(t, t);                  // t^2
    int32_t w0 = (t2 - t) + 0x1000;           // (1-t)^2
    int32_t w1 = t2 * -2;                     // 2t(1-t)
    p[0] = mulq(A[0], w0) + mulq(tt, B[0]) + mulq(w1, C[0]);
    p[1] = mulq(tt, B[1]) + mulq(A[1], w0) + mulq(w1, C[1]);
    int32_t k = (t * -2 + 0x1000) * 2, t2x = t * 2;
    d[0] = mulq(k, C[0]) + mulq(A[0], t2x - 0x2000) + mulq(B[0], t2x);
    d[1] = mulq(k, C[1]) + mulq(A[1], t2x - 0x2000) + mulq(B[1], t2x);
    normalise2(d);
}

void AISpline::computeDistance(const RoadNetwork& rn) {   // cAISpline::ComputeDistance
    if (sameLink(1, 2)) {
        int32_t A[2], B[2];
        rn.pos2d(t_[1].a, A);
        rn.pos2d(t_[1].b, B);
        int32_t dr = ((int32_t)t_[2].ratio << 12) / 10000 - ((int32_t)t_[1].ratio << 12) / 10000;
        len_ = mulq(dr, isqrt64((int64_t)(A[0] - B[0]) * (A[0] - B[0]) + (int64_t)(A[1] - B[1]) * (A[1] - B[1])));
        return;
    }
    len_ = 0;
    int32_t P1[3], P2[3];
    worldPos(rn, t_[1], seed_, P1);
    worldPos(rn, t_[2], seed_, P2);
    int32_t prev[2] = {P1[0], P1[1]};
    for (int i = 0, t = 0; i < 11; ++i, t += 0x199) {
        int32_t p[2], d[2];
        curvePoint(P1, icpt_, P2, t, p, d);
        len_ += isqrt64((int64_t)(p[1] - prev[1]) * (p[1] - prev[1]) + (int64_t)(p[0] - prev[0]) * (p[0] - prev[0]));
        prev[0] = p[0]; prev[1] = p[1];
    }
}

void AISpline::shuffleDown(const RoadNetwork& rn, bool cop) {   // cAISpline::ShuffleDown
    dirty_ = true;
    t_[0] = t_[1];
    t_[1] = t_[2];
    t_[2] = t_[3];
    if (sameLink(1, 2)) {
        int lane = t_[2].lane;
        int next = t_[2].b;
        if (!randomNode(rn, next, t_[2].a, &lane, t_[2].metaB ? t_[2].b : t_[2].a, cop, false)) next = t_[2].a;
        fixUpTuplet(rn, t_[2].a, t_[2].b, next, 2, t_[2].lane);
    } else {
        setLink(rn, t_[3], t_[2].a, t_[2].b, 0x1000, t_[2].lane, true);
    }
    computeIntercept(rn);
    computeDistance(rn);
}

void AISpline::getPosAndDir(const RoadNetwork& rn, int32_t pos[2], int32_t dir[2]) {   // cAISpline::GetPosAndDir
    if (dirty_) {
        int32_t a[3], b[3];
        worldPos(rn, t_[1], seed_, a);
        worldPos(rn, t_[2], seed_, b);
        p1_[0] = a[0]; p1_[1] = a[1]; p2_[0] = b[0]; p2_[1] = b[1];
        dirty_ = false;
    }
    if (sameLink(1, 2)) {
        int32_t s = param_;
        pos[0] = mulq(s, p2_[0]) + mulq(0x1000 - s, p1_[0]);
        pos[1] = mulq(s, p2_[1]) + mulq(0x1000 - s, p1_[1]);
        dir[0] = p2_[0] - p1_[0];
        dir[1] = p2_[1] - p1_[1];
        if (!dir[0] && !dir[1]) {
            int32_t z[3];
            worldPos(rn, t_[0], seed_, z);
            dir[0] = p1_[0] - z[0];
            dir[1] = p1_[1] - z[1];
        }
        normalise2(dir);
    } else {
        curvePoint(p1_, icpt_, p2_, param_, pos, dir);
    }
}

bool AISpline::inc(const RoadNetwork& rn, int32_t dist, bool cop) {   // cAISpline::Inc
    bool shuffled = len_ == 0;
    int32_t t = param_;
    if (!len_) {
        for (int guard = 0; !len_ && guard < 8; ++guard) shuffleDown(rn, cop);
        t = 0;
    }
    if (len_) t += (int32_t)((((int64_t)(uint32_t)dist << 32) / len_) >> 20);
    if (t > 0x1000) {
        int32_t oldLen = len_;
        for (int guard = 0; t > 0x1000 && guard < 8; ++guard) {
            shuffleDown(rn, cop);
            if (!len_) { t = 0x1000; break; }
            t = (int32_t)(((int64_t)(t - 0x1000) * oldLen) / len_);
            oldLen = len_;
        }
        shuffled = true;
    }
    param_ = (int16_t)t;
    return shuffled;
}

bool AISpline::tooSlowForHoldingPattern(const RoadNetwork& rn, uint32_t frame, int32_t& dist, bool& deadEnd) {
    deadEnd = false;
    dist = 0;
    if (!t_[1].valid() || !t_[2].valid() || !sameLink(1, 2)) return false;
    const RoadNetwork::Node& end = rn.node(t_[1].b);
    if ((int16_t)end.flags >= 0) return false;   // not a controlled junction
    const RoadNetwork::Node& start = rn.node(t_[1].a);
    if ((int16_t)start.flags < 0 && len_ <= 0x3000 /* FAST_THROUGH_JUNCTION_DISTANCE */) return false;
    int32_t pos[2], dir[2];
    getPosAndDir(rn, pos, dir);
    // cNodeId::IsToSlowForHoldingPattern: a dead end, or the traffic lights hold this direction
    int32_t N[3];
    rn.pos(t_[1].b, N);
    bool held;
    if ((end.flags & 0x1C) == 4) held = true;
    else {   // cTrafficLightManager::IsTrafficBeingHeld: a 1024-frame cycle (north-south, all red, east-west, all red)
        uint32_t f = frame & 0x3FF;
        uint32_t flow = f < 0x180 ? 0 : (f - 0x200 < 0x180 ? 1 : 3);
        uint32_t direction = std::abs(N[1] - pos[1]) <= std::abs(N[0] - pos[0]) ? 1 : 0;
        held = flow != direction;
    }
    if (!held) return false;
    RoadMeta m = rn.meta(t_[1].b);
    int32_t stop = 0x5000 /* VEHICLE_AVERAGE_LENGTH */ + m.width * 0x1000;
    int32_t d = isqrt64((int64_t)(N[1] - pos[1]) * (N[1] - pos[1]) + (int64_t)(N[0] - pos[0]) * (N[0] - pos[0]) + (int64_t)N[2] * N[2]);
    int32_t left = d - stop;
    if ((uint32_t)(left - 0x8001) <= 0xFFFF8FFEu) return false;   // only within 1..8 units of the stop line
    dist = left < stop ? left : stop;
    if ((end.flags & 0x1C) == 4) { deadEnd = true; return false; }
    return true;
}
