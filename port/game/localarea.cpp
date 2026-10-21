// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "localarea.h"
#include "fixedmath.h"
#include "game.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
int fastsin(int a) { return (int)(sinf((float)a * 9.587378e-05f) * 4096.f); }

void nodePos(const Collision::PathNode& n, int32_t out[3]) {   // cBaseNode::Pos
    out[0] = n.x * 512;
    out[1] = n.y * 512;
    out[2] = n.z * 2048;
}

const Collision::PathNode* nodeOf(Game& g, const Pedestrians::NodeRef& r) {
    if (!g.sectors.loaded(r.cx, r.cy)) return nullptr;   // cNodeId::IsValidFast: its sector is loaded
    const Collision::PedPaths* paths = g.collision.pedPaths(r.cx, r.cy);
    if (!paths || r.index >= paths->nodes.size()) return nullptr;
    return &paths->nodes[r.index];
}

bool linkNode(Game& g, const Pedestrians::NodeRef& r, int32_t out[3], uint16_t& flags) {
    if (r.network == 0) {
        if (!r.valid() || r.index >= g.roads.count()) return false;
        g.roads.pos(r.index, out);
        flags = g.roads.node(r.index).flags;
        return true;
    }
    const auto* n = nodeOf(g, r);
    if (!n) return false;
    nodePos(*n, out);
    flags = n->flags;
    return true;
}
}   // namespace

// ---- FrameScheduler -------------------------------------------------------------------------------------------------

void FrameScheduler::reset() { memset(bins_, 0, sizeof bins_); }

void FrameScheduler::add(const Schedule& s, int delta) {
    int bin = s.start, n = 0;
    do {
        n += s.period;
        bins_[bin] = (int8_t)(bins_[bin] + delta);
        bin = (bin + s.period) & 0x3F;
    } while (n < 64);
}

int FrameScheduler::lowestBin(int period) const {   // cFrameTimerScheduler::LowestBin
    int load[64], least = 999;
    for (int s = 0; s < 64; ++s) {
        int sum = 0, b = s;
        do {
            sum += bins_[b];
            b = (b + period) & 0x3F;
        } while (b != s);
        load[s] = sum;
        least = std::min(least, sum);
    }
    int v[64];
    for (int i = 0; i < 64; ++i) v[i] = load[i] == least;
    // each run of least loaded bins becomes one value, its half length, at its middle
    int len = 0, start = 0, first = -1, i = 0;
    do {
        const int f = v[i];
        if (len) {
            if (f) ++len;
            else {
                for (int k = start; k != start + len; ++k) v[k & 0x3F] = 0;
                const int half = len >> 1;
                v[(start + half) & 0x3F] = half;
                len = 0;
            }
        } else if (f) {
            len = 1;
            start = i;
            if (first == -1) first = i;
        }
        i = i + 1 <= 63 ? i + 1 : 0;
    } while (i != first && len < 64);
    int result = 0, best = 0;
    for (int k = 0; k < 64; ++k)
        if (v[k] > best) { best = v[k]; result = k; }
    return result;
}

void FrameScheduler::create(Schedule& s, int period) {
    s.period = (uint8_t)period;
    s.start = (uint8_t)(lowestBin(period) % period);
    add(s, 1);
}

void FrameScheduler::setTiming(Schedule& s, int period) {
    add(s, -1);
    s.period = (uint8_t)period;
    s.start = (uint8_t)(lowestBin(period) % period);
    add(s, 1);
}

// ---- CameraView -----------------------------------------------------------------------------------------------------

void CameraView::set(const int32_t eye[3], uint16_t yaw, uint16_t pitch, int width, int height) {
    for (int k = 0; k < 3; ++k) pos[k] = eye[k];
    // CMatrix43::MakeRotationZ(yaw), MakeRotationX(-0x4000 - pitch), MatrixMultiply(x, z)
    const int sz = fastsin((int16_t)yaw), cz = fastsin((int16_t)yaw + 0x4000);
    const int16_t ax = (int16_t)(-0x4000 - (int)pitch);
    const int sx = fastsin(ax), cx = fastsin(ax + 0x4000);
    const int A[9] = {4096, 0, 0, 0, cx, -sx, 0, sx, cx};
    const int B[9] = {cz, -sz, 0, sz, cz, 0, 0, 0, 4096};
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            m[r * 3 + c] = (int16_t)(((A[r * 3] * B[c]) >> 12) + ((A[r * 3 + 1] * B[3 + c]) >> 12) +
                                     ((A[r * 3 + 2] * B[6 + c]) >> 12));
    // the frustum (RecalculateMatrix with the default field of view 5461 and the screen's aspect ratio)
    const int16_t cosFov = (int16_t)fastsin(5461 + 0x4000);
    invCos = (int16_t)(0x1000000 / cosFov);
    const uint32_t c2 = (uint32_t)(cosFov * cosFov) >> 12;
    const int64_t tan2 = ((((int64_t)((uint64_t)(4096 - c2) << 32) / (int64_t)c2) << 12) >> 20) & ~0xFFFLL;
    tanV = (int16_t)(uint32_t)std::sqrt((double)tan2);
    const int aspect = (width << 12) / std::max(1, height);
    tanH = (int16_t)((uint32_t)((int)tanV * (int16_t)aspect) >> 12);
}

bool CameraView::canSee(const int32_t p[3], int32_t radius) const {   // cBaseCam::CanSee
    const int64_t d[3] = {(int64_t)p[0] - pos[0], (int64_t)p[1] - pos[1], (int64_t)p[2] - pos[2]};
    const int32_t depth = (int32_t)((-m[6] * d[0] - m[7] * d[1] - m[8] * d[2]) >> 12);
    const int32_t up = (int32_t)((m[3] * d[0] + m[4] * d[1] + m[5] * d[2]) >> 12);
    if ((up ^ (up >> 31)) > (int32_t)(((int64_t)depth * tanV + (int64_t)invCos * radius) >> 12)) return false;
    const int32_t side = (int32_t)((m[0] * d[0] + m[1] * d[1] + m[2] * d[2]) >> 12);
    return (side ^ (side >> 31)) <= (int32_t)(((int64_t)depth * tanH + (int64_t)invCos * radius) >> 12);
}

// ---- WorldSectors ---------------------------------------------------------------------------------------------------

void WorldSectors::sectorOf(int32_t x, int32_t y, int& sx, int& sy) {   // wv2d::Set
    sx = std::clamp(((x >> 12) + 3500) / 50, 0, kColumns - 1);
    sy = std::clamp(((y >> 12) + 2500) / 50, 0, kRows - 1);
}

void WorldSectors::reset() {
    memset(refs_, 0, sizeof refs_);
    memset(loaded_, 0, sizeof loaded_);
    list_.clear();
    critical_ = 0;
    held_ = -1;
}

void WorldSectors::load(int i) {   // cWorld::RequestLoad / ForceLoad: a new sector joins the end of the list
    if (loaded_[i]) return;
    loaded_[i] = true;
    list_.push_back((uint16_t)i);
}

void WorldSectors::hold(int32_t x, int32_t y) {
    int sx, sy;
    sectorOf(x, y, sx, sy);
    const int i = sx * kRows + sy;
    if (i == held_) return;
    if (held_ >= 0) refs_[held_] = (uint8_t)(refs_[held_] - 1);   // cWorldSector::Remove
    load(i);                                                       // cEntity::DoAddToSector -> cWorld::ForceLoad
    refs_[i] = (uint8_t)(refs_[i] + 1);                            // cWorldSector::Add
    held_ = i;
}

bool WorldSectors::inBox(const int32_t centre[3], int32_t radius, int32_t x, int32_t y) {
    int x0, y0, x1, y1, sx, sy;
    sectorOf(centre[0] - radius, centre[1] - radius, x0, y0);
    sectorOf(centre[0] + radius, centre[1] + radius, x1, y1);
    sectorOf(x, y, sx, sy);
    return sx >= x0 && sx <= x1 && sy >= y0 && sy <= y1;
}

bool WorldSectors::loaded(int sx, int sy) const {
    return sx >= 0 && sx < kColumns && sy >= 0 && sy < kRows && loaded_[sx * kRows + sy];
}

bool WorldSectors::loadedAt(int32_t x, int32_t y) const {
    int sx, sy;
    sectorOf(x, y, sx, sy);
    return loaded(sx, sy);
}

void WorldSectors::area(int cx, int cy, int delta) {   // AddCriticalArea / RemoveCriticalArea, radius 1
    for (int sx = std::max(cx - 1, 0); sx <= std::min(cx + 1, kColumns - 1); ++sx)
        for (int sy = std::max(cy - 1, 0); sy <= std::min(cy + 1, kRows - 1); ++sy) {
            const int i = sx * kRows + sy;
            if (delta > 0) load(i);
            refs_[i] = (uint8_t)(refs_[i] + delta);
        }
}

void WorldSectors::updateCriticalArea(const int32_t player[3], const CameraView& cam) {
    // where the camera's view ray (along -row 2) meets z = -25, at most 32 units from the player
    const int32_t back[3] = {cam.m[6], cam.m[7], cam.m[8]};
    if (!back[2]) return;
    const int32_t t = (int32_t)((uint64_t)((int64_t)((uint64_t)(uint32_t)(cam.pos[2] + 102400) << 32) / -back[2]) >> 20);
    const int64_t ox = (int64_t)t * -back[0], oy = (int64_t)t * -back[1];
    const int32_t dx = std::clamp((int32_t)(cam.pos[0] - player[0] - (int32_t)(ox >> 12)), -0x20000, 0x20000);
    const int32_t dy = std::clamp((int32_t)(cam.pos[1] - player[1] - (int32_t)(oy >> 12)), -0x20000, 0x20000);
    int sx, sy;
    sectorOf(dx + player[0], dy + player[1], sx, sy);
    const uint16_t centre = (uint16_t)(sy | sx << 8);
    if (centre == critical_) return;
    if (critical_) area(critical_ >> 8, critical_ & 0xFF, -1);
    critical_ = centre;
    if (centre) area(sx, sy, 1);
}

void WorldSectors::collect() {   // cWorld::GarbageCollection: unload what nothing holds
    list_.erase(std::remove_if(list_.begin(), list_.end(), [&](uint16_t i) {
        if (refs_[i]) return false;
        loaded_[i] = false;
        return true;
    }), list_.end());
}

// ---- maths ----------------------------------------------------------------------------------------------------------

bool ChordCircle(const int32_t a[2], const int32_t b[2], const int32_t c[2], int32_t r, uint16_t& t0, uint16_t& t1,
                 int32_t& len) {   // maths::GetChordCircle
    const int32_t dy = b[1] - a[1], dx = b[0] - a[0];
    len = (int32_t)(uint32_t)std::sqrt((double)(uint64_t)((int64_t)dy * dy + (int64_t)dx * dx));
    if (len < 4096) return false;
    const int32_t ey = dy >> 5, ex = dx >> 5, fx = a[0] - c[0], fy = (a[1] - c[1]) >> 5;
    const int64_t aa = (int64_t)ey * ey + (int64_t)ex * ex;
    const int64_t bb = (int64_t)fy * ey + (int64_t)(fx >> 5) * ex;
    const int32_t a12 = (int32_t)(aa >> 12);
    const int64_t b12 = bb >> 12;
    const int64_t disc = (int64_t)(int32_t)b12 * (int32_t)b12 -
        (int64_t)(int32_t)((((int64_t)(fx >> 5) + (r >> 5)) * ((fx >> 5) - (r >> 5)) + (int64_t)fy * fy) >> 12) * a12;
    if (disc < 0 || !a12) return false;
    const int64_t mb = (int64_t)((uint64_t)0xFFFFFFFF00000000ULL * (uint64_t)b12) >> 20;
    const int64_t inv = (0x100000000000LL / a12) >> 20;
    const int64_t root = (int64_t)(uint32_t)std::sqrt((double)(uint64_t)disc) << 12;
    const int32_t lo = (int32_t)((uint64_t)(inv * (int32_t)((mb - root) >> 12)) >> 12);
    const int32_t hi = (int32_t)((inv * (int32_t)((root + mb) >> 12)) >> 12);
    if (lo < 0 && hi < 0) return false;
    if (lo > 4096 && hi > 4096) return false;
    t0 = (uint16_t)std::clamp(lo, 0, 4096);
    t1 = (uint16_t)std::clamp(hi, 0, 4096);
    return true;
}

// ---- LocalArea ------------------------------------------------------------------------------------------------------

void LocalArea::reset(FrameScheduler& scheduler) {
    links_.clear();
    roadLinks_.clear();
    cursor_ = 0;
    roads_ = false;
    rebuiltEntities_ = false;
    entityCacheResetDue_ = false;
    scheduler.create(schedule_, 16);
}

void LocalArea::dropInvalid(Game& g) {   // ResetCache: swap out links whose nodes are no longer loaded
    for (size_t i = 0; i < links_.size();) {
        if (nodeOf(g, links_[i].a) && nodeOf(g, links_[i].b)) { ++i; continue; }
        links_[i] = links_.back();
        links_.pop_back();
    }
    for (size_t i = 0; i < roadLinks_.size();) {
        if (roadLinks_[i].a.index < g.roads.count() && roadLinks_[i].b.index < g.roads.count()) { ++i; continue; }
        roadLinks_[i] = roadLinks_.back();
        roadLinks_.pop_back();
    }
}

void LocalArea::add(const Link& l, int network) {   // AddToArray: replace the shortest chord with a longer one
    auto& links = network == 0 ? roadLinks_ : links_;
    if (links.size() < 48) { links.push_back(l); return; }
    int32_t least = 409600000;
    int pick = -1;
    for (int i = 0; i < 48; ++i) {
        const int32_t m = (int32_t)(((int64_t)(int16_t)(links[i].t1 - links[i].t0) * links[i].length) >> 12);
        if (least > m) pick = i;
        if (least >= m) least = m;
    }
    if (pick >= 0 && least < (int32_t)(((int64_t)(int16_t)(l.t1 - l.t0) * l.length) >> 12)) links[pick] = l;
}

void LocalArea::update(Game& g) {   // cLocalAreaKnowledge::Update
    rebuiltEntities_ = false;
    entityCacheResetDue_ = !FrameScheduler::fired(schedule_, g.frame);
    if (entityCacheResetDue_) { dropInvalid(g); return; }
    if (roads_) {
        roadLinks_.clear();
        int32_t p[3];
        g.focus(p);
        addRoads(g, p);
    } else {
        rebuiltEntities_ = true;
        links_.clear();   // Reset clears the half that is rebuilt
        int32_t p[3];
        g.focus(p);
        addPavement(g, p);
    }
    roads_ = !roads_;
}

// Native loaded-sector road-link collection (0x911730..0x911E50). Read each sector's global-node references
// in load order, visiting a node once, and keep chords of the 100-unit player circle. On land the budgets are
// 48 land links / 12 water links, with 48 total slots. The reverse-link lookup suppresses an already-seen link.
void LocalArea::addRoads(Game& g, const int32_t player[3]) {
    std::vector<bool> seen((size_t)g.roads.count(), false);
    int land = 48, water = 12;
    for (uint16_t sector : g.sectors.list()) {
        const auto* ids = g.collision.roadNodes(sector / 100, sector % 100);
        if (!ids) continue;
        for (uint16_t index : *ids) {
            if (index >= g.roads.count() || seen[index]) continue;
            seen[index] = true;
            const auto& node = g.roads.node(index);
            if (node.flags & 3) continue;
            int& budget = node.z < -4 ? water : land;
            int32_t at[3]; g.roads.pos(index, at);
            const NodeRef self{0, 0, index, 0};
            for (int k = 0; k < g.roads.linkCount(index); ++k) {
                if (!budget) break;
                const int next = g.roads.link(index, k);
                if (next >= g.roads.count() || (g.roads.node(next).flags & 3)) continue;
                const NodeRef other{0, 0, (uint16_t)next, 0};
                bool added = false;
                for (const Link& l : roadLinks_) added = added || (l.a == other && l.b == self);
                if (added) continue;
                int32_t to[3]; g.roads.pos(next, to);
                Link l{self, other};
                if (!ChordCircle(at, to, player, 100 * 4096, l.t0, l.t1, l.length)) continue;
                add(l, 0);
                --budget;
                if (roadLinks_.size() == 48) return;
                // The native optional reverse insertion checks for this just-added forward link first
                // (0x911BD0..0x911C88), so it retains the first encountered orientation.
            }
        }
    }
}

void LocalArea::addPavement(Game& g, const int32_t player[3]) {   // AddPathingNodes, sector nodes
    const int32_t r = 409600;   // 100 units
    int x0, y0, x1, y1;
    WorldSectors::sectorOf(player[0] - r, player[1] - r, x0, y0);
    WorldSectors::sectorOf(player[0] + r, player[1] + r, x1, y1);
    for (int sy = y0; sy <= y1; ++sy)
        for (int sx = x0; sx <= x1; ++sx) {
            if (links_.size() > 47) return;
            if (!g.sectors.loaded(sx, sy)) continue;
            const Collision::PedPaths* paths = g.collision.pedPaths(sx, sy);
            if (!paths) continue;
            for (size_t n = 0; n < paths->nodes.size(); ++n) {
                if (links_.size() > 47) break;
                const Collision::PathNode& node = paths->nodes[n];
                if (node.flags & 3) continue;
                int32_t at[3];
                nodePos(node, at);
                if (std::abs(at[0] - player[0]) >= 0x8D7AC || std::abs(at[1] - player[1]) >= 0x8D7AC) continue;
                const NodeRef self{(int16_t)sx, (int16_t)sy, (uint16_t)n};
                // cNodeId::NeighbourCount / NeighbourIndex: the links, and at a bridge into a loaded neighbouring
                // sector one more: the node past the crossing (ResolveBridgeNode)
                std::vector<NodeRef> next;
                const int count = node.flags >> 2 & 7;
                for (int k = 0; k < count; ++k) {
                    const size_t li = (size_t)node.first + k;
                    if (li < paths->links.size() && paths->links[li] < paths->nodes.size())
                        next.push_back({(int16_t)sx, (int16_t)sy, paths->links[li]});
                }
                NodeRef across;
                if ((node.flags & 0x80) && g.peds.resolveBridge(g, self, across)) next.push_back(across);
                for (const NodeRef& other : next) {
                    bool added = false;   // IsAddedLink, the other way round
                    for (const Link& l : links_) added = added || (l.a == other && l.b == self);
                    if (added) continue;
                    const Collision::PathNode* on = nodeOf(g, other);
                    if (!on || (on->flags & 3)) continue;
                    int32_t to[3];
                    nodePos(*on, to);
                    Link l{self, other, 0, 0, 0};
                    if (!ChordCircle(at, to, player, r, l.t0, l.t1, l.length)) continue;
                    add(l);
                }
            }
        }
}

bool LocalArea::createPosition(Game& g, const int32_t centre[3], int32_t radius, int32_t out[3], NodeRef& a,
                               NodeRef& b) {   // GetCreatePosition with GenerateRandomPed's constraints
    const int count = (int)links_.size();
    if (!count) return false;
    if (cursor_ > count) cursor_ = (uint16_t)(cursor_ % count);
    for (int tries = 1;; ++tries) {
        cursor_ = (uint16_t)(count > cursor_ + 1 ? cursor_ + 1 : 0);
        const Link& l = links_[cursor_];
        const Collision::PathNode* na = nodeOf(g, l.a);
        const Collision::PathNode* nb = nodeOf(g, l.b);
        bool found = false;
        if (na && nb) {
            int32_t A[3], B[3];
            nodePos(*na, A);
            nodePos(*nb, B);
            uint16_t t0, t1;
            int32_t len;
            if (ChordCircle(A, B, centre, radius, t0, t1, len) && !(t0 == 0 && t1 == 4096)) {
                int32_t dir[3] = {B[0] - A[0], B[1] - A[1], B[2] - A[2]};
                Normalise3(dir);   // maths::AccurateNormalise
                // both crossings inside the link: Rand16Critical(2) picks; one: that one
                int32_t t = t0 ? (t1 != 4096 ? (Rand16Critical(2) ? t1 : t0) : t0) : t1;
                const int64_t scaled = (int64_t)l.length << 20;
                for (int k = 0; k < 3; ++k)
                    out[k] = A[k] + (int32_t)((uint64_t)((int64_t)(int32_t)((uint64_t)(scaled * dir[k]) >> 32) * t) >> 12);
                // cPositionConstraints::IsPositionValid: a loaded sector, and nobody sees it (radius 1)
                found = g.sectors.loadedAt(out[0], out[1]) && !g.view.canSee(out, 4096);
                if (found) { a = l.a; b = l.b; }
            }
        }
        if (found) return true;
        if (tries >= count) return false;
    }
}

bool LocalArea::nearestNodePosition(Game& g, const int32_t p[3], int32_t range, NodeRef& a, NodeRef& b,
                                    int32_t point[3], int32_t& t, int network) const {   // GetNearestNodePosition
    int64_t best = (((int64_t)range << 12) * ((int64_t)range << 12)) >> 24;
    bool found = false;
    for (const Link& l : network == 0 ? roadLinks_ : links_) {
        int32_t A[3], B[3];
        uint16_t af, bf;
        if (!linkNode(g, l.a, A, af) || !linkNode(g, l.b, B, bf)) continue;
        // Wander's secondary query uses network 0, land constraint 2 and no restricted links.
        if (network == 0 && (((af | bf) & 0x800) || A[2] < -0x2000)) continue;
        const int32_t d[3] = {B[0] - A[0], B[1] - A[1], B[2] - A[2]};
        const int32_t len = (int32_t)(uint32_t)std::sqrt((double)(uint64_t)((int64_t)d[1] * d[1] + (int64_t)d[0] * d[0] +
                                                                           (int64_t)d[2] * d[2]));
        if (!len) continue;
        int32_t u[3];
        for (int k = 0; k < 3; ++k) u[k] = (int32_t)((((int64_t)d[k] << 32) / len) >> 20);
        const int64_t dot = (int64_t)u[0] * (p[0] - A[0]) + (int64_t)u[1] * (p[1] - A[1]) + (int64_t)u[2] * (p[2] - A[2]);
        const int32_t s = (int32_t)(dot >> 12);
        if (s < 1 || len <= s) continue;
        int32_t at[3];
        for (int k = 0; k < 3; ++k) at[k] = A[k] + (int32_t)((uint64_t)((int64_t)s * u[k]) >> 12);
        const int64_t d2 = (int64_t)(at[1] - p[1]) * (at[1] - p[1]) + (int64_t)(at[0] - p[0]) * (at[0] - p[0]) +
                           (int64_t)(at[2] - p[2]) * (at[2] - p[2]);
        if (d2 >= best) continue;
        best = d2;
        found = true;
        a = l.a;
        b = l.b;
        std::copy(at, at + 3, point);
        t = (int32_t)((uint64_t)(((int64_t)s << 32) / len) >> 20);
    }
    return found;
}
