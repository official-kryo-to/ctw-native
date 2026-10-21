// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "traffic.h"
#include "fixedmath.h"
#include "game.h"
#include "os/gamefs.h"
#include "lookups.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace {
bool tdbg() { static int d = getenv("CTW_TRAFFICDBG") ? 1 : 0; return d; }
inline int32_t mulq(int64_t a, int64_t b) { return (int32_t)((a * b) >> 12); }
inline int32_t isqrt64(int64_t v) { return v <= 0 ? 0 : (int32_t)std::sqrt((double)v); }

// PopulationProfile(p1..p12) -> the sPopStats fields (7 bits each, field = ped type)
void profileFields(const int p[12], int f[20]) {
    for (int i = 0; i < 20; ++i) f[i] = 0;
    f[1] = p[0] & 0x7F; f[10] = p[1] & 0x7F; f[0] = p[2] & 0x7F; f[3] = p[3] & 0x7F; f[5] = p[4] & 0x7F;
    f[19] = p[5] & 0x7F; f[7] = p[6] & 0x7F; f[11] = p[7] & 0x7F; f[12] = p[8] & 0x7F; f[8] = p[9] & 0x7F;
    f[15] = p[10] & 0x7F; f[18] = p[11] & 0x7F;
}
}   // namespace

bool Traffic::init(const std::string& dataDir) {
    zones_.clear();
    infos_[0].clear(); infos_[1].clear();
    lists_.clear(); makeups_.clear(); drivers_.clear();
    linkCursor_ = 0;
    spawnFallback_ = 0;
    avoidance_.reset();
    if (!drivingProps_.load(dataDir + "/driving_props.bin"))
        fprintf(stderr,"driving_props.bin missing or invalid: traffic rejoining needs scripts/setup_game.py\n");
    PopulationTables tables;
    if (!tables.load(dataDir + "/population_tables.bin")) return false;
    GameFs fs;
    std::vector<uint8_t> z, pi;
    if (!fs.open(dataDir) || !fs.read("infozones.bin", z) || !fs.read("popinfo.bin", pi) || pi.size() < 14) return false;
    // cZoneMgr::Init + cPopulationZones::Init: records sharing a name share their population info
    std::vector<std::string> names;
    for (size_t i = 0; i + 16 <= z.size(); i += 16) {
        Zone zn;
        zn.name = std::string((const char*)&z[i], strnlen((const char*)&z[i], 8));
        memcpy(&zn.x0, &z[i + 8], 2); memcpy(&zn.y0, &z[i + 10], 2); memcpy(&zn.x1, &z[i + 12], 2); memcpy(&zn.y1, &z[i + 14], 2);
        auto it = std::find(names.begin(), names.end(), zn.name);
        zn.info = (int)(it - names.begin());
        if (it == names.end()) names.push_back(zn.name);
        zones_.push_back(zn);
    }
    ZoneInfo def{};   // cPopulationZones::mNoZoneDefault: pop stats 0x500, makeups 0 / 5, densities 25 / 25
    def.fields[1] = 10;
    def.pedMakeup = 0; def.vehMakeup = 5; def.pedDensity = 25; def.carDensity = 25;
    for (int dn = 0; dn < 2; ++dn) infos_[dn].assign(names.size(), def);
    for (const ZoneSetup& s : tables.zones) {   // cZoneManager::SetupDefaultPopulation -> cPopulationZones::Add
        auto it = std::find(names.begin(), names.end(), std::string(s.name, strnlen(s.name, 8)));
        if (it == names.end()) continue;
        ZoneInfo& zi = infos_[s.night][it - names.begin()];
        profileFields(tables.profiles[s.profile].data(), zi.fields);
        zi.pedMakeup = s.pedMakeup; zi.vehMakeup = s.vehMakeup;
        zi.pedDensity = std::min(s.pedDensity, 100); zi.carDensity = std::min(s.carDensity, 100);
        zi.sex = s.sex;
    }
    // popinfo.bin (cPopInfoManager::Init): u32 listTable, u32 pedMakeups, u32 vehicleMakeups, u16 listCount;
    // list = {u32 offset, u8 count, ...} -> count vehicle ids; vehicle makeup (8) = {u32 offset, u16 n, u8 sea, u8 land}
    // -> n x {u16 list, u16 weight}
    uint32_t off0, off1, off2;
    uint16_t cnt;
    memcpy(&off0, &pi[0], 4); memcpy(&off1, &pi[4], 4); memcpy(&off2, &pi[8], 4); memcpy(&cnt, &pi[12], 2);
    // ped makeups (8 bytes each up to the vehicle makeups) = {u32 offset, u16 n} -> n x {u16 pedinfo record, u16 weight}
    pedMakeups_.clear();
    for (uint32_t m = off1; m + 8 <= off2 && m + 8 <= pi.size(); m += 8) {
        uint32_t o;
        uint16_t n;
        memcpy(&o, &pi[m], 4); memcpy(&n, &pi[m + 4], 2);
        std::vector<std::pair<uint16_t, uint16_t>> entries;
        for (uint16_t j = 0; j < n && o + j * 4 + 4 <= pi.size(); ++j) {
            uint16_t id, w;
            memcpy(&id, &pi[o + j * 4], 2); memcpy(&w, &pi[o + j * 4 + 2], 2);
            entries.push_back({id, w});
        }
        pedMakeups_.push_back(entries);
    }
    for (uint16_t i = 0; i < cnt && off0 + i * 8 + 8 <= pi.size(); ++i) {
        uint32_t o;
        memcpy(&o, &pi[off0 + i * 8], 4);
        uint8_t c = pi[off0 + i * 8 + 4];
        std::vector<uint8_t> ids;
        for (uint8_t k = 0; k < c && o + k < pi.size(); ++k) ids.push_back(pi[o + k]);
        lists_.push_back(ids);
    }
    for (int k = 0; k < 8 && off2 + k * 8 + 8 <= pi.size(); ++k) {
        Makeup m;
        uint32_t o;
        uint16_t n;
        memcpy(&o, &pi[off2 + k * 8], 4); memcpy(&n, &pi[off2 + k * 8 + 4], 2);
        m.sea = pi[off2 + k * 8 + 6]; m.land = pi[off2 + k * 8 + 7];
        for (uint16_t j = 0; j < n && o + j * 4 + 4 <= pi.size(); ++j) {
            uint16_t l, w;
            memcpy(&l, &pi[o + j * 4], 2); memcpy(&w, &pi[o + j * 4 + 2], 2);
            m.entries.push_back({l, w});
        }
        makeups_.push_back(m);
    }
    return true;
}

const Traffic::ZoneInfo* Traffic::zoneAt(int32_t x, int32_t y, bool night) const {   // cZoneMgr::GetZoneForPoint
    int X = (x >> 12) / 5, Y = (y >> 12) / 5;
    for (const Zone& z : zones_)
        if (z.x0 < X && X <= z.x1 && z.y0 < Y && Y <= z.y1) return &infos_[night ? 1 : 0][z.info];
    return nullptr;
}

const Traffic::ZoneInfo& Traffic::zoneOrDefault(int32_t x, int32_t y, bool night) const {
    static const ZoneInfo def = [] {   // cPopulationZones::mNoZoneDefault
        ZoneInfo d{};
        d.fields[1] = 10; d.vehMakeup = 5; d.pedDensity = 25; d.carDensity = 25;
        return d;
    }();
    const ZoneInfo* z = zoneAt(x, y, night);
    return z ? *z : def;
}

std::string Traffic::zoneName(int32_t x, int32_t y) const {
    int X = (x >> 12) / 5, Y = (y >> 12) / 5;
    for (const Zone& z : zones_)
        if (z.x0 < X && X <= z.x1 && z.y0 < Y && Y <= z.y1) return z.name;
    return "";
}

int Traffic::vehicleFromMakeup(const Game& g, int makeup, int limit) const {   // cPopInfoManager::GetVehicleId
    if (makeup < 0 || makeup >= (int)makeups_.size()) return 0x7F;
    std::vector<std::pair<int, int>> cand;
    int total = 0;
    for (auto [l, w] : makeups_[makeup].entries) {
        if (l >= lists_.size() || lists_[l].empty()) continue;
        int per = std::max(1, (int)(w / lists_[l].size()));
        for (uint8_t id : lists_[l]) {
            if (id >= g.vehicleInfos.size()) continue;
            bool notBoat = (g.vehicleInfos[id].type() & 0xFFFE) != 2;
            if (notBoat != (limit != 0)) { cand.push_back({id, per}); total += per; }
        }
    }
    if (cand.empty()) return 0x7F;
    int r = (uint16_t)Rand16Critical(total);
    for (auto [id, w] : cand) {
        r -= w;
        if (r < 0) return id;
    }
    return cand.back().first;
}

// Road-cache branch of the native spawn search (0x9133d0), with ordinary population constraints.
// The approaching-traffic preference retains its best candidate even outside the early-accept ten-unit band.
bool Traffic::createPos(Game& g, int network, int& a, int& b, int32_t pos[3], int& lane, int32_t& ratio) {
    const auto& links = g.localArea.roads();
    const int count = (int)links.size();
    if (!count) return false;
    if (linkCursor_ > (uint32_t)count) linkCursor_ %= count;
    const uint32_t savedCursor = linkCursor_;
    const bool preferApproaching = spawnFallback_ == 0;
    const int64_t playerSpeed2 = (int64_t)g.player.vel[0]*g.player.vel[0] +
        (int64_t)g.player.vel[1]*g.player.vel[1] + (int64_t)g.player.vel[2]*g.player.vel[2];
    const bool slowPlayer = playerSpeed2 < (int64_t)(20*4096)*(20*4096);
    int32_t centre[3]; g.focus(centre);
    struct Candidate { int a = -1, b = -1; int32_t pos[3]{}; } best;
    int64_t bestDistance = 0xF4240000000LL;
    for (int tries = 1;; ++tries) {
        linkCursor_ = count > linkCursor_ + 1 ? linkCursor_ + 1 : 0;
        const auto& link = links[linkCursor_];
        const int from = link.a.index, to = link.b.index;
        bool accepted = false;
        if (from < g.roads.count() && to < g.roads.count()) {
            const auto& na = g.roads.node(from);
            const auto& nb = g.roads.node(to);
            if (!((na.flags | nb.flags) & 0x800) &&
                !(network == 1 && na.z >= -4) && !(network == 2 && na.z < -4)) {
                int32_t A[3], B[3], length;
                uint16_t lo, hi;
                g.roads.pos(from,A); g.roads.pos(to,B);
                if (ChordCircle(A,B,centre,0x45000,lo,hi,length) && !(lo == 0 && hi == 4096)) {
                    int32_t direction[3] = {B[0]-A[0],B[1]-A[1],B[2]-A[2]};
                    Normalise3(direction);
                    auto position = [&](int t, int32_t out[3]) {
                        for (int k=0;k<3;++k) {
                            const int32_t span = (int32_t)(((int64_t)link.length*direction[k])>>12);
                            out[k] = A[k] + mulq(span,t);
                        }
                    };
                    bool useLo = lo != 0, useHi = hi != 4096;
                    if (preferApproaching) {
                        int32_t low[3], high[3]; position(lo,low);position(hi,high);
                        int64_t along=0, lowForward=0, highForward=0;
                        for (int k=0;k<3;++k) {
                            along += (int64_t)g.player.fwd[k]*direction[k];
                            lowForward += (int64_t)g.player.fwd[k]*(low[k]-g.player.pos[k]);
                            highForward += (int64_t)g.player.fwd[k]*(high[k]-g.player.pos[k]);
                        }
                        if (lowForward > 0) useLo = useLo && (along < 0 ? slowPlayer : !slowPlayer);
                        if (highForward > 0 && (along < 0 ? !slowPlayer : slowPlayer)) useHi = false;
                    }
                    if (useLo || useHi) {
                        const int t = useLo ? (useHi && Rand16Critical(2) ? hi : lo) : hi;
                        int32_t point[3];position(t,point);
                        bool clear = !g.view.canSee(point,0x5000);
                        for (const auto& car : g.cars) {
                            const int64_t dx=(int64_t)car.pos[0]-point[0],dy=(int64_t)car.pos[1]-point[1];
                            if (dx*dx+dy*dy < (int64_t)0x5000*0x5000) { clear=false;break; }
                        }
                        if (clear) {
                            int64_t metric=0, projection=0;
                            for (int k=0;k<3;++k) {
                                const int64_t d=(int64_t)g.player.pos[k]-A[k];
                                metric += d*d; projection += direction[k]*d;
                            }
                            projection >>= 12;
                            metric -= projection*projection;
                            if (!preferApproaching || metric < bestDistance) {
                                best.a=from;best.b=to;std::copy(point,point+3,best.pos);
                                bestDistance=metric;
                            }
                            accepted = !preferApproaching || bestDistance <= 1677721600;
                        }
                    }
                }
            }
        }
        const int limit = preferApproaching && best.a >= 0 ? count/2 : count;
        if (accepted || tries >= limit) break;
    }
    if (preferApproaching) linkCursor_ = savedCursor;
    if (best.a < 0) return false;
    a=best.a;b=best.b;std::copy(best.pos,best.pos+3,pos);
    const int lanes = preferApproaching && g.roads.meta(a).flag5 ? 3 : 6;
    lane = Rand16Critical(lanes);
    int32_t A[3],B[3];g.roads.pos(a,A);g.roads.pos(b,B);
    const int32_t dx=B[0]-A[0],dy=B[1]-A[1];
    const int32_t length=isqrt64((int64_t)dx*dx+(int64_t)dy*dy);
    const int32_t ux=length ? (int32_t)(((int64_t)dx*4294967296LL/length)>>20) : 0;
    const int32_t uy=length ? (int32_t)(((int64_t)dy*4294967296LL/length)>>20) : 0;
    ratio=length ? (int32_t)(((int64_t)ux*(pos[0]-A[0])+(int64_t)uy*(pos[1]-A[1]))/length) : 0;
    return true;
}

void Traffic::generate(Game& g, int pedType, const ZoneInfo& z) {   // cPopulationManager::GenerateRandomVehicle
    int limit = 0;   // eRandVehicleLimit: 0 land, 1 sea, 2 either
    if (pedType == 1 && z.vehMakeup >= 0 && z.vehMakeup < (int)makeups_.size()) {
        const Makeup& m = makeups_[z.vehMakeup];   // cPopInfoManager::GetRandVehicleLimitStatus
        limit = m.land == 0 ? 2 : 0;
        if (m.sea) limit = m.land == 0 ? 1 : 2;
    }
    int network = limit == 1 ? 1 : limit == 0 ? 2 : 0;
    int a, b, lane;
    int32_t pos[3], ratio;
    if (!createPos(g, network, a, b, pos, lane, ratio)) {
        spawnFallback_ = 4; // native ordinary-population failure path at 0x892298
        if (tdbg()) printf("traffic: no create pos (type %d network %d)\n",pedType,network);
        return;
    }
    limit = g.roads.node(a).z < -4 ? 1 : 0;
    int vid = 0x7F;
    if (pedType == 1) vid = vehicleFromMakeup(g, z.vehMakeup, limit);
    else if (pedType == 10 && limit == 0) vid = 0x12;   // GetCopsVehicleId at no wanted level: police
    if (tdbg()) printf("traffic: type %d vehicle %d at %.1f %.1f link %d->%d lane %d ratio %d\n", pedType, vid, pos[0] / 4096.f, pos[1] / 4096.f, a, b, lane, ratio);
    if (vid == 0x7F || vid >= (int)g.vehicleInfos.size() || g.vehicleInfos[vid].type() != 0) {
        if (tdbg()) printf("traffic: unsupported vehicle %d (type %d)\n",vid,vid>=0 && vid<(int)g.vehicleInfos.size() ? g.vehicleInfos[vid].type() : -1);
        return;   // cars only
    }
    int idx = g.spawnCar(vid, pos, 0);
    if (idx < 0) { if (tdbg()) printf("traffic: vehicle spawn failed\n");return; }
    // Native positioning destroys an ordinary entity outside available sectors (0x90ce48).
    // GenerateRandomVehicle then switches to fallback attempts when it sees that destruction flag.
    if (!g.sectors.loadedAt(pos[0],pos[1])) spawnFallback_ = 4;
    Vehicle& v = g.cars[idx];
    v.generated = true;
    v.engineOn = true;
    v.setToPhysics(false);
    Driver d;   // cWanderRoads::cWanderRoads(vehicle, a, b, lane, 6 lanes, ratio)
    d.cop = pedType == 10;
    d.lastFrame = g.frame;
    d.spline.set(g.roads, a, b, (int16_t)ratio, lane, 6, d.cop, (int)(v.uid & 0xFF));
    int32_t p[2], dir[2];
    d.spline.getPosAndDir(g.roads, p, dir);
    v.placeOnRail(p[0], p[1], d.spline.z(), dir, d.speed);
    if (!g.peds.createDriver(g,v,pedType,z)) { if (tdbg()) printf("traffic: driver appearance failed (ped type %d)\n",pedType);g.removeCar(idx);return; }
    drivers_[v.uid] = d;
    if (spawnFallback_) --spawnFallback_;
    if (tdbg()) printf("traffic: created uid %u at %.1f %.1f loaded %d\n",v.uid,v.pos[0]/4096.f,v.pos[1]/4096.f,g.sectors.loadedAt(v.pos[0],v.pos[1]));
}

// cPhysicalIntegrator::DoSimpleProximityBoxesIntersect: 2D oriented boxes (forward / right axes, half sizes x = across,
// y = along) separated by d
static bool boxesIntersect(const int32_t a1[2], const int32_t b1[2], const int32_t e1[2], const int32_t a2[2],
                           const int32_t b2[2], const int32_t e2[2], const int32_t d[2]) {
    auto dot = [](const int32_t* u, const int32_t* v) { return (int32_t)((((int64_t)u[0] * v[0] + (int64_t)u[1] * v[1]) * 0x100000) >> 32); };
    auto adot = [](const int32_t* u, const int32_t* v) { return std::abs((int32_t)(((int64_t)u[0] * v[0] + (int64_t)u[1] * v[1]) >> 12)); };
    int32_t a2a1 = dot(a2, a1), b2a1 = dot(b2, a1), b1a2 = dot(b1, a2), b1b2 = dot(b1, b2);
    auto sum = [](int64_t x, int64_t y) { return (int32_t)((std::llabs(x) + std::llabs(y)) >> 12); };
    if (adot(d, a1) >= e1[1] + sum((int64_t)a2a1 * e2[1], (int64_t)b2a1 * e2[0])) return false;
    if (adot(d, b1) >= e1[0] + sum((int64_t)b1a2 * e2[1], (int64_t)b1b2 * e2[0])) return false;
    if (adot(a2, d) >= e2[1] + sum((int64_t)a2a1 * e1[1], (int64_t)b1a2 * e1[0])) return false;
    return adot(b2, d) < e2[0] + sum((int64_t)b2a1 * e1[1], (int64_t)b1b2 * e1[0]);
}

void Traffic::proximity(Game& g) {   // cPhysicalIntegrator::VehicleSimpleProximityProcess
    std::vector<Vehicle>& cars = g.cars;
    for (Vehicle& v : cars) { v.blocked = false; v.leastCollide = 0x3E8000; }
    for (size_t i = 0; i < cars.size(); ++i) {
        Vehicle& vi = cars[i];
        if (vi.physicsActive()) continue;
        int32_t R = vi.lookAhead + vi.boundRadius();   // GetRailIntersectRadius
        int32_t fi[2] = {vi.fwd[0], vi.fwd[1]}, ri[2] = {vi.right[0], vi.right[1]};
        for (size_t j = 0; j < cars.size(); ++j) {
            if (j == i) continue;
            const Vehicle& vj = cars[j];
            int32_t d[2] = {vi.pos[0] - vj.pos[0], vi.pos[1] - vj.pos[1]};
            if ((int64_t)d[0] * d[0] + (int64_t)d[1] * d[1] >= (int64_t)R * R) continue;
            int64_t sp2 = (int64_t)vj.vel[0] * vj.vel[0] + (int64_t)vj.vel[1] * vj.vel[1] + (int64_t)vj.vel[2] * vj.vel[2];
            if (vj.physicsActive() && sp2 >= 0xA000001) continue;
            if ((int64_t)fi[0] * d[0] + (int64_t)fi[1] * d[1] >= 1) continue;   // only what is in front
            int32_t fj[2] = {vj.fwd[0], vj.fwd[1]}, rj[2] = {vj.right[0], vj.right[1]};
            int32_t ext = (int32_t)(((int64_t)R * 0x4CC) >> 12);
            int32_t ei[2] = {vi.hx + 0x800, vi.hy + ext}, ej[2] = {vj.hx + 0x800, vj.hy + 0x800};
            int64_t same = (int64_t)fj[0] * fi[0] + (int64_t)fj[1] * fi[1];
            if (same >= 0) {   // going the same way: look further along its direction
                int32_t k = (int32_t)(same >> 12);
                d[0] += mulq(k, mulq(ext, fj[0]));
                d[1] += mulq(k, mulq(ext, fj[1]));
            }
            if (!boxesIntersect(fi, ri, ei, fj, rj, ej, d)) continue;
            int32_t dist = isqrt64((int64_t)d[1] * d[1] + (int64_t)d[0] * d[0]) -
                           (isqrt64((int64_t)vj.hy * vj.hy + (int64_t)vj.hx * vj.hx) + isqrt64((int64_t)vi.hy * vi.hy + (int64_t)vi.hx * vi.hx));
            vi.setLeastCollideDistance(dist, false);
        }
        // peds on foot (the player and pedestrians) in its path
        auto pedAhead = [&](const int32_t* pos) {
            int32_t d[2] = {pos[0] - vi.pos[0], pos[1] - vi.pos[1]};
            if ((int64_t)d[0] * d[0] + (int64_t)d[1] * d[1] < (int64_t)R * R && (int64_t)fi[0] * d[0] + (int64_t)fi[1] * d[1] > 0) {
                int64_t side = (int64_t)ri[0] * d[0] + (int64_t)ri[1] * d[1];
                if (std::llabs(side) < (int64_t)(vi.hx + 0x1000) * 0x1000) {
                    int32_t dist = isqrt64((int64_t)d[0] * d[0] + (int64_t)d[1] * d[1]) - isqrt64((int64_t)vi.hx * vi.hx + (int64_t)vi.hy * vi.hy);
                    vi.setLeastCollideDistance(dist, false);
                }
            }
        };
        if (g.playerCar < 0 && !g.player.hidden && g.player.pos[2] < 0x5000) pedAhead(g.player.pos);
        // The native pedestrian prepass excludes occupants and every body at or above five units.
        for (const Pedestrians::Ped& p : g.peds.peds)
            if (!p.vehicle && p.body.pos[2]<0x5000) pedAhead(p.body.pos);
    }
}

void Traffic::forwardSpeed(Game& g, Vehicle& v, Driver& d) {   // cWanderRoads::FindForwardSpeed
    int32_t holdDist = 0x3E8000;
    bool deadEnd = false;
    bool hold = d.spline.tooSlowForHoldingPattern(g.roads, g.frame, holdDist, deadEnd);
    if (!hold) holdDist = holdDist ? holdDist : 0x3E8000;
    int counter;
    if (d.holdCounter > 1) counter = --d.holdCounter;
    else {
        d.holdCounter = 0;
        counter = 0;
        if (deadEnd) { counter = 0x5A; d.holdCounter = 0x5A; }
    }
    int32_t target;
    bool stopping;
    if (!d.noNext) {
        bool stop = counter > 50 || hold;
        if (v.blocked || stop) {
            if (!stop && d.horn == 0) d.horn = (uint8_t)(Rand16Critical(10) + 5);
            target = 0;
            stopping = stop;
        } else {
            // iAITask::GetRoadSpeed: 6 + (id & 15) x 0x66 - 0x2CA; x2 on two-lane roads (x2.5 in the outer lane)
            target = ((int32_t)((v.uid & 0xF) * 0x66) | (6 << 12)) - 0x2CA;
            const LinkTarget& t2 = d.spline.target(2);
            if (t2.valid()) {
                RoadMeta m = g.roads.meta(t2.metaB ? t2.b : t2.a);
                if (!m.flag5 && m.lanes > 1) target = t2.lane ? mulq(target, 0x2800) : target * 2;
            }
            stopping = false;
            d.horn = 0;
        }
    } else {
        target = 0;
        stopping = true;
    }
    int32_t sp = d.speed;
    if (target < sp) {   // brake towards the obstacle / stop line: dv = (v^2 - target^2) / d x 17 / 256
        int32_t dist = v.leastCollide;
        if (stopping && holdDist < dist) dist = holdDist;
        if (!stopping) dist = v.leastCollide;
        int32_t dec;
        if (dist < 0x64000) {
            if (dist < 0x1000) dec = -0x64000;
            else {
                int32_t q = (int32_t)(((int64_t)(target + sp) * (int64_t)(target - sp)) / dist);
                dec = (int32_t)(((int64_t)q + (int64_t)q * 0x10) >> 8) & ~1;
                if (dec > -0x4CF) dec = -0x4CE;
            }
        } else dec = -0x4CE;
        d.speed = sp - target < -dec ? target : sp + dec;
        return;
    }
    int32_t diff;
    if (sp == 0) {
        if (target == 0) { d.startDelay = 0; return; }
        if (d.startDelay == 0) d.startDelay = (uint8_t)Rand16Critical(10);   // a moment before pulling away
        d.startDelay = (uint8_t)(d.startDelay - 1);
        if (d.startDelay != 0) target = 0;
        diff = target;
        if (diff <= 0) return;
    } else {
        diff = target - sp;
        if (diff <= 0) return;
    }
    d.speed = diff < 0x4CE ? target : sp + 0x4CE;
}

void Traffic::wander(Game& g, Vehicle& v, Driver& d) {   // On-rail state at 0x722f50
    int32_t dir[2] = {0, 0x1000};   // DAT_0057ec70 when standing still
    int32_t p[2] = {v.pos[0], v.pos[1]};
    if (d.speed != 0) {
        int32_t dist = (int32_t)(((int64_t)(int32_t)(g.frame - d.lastFrame) * d.speed * 0x88) >> 12);
        d.spline.inc(g.roads, dist, d.cop);
        d.spline.getPosAndDir(g.roads, p, dir);
        v.placeOnRail(p[0], p[1], d.spline.z(), dir, d.speed);
    }
    int32_t before = d.speed;
    forwardSpeed(g, v, d);
    // the driver's yoke: slowing down is the brake (Act: b64 |= 2); cAISpline::InaugerateYoke gives the indicators,
    // which cVehicle::CommonActToSimpleAndPhysics copies to +0x948 bits 7 / 8
    v.railBraking = d.speed < before;
    bool left, right;
    d.spline.yokeIndicators(left, right);
    v.setIndicatorsFromYoke(left, right);
    v.vel[0] = mulq(d.speed, dir[0]);
    v.vel[1] = mulq(d.speed, dir[1]);
    v.vel[2] = 0;
    d.lastFrame = g.frame;
}

void Traffic::update(Game& g) {
    // Physics activation starts rejoining; it does not remove the seated driver's road task.
    for (auto it = drivers_.begin(); it != drivers_.end();) {
        bool keep = false;
        for (int i = 0; i < (int)g.cars.size(); ++i)
            if (g.cars[i].uid == it->first) keep = !g.cars[i].dead() && g.cars[i].seatUser[0] > 0 &&
                i != g.playerCar && i != g.task.car;
        it = keep ? std::next(it) : drivers_.erase(it);
    }
    avoidance_.update(g);
    proximity(g);
    for (Vehicle& v : g.cars) {
        auto it = drivers_.find(v.uid);
        if (it != drivers_.end()) {
            Driver& d=it->second;
            d.controls={};
            d.movementRequested=false;
            std::copy(v.pos,v.pos+3,d.beforePhysics);
            if (v.physicsActive() || d.rejoining) rejoin(g,v,d);
            else wander(g,v,d);
        }
    }
}

void Traffic::afterPhysics(Game& g) {
    // Moving-car branch of the native process manager's stuck records (0x916498 / 0x9162e0).
    for (const Vehicle& v:g.cars) {
        auto it=drivers_.find(v.uid);
        if (it==drivers_.end()) continue;
        Driver& d=it->second;
        if (!d.movementRequested) { d.trackingStuck=d.stuck=false;d.stuckFrames=0;continue; }
        if (!d.trackingStuck) {
            int64_t moved=0;
            for (int k=0;k<3;++k) { int64_t delta=(int64_t)v.pos[k]-d.beforePhysics[k];moved+=delta*delta; }
            if (moved>=1509950) continue;
            d.stuckOrigin[0]=v.pos[0];d.stuckOrigin[1]=v.pos[1];d.stuckFrames=0;d.trackingStuck=true;
        }
        const int64_t dx=(int64_t)v.pos[0]-d.stuckOrigin[0],dy=(int64_t)v.pos[1]-d.stuckOrigin[1];
        if (dx*dx+dy*dy>=0x1000000) { d.trackingStuck=d.stuck=false;d.stuckFrames=0; }
        else d.stuck=++d.stuckFrames>=46;
    }
}

Vehicle::Controls Traffic::controls(uint32_t uid) const {
    const auto it=drivers_.find(uid);
    return it==drivers_.end() ? Vehicle::Controls{} : it->second.controls;
}

// Global-road branch of CopyInfoTo / GetClosestNode. Sector and dynamic vehicle networks are not present here.
bool Traffic::copyRoadPosition(Game& g,const Vehicle& v,Driver& d,int32_t& distance) {
    const RoadNetwork& rn=g.roads;
    int from=-1,to=-1; int32_t ratio=0;
    for (int pass=0;pass<3 && from<0;++pass) {
        int32_t best=0xFA0000;
        const int32_t extent=pass==2 ? 0xFA0000 : 0x12C000;
        for (int i=0;i<rn.count();++i) {
            const auto& n=rn.node(i);
            if (n.flags & 0x800) continue;
            int32_t a[3]; rn.pos(i,a);
            if (std::abs((int64_t)a[0]-v.pos[0])>=extent || std::abs((int64_t)a[1]-v.pos[1])>=extent) continue;
            for (int k=0;k<rn.linkCount(i);++k) {
                const int j=rn.link(i,k);
                if (j<0 || j>=rn.count() || (rn.node(j).flags&0x800)) continue;
                int32_t b[3], delta[3],direction[3]; rn.pos(j,b);
                int64_t forward=0,square=0;
                for (int t=0;t<3;++t) { delta[t]=b[t]-a[t]; square+=(int64_t)delta[t]*delta[t]; forward+=(int64_t)v.fwd[t]*(b[t]-v.pos[t]); }
                if ((!pass && forward<1) || !square) continue;
                const int32_t length=isqrt64(square);
                int64_t projection=0;
                for (int t=0;t<3;++t) { direction[t]=(int32_t)(((int64_t)delta[t]*4294967296LL/length)>>20); projection+=(int64_t)direction[t]*(v.pos[t]-a[t]); }
                const int32_t along=std::clamp((int32_t)(projection>>12),0,length);
                const int32_t x=a[0]+mulq(direction[0],along), y=a[1]+mulq(direction[1],along);
                const int32_t metric=(int32_t)std::max(std::abs((int64_t)x-v.pos[0]),std::abs((int64_t)y-v.pos[1]));
                if (metric>=best) continue;
                best=metric; from=i; to=j;
                ratio=(int32_t)(((int64_t)along*4294967296LL/length)>>20);
            }
        }
    }
    if (from<0) { distance=-4096; return false; }
    int32_t a[3],b[3]; rn.pos(from,a);rn.pos(to,b);
    int64_t forward=0;
    for (int k=0;k<3;++k) forward+=(int64_t)v.fwd[k]*(b[k]-a[k]);
    d.spline.set(rn,from,to,(int16_t)ratio,forward<0 ? 5 : 0,6,d.cop,v.uid&255);
    distance=d.spline.project(rn,v.pos);
    return true;
}

bool Traffic::computeAim(Game& g,const Vehicle& v,Driver& d) {
    int32_t distance;
    if (!copyRoadPosition(g,v,d,distance)) return false;
    d.spline.inc(g.roads,std::min(mulq(distance,11252),0x32000),d.cop);
    int32_t direction[2];
    d.spline.getPosAndDir(g.roads,d.aim,direction);
    d.aim[2]=0;
    int32_t stop; bool deadEnd;
    if (d.spline.tooSlowForHoldingPattern(g.roads,g.frame,stop,deadEnd))
        std::copy(v.pos,v.pos+3,d.aim);
    d.previousTargetCheck[0]=d.previousTargetCheck[1]=-1;
    d.completionFrame=0;d.cachedCompletion=false;
    return true;
}

bool Traffic::completedAim(const Game& g,const Vehicle& v,Driver& d) {
    // Fixed-position completion branch at 0x6ee9b8: target radius plus speed-dependent lead.
    const int32_t speed=v.speed();
    const int32_t radius=10240+mulq(136,speed>>1)+((speed>>1)>=61440 ? 4096 : 12288);
    const int64_t dx=(int64_t)v.pos[0]-d.aim[0],dy=(int64_t)v.pos[1]-d.aim[1];
    if (dx*dx+dy*dy<=((int64_t)radius*radius & ~4095LL)) return true;
    if (g.frame%10==d.completionFrame) return d.cachedCompletion;
    d.completionFrame=(uint8_t)(g.frame%10);d.cachedCompletion=false;
    if (((g.frame+(v.uid&255))&7)!=7) return false;
    const int16_t x=(int16_t)(v.pos[0]>>12),y=(int16_t)(v.pos[1]>>12);
    if (d.previousTargetCheck[0]!=-1 && d.previousTargetCheck[1]!=-1 &&
        x!=d.previousTargetCheck[0] && y!=d.previousTargetCheck[1]) {
        const int32_t old[2]={d.previousTargetCheck[0]*4096,d.previousTargetCheck[1]*4096};
        const int32_t delta[2]={v.pos[0]-old[0],v.pos[1]-old[1]};
        const int32_t length=isqrt64((int64_t)delta[0]*delta[0]+(int64_t)delta[1]*delta[1]);
        if (length) {
            int32_t unit[2];
            for (int k=0;k<2;++k) unit[k]=(int32_t)(((int64_t)delta[k]*4294967296LL/length)>>20);
            const int32_t along=(int32_t)(((int64_t)unit[0]*(d.aim[0]-old[0])+(int64_t)unit[1]*(d.aim[1]-old[1]))>>12);
            if (std::abs((int64_t)along)<length) {
                const int64_t px=(int64_t)old[0]-d.aim[0]+mulq(unit[0],along);
                const int64_t py=(int64_t)old[1]-d.aim[1]+mulq(unit[1],along);
                d.cachedCompletion=px*px+py*py<0x190000000LL; // max(3*2.5,20)^2 units
            }
        }
    }
    d.previousTargetCheck[0]=x;d.previousTargetCheck[1]=y;
    return d.cachedCompletion;
}

void Traffic::rejoin(Game& g,Vehicle& v,Driver& d) {
    d.rejoining=true;
    // A missing table disables the physical driving branch without fabricating vehicle-specific data.
    if (!drivingProps_.ok() || d.rejoinFailed) { d.lastFrame=g.frame; return; }
    if (!d.aimValid) {
        d.aimValid=computeAim(g,v,d);
        d.rejoinFailed=!d.aimValid;
    }
    if (!d.aimValid) { d.lastFrame=g.frame; return; }
    if (completedAim(g,v,d)) {
        int32_t distance;
        if (copyRoadPosition(g,v,d,distance)) {
            int32_t p[2],dir[2];d.spline.getPosAndDir(g.roads,p,dir);
            v.setToPhysics(false);
            v.placeOnRail(p[0],p[1],d.spline.z(),dir,d.speed);
            d.rejoining=d.aimValid=false;
        }
    } else {
        d.movementRequested=true;
        int32_t aim[3];std::copy(d.aim,d.aim+3,aim);
        avoidance_.queue(g,v,aim);
        d.controls=AIDriving::towards(v,drivingProps_,aim,6<<12); // cVehicle constructor's +0x955 default
        const DrivingData data(v,aim);
        d.recovery.update(v,drivingProps_,data,d.stuck,d.controls);
    }
    d.lastFrame=g.frame;
}

// cPopulationManager::Update when the vehicle schedule fired: roll a ped type from the zone's profile, then
// generate a vehicle for it if there is room (GenerateRandomVehicle)
void Traffic::generateRandom(Game& g, const ZoneInfo& z, bool room) {
    if (!g.roads.ok()) return;
    int total = 0;
    for (int v : z.fields) total += v;
    if (!total) return;
    int r = (int)Rand32Critical(total), type = -1;
    for (int i = 0; i < 20; ++i) {
        if (r < z.fields[i]) { type = i; break; }
        r -= z.fields[i];
    }
    if (tdbg()) printf("traffic: roll type %d (count %d max %d room %d)\n", type, count(), maxCars, room);
    if (type < 0 || !room) return;
    generate(g, type, z);
}
