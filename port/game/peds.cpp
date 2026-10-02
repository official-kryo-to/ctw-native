// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "peds.h"
#include "game.h"
#include "plugins.h"
#include "random.h"
#include "sound.h"
#include "fixedmath.h"
#include "gfx/pedsprites.h"
#include "os/gamefs.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
constexpr int32_t kSpawnRadius = 0x3C000;            // cPositionConstraints::Reset(network 1): 60 units
constexpr int64_t kRemoveDist2 = 0x1C39000000LL;     // cPed::ShouldBeDestroyed: about 85 units
constexpr int32_t kArrive = 0x800;                   // cWanderPath: cTarget radius 0.5
constexpr int32_t kFleeArrive = 0x2000;              // cFleeOnFoot: cTarget::SetNode(node, 2.0)
constexpr int32_t kNearPlayer = 0x19000;             // HandleEvasiveManeuvers: 25 units
constexpr int32_t kExplosionReach = 0x16000;         // cExplosionBigVehicle::Process -> AffectEntities: 22 units
constexpr int kSectorColumns = 140;                  // cNodeId / cWorld sector grid

int16_t padAngle(int64_t x, int64_t y) { return (int16_t)(atan2f((float)x, (float)y) * 10430.f); }
int64_t dist2(const int32_t a[2], const int32_t b[2]) {
    const int64_t dx = (int64_t)a[0] - b[0], dy = (int64_t)a[1] - b[1];
    return dx * dx + dy * dy;
}
void nodePos(const Collision::PathNode& n, int32_t out[3]) {   // cBaseNode::Pos: 1/8 unit x, y; 1/2 unit z
    out[0] = n.x * 512; out[1] = n.y * 512; out[2] = n.z * 2048;
}
// cWanderPath target offset: ((Rand16Critical(101) << 12) - 50.0) x 0.75 / 50, up to +-0.75 units per axis
int32_t jitter() {
    const int32_t r = (int32_t)(Rand16Critical(101) << 12) - 0x32000;
    return (int32_t)((int64_t)r * 0xC00 / 204800);
}
}   // namespace

bool Pedestrians::init(const std::string& dataDir) {
    records_.clear();
    firstRecord_.clear();
    peds.clear();
    GameFs fs;
    std::vector<uint8_t> d;
    if (!fs.open(dataDir) || !fs.read("pedinfo.bin", d) || d.size() < 8 + 20 * 4) return false;
    // cPedInfoManager::Init: u32 at +4 = the 32-byte records; from +8 one u32 per ped type, the low half being the
    // type's first record. In a record: +8 file offset of the reactions (3 bytes each: chance, reaction, action),
    // +0xC their count, +0xD type, +0xE subtype, +0x19 male only, +0x1A female only, +0x1B / +0x1C fixed legs /
    // upper palette + 1 (0: none).
    uint32_t recOffset;
    memcpy(&recOffset, &d[4], 4);
    for (int t = 0; t < 20; ++t) {
        uint32_t v;
        memcpy(&v, &d[8 + t * 4], 4);
        firstRecord_.push_back((uint16_t)v);
    }
    for (size_t o = recOffset; o + 32 <= d.size(); o += 32) {
        Record r{d[o + 0xD], d[o + 0xE], d[o + 0x19], d[o + 0x1A], d[o + 0x1B], d[o + 0x1C], {}};
        uint32_t at;
        memcpy(&at, &d[o + 8], 4);
        for (size_t k = 0; k < d[o + 0xC] && (size_t)at + 3 * k + 3 <= d.size(); ++k)
            r.reactions.push_back({d[at + 3 * k], d[at + 3 * k + 1], d[at + 3 * k + 2]});
        records_.push_back(std::move(r));
    }
    return !records_.empty();
}

const Pedestrians::Record* Pedestrians::record(int type, int subtype) const {   // cPedInfoManager::GetInfo
    if (type < 0 || type >= (int)firstRecord_.size() || subtype < 0) return nullptr;
    const size_t i = (size_t)firstRecord_[type] + subtype;
    if (i >= records_.size() || records_[i].type != type || records_[i].subtype != subtype) return nullptr;
    return &records_[i];
}

int Pedestrians::alive() const {
    int n = 0;
    for (const Ped& p : peds) n += p.deadFrames < 0;
    return n;
}

void Pedestrians::dress(Ped& p, int zoneSex) const {
    // cPed::cPed: Rand16Critical(100) > 40 is male; SetPedType applies the record; CreateRandomPed then
    // applies the zone's sex makeup to civilians.
    bool male = Rand16Critical(100) > 40;
    const Record* r = record(p.type, p.subtype);
    if (r && r->maleOnly) male = true;
    else if (r && r->femaleOnly) male = false;
    else if (p.type == 1 && zoneSex == 1) male = true;
    else if (p.type == 1 && zoneSex == 2) male = false;
    int upper, legs;
    if (male) { upper = (int)Rand16Critical(5); legs = (int)Rand16Critical(5); }   // ChooseRandomMalePalette
    else { upper = 30 + (int)Rand16Critical(5); legs = 30 + (int)Rand16Critical(5); }   // ChooseRandomFemalePalette
    if (p.type == 10) {   // SetPedType: cop subtypes have their own uniform palettes
        static const int cop[4] = {0x10, 0x12, 0x0C, 0x11};
        male = true;
        upper = legs = p.subtype >= 0 && p.subtype < 4 ? cop[p.subtype] : 0x10;
    } else if (p.type == 1 && (p.subtype == 3 || p.subtype == 4)) {
        male = true;
        upper = legs = p.subtype == 3 ? 0x0D : 0x0E;
    }
    if (r && r->upperPalette) { upper = r->upperPalette - 1; legs = r->legsPalette - 1; }
    p.male = male;
    p.body.bodySet = male ? 1 : 0;   // cPed +0x220 selects the body set (0x113 animations each)
    p.body.palUpper = upper;
    p.body.palLegs = legs;
}

// cLocalAreaKnowledge::GetCreatePosition for the pavement network: where a link of the loaded pavement nodes crosses
// the 60-unit circle around the player, out of sight and with nobody close.
bool Pedestrians::createPos(Game& g, int32_t pos[3], NodeRef& from, NodeRef& to) {
    int32_t P[3];
    g.focus(P);
    int pcx, pcy;
    Collision::cellOfPos(P[0], P[1], pcx, pcy);
    struct Candidate { int32_t pos[3]; NodeRef a, b; };
    std::vector<Candidate> found;
    for (int dy = -2; dy <= 2; ++dy)
        for (int dx = -2; dx <= 2; ++dx) {
            const int cx = pcx + dx, cy = pcy + dy;
            const Collision::PedPaths* paths = g.collision.pedPaths(cx, cy);
            if (!paths) continue;
            for (size_t i = 0; i < paths->nodes.size(); ++i) {
                const Collision::PathNode& na = paths->nodes[i];
                for (int k = 0; k < (na.flags >> 2 & 7); ++k) {
                    const size_t li = (size_t)na.first + k;
                    if (li >= paths->links.size() || paths->links[li] >= paths->nodes.size()) continue;
                    const uint16_t j = paths->links[li];
                    if (j < i) continue;   // each two-way link once
                    int32_t A[3], B[3];
                    nodePos(na, A);
                    nodePos(paths->nodes[j], B);
                    // maths::GetChordCircle
                    const double ex = B[0] - A[0], ey = B[1] - A[1], fx = A[0] - P[0], fy = A[1] - P[1];
                    const double aa = ex * ex + ey * ey;
                    if (aa < 1.0) continue;
                    const double bb = 2 * (fx * ex + fy * ey), cc = fx * fx + fy * fy - (double)kSpawnRadius * kSpawnRadius;
                    const double disc = bb * bb - 4 * aa * cc;
                    if (disc < 0) continue;
                    for (double t : {(-bb - std::sqrt(disc)) / (2 * aa), (-bb + std::sqrt(disc)) / (2 * aa)}) {
                        if (t < 0 || t > 1) continue;
                        Candidate c;
                        c.pos[0] = A[0] + (int32_t)(ex * t);
                        c.pos[1] = A[1] + (int32_t)(ey * t);
                        c.pos[2] = A[2] + (int32_t)((B[2] - A[2]) * t);
                        c.a = {(int16_t)cx, (int16_t)cy, (uint16_t)i};
                        c.b = {(int16_t)cx, (int16_t)cy, j};
                        found.push_back(c);
                    }
                }
            }
        }
    // cPositionConstraints::IsPositionValid / cWorld::IsPositionSafe: off screen, 2 units from peds, 4 from vehicles
    std::vector<size_t> valid;
    for (size_t i = 0; i < found.size(); ++i) {
        const Candidate& c = found[i];
        if (g.canSee(c.pos, 1.f)) continue;
        bool clear = true;
        for (const Ped& p : peds) clear = clear && dist2(p.body.pos, c.pos) >= (int64_t)0x2000 * 0x2000;
        for (const Vehicle& v : g.cars) clear = clear && dist2(v.pos, c.pos) >= (int64_t)0x4000 * 0x4000;
        if (clear) valid.push_back(i);
    }
    if (valid.empty()) return false;
    const Candidate& c = found[valid[Rand32Critical((uint32_t)valid.size())]];
    std::copy(c.pos, c.pos + 3, pos);
    // which way along the link it walks (GenerateRandomPed faces it from the position along the link)
    if (Rand16Critical(2)) { from = c.a; to = c.b; } else { from = c.b; to = c.a; }
    return true;
}

void Pedestrians::setTarget(Game& g, Ped& p) {   // cWanderPath::SetNext
    const Collision::PedPaths* paths = g.collision.pedPaths(p.cur.cx, p.cur.cy);
    if (!paths || p.cur.index >= paths->nodes.size()) return;
    int32_t at[3];
    nodePos(paths->nodes[p.cur.index], at);
    p.target[0] = at[0] + jitter();
    p.target[1] = at[1] + jitter();
}

bool Pedestrians::nodeAt(Game& g, const NodeRef& n, int32_t out[3]) {
    const Collision::PedPaths* paths = g.collision.pedPaths(n.cx, n.cy);
    if (!paths || n.index >= paths->nodes.size()) return false;
    nodePos(paths->nodes[n.index], out);
    return true;
}

// cSectorNodeData::ResolveBridgeNode: the bridge node of the neighbouring sector at the same point (node 0 if there
// is none), then the first node it links to - the walk continues past the sector edge.
bool Pedestrians::resolveBridge(Game& g, const NodeRef& bridge, NodeRef& out) const {
    const Collision::PedPaths* paths = g.collision.pedPaths(bridge.cx, bridge.cy);
    if (!paths || bridge.index >= paths->nodes.size()) return false;
    int nx = -1, ny = -1;
    for (auto [bridgeNode, sector] : paths->bridges)
        if (bridgeNode == bridge.index) { nx = sector % kSectorColumns; ny = sector / kSectorColumns; break; }
    if (nx < 0) return false;
    const Collision::PedPaths* other = g.collision.pedPaths(nx, ny);   // (the original needs the sector loaded)
    if (!other || other->nodes.empty()) return false;
    const int own = bridge.cy * kSectorColumns + bridge.cx;
    const Collision::PathNode& node = paths->nodes[bridge.index];
    uint16_t twin = 0;
    for (auto [otherNode, otherSector] : other->bridges)
        if (otherSector == own && otherNode < other->nodes.size() && other->nodes[otherNode].x == node.x &&
            other->nodes[otherNode].y == node.y) { twin = otherNode; break; }
    const size_t link = other->nodes[twin].first;
    if (link >= other->links.size() || other->links[link] >= other->nodes.size()) return false;
    out = {(int16_t)nx, (int16_t)ny, other->links[link]};
    return true;
}

// cSectorNodeData::GetRandomNode: the next node from `cur`, arriving from `from`. Candidates are cur's links and,
// at a bridge node, the crossing itself (resolved into the neighbouring sector). One whose direction is within
// about 20 degrees of straight back (dot < -3849 with the arrival direction) is kept only as the fallback (the last
// such). With a best direction the most aligned candidate wins (ties: the later one), otherwise
// Rand16Critical(count). A candidate that is a bridge node and was not resolved is resolved at the end.
bool Pedestrians::nextNode(Game& g, const NodeRef& cur, const int32_t from[2], const int32_t* best, NodeRef& out) const {
    const Collision::PedPaths* paths = g.collision.pedPaths(cur.cx, cur.cy);
    if (!paths || cur.index >= paths->nodes.size()) return false;
    const Collision::PathNode& node = paths->nodes[cur.index];
    int32_t here[3];
    nodePos(node, here);
    int32_t arrive[2] = {here[0] - from[0], here[1] - from[1]};
    if (arrive[0] || arrive[1]) Normalise2(arrive);
    const int linkCount = node.flags >> 2 & 7, count = linkCount + (node.flags >> 7 & 1);
    struct Candidate { NodeRef id; int32_t dir[2]; bool resolved; };
    std::vector<Candidate> accepted;
    NodeRef fallback;
    for (int i = 0; i < count; ++i) {
        NodeRef cand = cur;
        if (i != linkCount) {
            const size_t li = (size_t)node.first + i;
            if (li >= paths->links.size() || paths->links[li] >= paths->nodes.size()) continue;
            cand = {cur.cx, cur.cy, paths->links[li]};
        }
        Candidate c{cand, {0, 0}, false};
        bool plain = !(paths->nodes[cand.index].flags & 0x80);
        if (!plain) {
            NodeRef across;
            int32_t at[3];
            if (resolveBridge(g, cand, across) && nodeAt(g, across, at)) {
                c.id = across;
                c.resolved = true;
                c.dir[0] = at[0] - here[0];
                c.dir[1] = at[1] - here[1];
            } else if (!(cand == cur)) {
                plain = true;   // sRandomNode +0x28: other bridge nodes count as plain ones
            } else {
                continue;       // its own crossing into a sector without nodes (no cHoldSector here)
            }
        }
        if (plain) {
            int32_t at[3];
            if (cand == cur) {   // (the crossing of an unloaded neighbour: away from the first link)
                const size_t li = node.first;
                if (li >= paths->links.size() || paths->links[li] >= paths->nodes.size()) continue;
                nodePos(paths->nodes[paths->links[li]], at);
                c.dir[0] = here[0] - at[0];
                c.dir[1] = here[1] - at[1];
            } else {
                nodePos(paths->nodes[cand.index], at);
                c.dir[0] = at[0] - here[0];
                c.dir[1] = at[1] - here[1];
            }
        }
        if (c.dir[0] || c.dir[1]) Normalise2(c.dir);
        if ((int32_t)(((int64_t)arrive[0] * c.dir[0] + (int64_t)arrive[1] * c.dir[1]) >> 12) < -3849) fallback = cand;
        else accepted.push_back(c);
    }
    if (accepted.empty()) {
        if (!fallback.valid()) return false;
        out = fallback;
        return true;
    }
    const Candidate* pick = nullptr;
    if (best) {
        int32_t top = -4096000;
        for (const Candidate& c : accepted) {
            const int32_t d = (int32_t)(((int64_t)best[0] * c.dir[0] + (int64_t)best[1] * c.dir[1]) >> 12);
            if (top <= d) { top = d; pick = &c; }
        }
    } else {
        pick = &accepted[Rand16Critical((uint32_t)accepted.size())];
    }
    out = pick->id;
    const Collision::PedPaths* outPaths = g.collision.pedPaths(out.cx, out.cy);
    if (!pick->resolved && outPaths && out.index < outPaths->nodes.size() && (outPaths->nodes[out.index].flags & 0x80)) {
        NodeRef across;
        if (resolveBridge(g, out, across)) out = across;
    }
    return true;
}

// Arrived at cur: the next node (cWanderPath::GetNextNode -> cSectorNodeData::GetRandomNode, arriving from the
// previous node), then cWanderPath::SetNext.
bool Pedestrians::advance(Game& g, Ped& p) {
    int32_t from[3];
    if (!p.prev.valid() || !nodeAt(g, p.prev, from)) nodeAt(g, p.cur, from);
    NodeRef next;
    if (!nextNode(g, p.cur, from, nullptr, next)) return false;
    p.prev = p.cur;
    p.cur = next;
    setTarget(g, p);
    return true;
}

void Pedestrians::spawn(Game& g, int type) {   // cPopulationManager::GenerateRandomPed
    int32_t pos[3];
    NodeRef from, to;
    if (!createPos(g, pos, from, to)) return;
    int32_t f[3];
    g.focus(f);
    const uint32_t t = g.world.timeCycle().time();
    const Traffic::ZoneInfo& z = g.traffic.zoneOrDefault(f[0], f[1], t - 0x14000u < 0xFFFF3000u);
    Ped p;
    p.uid = nextUid_++;
    p.type = type;
    p.subtype = 0;
    // CreateRandomPed: gangs and other ped types in mask 0xC99A9 use subtype 1; civilians roll one from the zone's
    // ped makeup (cPopInfoManager::GetSubType -> cPedInfoManager::ResolveId); cops use GetCopSubType (0 when not wanted)
    if (type >= 0 && type <= 0x13 && (0xC99A9u >> type & 1)) p.subtype = 1;
    else if (type == 1) {
        if (const auto* makeup = g.traffic.pedMakeup(z.pedMakeup)) {
            int total = 0;
            for (auto [id, w] : *makeup) total += w;
            int r = total ? (int)Rand16Critical((uint32_t)total) : 0;
            for (auto [id, w] : *makeup) {
                if ((r -= w) >= 0) continue;
                if (id < records_.size()) p.subtype = records_[id].subtype;
                break;
            }
        }
    }
    if (!record(p.type, p.subtype)) {   // no such ped in pedinfo.bin: use the first of its type
        p.subtype = 0;
        if (!record(p.type, 0)) return;
    }
    dress(p, z.sex);
    std::copy(pos, pos + 3, p.body.pos);
    p.body.placeOnGround(&g.collision);
    p.prev = from;
    p.cur = to;
    setTarget(g, p);
    p.body.setHeading(padAngle(p.target[0] - pos[0], p.target[1] - pos[1]));
    peds.push_back(std::move(p));
}

// A vehicle over the ped: below cPedCollisionData's gentle-contact speed (speed x 2 < 0x3000) it pushes the ped
// out of its box; faster, the ped is knocked down (the on-ground death animation also used for the player).
void Pedestrians::runOver(Game& g, Ped& p) {
    for (const Vehicle& v : g.cars) {
        const int32_t rel[3] = {p.body.pos[0] - v.pos[0], p.body.pos[1] - v.pos[1], p.body.pos[2] - v.pos[2]};
        if (std::abs(rel[2]) > 0x3000 || std::abs(rel[0]) > 0x10000 || std::abs(rel[1]) > 0x10000) continue;
        const int32_t across = (int32_t)(((int64_t)rel[0] * v.right[0] + (int64_t)rel[1] * v.right[1]) >> 12);
        const int32_t along = (int32_t)(((int64_t)rel[0] * v.fwd[0] + (int64_t)rel[1] * v.fwd[1]) >> 12);
        const int32_t ex = v.hx + Player::kRadius / 2, ey = v.hy + Player::kRadius / 2;
        if (std::abs(across) >= ex || std::abs(along) >= ey) continue;
        const int32_t speed = v.speed();
        if (speed * 2 >= 0x3000 && p.deadFrames < 0) {
            p.deadFrames = 0;
            p.body.playOneShot(0x18, 0x19, false);
            TheSound().collision(v, std::min(127, speed >> 10));
            TheSound().pedDeath(p.body.pos, p.male, false);
            Plugins_EmitEvent(CTW_EVENT_PED_KNOCKED_DOWN, v.uid, v.infoId, (int)p.uid);
        }
        // out of the box along the shallower side
        const int32_t outX = ex - std::abs(across), outY = ey - std::abs(along);
        const int32_t push = std::min(outX, outY) + 0x100;
        const int32_t sx = across < 0 ? -1 : 1, sy = along < 0 ? -1 : 1;
        for (int i = 0; i < 2; ++i)
            p.body.pos[i] += outX < outY ? (int32_t)(((int64_t)sx * push * v.right[i]) >> 12)
                                         : (int32_t)(((int64_t)sy * push * v.fwd[i]) >> 12);
    }
}

// cWanderPath::HandleEvasiveManeuvers. The original asks cLocalAreaKnowledge::GetIntercept for a vehicle reaching the
// ped within 30 frames; this uses the car's straight line over the next second. Within 25 units of the player in a
// car: a ped that has rolled away sprints; otherwise one in the path of the car, driven at 10 units/s or more and
// heading towards it, rolls along the car's right axis, to a random side (Rand16Critical(2)).
bool Pedestrians::evade(Game& g, Ped& p) {
    if (g.playerCar < 0 || dist2(p.body.pos, g.player.pos) > (int64_t)kNearPlayer * kNearPlayer) return false;
    if (p.evading) return true;
    const Vehicle& car = g.cars[g.playerCar];
    const int64_t speed2 = (int64_t)car.vel[0] * car.vel[0] + (int64_t)car.vel[1] * car.vel[1] + (int64_t)car.vel[2] * car.vel[2];
    if (speed2 < 0x64000001LL) return false;                                      // 10 units/s
    const int64_t rel[2] = {(int64_t)p.body.pos[0] - car.pos[0], (int64_t)p.body.pos[1] - car.pos[1]};
    if (rel[0] * car.fwd[0] + rel[1] * car.fwd[1] < 1) return false;              // the car heads towards the ped
    // closest approach along the car's velocity within 30 frames
    const double vx = car.vel[0] / 30.0, vy = car.vel[1] / 30.0, vv = vx * vx + vy * vy;
    const double t = std::clamp((rel[0] * vx + rel[1] * vy) / vv, 0.0, 30.0);
    const double cx = rel[0] - vx * t, cy = rel[1] - vy * t;
    const double reach = (double)car.hx + Player::kRadius + 0x1000;
    if (cx * cx + cy * cy > reach * reach) return false;
    const int side = Rand16Critical(2) ? -1 : 1;
    const int32_t dir[2] = {side * car.right[0], side * car.right[1]};
    startRoll(p, dir);
    p.evading = true;
    return true;
}

// cRollOutOfVehicle on foot -> cAnimation(state 0xB, direction, strength 0x14): the roll animations 0x3C / 0x3D and
// ApplyPhysicalEffect(0), a push of strength x 1 unit/s along the direction. The original also turns the ped to one
// of four quadrants of the direction (DetermineQuadrant); here it faces the roll.
void Pedestrians::startRoll(Ped& p, const int32_t dir[2]) {
    p.body.setHeading(padAngle(dir[0], dir[1]));
    p.body.playOneShot(0x3C, 0x3D, false);
    p.body.vel[0] = p.body.vel[1] = p.body.vel[2] = 0;
    p.rollVel[0] = dir[0] * 0x14;
    p.rollVel[1] = dir[1] * 0x14;
    p.rollFrames = 0;
}

// cAnimation::ApplyPhysics while rolling on the ground: the speed drops by min(0xFAE, max(0x28, 17 v^2 / 512 x 40))
// / 4096 a frame (v^2 in units^2/s^2, Q12) and stops below 0.5 units/s. Walls stop the roll.
bool Pedestrians::roll(Game& g, Ped& p) {
    if (p.rollFrames < 0) return false;
    ++p.rollFrames;
    const int64_t speed2 = (int64_t)p.rollVel[0] * p.rollVel[0] + (int64_t)p.rollVel[1] * p.rollVel[1];
    if (speed2 < 0x400000) p.rollVel[0] = p.rollVel[1] = 0;
    else {
        const int64_t s = speed2 >> 12;
        const int64_t k = std::clamp((((s + (s << 4)) >> 9) * 40) >> 12, (int64_t)0x28, (int64_t)0xFAE);
        for (int i = 0; i < 2; ++i) p.rollVel[i] = (int32_t)(((int64_t)p.rollVel[i] * (0x1000 - k)) >> 12);
    }
    if (p.rollVel[0] || p.rollVel[1]) {
        const int32_t a[3] = {p.body.pos[0], p.body.pos[1], p.body.pos[2] + 0x1000};
        const int32_t b[3] = {a[0] + p.rollVel[0] / 30, a[1] + p.rollVel[1] / 30, a[2]};
        if (g.collision.staticLine(a, b, 0x40000000 | 0x400 | 0x200 | 0x100)) p.rollVel[0] = p.rollVel[1] = 0;
        else { p.body.pos[0] = b[0]; p.body.pos[1] = b[1]; p.body.placeOnGround(&g.collision); }
    }
    if (p.body.stepOneShot(&g.pedSprites)) p.rollFrames = -1;
    return true;
}

int Pedestrians::reactionFor(const Ped& p, int action) const {
    const Record* r = record(p.type, p.subtype);
    if (!r) return None;
    int reaction = None;
    for (const Reaction& x : r->reactions) {   // records for this action or any (9); the first roll within its chance
        if (x.action != action && x.action != 9) continue;
        if (Rand32Critical(100) <= x.chance) { reaction = x.reaction; break; }
    }
    // cPed +0x220 = 0 (the women's body set): attacking, getting away and rolling become fleeing
    if (p.body.bodySet == 0 && (reaction == Attack || reaction == FleeOrGetIn || reaction == Roll)) reaction = Flee;
    return reaction;
}

void Pedestrians::react(Game& g, Ped& p, int action, const int32_t from[3], uint32_t fromCar) {
    if (p.deadFrames >= 0 || p.flee.on || p.rollFrames >= 0) return;
    const int r = reactionFor(p, action);
    if (r == Roll) {   // cRollOutOfVehicle away from the threat
        const double dx = (double)p.body.pos[0] - from[0], dy = (double)p.body.pos[1] - from[1];
        const double len = std::sqrt(dx * dx + dy * dy);
        const int32_t dir[2] = {len > 0 ? (int32_t)(dx / len * 4096) : 0, len > 0 ? (int32_t)(dy / len * 4096) : 0x1000};
        startRoll(p, dir);
        return;
    }
    if (r != Flee && r != FleeOrGetIn && r != Attack) return;   // (attacking is not ported: the ped flees)
    // cFleeOnFoot::SetupFlee: on the pavement network, follow it from whichever end of its link leads away;
    // without one, run straight away
    p.flee = {};
    p.flee.on = true;
    std::copy(from, from + 3, p.flee.from);
    p.flee.fromCar = fromCar;
    p.evading = false;
    int32_t a[3], b[3];
    if (p.prev.valid() && nodeAt(g, p.prev, a) && nodeAt(g, p.cur, b)) {
        const int64_t away[2] = {(int64_t)p.body.pos[0] - from[0], (int64_t)p.body.pos[1] - from[1]};
        const int64_t toA = ((int64_t)a[0] - p.body.pos[0]) * away[0] + ((int64_t)a[1] - p.body.pos[1]) * away[1];
        const int64_t toB = ((int64_t)b[0] - p.body.pos[0]) * away[0] + ((int64_t)b[1] - p.body.pos[1]) * away[1];
        if (toA > toB) { std::swap(p.prev, p.cur); std::swap(a, b); }
        p.target[0] = b[0]; p.target[1] = b[1];
    } else {
        p.flee.straight = true;
    }
}

void Pedestrians::explosion(Game& g, const int32_t pos[3]) {
    // cBaseExplosion::AffectEntities for type 4: every ped within 22 units gets an sDamageInfo of weapon type 6
    // (qword_481768[type - 4]), which cSimpleMover::GetAction makes action 3; cPed::ReactToDamage reacts to it
    for (Ped& p : peds)
        if (dist2(p.body.pos, pos) <= (int64_t)kExplosionReach * kExplosionReach) react(g, p, 3, pos, 0);
}

// cFleeOnFoot::Process: sprint (ConstrainWalkSpeed(3, 3)); at each node GetNextNode takes the link whose direction
// best matches the way away from the threat (it may turn back), or runs straight away off the network
// (RecomputeFleeTargetForStraightLine: 200 units from the ped, away from the threat).
void Pedestrians::fleeStep(Game& g, Ped& p) {
    for (const Vehicle& v : g.cars)
        if (p.flee.fromCar && v.uid == p.flee.fromCar) std::copy(v.pos, v.pos + 3, p.flee.from);
    const int32_t* from = p.flee.from;
    auto awayFrom = [&](const int32_t at[2], double& ux, double& uy) {
        const double dx = (double)at[0] - from[0], dy = (double)at[1] - from[1], len = std::sqrt(dx * dx + dy * dy);
        ux = len > 0 ? dx / len : 0;
        uy = len > 0 ? dy / len : 1;
    };
    if (!p.flee.straight && dist2(p.body.pos, p.target) <= (int64_t)kFleeArrive * kFleeArrive) {
        // cFleeOnFoot::GetNextNode: sRandomNode from the threat, SetBestDir(normalised node - threat)
        int32_t here[3];
        NodeRef next;
        int32_t away[2] = {0, 0};
        if (nodeAt(g, p.cur, here)) {
            away[0] = here[0] - from[0];
            away[1] = here[1] - from[1];
            if (away[0] || away[1]) Normalise2(away);
        }
        int32_t at[3];
        if (!nodeAt(g, p.cur, here) || !nextNode(g, p.cur, from, away, next) || !nodeAt(g, next, at)) p.flee.straight = true;
        else {
            p.prev = p.cur;
            p.cur = next;
            p.target[0] = at[0];
            p.target[1] = at[1];
        }
    }
    if (p.flee.straight) {
        if ((!p.flee.to[0] && !p.flee.to[1]) || dist2(p.body.pos, p.flee.to) <= (int64_t)kFleeArrive * kFleeArrive) {
            double ux, uy;
            awayFrom(p.body.pos, ux, uy);
            p.flee.to[0] = p.body.pos[0] + (int32_t)(ux * 0xC8000);
            p.flee.to[1] = p.body.pos[1] + (int32_t)(uy * 0xC8000);
        }
        p.target[0] = p.flee.to[0];
        p.target[1] = p.flee.to[1];
    }
    Player::Input in;
    in.moving = true;
    in.heading = padAngle(p.target[0] - p.body.pos[0], p.target[1] - p.body.pos[1]);
    in.sprint = true;
    in.maxLevel = 3;
    const int32_t before[2] = {p.body.pos[0], p.body.pos[1]};
    p.body.update(in, &g.collision, &g.pedSprites);
    // PC approximation (as in wander): blocked for 1.5 seconds, it switches between the network and straight away
    p.stuckFrames = dist2(before, p.body.pos) < 0x40 * 0x40 ? p.stuckFrames + 1 : 0;
    if (p.stuckFrames > 45) {
        p.flee.straight = !p.flee.straight || !p.prev.valid();
        p.flee.to[0] = p.flee.to[1] = 0;
        int32_t back[3];
        if (!p.flee.straight && (std::swap(p.cur, p.prev), nodeAt(g, p.cur, back))) { p.target[0] = back[0]; p.target[1] = back[1]; }
        p.stuckFrames = 0;
    }
}

void Pedestrians::wander(Game& g, Ped& p) {   // cWanderPath::Process (walking part)
    if (roll(g, p)) return;
    if (p.flee.on) { fleeStep(g, p); return; }
    if (!evade(g, p)) p.evading = false;      // cWanderPath +0x6A is cleared once the player's car is gone
    if (roll(g, p)) return;
    if (dist2(p.body.pos, p.target) <= (int64_t)kArrive * kArrive && !advance(g, p)) return;
    Player::Input in;
    in.moving = true;
    in.heading = padAngle(p.target[0] - p.body.pos[0], p.target[1] - p.body.pos[1]);
    // ConstrainWalkSpeed: walk; joggers (civilian subtype 1) and everyone in rain run; after a roll, sprint
    in.maxLevel = 1;
    if ((p.type == 1 && p.subtype == 1) || g.world.timeCycle().value(28) >= 2048.f) in.maxLevel = 2;
    if (p.evading) { in.maxLevel = 3; in.sprint = true; }
    const int32_t before[2] = {p.body.pos[0], p.body.pos[1]};
    p.body.update(in, &g.collision, &g.pedSprites);
    // PC approximation: a ped that makes no progress for 1.5 seconds (blocked by something the pavement graph
    // ignores) turns round instead of walking on the spot.
    p.stuckFrames = dist2(before, p.body.pos) < 0x40 * 0x40 ? p.stuckFrames + 1 : 0;
    if (p.stuckFrames > 45 && p.prev.valid()) {
        std::swap(p.cur, p.prev);
        setTarget(g, p);
        p.stuckFrames = 0;
    }
}

void Pedestrians::update(Game& g) {
    // still, upright vehicles are boxes for walking peds (as for the player in Game::tick)
    std::vector<Collision::Box> still;
    for (const Vehicle& c : g.cars) {
        if (c.vel[0] || c.vel[1] || c.vel[2] || c.up[2] < 0xFD8) continue;
        still.push_back({c.pos[0], c.pos[1], c.pos[2], c.hx, c.hy, c.hz, (int16_t)-c.heading(), 0});
    }
    for (Ped& p : peds) {
        if (p.deadFrames >= 0) {
            ++p.deadFrames;
            p.body.stepOneShot(&g.pedSprites);
            continue;
        }
        p.body.obstacles = still;
        wander(g, p);
        runOver(g, p);
        // PC approximation of ped/player contact: walkers step aside instead of walking through the player
        if (g.playerCar < 0 && !g.player.hidden) {
            const int64_t d = dist2(p.body.pos, g.player.pos), r = (int64_t)Player::kRadius * 2;
            if (d < r * r && d > 0) {
                const double len = std::sqrt((double)d), k = (r - len) / len;
                p.body.pos[0] += (int32_t)((p.body.pos[0] - g.player.pos[0]) * k);
                p.body.pos[1] += (int32_t)((p.body.pos[1] - g.player.pos[1]) * k);
            }
        }
    }
    // cPed::ShouldBeDestroyed: about 85 units from the player and off screen
    int32_t f[3];
    g.focus(f);
    peds.erase(std::remove_if(peds.begin(), peds.end(), [&](const Ped& p) {
        return dist2(p.body.pos, f) >= kRemoveDist2 && !g.canSee(p.body.pos, 2.f);
    }), peds.end());

    // cPopulationManager::Update (peds): the target count from the zone's ped density, then the spawn schedule
    const uint32_t t = g.world.timeCycle().time();
    const Traffic::ZoneInfo& z = g.traffic.zoneOrDefault(f[0], f[1], t - 0x14000u < 0xFFFF3000u);
    maxPeds = std::min(64, (int)(std::min(16, 20 * z.pedDensity * 100 / 7500) * densityScale));
    if (!maxPeds || records_.empty() || g.traffic.firedThisFrame) return;
    const int32_t* vel = g.playerCar >= 0 ? g.cars[g.playerCar].vel : g.player.vel;
    const int64_t speed = (int64_t)std::sqrt((double)vel[0] * vel[0] + (double)vel[1] * vel[1] + (double)vel[2] * vel[2]);
    const int64_t q = (int64_t)(0x9100000000000ULL / (uint64_t)(speed + 0x3F48)) >> 20;
    const int32_t k = (int32_t)(30 * q / maxPeds);
    const int period = k <= 0x1000 ? 1 : k <= 0x3000 ? 2 : k <= 0x6000 ? 4 : k <= 0xC000 ? 8 : k <= 0x18000 ? 16 : k <= 0x30000 ? 32 : 64;
    // cFrameSchedule::Fired: ((frame + phase) & (period - 1)) == 0. SetTiming gives each schedule the least loaded of
    // 64 frame bins; the vehicle schedule holds bin 0, so the ped schedule runs one frame after it.
    if (((g.frame + 1) & (uint32_t)(period - 1)) || alive() >= maxPeds) return;
    int total = 0;
    for (int v : z.fields) total += v;
    if (!total) return;
    int r = (int)Rand32Critical((uint32_t)total), type = -1;
    for (int i = 0; i < 20; ++i) {
        if (r < z.fields[i]) { type = i; break; }
        r -= z.fields[i];
    }
    if (type >= 0) spawn(g, type);
}

void Pedestrians::render(const PedSprites* sprites, const PedLight* light) const {
    for (const Ped& p : peds) p.body.render(sprites, light);
}
