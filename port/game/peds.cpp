// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "peds.h"
#include "game.h"
#include "explosions.h"
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
constexpr int32_t kArrive = 0x1000;                  // native completion (0x6EE9B8) clamps the target radius to 1
constexpr int32_t kFleeArrive = 0x2000;              // cFleeOnFoot: cTarget::SetNode(node, 2.0)
constexpr int32_t kNearPlayer = 0x19000;             // HandleEvasiveManeuvers: 25 units
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

// CCollision::Process2 sphere against the vehicle's oriented local box. Keep
// the contact normal in world space, including pitch/roll; do not shove feet
// to the side of an upright XY rectangle.
bool vehicleContact(Player& body, const Vehicle& car, int32_t normal[3], int32_t& depth) {
    int32_t local[3]{}, end[3]{}, atVelocity[3];
    car.velocityAtWorldPos(body.pos, atVelocity);
    const int16_t* axes[3] = {car.right, car.fwd, car.up};
    for (int i = 0; i < 3; ++i) {
        int64_t p = 0, v = 0;
        for (int k = 0; k < 3; ++k) {
            p += ((int64_t)body.pos[k] + (k == 2 ? Player::kRadius : 0) - car.pos[k]) * axes[i][k];
            v += ((int64_t)body.vel[k] - atVelocity[k]) * axes[i][k];
        }
        local[i] = (int32_t)(p >> 12);
        end[i] = local[i] + (int32_t)(((v >> 12) * 17) >> 9);
    }
    const Collision::Box box{0, 0, car.hz, car.hx, car.hy, car.hz, 0, 0};
    int32_t hit[3], n[3], time;
    depth = 0;
    if (!Collision::sphereVBox(local, Player::kRadius, box, hit, n, depth)) {
        if (!Collision::sweptSphereVBox(local, end, Player::kRadius, box, hit, time)) return false;
        double length2 = 0;
        for (int i = 0; i < 3; ++i) {
            n[i] = local[i] + (int32_t)(((int64_t)(end[i]-local[i])*time)>>12) - hit[i];
            length2 += (double)n[i]*n[i];
        }
        if (length2 <= 0) return false;
        const double factor = 4096 / std::sqrt(length2);
        for (int32_t& v : n) v = (int32_t)std::lround(v * factor);
    }
    for (int k = 0; k < 3; ++k)
        normal[k] = (int32_t)(((int64_t)n[0]*car.right[k] + (int64_t)n[1]*car.fwd[k] + (int64_t)n[2]*car.up[k]) >> 12);
    // cPedCollisionData's gentle-contact branch sets its slow-vehicle flag,
    // then still applies the relative-velocity impulse (0x8860E8 -> 0x886204).
    body.dynamicContact(normal, atVelocity, depth);
    return true;
}

bool sphereContact(const Player& a, const Player& b, int32_t normal[3]) {
    // The dynamic sphere is kRadius; only static world sweeps use kSphere.
    double rel[3], move[3], distance2 = 0, vv = 0, rv = 0;
    for (int i = 0; i < 3; ++i) {
        rel[i] = (double)a.pos[i] - b.pos[i];
        move[i] = (double)(((int64_t)a.vel[i] * 17) >> 9);
        distance2 += rel[i]*rel[i]; vv += move[i]*move[i]; rv += rel[i]*move[i];
    }
    const double radius = 2 * Player::kRadius;
    if (distance2 >= radius*radius) {
        const double disc = rv*rv - vv*(distance2-radius*radius);
        if (vv == 0 || rv >= 0 || disc < 0) return false;
        const double t = (-rv-std::sqrt(disc))/vv;
        if (t < 0 || t > 1) return false;
        distance2 = 0;
        for (int i = 0; i < 3; ++i) { rel[i] += move[i]*t; distance2 += rel[i]*rel[i]; }
    }
    const double scale = distance2 > 0 ? 4096 / std::sqrt(distance2) : 0;
    for (int i = 0; i < 3; ++i) normal[i] = (int32_t)std::lround(rel[i]*scale);
    return true;
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
        uint32_t enemies, friends;
        memcpy(&enemies,&d[o],4);memcpy(&friends,&d[o+4],4);
        if ((size_t)enemies+7<=d.size()) std::copy_n(d.data()+enemies,7,r.enemies.begin());
        if ((size_t)friends+7<=d.size()) std::copy_n(d.data()+friends,7,r.friends.begin());
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
    // The palette bytes as the original keeps them: u = the upper sprite's (+0x3D and +0x3E), a and l = the legs
    // sprite's +0x3E (its first layer) and +0x3D (the rest).
    int body = 0, u, a, l;
    const auto maleChoice = [&] { u = a = (int)Rand16Critical(5); l = (int)Rand16Critical(5); };          // ChooseRandomMalePalette
    const auto femaleChoice = [&] { u = a = 30 + (int)Rand16Critical(5); l = 30 + (int)Rand16Critical(5); };   // ChooseRandomFemalePalette
    const auto setMale = [&](bool male, int type, int subtype) {   // cPed::SetMale
        const Record* info = record(type, subtype);
        if (info && info->maleOnly && male) return;
        if (info && info->femaleOnly && !male) return;
        if (type == 10 || subtype == 3 || subtype == 4) return;
        body = male ? 1 : 0;
        if (!male) femaleChoice();
        else if ((unsigned)(a - 30) <= 4) maleChoice();   // only a woman's palettes are replaced
    };
    // cPed::cPed: each cPedSprite takes cPedInfoManager::RandomColour (Rand16Critical(5)); SetMale(Rand16Critical(100)
    // > 40) for a type-1 subtype-0 ped; then the legs take their first layer's colour throughout, and an upper body
    // in the player's palette 4 is given palette 5.
    u = (int)Rand16Critical(5);
    a = l = (int)Rand16Critical(5);
    setMale(Rand16Critical(100) > 40, 1, 0);
    l = a;
    if (u == 4) u = 5;
    a = u;
    Rand32Critical(8);   // cPed::cPed: +0x338 bit 6 (Rand32Critical(8) > 1)
    // cPed::SetPedType
    const int t = p.type, sub = p.subtype;
    if (t == 10) {   // cops: a uniform per subtype
        static const int cop[4] = {0x10, 0x12, 0x0C, 0x11};
        body = 1;
        u = a = l = sub >= 0 && sub < 4 ? cop[sub] : 0x10;
    } else if (t == 13 || t == 16) {   // dressed as the player: palettes 4 and 5
        body = 1;
        u = a = 4; l = 5;
    } else if (t == 1) {
        if (sub == 3 || sub == 4) { body = 1; u = a = l = sub == 3 ? 0x0D : 0x0E; }
    } else if (t != 4 && t != 9) body = 1;   // other types are men (in whatever palettes cPed::cPed chose)
    const Record* r = record(t, sub);
    if (r && r->maleOnly) body = 1;
    else if (r && r->femaleOnly) { body = 0; femaleChoice(); }
    if (r && r->upperPalette) { u = a = r->upperPalette - 1; l = r->legsPalette - 1; }
    // cPopulationManager::CreateRandomPed: a civilian takes the zone's sex makeup
    if (t == 1 && (zoneSex == 1 || zoneSex == 2)) setMale(zoneSex == 1, t, sub);
    p.male = body != 0;
    p.body.bodySet = body;   // cPed +0x220 selects the body set (0x113 animations each)
    p.body.palUpper = u;     // (a is always u here: the legs' first layer is drawn in palUpper)
    p.body.palLegs = l;
}

void Pedestrians::setTarget(Game& g, Ped& p) {   // cWanderPath::SetNext
    int32_t at[3];
    if (!nodeAt(g, p.cur, at)) return;
    p.target[0] = at[0] + jitter();
    p.target[1] = at[1] + jitter();
}

bool Pedestrians::nodeAt(Game& g, const NodeRef& n, int32_t out[3]) {
    if (n.network == 0) {
        if (!n.valid() || n.index >= g.roads.count()) return false;
        g.roads.pos(n.index, out);
        return true;
    }
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
    if (cur.network == 0) {
        if (!cur.valid() || cur.index >= g.roads.count()) return false;
        std::vector<uint16_t> candidates;
        for (int k = 0; k < g.roads.linkCount(cur.index); ++k) {
            const int next = g.roads.link(cur.index, k);
            if (next < g.roads.count() && !(g.roads.node(next).flags & 0x803)) candidates.push_back((uint16_t)next);
        }
        if (candidates.empty()) return false;
        // Native world-node choice without previous-node/lane hints (0x6EF6D4).
        out = {0, 0, candidates[Rand16Critical((uint32_t)candidates.size())], 0};
        return true;
    }
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
    // The native road fallback chooses from its previous global node; sector paths choose from the arrival node.
    const NodeRef source = p.prev.valid() && p.prev.network == 0 ? p.prev : p.cur;
    if (!nextNode(g, source, from, nullptr, next) || next == p.cur) return false;
    p.prev = p.cur;
    p.cur = next;
    setTarget(g, p);
    return true;
}

void Pedestrians::generate(Game& g, const Traffic::ZoneInfo& z) {
    if (records_.empty()) return;
    int total = 0;
    for (int v : z.fields) total += v;
    if (!total) return;
    int r = (int)Rand32Critical((uint32_t)total), type = -1;
    for (int i = 0; i < 20; ++i) {
        if (r < z.fields[i]) { type = i; break; }
        r -= z.fields[i];
    }
    if (type >= 0) spawn(g, type, z);
}

void Pedestrians::collect(Game& g) {
    peds.erase(std::remove_if(peds.begin(), peds.end(), [&](const Ped& p) {
        if (p.vehicle) return false; // its vehicle owns the lifetime of an occupant
        return !g.sectors.loadedAt(p.body.pos[0], p.body.pos[1]);
    }), peds.end());
}

// cPopulationManager::GenerateRandomPed: cLocalAreaKnowledge::GetCreatePosition (pavement, 60 units around the
// player, off screen, in a loaded sector), then cWanderPath from the link's first node to its second. (The original
// sets the heading from an unwritten stack value; the port faces the first target.)
void Pedestrians::spawn(Game& g, int type, const Traffic::ZoneInfo& z) {
    int32_t pos[3];
    NodeRef from, to;
    if (!g.localArea.createPosition(g, g.player.pos, kSpawnRadius, pos, from, to)) return;
    Ped p;
    if (!appearance(g,p,type,z)) return;
    std::copy(pos, pos + 3, p.body.pos);
    p.body.placeOnGround(&g.collision);
    p.prev = from;
    p.cur = to;
    setTarget(g, p);
    p.body.setHeading(padAngle(p.target[0] - pos[0], p.target[1] - pos[1]));
    peds.push_back(std::move(p));
}

bool Pedestrians::appearance(Game& g, Ped& p, int type, const Traffic::ZoneInfo& z, int subtype) {
    p.uid = nextUid_++;
    p.type = type;
    p.subtype = 0;
    // CreateRandomPed: gangs and other ped types in mask 0xC99A9 use subtype 1; civilians roll one from the zone's
    // ped makeup (cPopInfoManager::GetSubType -> cPedInfoManager::ResolveId); cops use GetCopSubType (0 when not wanted)
    if (type >= 0 && type <= 0x13 && (0xC99A9u >> type & 1)) p.subtype = 1;
    else if (type == 10) p.subtype = subtype >= 0 ? subtype : Traffic::copSubType(g);
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
        if (!record(p.type, 0)) return false;
    }
    dress(p, z.sex);
    p.body.playerControlled = false;
    p.wander.socialStart = (uint16_t)g.frame;
    p.wander.initialized = true;
    return true;
}

bool Pedestrians::createDriver(Game& g, Vehicle& car, int type, const Traffic::ZoneInfo& zone, int subtype) {
    if (!car.uid || car.dead() || car.seatUser[0] != -1) return false;
    Ped p;
    if (!appearance(g,p,type,zone,subtype)) return false;
    p.vehicle=car.uid; p.seat=0;
    car.seatOffset(0,p.seatPosition);
    car.localToWorld(p.seatPosition,p.body.pos);
    p.body.setHeading(car.heading());
    p.body.hidden=p.body.attached=true;
    car.seatUser[0]=(int)p.uid;
    peds.push_back(std::move(p));
    return true;
}

bool Pedestrians::exitOccupant(Game& g, Vehicle& car, int seat, bool forgetTraffic) {
    if(seat<0 || seat>=4) return false;
    if(car.seatUser[seat]==-1) return true;
    if(car.seatUser[seat]<0) return false;
    for(Ped& p:peds) if(p.uid==(uint32_t)car.seatUser[seat] && p.vehicle==car.uid) {
        if(p.exiting) return false;
        p.exiting=true; p.body.hidden=false;
        car.seatOffset(seat,p.seatPosition); p.seatPosition[2]-=0x2800;
        car.doorSpawnPoint(seat,p.exitTarget); p.exitTarget[1]-=0x800;
        p.body.playOneShot(0xE,0xF,(seat&1)!=0);
        const int anim=0xE + p.body.bodySet*0x113;
        const uint32_t rate=(uint32_t)(g.pedSprites.rate(anim)<<4)>>12;
        const uint32_t frames=rate ? g.pedSprites.numFrames(anim)*30/rate : 0;
        const int32_t fraction=frames ? 4096/(int32_t)frames : 0;
        for(int k=0;k<3;++k) p.exitVelocity[k]=(int16_t)(((int64_t)fraction*(p.exitTarget[k]-p.seatPosition[k]))>>12);
        car.openDoor(seat);
        if (forgetTraffic) g.traffic.forget(car.uid);
        car.setToPhysics(true);
        return false;
    }
    car.seatUser[seat]=-1; // stale mod-removed pedestrian handle
    return true;
}

void Pedestrians::updateOccupant(Game& g, Ped& p) {
    for(Vehicle& car:g.cars) if(car.uid==p.vehicle) {
        bool done=false;
        if(p.exiting) {
            done=p.body.stepOneShot(&g.pedSprites);
            for(int k=0;k<3;++k) p.seatPosition[k]=done ? p.exitTarget[k] : p.seatPosition[k]+p.exitVelocity[k];
        }
        car.localToWorld(p.seatPosition,p.body.pos);
        p.body.setHeading(car.heading());
        std::copy(car.vel,car.vel+3,p.body.vel);
        if(done) {
            if(car.seatUser[p.seat]==(int)p.uid) car.seatUser[p.seat]=-1;
            p.vehicle=0; p.seat=-1; p.exiting=false;
            p.body.hidden=p.body.attached=false;
            p.body.placeOnGround(&g.collision);
            car.engineOn=false;
            if(p.exitFlee.set) {   // cFleeOnFoot from a reaction: away from the threat
                p.exitFlee.set=false;
                const int32_t from[3]={p.exitFlee.from[0],p.exitFlee.from[1],p.exitFlee.from[2]};
                react(g,p,-1,from,p.exitFlee.fromCar);
            } else if (p.enforcer.leavingVehicle) {
                p.enforcer.leavingVehicle = false;
                p.flee = {};
            } else {
                // Until the full carjacking reaction task is ported, a displaced driver exits then flees.
                p.flee.on=p.flee.straight=true; p.flee.fromCar=car.uid;
                std::copy(car.pos,car.pos+3,p.flee.from);
            }
        }
        return;
    }
    p.vehicle=0; p.seat=-1; p.exiting=false;
    p.body.hidden=p.body.attached=false;
    p.body.placeOnGround(&g.collision);
}

void Pedestrians::releaseOccupants(Game& g, Vehicle& car, bool killed) {
    for(Ped& p:peds) if(p.vehicle==car.uid) {
        int32_t door[3]; car.doorSpawnPoint(p.seat,door); car.localToWorld(door,p.body.pos);
        if(car.seatUser[p.seat]==(int)p.uid) car.seatUser[p.seat]=-1;
        p.vehicle=0; p.seat=-1; p.exiting=false;
        p.body.hidden=p.body.attached=false;
        std::fill(p.body.vel,p.body.vel+3,0);
        p.body.placeOnGround(&g.collision);
        if(killed && p.body.takeDamage(p.body.health)) {
            p.deadFrames=0;
            TheSound().pedDeath(p.body.pos,p.male,false);
        }
    }
}

void Pedestrians::knockOffDriver(Game& g, Vehicle& car, const int32_t dir[3]) {
    for (Ped& p : peds) {
        if (p.vehicle != car.uid || p.seat != 0) continue;
        if (car.seatUser[0] == (int)p.uid) car.seatUser[0] = -1;
        p.vehicle = 0; p.seat = -1; p.exiting = false;
        p.body.hidden = p.body.attached = false;
        std::copy(car.pos, car.pos + 3, p.body.pos);   // HelperWarpOutOf: at the vehicle's position
        p.body.placeOnGround(&g.collision);
        std::fill(p.body.vel, p.body.vel + 3, 0);
        if (p.body.knockedOff(dir) || p.body.dead) { p.deadFrames = 0; p.rollFrames = -1; }
        g.addBlood(p.body.pos, 1);   // type 21 bleeds one drop
        if (p.body.dead) TheSound().pedDeath(p.body.pos, p.male, false);
        else react(g, p, 0, car.pos, 0);   // ReactToDamage: GetAction(21) is action 0
        return;
    }
}

void Pedestrians::discardOccupants(Vehicle& car) {
    peds.erase(std::remove_if(peds.begin(),peds.end(),[&](const Ped& p) {
        if(p.vehicle!=car.uid) return false;
        if(p.seat>=0 && p.seat<4 && car.seatUser[p.seat]==(int)p.uid) car.seatUser[p.seat]=-1;
        return true;
    }),peds.end());
}

void Pedestrians::runOver(Game& g, Ped& p) {
    for (const Vehicle& v : g.cars) {
        int32_t normal[3], depth;
        if (!vehicleContact(p.body, v, normal, depth)) continue;
        const bool wasDead = p.body.dead;
        const int health = p.body.health;
        const bool impact = p.body.vehicleImpact(v, normal, false);
        if (p.body.health != health) {
            g.addBlood(p.body.pos, 1);   // run-over damage (types 8/9): one drop
            if (g.playerCar >= 0 && &g.cars[g.playerCar] == &v) g.pedCrime(p, 8);
        }
        if (impact) {
            p.deadFrames = 0;
            p.rollFrames = -1;
            TheSound().collision(v, std::min(127, v.speed() >> 10));
            Plugins_EmitEvent(CTW_EVENT_PED_KNOCKED_DOWN, v.uid, v.infoId, (int)p.uid);
        }
        if (!wasDead && p.body.dead) {
            TheSound().pedDeath(p.body.pos, p.male, false);
            // cPed::HandlePreDead: a kill by the player's vehicle counts as the player's
            if (g.playerCar >= 0 && &v == &g.cars[g.playerCar]) Stats_PedKilledByPlayer(g, p.subtype);
        }
    }
}

// Native evasive-handler branch at 0x722210: the scheduled interception cache chooses the vehicle.
// A nearby player in a car preserves the run limit even when there is no qualifying intercept.
bool Pedestrians::evade(Game& g, Ped& p) {
    const int64_t dz = (int64_t)p.body.pos[2] - g.player.pos[2];
    if (g.playerCar < 0 || g.playerCar >= (int)g.cars.size() ||
        dist2(p.body.pos,g.player.pos)+dz*dz > (int64_t)kNearPlayer*kNearPlayer) return false;
    if (p.evading) return true;
    TrafficAvoidance::Intercept hit;
    if (!g.traffic.pedIntercept(g,p.uid,hit) || hit.time >= 30*4096 || hit.pedestrian) return true;
    // The native handler accepts driver types 13/16 (players); ordinary NPC drivers do not trigger its roll.
    const Vehicle& car=g.cars[g.playerCar];
    if (hit.uid != car.uid) return true;
    const int64_t speed2=(int64_t)car.vel[0]*car.vel[0]+(int64_t)car.vel[1]*car.vel[1]+(int64_t)car.vel[2]*car.vel[2];
    if (speed2 < 0x64000001LL) return true;
    int64_t ahead=0;
    for (int k=0;k<3;++k) ahead+=(int64_t)car.fwd[k]*(p.body.pos[k]-car.pos[k]);
    if (ahead < 1) return true;
    const int side = Rand16Critical(2) ? -1 : 1;
    const int32_t dir[2] = {side * car.right[0], side * car.right[1]};
    startRoll(p, dir);
    p.evading = true;
    return true;
}

// On-foot roll setup (0x6fd7bc): select its native facing quadrant while the impulse keeps its own direction.
// Sprite rotation is separate from body facing, as in 0x89ac44; a side roll must not turn the NPC toward its car.
void Pedestrians::startRoll(Ped& p, const int32_t dir[2]) {
    const int16_t angle=padAngle(dir[0],dir[1]);
    const int32_t x=(int32_t)(sinf((float)angle*9.587378e-05f)*4096.f);
    const int32_t y=(int32_t)(sinf((float)(angle+0x4000)*9.587378e-05f)*4096.f);
    const int32_t forward=(int32_t)(((int64_t)x*p.body.fwd[0]+(int64_t)y*p.body.fwd[1])>>12);
    const int64_t right=(int64_t)x*p.body.fwd[1]-(int64_t)y*p.body.fwd[0];
    const int quadrant=forward>2896 ? 0 : forward < -2896 ? 3 : right < -11862016 ? 1 : 2;
    int16_t facing=angle;
    uint8_t rotation=0;
    if (quadrant==1) { facing=padAngle(y,-x);rotation=2; }
    else if (quadrant==2) { facing=padAngle(-y,x);rotation=1; }
    else if (quadrant==3) { facing=(int16_t)(angle+0x7FFF);rotation=3; }
    p.body.setHeading(facing);
    p.body.playOneShot(0x3C,0x3D,false);
    p.body.setAnimationRotation(rotation);
    p.body.vel[0]=p.body.vel[1]=p.body.vel[2]=0;
    p.rollVel[0]=x*0x14;p.rollVel[1]=y*0x14;
    p.rollFrames=0;
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
    p.body.vel[0] = p.rollVel[0]; p.body.vel[1] = p.rollVel[1];
    p.movementPrepared = true;
    if (p.body.stepOneShot(&g.pedSprites)) p.rollFrames = -1;
    return true;
}

int Pedestrians::reactionFor(const Ped& p, int action, bool fromPlayer) const {
    const Record* r = record(p.type, p.subtype);
    if (!r) return None;
    int reaction = None;
    for (const Reaction& x : r->reactions) {   // records for this action or any (9); the first roll within its chance
        if (x.action != action && x.action != 9) continue;
        if (Rand32Critical(100) <= x.chance) { reaction = x.reaction; break; }
    }
    // cPed::AdjustPedInVehicleReaction
    if (reaction == VehicleReaction) {
        if (!p.vehicle) reaction = r->maleOnly ? Attack : Flee;
        else if (p.seat != 0) reaction = None;   // cVehicle::IsDriver
    } else if (reaction == Flee && p.vehicle && (action == 0 || action == 5)) {
        if (Rand16Critical(100) > 10) reaction = None;
    }
    // cPed::AdjustCopAgainstPlayerReaction
    if (p.type == 10 && fromPlayer && (action == 0 || action == 4)) reaction = None;
    // cPed::ValidateFemaleReaction (+0x220 = 0, the women's body set)
    if (p.body.bodySet == 0 && (reaction == Attack || reaction == FleeOrGetIn || reaction == Roll)) reaction = Flee;
    return reaction;
}

void Pedestrians::fleeFromVehicle(Game& g, Ped& p, Vehicle& car, const int32_t from[3], uint32_t fromCar) {
    if (p.vehicle != car.uid || p.exiting) return;
    p.exitFlee.set = true;
    std::copy(from, from + 3, p.exitFlee.from);
    p.exitFlee.fromCar = fromCar;
    exitOccupant(g, car, p.seat);
}

void Pedestrians::react(Game& g, Ped& p, int action, const int32_t from[3], uint32_t fromCar, bool fromPlayer) {
    if (p.deadFrames >= 0 || p.flee.on || p.rollFrames >= 0) return;
    const int r = action < 0 ? Flee : reactionFor(p, action, fromPlayer);   // (action -1: the reaction is already chosen)
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

// cPed::Damage's switch on sDamageInfo type: how many times it calls cGeneralParticleController::AddBlood.
// 3 (taser), 4, 5 and 13 bleed nothing; 12 (chainsaw) starts a cParticleEmitterBloodSpray, which is not ported.
static int bloodDrops(int type, int damage, int effect) {
    switch (type) {
        case 0: return effect == 0x1D ? 0 : 4;
        case 1: return damage >= 5 ? 15 : 0;
        case 2: return 15;
        case 3: case 4: case 5: case 12: case 13: return 0;
        default: return 1;
    }
}

void Pedestrians::meleeHit(Game& g, Ped& p, int damage, int type, const int32_t from[3], int effect, bool fromPlayer) {
    if (p.vehicle || p.deadFrames >= 0 || p.body.dead) return;   // CanBeHit: alive, not in a closed vehicle
    const int d = damage && 100 * damage < 100 ? 1 : 100 * damage / 100;   // FixDamageForArmour (cPed::Reset: 100)
    const bool killed = p.body.takeDamage(d);
    // cMeleeBasedWeapon::HitStuffHelper: the victim grunts (cAudioManager::DoFightGrunt(ped, false, false))
    TheSound().fightGrunt(p.uid, p.body.pos, p.body.bodySet != 0, false, false);
    // the hit animation (+0xEB: 2 / 16 or 102 / 116; the animation is not ported) comes before type 0's blood
    if (type == 0 && effect != 0x1D) Rand16Critical(2);
    g.addBlood(p.body.pos, bloodDrops(type, d, effect));
    if (killed) {
        p.deadFrames = 0;
        p.rollFrames = -1;
        TheSound().pedDeath(p.body.pos, p.male, false);
        if (fromPlayer) Stats_PedKilledByPlayer(g, p.subtype);   // cPed::HandlePreDead
        if (type == 0) Rand32Critical(100);
        if (fromPlayer) Stats_PlayerDamage(g, 0, type, effect);
        Plugins_EmitEvent(CTW_EVENT_PED_KNOCKED_DOWN, 0, -1, (int)p.uid);
        return;
    }
    react(g, p, 0, from, 0, fromPlayer);   // GetAction: types 0 and 12 are action 0
    // after HelperForRegisteringCrimesDoneToThisPed cPed::Damage rolls (and discards) Rand32Critical(100) for type 0
    if (type == 0) Rand32Critical(100);
    if (fromPlayer) Stats_PlayerDamage(g, p.body.health, type, effect);
}

void Pedestrians::bulletHit(Game& g, Ped& p, int damage, const int32_t dir[3], int effect, const int32_t from[3],
                            int type, bool fromPlayer) {
    (void)dir;
    (void)effect;   // ApplyDamageResponse for the weapon's effect (+0x30) is not ported yet
    if (p.vehicle || p.deadFrames >= 0 || p.body.dead) return;   // CanBeHit: alive, not in a closed vehicle
    if (fromPlayer) g.stats.incBulletsThatHit();   // the player's bullets (types 1 and 2)
    TheSound().bulletHitPed(g, p.uid);   // cPed::Damage: cAudioManager::AddBulletHitPed
    const int d = damage && 100 * damage < 100 ? 1 : 100 * damage / 100;   // FixDamageForArmour (cPed::Reset: 100)
    const bool killed = p.body.takeDamage(d);
    g.addBlood(p.body.pos, bloodDrops(type, d, effect));
    if (killed) {
        p.deadFrames = 0;
        p.rollFrames = -1;
        TheSound().pedDeath(p.body.pos, p.male, false);   // cPed::SetDead -> PlayDeathSound
        if (fromPlayer) Stats_PedKilledByPlayer(g, p.subtype);           // cPed::HandlePreDead
        if (fromPlayer) Stats_PlayerDamage(g, 0, type, effect);
        Plugins_EmitEvent(CTW_EVENT_PED_KNOCKED_DOWN, 0, -1, (int)p.uid);
        return;
    }
    react(g, p, 0, from, 0, fromPlayer);
    if (fromPlayer) Stats_PlayerDamage(g, p.body.health, type, effect);
}

void Pedestrians::fireHit(Game& g, Ped& p, int damage, const int32_t from[3]) {
    if (p.vehicle || p.deadFrames >= 0 || p.body.dead) return;
    const int d = damage && 100 * damage < 100 ? 1 : 100 * damage / 100;   // FixDamageForArmour (cPed::Reset: 100)
    if (p.body.takeDamage(d)) {
        p.deadFrames = 0;
        p.rollFrames = -1;
        TheSound().pedDeath(p.body.pos, p.male, false);
        Plugins_EmitEvent(CTW_EVENT_PED_KNOCKED_DOWN, 0, -1, (int)p.uid);
        return;
    }
    react(g, p, 0, from, 0);
}

void Pedestrians::explosion(Game& g, const ExplosionBlast& blast) {
    (void)g; // source-entity reaction tasks are a separate, still unported path
    std::vector<uint32_t> knockedDown;
    for (Ped& p : peds) {
        int damage;
        if (p.vehicle || p.body.dead || !blast.pedHit(p.body.pos, false, damage)) continue;
        int32_t direction[3]; for (int k = 0; k < 3; ++k) direction[k] = p.body.pos[k] - blast.pos[k];
        const bool response = p.body.explosionHit(damage, direction);
        if (p.body.dead) TheSound().pedDeath(p.body.pos, p.male, false);
        if (response || p.body.dead) {
            p.deadFrames = 0; p.rollFrames = -1;
            knockedDown.push_back(p.uid);
        }
    }
    // Mod callbacks may remove pedestrians or spawn new ones. Do not dispatch while holding a body reference.
    for (uint32_t uid : knockedDown) Plugins_EmitEvent(CTW_EVENT_PED_KNOCKED_DOWN, 0, -1, (int)uid);
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
    gotoTarget(g, p, p.target, 3, true);
}

// Native process-manager movement records (0x916498 / 0x9162E0), for the on-foot population. An entity that
// requests movement and moves less than sqrt(1509950) starts a record. It becomes stuck at tick 46 unless it
// has moved one unit from the recorded XY point, or stopped requesting movement. This is unrelated to the
// player's separate counter for hanging in the air.
void Pedestrians::updateStuck(Ped& p, const int32_t before[3]) {
    auto& s = p.stuck;
    if (!p.wantsMovement || p.body.dead || p.body.attached) { s = {}; return; }
    if (!s.tracked) {
        int64_t moved2 = 0;
        for (int k = 0; k < 3; ++k) { const int64_t d = (int64_t)p.body.pos[k] - before[k]; moved2 += d*d; }
        if (moved2 >= 1509950) return;
        s.tracked = true;
        std::copy(p.body.pos, p.body.pos + 2, s.origin);
        s.frames = 0;
    }
    if (dist2(p.body.pos, s.origin) >= 0x1000000) { s = {}; return; }
    if (++s.frames >= 46) s.stuck = true;
}

// The native on-foot goto's entity-stuck branch (0x710228) substitutes a random point ten units away for
// ten ticks, then retries the original target. It does not reverse the pavement nodes. Blocked-target and
// dynamic-contact detours need their original sensor flags; only the established entity-stuck branch is here.
void Pedestrians::gotoTarget(Game& g, Ped& p, const int32_t target[2], int maxLevel, bool sprint) {
    if (p.go.detourFrames) --p.go.detourFrames;
    if (!p.go.detourFrames && p.stuck.stuck) {
        const int16_t angle = (int16_t)(Rand32Critical(0xFFFFu) - 0x7FFF);
        p.go.target[0] = p.body.pos[0] + 10 * (int32_t)(sinf((float)angle * 9.587378e-05f) * 4096.f);
        p.go.target[1] = p.body.pos[1] + 10 * (int32_t)(sinf((float)(angle + 0x4000) * 9.587378e-05f) * 4096.f);
        p.go.detourFrames = 10;
    }
    const int32_t* aim = p.go.detourFrames ? p.go.target : target;
    if (p.go.detourFrames && dist2(p.body.pos, aim) <= (int64_t)kFleeArrive * kFleeArrive)
        p.go.detourFrames = 0;
    Player::Input in;
    in.moving = true;
    in.heading = padAngle(aim[0] - p.body.pos[0], aim[1] - p.body.pos[1]);
    in.maxLevel = (uint8_t)maxLevel;
    in.sprint = sprint;
    p.body.act(in, &g.pedSprites);
    p.wantsMovement = p.movementPrepared = true;
}

// Native wander state 0 (0x721E5C) / on-foot network rejoin: approach the nearest pavement link at level 1.
// Native link targets store the ratio in ten-thousandths (0x6EE1BC), then expand it back to Q12 (0x6EE498).
// The original's secondary road-network query still needs the local-area road-link cache.
bool Pedestrians::rejoin(Game& g, Ped& p) {
    auto& w = p.wander;
    auto wait = [&] {
        Player::Input in;
        in.maxLevel = 2;
        p.body.act(in, &g.pedSprites);
        p.movementPrepared = true;
    };
    if (!w.joining) {
        NodeRef a, b;
        int32_t at[3], ratio;
        if (!g.localArea.nearestNodePosition(g, p.body.pos, 1000 * 4096, a, b, at, ratio) &&
            !g.localArea.nearestNodePosition(g, p.body.pos, 1000 * 4096, a, b, at, ratio, 0)) {
            p.cur = {}; p.prev = {};
            wait();
            return false;
        }
        int32_t A[3], B[3];
        if (!nodeAt(g, a, A) || !nodeAt(g, b, B)) { wait(); return false; }
        const int32_t encoded = (int16_t)(((int64_t)ratio * 10000) >> 12);
        const int32_t t = encoded * 4096 / 10000;
        for (int k = 0; k < 2; ++k)
            w.joinTarget[k] = (int32_t)(((int64_t)A[k] * (4096-t)) >> 12) + (int32_t)(((int64_t)B[k] * t) >> 12);
        p.prev = a; p.cur = b;
        setTarget(g, p);
        w.joining = true;
    }
    if (dist2(p.body.pos, w.joinTarget) <= (int64_t)kFleeArrive * kFleeArrive) {
        w.joining = false;
        w.phase = Ped::Wander::Walk;
        if (p.cur.network == 0) p.prev = p.cur;
        wait();
        return true;
    }
    gotoTarget(g, p, w.joinTarget, 1);
    return true;
}

// cIdle(flags 0, duration): repeat the original 172/173 one-shot until the timed task completes. Locomotion
// still decelerates and physics still integrates; the task overrides the sprite cycles supplied to cPed::Act.
bool Pedestrians::idle(Game& g, Ped& p) {
    auto& w = p.wander;
    if (!w.idleFrames || --w.idleFrames == 0) {
        w.phase = Ped::Wander::Walk;
        w.idleAnimation = false;
        Player::Input in;
        in.maxLevel = 2;
        p.body.act(in, &g.pedSprites);
        p.movementPrepared = true;
        return true;
    }
    Player::Input in;
    in.maxLevel = 2;
    in.taskAnimation = true;
    p.body.act(in, &g.pedSprites);
    if (!w.idleAnimation) {
        p.body.playOneShot(172, 173, false, true);
        w.idleAnimation = true;
    } else if (p.body.stepOneShot(&g.pedSprites)) w.idleAnimation = false;
    p.movementPrepared = true;
    return true;
}

void Pedestrians::wander(Game& g, Ped& p) {   // Native wander processing (0x721760): supported on-foot states
    if (roll(g, p)) return;
    if (p.flee.on) { fleeStep(g, p); return; }
    auto& w = p.wander;
    if (!w.initialized) { w.socialStart = (uint16_t)g.frame; w.initialized = true; }
    // Native long-frame schedule (0x8D47B8 / 0x8D47EC): the 16-bit clock handles wrap at 65536.
    int elapsed = (uint16_t)g.frame - w.socialStart;
    if (elapsed < -32767) elapsed += 65536;
    const bool socialDue = elapsed >= 150;
    if (socialDue) w.socialStart = (uint16_t)g.frame;
    if (talk(g,p)) return;
    if (w.phase == Ped::Wander::Mooch && idle(g, p)) return;
    int32_t node[3];
    if (!p.cur.valid() || !nodeAt(g, p.cur, node)) w.phase = Ped::Wander::Rejoin;
    if (w.phase == Ped::Wander::Rejoin) { rejoin(g, p); return; }
    if (socialDue) {
        const bool opportunity=Rand16Critical(100)<=10;
        if (opportunity && canTalk(p) && p.socialFriend) {
            startTalk(p);
            Player::Input in;in.maxLevel=2;p.body.act(in,&g.pedSprites);
            p.movementPrepared=true;
            return;
        }
        const auto* paths = p.cur.network == 1 ? g.collision.pedPaths(p.cur.cx, p.cur.cy) : nullptr;
        const bool moochNode = p.cur.network == 0 ? (g.roads.node(p.cur.index).flags & 0x2000) != 0 :
            paths && p.cur.index < paths->nodes.size() && (paths->nodes[p.cur.index].flags & 0x2000);
        if (p.type == 1 && p.subtype != 1 && moochNode && Rand16Critical(100) <= 74) {
            w.phase = Ped::Wander::Mooch;
            w.idleFrames = Rand32Critical(70) + 20;
            w.idleAnimation = false;
            // AddSubTask does not process the newly created idle task until the next slice.
            Player::Input in;
            in.maxLevel = 2;
            p.body.act(in, &g.pedSprites);
            p.movementPrepared = true;
            return;
        }
    }
    const bool evasive = evade(g, p);
    if (!evasive) p.evading = false;      // cWanderPath +0x6A is cleared once the player's car is gone
    if (roll(g, p)) return;
    if (!p.go.detourFrames && dist2(p.body.pos, p.target) <= (int64_t)kArrive * kArrive && !advance(g, p)) {
        w.phase = Ped::Wander::Rejoin;
        p.cur = {}; p.prev = {};
        Player::Input in;
        in.maxLevel = 2;
        p.body.act(in, &g.pedSprites);
        p.movementPrepared = true;
        return;
    }
    // Process first caps the maximum at 2. Rain/joggers raise its minimum to 2; a nearby player's car skips the
    // ordinary (1,1) walking restriction. A later (3,3) constraint cannot override that existing maximum.
    int maxLevel = 1;
    if ((p.type == 1 && p.subtype == 1) || g.world.timeCycle().value(28) >= 2048.f || evasive) maxLevel = 2;
    gotoTarget(g, p, p.target, maxLevel);
}

void Pedestrians::update(Game& g) {
    // still, upright vehicles are boxes for walking peds (as for the player in Game::tick)
    std::vector<Collision::Box> still;
    for (const Vehicle& c : g.cars) {
        if (c.vel[0] || c.vel[1] || c.vel[2] || c.up[2] < 0xFD8) continue;
        still.push_back({c.pos[0], c.pos[1], c.pos[2] + c.hz, c.hx, c.hy, c.hz, (int16_t)-c.heading(), 0});
    }
    for (Ped& p : peds) {
        p.body.playerControlled = false;
        if (p.talkingCooldown) --p.talkingCooldown; // native 0x8576ac, time slice zero
        if (p.talk.role!=Ped::Talk::None && (p.body.dead || p.body.reacting() || p.vehicle || p.flee.on || p.rollFrames>=0))
            finishTalk(p);
        p.movementPrepared = false;
        p.wantsMovement = false;
        if(p.vehicle) { updateOccupant(g,p); continue; }
        p.body.obstacles = still;
        if (p.body.reacting()) { p.body.actReaction(&g.pedSprites,vehicleAbove(g,p.body)); p.movementPrepared = true; }
        else if (p.deadFrames >= 0) { p.body.stepOneShot(&g.pedSprites); p.movementPrepared = true; }
        else if (!policeStep(g, p)) wander(g, p);
        updateSocialSensor(g,p);
        if (!p.body.reacting() && !p.body.dead && p.deadFrames >= 0) p.deadFrames = -1;
    }
    contacts(g);
    for (Ped& p : peds) {
        if(p.vehicle) continue;
        runOver(g, p);
        if (p.deadFrames >= 0) ++p.deadFrames;
        if (!p.movementPrepared && !p.body.reacting()) continue;
        const int32_t before[3] = {p.body.pos[0], p.body.pos[1], p.body.pos[2]};
        p.body.advance(&g.collision, p.deadFrames < 0);
        updateStuck(p, before);
        if (p.rollFrames >= 0) { p.rollVel[0] = p.body.vel[0]; p.rollVel[1] = p.body.vel[1]; }
    }
    if (g.playerCar < 0 && !g.player.hidden && !g.player.attached) for (const Vehicle& car : g.cars) {
        int32_t normal[3], depth;
        if (!vehicleContact(g.player, car, normal, depth)) continue;
        const int health = g.player.health;
        g.player.vehicleImpact(car, normal, true);
        if (g.player.health != health) g.addBlood(g.player.pos, 1);
    }
    // cPed::ShouldBeDestroyed: about 85 units from the player and off screen
    int32_t f[3];
    g.focus(f);
    peds.erase(std::remove_if(peds.begin(), peds.end(), [&](const Ped& p) {
        return !p.vehicle && dist2(p.body.pos, f) >= kRemoveDist2 && !g.view.canSee(p.body.pos, 0x2000);
    }), peds.end());
}

void Pedestrians::contacts(Game& g) {
    std::vector<Player*> bodies;
    if (g.playerCar < 0 && !g.player.hidden && !g.player.attached) bodies.push_back(&g.player);
    for (Ped& p : peds) if (!p.body.hidden && !p.body.attached) bodies.push_back(&p.body);
    for (size_t a = 0; a < bodies.size(); ++a) for (size_t b = a + 1; b < bodies.size(); ++b) {
        int32_t n[3];
        if (!sphereContact(*bodies[a], *bodies[b], n)) {
            if (!sphereContact(*bodies[b], *bodies[a], n)) continue;
            for (int32_t& v : n) v = -v;
        }
        bodies[a]->dynamicContact(n, bodies[b]->vel);
        for (int32_t& v : n) v = -v;
        bodies[b]->dynamicContact(n, bodies[a]->vel);
    }
}

bool Pedestrians::vehicleAbove(const Game& g, const Player& body) {
    for (const Vehicle& car : g.cars) {
        const int16_t* axes[3] = {car.right,car.fwd,car.up};
        int32_t p[3], hit[3], normal[3], depth;
        for (int i = 0; i < 3; ++i) {
            int64_t dot = 0;
            for (int k = 0; k < 3; ++k) dot += ((int64_t)body.pos[k]-car.pos[k])*axes[i][k];
            p[i] = (int32_t)(dot >> 12);
        }
        const Collision::Box box{0,0,car.hz,car.hx,car.hy,car.hz,0,0};
        if (Collision::sphereVBox(p,0xC00,box,hit,normal,depth)) return true;
    }
    return false;
}

void Pedestrians::render(const PedSprites* sprites, const PedLight* light, float alpha) const {
    for (const Ped& p : peds) {
        if (p.body.hidden) continue;
        auto pose=p.body.renderPose(sprites,p.uid,p.type,p.body.dead ? CTW_CHARACTER_DEAD : 0,alpha);
        if (!Plugins_RenderCharacter(pose)) p.body.render(sprites, light);
    }
}
