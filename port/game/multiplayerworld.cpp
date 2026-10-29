// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "multiplayerworld.h"
#include "game.h"
#include "skidmarks.h"
#include "os/os.h"
#include <algorithm>
#include <cmath>

namespace {
bool vectorValid(const float* p, float limit = 32768.f) {
    if (!p) return false;
    for (int k = 0; k < 3; ++k) if (!std::isfinite(p[k]) || std::abs(p[k]) > limit) return false;
    return true;
}
void fixed(const float* p, int32_t* out) {
    for (int k = 0; k < 3; ++k) out[k] = (int32_t)std::lround(p[k] * 4096.f);
}
int16_t angle(float heading) { return (int16_t)(int32_t)(-std::fmod(heading, 360.f) * 65536.f / 360.f); }
int vehicleIndex(const Game& g, CtwVehicle handle) {
    for (int i = 0; handle && i < (int)g.cars.size(); ++i) if (g.cars[i].uid == handle) return i;
    return -1;
}
}

// The host plays a car from another player's states while that player (a player puppet) drives it. Mission and
// mod puppets drive with the game's own physics, as does everyone in a game without multiplayer.
static bool playerDriven(const Game& g, const Vehicle& car) {
    const auto* driver = car.seatUser[0] >= 0 ? g.peds.find((uint32_t)car.seatUser[0]) : nullptr;
    return driver && driver->puppet.on && !driver->puppet.world;
}

bool MultiplayerWorld::simulatesVehicle(const Game& g, int index) const {
    if (index < 0 || index >= (int)g.cars.size()) return false;
    if (index == g.playerCar) return g.playerSeat == 0;
    if (client()) return false;
    const Vehicle& car = g.cars[index];
    return !(role_ == CTW_MULTIPLAYER_HOST && motion_.count(car.uid) && playerDriven(g, car));
}

std::vector<std::array<int32_t, 3>> MultiplayerWorld::interestCenters(const Game& g) const {
    return interestList(g);
}

const std::vector<std::array<int32_t, 3>>& MultiplayerWorld::interestList(const Game& g) const {
    const int32_t* pos = g.playerCar >= 0 && g.playerCar < (int)g.cars.size() ? g.cars[g.playerCar].pos : g.player.pos;
    if (!interestCache_.empty() && interestFrame_ == g.frame && interestPedCount_ == g.peds.peds.size() &&
        std::equal(interestCache_[0].begin(), interestCache_[0].end(), pos)) return interestCache_;
    interestFrame_ = g.frame; interestPedCount_ = g.peds.peds.size();
    auto& centers = interestCache_; centers.clear();
    centers.push_back({pos[0], pos[1], pos[2]});
    if (role_ != CTW_MULTIPLAYER_HOST) return centers;
    for (const auto& p : g.peds.peds) {
        if (!p.puppet.on || p.puppet.world) continue;
        const auto* at = p.puppet.target;
        bool covered = false;
        for (const auto& c : centers) {
            const int64_t dx = (int64_t)c[0] - at[0], dy = (int64_t)c[1] - at[1];
            covered |= dx * dx + dy * dy < (int64_t)0xA000 * 0xA000;
        }
        if (!covered && centers.size() < 32) centers.push_back({at[0], at[1], at[2]});
    }
    return centers;
}

bool MultiplayerWorld::nearPlayer(const Game& g, const int32_t pos[3], int32_t radius) const {
    for (const auto& center : interestList(g)) {
        const int64_t dx = (int64_t)center[0] - pos[0], dy = (int64_t)center[1] - pos[1];
        if (dx * dx + dy * dy <= (int64_t)radius * radius) return true;
    }
    return false;
}

bool MultiplayerWorld::seenByOthers(const Game& g, const int32_t pos[3], int32_t radius) const {
    if (role_ != CTW_MULTIPLAYER_HOST) return false;
    for (const auto& view : views_) {
        const auto* p = g.peds.find(view.first);
        if (p && p->puppet.on && !p->puppet.world && view.second.canSee(pos, radius)) return true;
    }
    return false;
}

int MultiplayerWorld::setPuppetView(Game& g, CtwPuppet puppet, const CtwView* s) {
    const auto* p = g.peds.find(puppet);
    if (role_ != CTW_MULTIPLAYER_HOST || !p || !p->puppet.on || p->puppet.world) return 0;
    for (auto it = views_.begin(); it != views_.end();) {   // the players who left
        const auto* q = g.peds.find(it->first);
        it = q && q->puppet.on && !q->puppet.world ? std::next(it) : views_.erase(it);
    }
    if (!s) { views_.erase(puppet); return 1; }
    if (s->size < sizeof *s || s->yaw > 0xFFFF || s->pitch > 0xFFFF || s->width < 1 || s->width > 16384 ||
        s->height < 1 || s->height > 16384) return 0;
    for (int k = 0; k < 3; ++k) if (s->eye[k] < -134217727 || s->eye[k] > 134217727) return 0;
    views_[puppet].set(s->eye, (uint16_t)s->yaw, (uint16_t)s->pitch, s->width, s->height);
    return 1;
}

int MultiplayerWorld::setRole(Game& g, int role) {
    if (role < CTW_MULTIPLAYER_OFF || role > CTW_MULTIPLAYER_CLIENT) return 0;
    if (role == role_) return 1;
    // A disconnected passenger cannot retain a removed replica. Local single-player generation resumes on OFF.
    if (client() || role == CTW_MULTIPLAYER_CLIENT) {
        g.carGens.reset(); // release old sector/spawn requests; parked cars can regenerate after leaving
        setPlayerSeat(g, 0, -1);
        g.task = Game::CarTask{};
        for (auto& p : g.peds.peds) if (p.puppet.world || !p.puppet.on) p.vehicle = 0;
        g.peds.peds.erase(std::remove_if(g.peds.peds.begin(), g.peds.peds.end(), [](const auto& p) {
            return p.puppet.world || !p.puppet.on;
        }), g.peds.peds.end());
        for (int i = (int)g.cars.size() - 1; i >= 0; --i) {
            if (role == CTW_MULTIPLAYER_CLIENT || vehicleId(g, g.cars[i].uid)) {
                std::fill(std::begin(g.cars[i].seatUser), std::end(g.cars[i].seatUser), -1);
                g.removeCar(i);
            }
        }
    }
    g.pickups.reset();
    g.pickups.setShared(role != CTW_MULTIPLAYER_OFF);
    vehicles_.clear(); peds_.clear(); explosions_.clear(); skids_.clear(); skidSources_.clear(); motion_.clear();
    views_.clear();
    TheSkidmarks().setShared(role != CTW_MULTIPLAYER_OFF);
    interestCache_.clear();
    if (role != CTW_MULTIPLAYER_HOST) g.sectors.setAdditionalAreas({});
    if (role == CTW_MULTIPLAYER_OFF && g.playerCar >= 0 && g.playerSeat != 0) setPlayerSeat(g, 0, -1);
    role_ = role;
    return 1;
}

CtwVehicle MultiplayerWorld::vehicleHandle(const Game& g, uint32_t id) const {
    if (!id) return 0;
    const auto it = vehicles_.find(id);
    const CtwVehicle handle = client() ? (it == vehicles_.end() ? 0 : it->second) : id;
    return vehicleIndex(g, handle) >= 0 ? handle : 0;
}

uint32_t MultiplayerWorld::vehicleId(const Game& g, CtwVehicle handle) const {
    if (!handle || vehicleIndex(g, handle) < 0) return 0;
    if (!client()) return handle;
    for (const auto& entry : vehicles_) if (entry.second == handle) return entry.first;
    return 0;
}

int MultiplayerWorld::getVehicle(const Game& g, int index, CtwWorldVehicle* out) const {
    if (!out || out->size < sizeof *out || index < 0 || index >= (int)g.cars.size()) return 0;
    const Vehicle& v = g.cars[index];
    CtwWorldVehicle s{}; s.size = sizeof s; s.id = vehicleId(g, v.uid);
    s.model = v.infoId; s.palette = v.palette; s.health = v.health();
    s.flags = (v.dead() ? CTW_VEHICLE_DEAD : 0) | (v.engineOn ? CTW_VEHICLE_ENGINE_ON : 0);
    if (v.siren) s.flags |= 0x100; // shared-world siren flag; independent of legacy vehicle state flags
    s.flags |= v.destructionState();
    for (int k = 0; k < 3; ++k) { s.position[k] = v.pos[k] / 4096.f; s.velocity[k] = v.vel[k] / 4096.f; }
    v.orientation(s.quaternion); *out = s;
    return s.id != 0;
}

int MultiplayerWorld::setVehicle(Game& g, const CtwWorldVehicle* s, double received) {
    if (!s || s->size < sizeof *s || !s->id || role_ == CTW_MULTIPLAYER_OFF ||
        !vectorValid(s->position) || !vectorValid(s->velocity, 512.f) || s->palette < 0 || s->palette > 26 ||
        s->health < 0 || s->health > 255 || s->model < 0 || s->model >= (int)g.vehicleInfos.size()) return 0;
    float norm = 0.f;
    for (float q : s->quaternion) { if (!std::isfinite(q)) return 0; norm += q * q; }
    if (norm < 0.5f || norm > 1.5f) return 0;
    int index = vehicleIndex(g, vehicleHandle(g, s->id));
    bool created = false;
    if (index < 0) {
        if (!client() || vehicles_.size() >= 1024) return 0;
        int32_t pos[3]; fixed(s->position, pos);
        index = g.spawnCar(s->model, pos, 0, s->palette);
        if (index < 0) return 0;
        vehicles_[s->id] = g.cars[index].uid;
        created = true;
    }
    Vehicle& v = g.cars[index];
    if (v.infoId != s->model) return 0;
    int32_t pos[3], vel[3]; fixed(s->position, pos); fixed(s->velocity, vel);
    g.traffic.forget(v.uid);
    if (!client() || index != g.playerCar || g.playerSeat != 0) {
        if (received < 0.) received = OS_TimeAccurate();
        auto& samples = motion_[v.uid];
        Sample next{}; next.time = received;
        std::copy(s->position, s->position+3, next.position.begin());
        std::copy(s->velocity, s->velocity+3, next.velocity.begin());
        std::copy(s->quaternion, s->quaternion+4, next.rotation.begin());
        float distance = 0.f; for (int k=0;k<3;++k) { float d=s->position[k]-v.pos[k]/4096.f; distance+=d*d; }
        if (created || distance > 10000.f) { v.setPose(pos, s->quaternion, vel); samples.clear(); }
        if (!samples.empty() && next.time <= samples.back().time) next.time = samples.back().time + .000001;
        samples.push_back(next); while (samples.size() > 12) samples.pop_front();
    } else motion_.erase(v.uid);
    // A remote driver supplies motion, never the host's damage state. In particular, keep justDied until
    // Game::processVehicleReactions has emitted the destruction blast and detached the wreck's parts.
    if (!client()) return 1;
    v.palette = s->palette; v.engineOn = (s->flags & CTW_VEHICLE_ENGINE_ON) != 0;
    v.siren = (s->flags & 0x100) != 0;
    if (client()) v.keep = true;
    v.setReplicatedHealth(s->health, (s->flags & CTW_VEHICLE_DEAD) != 0);
    v.setReplicatedDestruction(s->flags, g.carModel(v.infoId));
    if (v.dead() && index == g.playerCar && !g.player.dead) g.damagePlayer(255);
    if (index == g.playerCar) std::copy(v.pos, v.pos + 3, g.player.pos);
    return 1;
}

void MultiplayerWorld::advanceVehicles(Game& g, double now) {
    for (auto it=motion_.begin(); it!=motion_.end();) {
        const int index=vehicleIndex(g,it->first);
        if (index<0 || (index==g.playerCar && g.playerSeat==0)) { it=motion_.erase(it); continue; }
        if (!client() && !playerDriven(g,g.cars[index])) { it=motion_.erase(it); continue; }   // they got out
        // A client shows the host's cars 0.12 s late, between two snapshots. The host's own copies are the other
        // players' cars, which its cars collide with: they are carried forward from the newest state instead, as
        // a car played back late would hit the host's cars late, and their reactions would come back later still.
        auto& q=it->second; const double at=now-(client() ? .12 : 0.);
        while(q.size()>2 && q[1].time<=at) q.pop_front();
        const Sample* a=&q.front(); const Sample* b=a;
        for (const auto& s:q) { if(s.time<=at) a=&s; if(s.time>=at) { b=&s; break; } b=&s; }
        float t=b->time>a->time ? (float)std::clamp((at-a->time)/(b->time-a->time),0.,1.) : 0.f;
        float pos[3], vel[3], rot[4]; float dot=0;
        for(int k=0;k<4;++k) dot+=a->rotation[k]*b->rotation[k];
        for(int k=0;k<4;++k) rot[k]=a->rotation[k]+((dot<0?-b->rotation[k]:b->rotation[k])-a->rotation[k])*t;
        const float extra=(float)std::clamp(at-b->time,0.,.15);
        for(int k=0;k<3;++k) { vel[k]=a->velocity[k]+(b->velocity[k]-a->velocity[k])*t;
            pos[k]=a->position[k]+(b->position[k]-a->position[k])*t+b->velocity[k]*extra; }
        int32_t p[3],v[3]; fixed(pos,p); fixed(vel,v); g.cars[index].setPose(p,rot,v);
        ++it;
    }
    if (g.playerCar>=0 && g.playerSeat>0) std::copy(g.cars[g.playerCar].pos,g.cars[g.playerCar].pos+3,g.player.pos);
}

int MultiplayerWorld::removeVehicle(Game& g, uint32_t id) {
    if (!client()) return 0;
    int index = vehicleIndex(g, vehicleHandle(g, id));
    if (index>=0) motion_.erase(g.cars[index].uid);
    if (index < 0) { vehicles_.erase(id); return 1; }
    if (index == g.playerCar || (g.task.op != Game::CarTask::None && index == g.task.car)) {
        setPlayerSeat(g, 0, -1); g.task = Game::CarTask{};
    }
    for (auto& p : g.peds.peds) if (p.vehicle == g.cars[index].uid) {
        p.vehicle = p.puppet.vehicle = 0; p.seat = -1; p.body.hidden = p.body.attached = false;
    }
    std::fill(std::begin(g.cars[index].seatUser), std::end(g.cars[index].seatUser), -1);
    const int ok = g.removeCar(index);
    if (ok) vehicles_.erase(id);
    return ok;
}

int MultiplayerWorld::adoptVehicle(Game& g, CtwVehicle local, uint32_t id) {
    int localIndex = vehicleIndex(g, local);
    if (!client() || !id || localIndex < 0) return 0;
    for (const auto& entry : vehicles_) if (entry.second == local && entry.first != id) return 0;
    const auto existing = vehicles_.find(id);
    if (existing == vehicles_.end() && vehicles_.size() >= 1024) return 0;
    const CtwVehicle duplicate = existing == vehicles_.end() ? 0 : existing->second;
    const int duplicateIndex = vehicleIndex(g, duplicate);
    if (duplicate != local && duplicateIndex >= 0) {
        // A host snapshot can beat its spawn acknowledgment. Retain the client's original car and all its
        // enter/exit references, then transfer occupants from the temporary replica before deleting it.
        for (auto& p : g.peds.peds) {
            if (p.puppet.vehicle == duplicate) p.puppet.vehicle = local;
            if (p.vehicle != duplicate) continue;
            if (p.seat >= 0 && p.seat < 4 && g.cars[localIndex].seatUser[p.seat] == -1) {
                g.cars[localIndex].seatUser[p.seat] = (int)p.uid;
                p.vehicle = local;
                g.cars[localIndex].seatOffset(p.seat, p.seatPosition);
            } else { p.vehicle = 0; p.seat = -1; p.body.hidden = p.body.attached = false; }
        }
        if (g.playerCar == duplicateIndex) {
            const int seat = g.playerSeat;
            if (!setPlayerSeat(g, local, seat)) setPlayerSeat(g, 0, -1);
        }
        if (g.task.op != Game::CarTask::None && g.task.car == duplicateIndex) g.task.car = localIndex;
        std::fill(std::begin(g.cars[duplicateIndex].seatUser), std::end(g.cars[duplicateIndex].seatUser), -1);
        if (!g.removeCar(duplicateIndex)) return 0;
    }
    vehicles_[id] = local;
    localIndex = vehicleIndex(g, local);
    if (localIndex < 0) return 0;
    g.cars[localIndex].keep = true;
    return 1;
}

int MultiplayerWorld::getPed(const Game& g, int index, CtwWorldPed* out) const {
    if (!out || out->size < sizeof *out || index < 0) return 0;
    for (const auto& p : g.peds.peds) {
        if (p.puppet.on && !p.puppet.world) continue;
        if (index--) continue;
        CtwWorldPed s{}; s.size = sizeof s; s.id = p.uid;
        if (client()) for (const auto& entry : peds_) if (entry.second == p.uid) s.id = entry.first;
        s.type = p.type; s.subtype = p.subtype; s.body = p.body.bodySet;
        s.upper = p.body.palUpper; s.legs = p.body.palLegs; s.move = p.body.level();
        s.health = p.body.health;
        s.weapon = p.type == 10 && p.enforcer.pursuing ? p.body.weaponType : p.weapon;
        s.flags = p.body.dead || p.deadFrames >= 0 ? CTW_PUPPET_DEAD : 0;
        if (p.body.upperAim && s.weapon >= 5) s.flags |= CTW_PUPPET_FIRING;
        for (int k = 0; k < 3; ++k) { s.position[k] = p.body.pos[k] / 4096.f; s.velocity[k] = p.body.vel[k] / 4096.f; }
        s.heading = -p.body.heading() * 360.f / 65536.f;
        s.vehicle = vehicleId(g, p.vehicle); s.seat = p.seat; *out = s;
        return 1;
    }
    return 0;
}

int MultiplayerWorld::setPed(Game& g, const CtwWorldPed* s) {
    if (!client() || !s || s->size < sizeof *s || !s->id || !vectorValid(s->position) ||
        !vectorValid(s->velocity, 512.f) || !std::isfinite(s->heading) || s->type < 0 || s->type > 19 ||
        s->subtype < 0 || s->subtype > 255 || s->body < 0 || s->body > 1 || s->upper < 0 || s->upper >= 64 ||
        s->legs < 0 || s->legs >= 64 || s->seat < -1 || s->seat > 3 || s->weapon < 0 || s->weapon >= 25 ||
        s->health < 0 || s->health > 255) return 0;
    auto it = peds_.find(s->id);
    auto* p = it == peds_.end() ? nullptr : g.peds.find(it->second);
    if (!p) {
        if (peds_.size() >= 2048) return 0;
        int32_t pos[3]; fixed(s->position, pos);
        const auto handle = g.peds.spawnPuppet(g, pos, angle(s->heading), s->body, s->upper, s->legs);
        if (!handle) return 0;
        peds_[s->id] = handle; p = g.peds.find(handle); p->puppet.world = true;
    }
    p->type = s->type; p->subtype = s->subtype; p->weapon = s->weapon;
    p->body.bodySet = s->body; p->body.palUpper = s->upper; p->body.palLegs = s->legs;
    p->male = s->body != 0;
    fixed(s->position, p->puppet.target); fixed(s->velocity, p->puppet.velocity);
    p->puppet.heading = angle(s->heading); p->puppet.move = std::clamp(s->move, 0, 3);
    p->puppet.dead = (s->flags & CTW_PUPPET_DEAD) != 0;
    p->puppet.vehicle = vehicleHandle(g, s->vehicle); p->puppet.seat = s->seat;
    p->puppet.weapon = s->weapon; p->puppet.firing = (s->flags & CTW_PUPPET_FIRING) != 0;
    return 1;
}

int MultiplayerWorld::removePed(Game& g, uint32_t id) {
    if (!client()) return 0;
    auto it = peds_.find(id);
    if (it == peds_.end()) return 1;
    g.peds.removePuppet(g, it->second); peds_.erase(it);
    return 1;
}

int MultiplayerWorld::takePedDamage(Game& g, uint32_t id) {
    const auto it = peds_.find(id);
    auto* p = it == peds_.end() ? nullptr : g.peds.find(it->second);
    if (!client() || !p) return 0;
    const int amount = std::min(p->puppet.damage, 255);
    p->puppet.damage -= amount;
    return amount;
}

int MultiplayerWorld::damagePed(Game& g, uint32_t id, int amount) {
    auto* p = g.peds.find(id);
    if (role_ != CTW_MULTIPLAYER_HOST || !p || p->puppet.on || p->body.dead || amount <= 0 || amount > 255) return 0;
    const int32_t direction[3] = {0, 0, 4096};
    g.peds.bulletHit(g, *p, amount, direction, 0, g.player.pos);
    return 1;
}

int MultiplayerWorld::setPlayerSeat(Game& g, CtwVehicle handle, int seat) {
    const int index = vehicleIndex(g, handle);
    const bool changedVehicle = index != g.playerCar;
    if (handle && (index < 0 || seat < 0 || seat >= g.cars[index].numSeats() || seat > 3 || g.cars[index].dead())) return 0;
    if (handle && g.cars[index].seatUser[seat] != -1 && g.cars[index].seatUser[seat] != -2) return 0;
    if (g.playerCar >= 0 && g.playerCar < (int)g.cars.size()) {
        auto& old = g.cars[g.playerCar];
        for (int& user : old.seatUser) if (user == -2) user = -1;
        if (!handle) {
            int32_t off[3]; old.doorSpawnPoint(g.playerSeat, off); old.localToWorld(off, g.player.pos);
        }
    }
    g.task = Game::CarTask{}; g.playerCar = index; g.playerSeat = handle ? seat : 0;
    g.player.hidden = handle != 0; g.player.attached = false;
    if (handle) {
        g.cars[index].seatUser[seat] = -2; g.cars[index].keep = true;
        std::copy(g.cars[index].pos, g.cars[index].pos + 3, g.player.pos);
        if (changedVehicle) g.carCam.setBehind(g.cars[index]);
    }
    return 1;
}

void MultiplayerWorld::captureExplosion(int type, const int32_t* pos, float scale, const int32_t* velocity) {
    if (role_ == CTW_MULTIPLAYER_OFF || replaying_ || explosions_.size() >= 128) return;
    CtwWorldExplosion s{}; s.size = sizeof s; s.type = type; s.scale = scale;
    for (int k = 0; k < 3; ++k) { s.position[k] = pos[k] / 4096.f; s.velocity[k] = velocity ? velocity[k] / 4096.f : 0.f; }
    explosions_.push_back(s);
}
int MultiplayerWorld::takeExplosion(CtwWorldExplosion* out) {
    if (!out || out->size < sizeof *out || explosions_.empty()) return 0;
    *out = explosions_.front(); explosions_.pop_front(); return 1;
}
int MultiplayerWorld::addExplosion(Game& g, const CtwWorldExplosion* s) {
    if (!s || s->size < sizeof *s || role_ == CTW_MULTIPLAYER_OFF || s->type < 0 || s->type > 11 ||
        !vectorValid(s->position) || !vectorValid(s->velocity, 512.f) || !std::isfinite(s->scale) ||
        s->scale <= 0.f || s->scale > 7.f) return 0;
    int32_t pos[3], vel[3]; fixed(s->position, pos); fixed(s->velocity, vel);
    replaying_ = true;
    const auto scale = (int16_t)std::lround(s->scale * 4096.f);
    if (s->type == 6 || s->type == 7) g.addBigExplosion(pos, s->type, scale, vel);
    else if (s->type == 11) g.addHotDogExplosion(pos, vel);
    else if (s->type == 8 || s->type == 9) g.addWaterSplash(pos, s->type, vel, -1);
    else g.addWeaponExplosion(pos, s->type, scale);
    replaying_ = false;
    return 1;
}

void MultiplayerWorld::captureSkid(const Game& g, CtwVehicle vehicle, uint32_t wheel, uint32_t strip, uint32_t sequence, const int32_t* pos, const int32_t* dir, int hold) {
    const uint32_t id = vehicleId(g, vehicle);
    if (!role_ || !id || wheel > 3) return;
    CtwSkidPoint point{}; point.size = sizeof point; point.vehicle = id; point.wheel = wheel;
    point.strip = strip; point.sequence = sequence; point.hold = hold;
    std::copy(pos, pos + 3, point.position); std::copy(dir, dir + 3, point.direction);
    const auto key = std::make_pair(vehicle * 4 + wheel, strip);
    if (skidSources_.size() >= 2048 && !skidSources_.count(key)) skidSources_.erase(skidSources_.begin());
    skidSources_[key] = point;
    if (skids_.size() >= 2048) skids_.pop_front();
    skids_.push_back(point);
}
int MultiplayerWorld::takeSkid(CtwSkidPoint* out) {
    if (!out || out->size < sizeof *out) return 0;
    if (!skids_.empty()) { *out = skids_.front(); skids_.pop_front(); return 1; }
    Skidmarks::Ended ended;
    while (TheSkidmarks().takeEnded(ended)) {
        auto it = skidSources_.find(std::make_pair(ended.id, ended.strip));
        if (it == skidSources_.end()) continue;
        *out = it->second; out->sequence = ended.sequence; out->hold = 0;
        skidSources_.erase(it); return 1;
    }
    return 0;
}
int MultiplayerWorld::addSkid(Game& g, const CtwSkidPoint* p) {
    if (!role_ || !p || p->size < sizeof *p || p->wheel > 3 || !p->strip || !p->sequence || (p->hold != 0 && p->hold != 1 && p->hold != 4)) return 0;
    (void)g;
    if (!p->vehicle) return 0;
    for (int k = 0; k < 3; ++k) if (p->position[k] < -134217727 || p->position[k] > 134217727 || p->direction[k] < -8192 || p->direction[k] > 8192) return 0;
    if (!p->hold) return TheSkidmarks().endRemote(p->vehicle, p->wheel, p->owner, p->strip, p->sequence);
    return TheSkidmarks().addRemote(p->vehicle, p->wheel, p->owner, p->strip, p->sequence, p->position, p->direction, p->hold);
}
