// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "cargens.h"
#include "game.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

void CarGenManager::reset() {
    for (Slot& s : slots_) s.used = false;
    sectors_.clear();
}

void CarGenManager::noteLoaded(int id) {
    if (std::find(loadedVehicles.begin(), loadedVehicles.end(), id) == loadedVehicles.end()) loadedVehicles.push_back(id);
}

// "Is anything within 8 units?" (the entities a 3-unit cWorldIterator finds, then dist^2 < 0x40000000)
bool CarGenManager::spotTaken(const Game& g, const int32_t p[3]) const {
    auto near8 = [&](const int32_t q[3]) {
        int64_t dx = q[0] - p[0], dy = q[1] - p[1];
        return dx * dx + dy * dy < 0x40000000LL;
    };
    if (near8(g.player.pos)) return true;
    for (const Vehicle& c : g.cars)
        if (near8(c.pos)) return true;
    return false;
}

// cVehicleInfoManager::RandomVehicle: a random entry of the loaded-vehicle list; land wants cars, sea wants boats
int CarGenManager::randomVehicle(const Game& g, bool sea) {
    if (loadedVehicles.empty()) return 0x7F;
    for (int tries = 0; tries < 3; ++tries) {
        int id = loadedVehicles[Rand32Critical((uint32_t)loadedVehicles.size())];
        int type = g.vehicleInfos[id].type();
        if (sea ? (type & 0xFFFE) == 2 : (type & 0xFFFA) == 0) return id;
    }
    return 0x7F;
}

void CarGenManager::addToCreationList(Game& g, const Collision::CarGen& r) {   // AddVehicleToCreationList
    Slot* s = nullptr;
    for (Slot& k : slots_)
        if (!k.used) { s = &k; break; }
    if (!s) return;   // "car gen slots all full, ignoring this one"
    uint32_t u = Rand32Critical(4);
    if (u < (uint32_t)(r.flags >> 3 & 7)) return;   // "car gen failed on chance"
    s->chance = u > 2 ? 25 : (uint8_t)(100 - 25 * u);
    s->pos[0] = r.x; s->pos[1] = r.y; s->pos[2] = r.z;
    if (r.flags & 4) {   // +-0.2 in x and y
        s->pos[0] += (int32_t)(((int64_t)((int)Rand32Critical(2) * 0x1000 - 0x1000) * 0x333) >> 12);
        s->pos[1] += (int32_t)(((int64_t)((int)Rand32Critical(2) * 0x1000 - 0x1000) * 0x333) >> 12);
    }
    s->heading = r.heading;
    if (r.flags & 2) s->heading = (int16_t)(s->heading + (int)Rand32Critical(0x71C) - 0x38E);
    s->vehicle = r.vehicle;
    if (r.flags & 1) {
        int v = randomVehicle(g, s->pos[2] < -0x5000);
        if (v == 0x7F) return;
        if (g.vehicleInfos[v].s16(0x8E) >> 1 & 1) return;
        s->vehicle = (uint8_t)v;
    }
    s->used = true;
    s->age = 0;
    s->unseenOnly = r.flags >> 6 & 1;
    s->colour = r.colour;
}

void CarGenManager::spawnAllInSector(Game& g, int cx, int cy) {   // SpawnAllCarGensInSector
    const std::vector<Collision::CarGen>* gens = g.collision.carGens(cx, cy);
    if (!gens) return;
    for (const Collision::CarGen& r : *gens) {
        if (r.flags & 0x80) continue;   // script-controlled generators (save game flag 0x3000) - no scripts yet
        int32_t p[3] = {r.x, r.y, r.z};
        if (!spotTaken(g, p)) addToCreationList(g, r);
    }
}

static bool sphereVisible(const WorldCamera& cam, const int32_t p[3], float r, float aspect) {   // cBaseCam::CanSee
    float d[3] = {p[0] / 4096.f - cam.eye[0], p[1] / 4096.f - cam.eye[1], p[2] / 4096.f - cam.eye[2]};
    float z = d[0] * cam.fwd[0] + d[1] * cam.fwd[1] + d[2] * cam.fwd[2];
    if (z < cam.zNear - r || z > cam.zFar + r) return false;
    float x = d[0] * cam.right[0] + d[1] * cam.right[1] + d[2] * cam.right[2];
    float y = d[0] * cam.up[0] + d[1] * cam.up[1] + d[2] * cam.up[2];
    float ty = tanf(cam.fovY * 3.14159265f / 360.f), tx = ty * aspect;
    // distance of the centre outside each side plane
    float ny = 1.f / std::sqrt(1 + ty * ty), nx = 1.f / std::sqrt(1 + tx * tx);
    if ((std::fabs(y) - z * ty) * ny > r) return false;
    if ((std::fabs(x) - z * tx) * nx > r) return false;
    return true;
}

void CarGenManager::update(Game& g) {
    // cWorld streaming: the 3 x 3 sectors around the player; a sector that comes in spawns its generators
    int32_t fp[3];
    g.focus(fp);
    int cx, cy;
    Collision::cellOfPos(fp[0], fp[1], cx, cy);
    std::set<int> now;
    for (int dx = -1; dx <= 1; ++dx)
        for (int dy = -1; dy <= 1; ++dy) now.insert((cx + dx) * 100 + (cy + dy));
    for (int k : now)
        if (!sectors_.count(k)) spawnAllInSector(g, k / 100, k % 100);
    sectors_ = now;

    // cCarGenManager::Process
    WorldCamera cam;
    g.viewCamera(cam);
    for (Slot& s : slots_) {
        if (!s.used) continue;
        if (++s.age == 0xFF) { s.used = false; continue; }   // "car gen too old, get rid of it!"
        int sx, sy;
        Collision::cellOfPos(s.pos[0], s.pos[1], sx, sy);
        if (!sectors_.count(sx * 100 + sy)) { s.used = false; continue; }   // cWorld::IsOff
        if (spotTaken(g, s.pos)) { s.used = false; continue; }
        if (s.unseenOnly && s.chance != 100 && sphereVisible(cam, s.pos, 5.0f /* VEHICLE_AVERAGE_LENGTH (0x5000) */, 16.f / 9.f)) {
            s.used = false;
            continue;
        }
        s.used = false;
        g.spawnGenerated(s.vehicle, s.pos, s.heading, s.colour == 25 ? -1 : s.colour);
    }
}
