// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "traffic.h"
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
    int r = (int)Rand32Critical(total);
    for (auto [id, w] : cand) {
        r -= w;
        if (r < 0) return id;
    }
    return cand.back().first;
}

// cLocalAreaKnowledge::GetCreatePosition (network constraint, 69 units out, "towards the player" mode)
bool Traffic::createPos(Game& g, int network, int& a, int& b, int32_t pos[3], int& lane, int32_t& ratio) {
    const RoadNetwork& rn = g.roads;
    int32_t P[3];
    g.focus(P);
    const int32_t radius = 0x45000;
    int n = rn.count();
    if (!n) return false;
    WorldCamera cam;
    g.viewCamera(cam);
    int crossing = 0, rejSeen = 0, rejSafe = 0, rejPerp = 0;
    for (int step = 0; step < n; ++step) {
        int i = (int)((linkCursor_ + step) % n);
        const RoadNetwork::Node& na = rn.node(i);
        if (std::abs((na.x << 9) - P[0]) > 0x12C000 || std::abs((na.y << 9) - P[1]) > 0x12C000) continue;
        for (int k = 0; k < rn.linkCount(i); ++k) {
            int j = rn.link(i, k);
            const RoadNetwork::Node& nb = rn.node(j);
            // cPositionConstraints::IsNodesValid: no restricted nodes; land / water network
            if ((na.flags >> 11 & 1) || (nb.flags >> 11 & 1)) continue;
            if (network == 1 && na.z >= -4) continue;
            if (network == 2 && na.z < -4) continue;
            int32_t A[3], B[3];
            rn.pos(i, A);
            rn.pos(j, B);
            // maths::GetChordCircle: where the link crosses the circle
            double dx = B[0] - A[0], dy = B[1] - A[1], fx = A[0] - P[0], fy = A[1] - P[1];
            double aa = dx * dx + dy * dy;
            if (aa < 4096.0 * 4096.0) continue;
            double bb = 2 * (fx * dx + fy * dy), cc = fx * fx + fy * fy - (double)radius * radius;
            double disc = bb * bb - 4 * aa * cc;
            if (disc < 0) continue;
            double s = std::sqrt(disc);
            int t1 = (int)((-bb - s) / (2 * aa) * 4096), t2 = (int)((-bb + s) / (2 * aa) * 4096);
            if ((t1 < 0 && t2 < 0) || (t1 > 0x1000 && t2 > 0x1000)) continue;
            t1 = std::clamp(t1, 0, 0x1000);
            t2 = std::clamp(t2, 0, 0x1000);
            if (t1 == 0 && t2 == 0x1000) continue;   // the whole link is inside the circle
            ++crossing;
            int t = t2;
            if (t1 != 0) { t = t1; if (t2 != 0x1000) t = Rand32Critical(2) ? t2 : t1; }
            int32_t c[3] = {A[0] + (int32_t)(dx * t / 4096), A[1] + (int32_t)(dy * t / 4096), A[2] + mulq(B[2] - A[2], t)};
            // cPositionConstraints::IsPositionValid: not on screen (radius VEHICLE_AVERAGE_LENGTH), 69 out
            if (g.canSee(c, 5.f)) { rejSeen++; continue; }
            // cWorld::IsPositionSafe: nothing within 5 units
            bool clear = true;
            for (const Vehicle& v : g.cars) {
                int64_t ex = v.pos[0] - c[0], ey = v.pos[1] - c[1];
                if (ex * ex + ey * ey < (int64_t)0x5000 * 0x5000) { clear = false; break; }
            }
            if (!clear) { rejSafe++; continue; }
            // the link's line must pass within 10 units of the player
            double len = std::sqrt(aa), ux = dx / len, uy = dy / len;
            double px = P[0] - A[0], py = P[1] - A[1], proj = px * ux + py * uy;
            double perp2 = px * px + py * py - proj * proj;
            if (perp2 > (double)0xA000 * 0xA000) { rejPerp++; continue; }
            a = i; b = j;
            pos[0] = c[0]; pos[1] = c[1]; pos[2] = c[2];
            lane = (int)Rand32Critical(6);
            ratio = t;
            linkCursor_ = (uint32_t)(i + 1);
            return true;
        }
    }
    linkCursor_ += 97;
    if (tdbg()) printf("traffic: createPos: %d crossings, %d seen, %d not clear, %d too far from the player\n", crossing, rejSeen, rejSafe, rejPerp);
    return false;
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
    if (!createPos(g, network, a, b, pos, lane, ratio)) { if (tdbg()) printf("traffic: no create pos (type %d network %d)\n", pedType, network); return; }
    limit = g.roads.node(a).z < -4 ? 1 : 0;
    int vid = 0x7F;
    if (pedType == 1) vid = vehicleFromMakeup(g, z.vehMakeup, limit);
    else if (pedType == 10 && limit == 0) vid = 0x12;   // GetCopsVehicleId at no wanted level: police
    if (tdbg()) printf("traffic: type %d vehicle %d at %.1f %.1f link %d->%d lane %d ratio %d\n", pedType, vid, pos[0] / 4096.f, pos[1] / 4096.f, a, b, lane, ratio);
    if (vid == 0x7F || vid >= (int)g.vehicleInfos.size() || g.vehicleInfos[vid].type() != 0) return;   // (cars only for now)
    int idx = g.spawnCar(vid, pos, 0);
    if (idx < 0) return;
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
    v.placeOnRail(p[0], p[1], g.roads.node(a).z < -4 ? -0x7800 : 0, dir, d.speed);
    drivers_[v.uid] = d;
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
        for (const Pedestrians::Ped& p : g.peds.peds) pedAhead(p.body.pos);
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
            if (!stop && d.horn == 0) d.horn = (uint8_t)(Rand32Critical(10) + 5);
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
        if (d.startDelay == 0) d.startDelay = (uint8_t)Rand32Critical(10);   // a moment before pulling away
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

void Traffic::wander(Game& g, Vehicle& v, Driver& d) {   // cWanderRoads::Process, on rails
    int32_t dir[2] = {0, 0x1000};   // DAT_0057ec70 when standing still
    int32_t p[2] = {v.pos[0], v.pos[1]};
    if (d.speed != 0) {
        int32_t dist = (int32_t)(((int64_t)(int32_t)(g.frame - d.lastFrame) * d.speed * 0x88) >> 12);
        d.spline.inc(g.roads, dist, d.cop);
        d.spline.getPosAndDir(g.roads, p, dir);
        v.placeOnRail(p[0], p[1], v.pos[2], dir, d.speed);
    }
    int32_t before = d.speed;
    forwardSpeed(g, v, d);
    // the driver's yoke: slowing down is the brake (Act: b64 |= 2), the spline's turn signals are the indicators
    // (cVehicle: yoke +0xC3 / +0xC4 -> +0x948 bits 7 / 8)
    v.railBraking = d.speed < before;
    v.indicators = (uint8_t)((d.spline.indicateLeft() ? 1 : 0) | (d.spline.indicateRight() ? 2 : 0));
    v.vel[0] = mulq(d.speed, dir[0]);
    v.vel[1] = mulq(d.speed, dir[1]);
    v.vel[2] = 0;
    d.lastFrame = g.frame;
}

void Traffic::update(Game& g) {
    // drivers whose car went away, got hit (physics) or taken by the player stop being traffic
    for (auto it = drivers_.begin(); it != drivers_.end();) {
        bool keep = false;
        for (int i = 0; i < (int)g.cars.size(); ++i)
            if (g.cars[i].uid == it->first) keep = !g.cars[i].physicsActive() && i != g.playerCar && i != g.task.car;
        it = keep ? std::next(it) : drivers_.erase(it);
    }
    proximity(g);
    for (Vehicle& v : g.cars) {
        auto it = drivers_.find(v.uid);
        if (it != drivers_.end()) wander(g, v, it->second);
    }

    // cPopulationManager::Update (vehicles)
    firedThisFrame = false;
    int32_t f[3];
    g.focus(f);
    uint32_t t = g.world.timeCycle().time();
    bool night = t - 0x14000u < 0xFFFF3000u;
    const ZoneInfo& z = zoneOrDefault(f[0], f[1], night);
    maxCars = std::min(56, (int)(std::min(14, z.carDensity * 100 * 16 / 7500) * densityScale));
    if (!maxCars || !g.roads.ok()) return;
    const int32_t* vel = g.playerCar >= 0 ? g.cars[g.playerCar].vel : g.player.vel;
    int64_t speed = isqrt64((int64_t)vel[0] * vel[0] + (int64_t)vel[1] * vel[1] + (int64_t)vel[2] * vel[2]);
    int64_t q = (int64_t)(0x7200000000000ULL / (uint64_t)(speed + 0x5FFA)) >> 20;
    int32_t k = (int32_t)(30 * q / maxCars);
    int period = k <= 0x1000 ? 1 : k <= 0x3000 ? 2 : k <= 0x6000 ? 4 : k <= 0xC000 ? 8 : k <= 0x18000 ? 16 : k <= 0x30000 ? 32 : 64;
    if (g.frame % (uint32_t)period) return;   // cFrameSchedule
    firedThisFrame = true;
    int total = 0;
    for (int v : z.fields) total += v;
    if (!total) return;
    int r = (int)Rand32Critical(total), type = -1;
    for (int i = 0; i < 20; ++i) {
        if (r < z.fields[i]) { type = i; break; }
        r -= z.fields[i];
    }
    if (tdbg()) printf("traffic: roll type %d (count %d max %d period %d)\n", type, count(), maxCars, period);
    if (type < 0 || count() >= maxCars) return;
    generate(g, type, z);
}
