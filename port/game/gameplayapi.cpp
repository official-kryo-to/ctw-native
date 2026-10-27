// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// Implementations for the append-only gameplay SDK. No engine internals cross the C ABI.
#include "gameplayapi.h"
#include "game.h"
#include "weaponhud.h"
#include "os/os.h"
#include <algorithm>
#include <cmath>

namespace {
bool vectorValid(const float* xyz, float limit = 32768.f) {
    if (!xyz) return false;
    for (int i = 0; i < 3; ++i) if (!std::isfinite(xyz[i]) || std::abs(xyz[i]) > limit) return false;
    return true;
}
void fixed(const float xyz[3], int32_t out[3]) {
    for (int i = 0; i < 3; ++i) out[i] = (int32_t)std::lround(xyz[i] * 4096.f);
}
int16_t angle(float heading) { return (int16_t)(int32_t)(-std::fmod(heading, 360.f) * 65536.f / 360.f); }
float degrees(int16_t heading) { return -heading * 360.f / 65536.f; }
int indexOf(CtwVehicle handle) {
    if (handle) for (int i = 0; i < (int)TheGame().cars.size(); ++i)
        if (TheGame().cars[i].uid == handle) return i;
    return -1;
}
Vehicle* vehicle(CtwVehicle handle) {
    int index = indexOf(handle);
    return index >= 0 ? &TheGame().cars[index] : nullptr;
}
uint32_t capabilities() {
    const Game& g = TheGame();
    uint32_t caps = CTW_CAP_GAME_EVENTS;
    if (!g.vehicleInfos.empty()) caps |= CTW_CAP_LIVE_VEHICLES;
    if (g.pedSprites.ok() && g.pedSprites.paletteCount()) caps |= CTW_CAP_PLAYER_APPEARANCE;
    if (g.collision.ok()) caps |= CTW_CAP_WORLD_QUERIES;
    if (g.roads.ok() && g.traffic.ok()) caps |= CTW_CAP_TRAFFIC;
    if (g.radio.stationCount()) caps |= CTW_CAP_RADIO;
    if (g.peds.ok()) caps |= CTW_CAP_PEDESTRIANS;
    return caps;
}
int getVehicle(CtwVehicle handle, CtwVehicleState* out) {
    const Vehicle* v = vehicle(handle);
    if (!v || !out || out->size < sizeof(CtwVehicleState)) return 0;
    CtwVehicleState s{};
    s.size = sizeof s; s.vehicle = handle; s.model_id = v->infoId;
    for (int k = 0; k < 3; ++k) { s.position[k] = v->pos[k] / 4096.f; s.velocity[k] = v->vel[k] / 4096.f; }
    s.heading = degrees(v->heading()); s.health = v->health(); s.palette = v->palette;
    s.flags = (v->dead() ? CTW_VEHICLE_DEAD : 0) | (v->engineOn ? CTW_VEHICLE_ENGINE_ON : 0) |
              (v->keep ? CTW_VEHICLE_PERSISTENT : 0) | (v->isBike() ? CTW_VEHICLE_BIKE : 0) |
              (indexOf(handle) == TheGame().playerCar ? CTW_VEHICLE_PLAYER : 0) |
              (v->physicsActive() ? CTW_VEHICLE_PHYSICS : 0);
    s.radio_station = v->radioStation;
    *out = s;
    return 1;
}
CtwVehicle spawn(int model, const float xyz[3], float heading, int palette) {
    if (!vectorValid(xyz) || !std::isfinite(heading)) return 0;
    int32_t pos[3]; fixed(xyz, pos);
    int index = TheGame().spawnCar(model, pos, angle(heading), palette);
    return index >= 0 ? TheGame().cars[index].uid : 0;
}
int transform(CtwVehicle handle, const float xyz[3], float heading) {
    Vehicle* v = vehicle(handle);
    Game& g = TheGame();
    if (!v || !vectorValid(xyz) || !std::isfinite(heading) ||
        (g.task.op != Game::CarTask::None && indexOf(handle) == g.task.car)) return 0;
    int32_t pos[3]; fixed(xyz, pos);
    g.traffic.forget(handle);
    v->teleport(pos, angle(heading));
    if (indexOf(handle) == g.playerCar) {
        std::copy(v->pos, v->pos + 3, g.player.pos);
        g.player.setHeading(v->heading());
        g.carCam.setBehind(*v);
    }
    return 1;
}
int velocity(CtwVehicle handle, const float xyz[3]) {
    Vehicle* v = vehicle(handle);
    if (!v || v->dead() || !vectorValid(xyz, 512.f)) return 0;
    TheGame().traffic.forget(handle);
    v->setToPhysics(true);
    fixed(xyz, v->vel);
    return 1;
}
int playerState(CtwPlayerState* out) {
    if (!out || out->size < sizeof(CtwPlayerState)) return 0;
    const Game& g = TheGame();
    CtwPlayerState s{}; s.size = sizeof s;
    const Vehicle* car = g.playerCar >= 0 && g.playerCar < (int)g.cars.size() ? &g.cars[g.playerCar] : nullptr;
    for (int k = 0; k < 3; ++k) {
        s.position[k] = (car ? car->pos[k] : g.player.pos[k]) / 4096.f;
        s.velocity[k] = (car ? car->vel[k] : g.player.vel[k]) / 4096.f;
    }
    s.heading = degrees(car ? car->heading() : g.player.heading());
    s.body_set = g.player.bodySet; s.palette_upper = g.player.palUpper; s.palette_legs = g.player.palLegs;
    s.flags = (g.player.dead ? CTW_PLAYER_DEAD : 0) | (car ? CTW_PLAYER_IN_VEHICLE : 0) |
              (g.task.op != Game::CarTask::None ? CTW_PLAYER_ENTERING_EXITING : 0);
    s.vehicle = car ? car->uid : 0;
    *out = s;
    return 1;
}
int appearance(int body, int upper, int legs) {
    Game& g = TheGame();
    if (g.player.dead || g.task.op != Game::CarTask::None || body < 0 || body > 1 ||
        upper < 0 || upper >= g.pedSprites.paletteCount() || legs < 0 || legs >= g.pedSprites.paletteCount() ||
        !g.pedSprites.numFrames(body * 0x113)) return 0;
    g.player.bodySet = body; g.player.palUpper = upper; g.player.palLegs = legs;
    return 1;
}
int enter(CtwVehicle handle) {
    Game& g = TheGame();
    int index = indexOf(handle);
    if (index < 0 || g.playerCar >= 0 || g.player.dead || g.task.op != Game::CarTask::None || !g.pedSprites.ok()) return 0;
    g.startEnter(index);
    return g.task.op != Game::CarTask::None ? 1 : 0;
}
int exitVehicle() {
    Game& g = TheGame();
    if (g.playerCar < 0 || g.player.dead || g.task.op != Game::CarTask::None || !g.pedSprites.ok()) return 0;
    g.startExit();
    return g.task.op != Game::CarTask::None ? 1 : 0;
}
int ground(const float xyz[3], CtwGround* out) {
    if (!vectorValid(xyz) || !out || out->size < sizeof(CtwGround) || !TheGame().collision.ok()) return 0;
    const auto g = TheGame().collision.ground(xyz[0], xyz[1], xyz[2]);
    CtwGround s{}; s.size = sizeof s; s.height = g.z; s.surface = g.surface;
    std::copy(g.normal, g.normal + 3, s.normal); *out = s;
    return 1;
}
int lineBoxes(const float from[3], const float to[3]) {
    if (!vectorValid(from) || !vectorValid(to) || !TheGame().collision.ok()) return -1;
    int32_t a[3], b[3]; fixed(from, a); fixed(to, b);
    return TheGame().collision.lineHitsBoxes(a, b, false) ? 1 : 0;
}
int worldLine(const float from[3], const float to[3], float* fraction) {
    if (fraction) *fraction = 1.f;
    if (!vectorValid(from) || !vectorValid(to) || !TheGame().collision.ok()) return -1;
    const float d[3] = {to[0] - from[0], to[1] - from[1], to[2] - from[2]};
    if (d[0] * d[0] + d[1] * d[1] + d[2] * d[2] > 256.f * 256.f) return -1;   // a short query, not a world scan
    int32_t a[3], b[3]; fixed(from, a); fixed(to, b);
    // Nearest hit against boxes, cylinders, meshes and the ground slab (GetLineIntersectWithStatics flags).
    Collision::LineHit hit;
    if (!TheGame().collision.staticLine(a, b, 0x40000000 | 0x800 | 0x400 | 0x200 | 0x100, &hit)) return 0;
    if (fraction) *fraction = std::clamp(hit.fraction / 4096.f, 0.f, 1.f);
    return 1;
}
}

void GameplayApi_Fill(CtwHostApi& api) {
    api.get_capabilities = capabilities;
    api.live_vehicle_count = [] { return (int)TheGame().cars.size(); };
    api.live_vehicle_at = [](int index) -> CtwVehicle {
        return index >= 0 && index < (int)TheGame().cars.size() ? TheGame().cars[index].uid : 0;
    };
    api.get_vehicle_state = getVehicle;
    api.spawn_vehicle_at = spawn;
    api.remove_vehicle = [](CtwVehicle v) -> int { return TheGame().removeCar(indexOf(v)); };
    api.set_vehicle_transform = transform; api.set_vehicle_velocity = velocity;
    api.set_vehicle_palette = [](CtwVehicle h, int palette) -> int {
        Vehicle* v = vehicle(h);
        if (!v || v->dead() || palette < 0 || palette > 26) return 0;
        v->palette = palette; return 1;
    };
    api.set_vehicle_persistent = [](CtwVehicle h, int persistent) -> int {
        Vehicle* v = vehicle(h); if (!v) return 0; v->keep = persistent != 0; return 1;
    };
    api.set_vehicle_engine = [](CtwVehicle h, int running) -> int {
        Vehicle* v = vehicle(h); if (!v || v->dead()) return 0;
        if (!running) { TheGame().traffic.forget(h); v->setToPhysics(true); }
        v->engineOn = running != 0; return 1;
    };
    api.set_vehicle_door = [](CtwVehicle h, int seat, int open) -> int {
        Vehicle* v = vehicle(h); if (!v || v->dead() || seat < 0 || seat > 3 || !v->hasDoor(seat)) return 0;
        if (open) v->openDoor(seat); else v->closeDoor(seat); return 1;
    };
    api.damage_vehicle = [](CtwVehicle h, int amount) -> int {
        Vehicle* v = vehicle(h); if (!v || v->dead() || amount <= 0 || amount > 255) return 0;
        v->damage(amount); return 1;
    };
    api.repair_vehicle = [](CtwVehicle h) -> int { Vehicle* v = vehicle(h); return v && v->repair(); };
    api.get_player_state = playerState;
    api.set_player_heading = [](float heading) -> int {
        Game& g = TheGame();
        if (!std::isfinite(heading) || g.player.dead || g.playerCar >= 0 || g.task.op != Game::CarTask::None) return 0;
        g.player.setHeading(angle(heading)); return 1;
    };
    api.set_player_appearance = appearance;
    api.player_enter_vehicle = enter; api.player_exit_vehicle = exitVehicle;
    api.get_ground = ground; api.line_hits_world_boxes = lineBoxes;
    api.get_traffic_density = [] { return TheGame().traffic.densityScale; };
    api.set_traffic_density = [](float scale) -> int {
        if (!std::isfinite(scale) || scale < 0 || scale > 4) return 0;
        TheGame().traffic.densityScale = scale; return 1;
    };
    api.radio_station_count = [] { return TheGame().radio.stationCount(); };
    api.radio_station_name = [](int id) { return TheGame().radio.stationName(id); };
    api.radio_station_available = [](int id) -> int { return TheGame().radio.stationAvailable(id); };
    api.get_radio_station = [] { return TheGame().radio.stationCount() ? TheGame().radio.station() : -1; };
    api.set_radio_station = [](int id) -> int { return TheGame().radio.tune(id, TheGame()); };
    api.get_radio_volume = [] { return TheGame().radio.volumeLevel(); };
    api.set_radio_volume = [](int level) -> int { return TheGame().radio.stationCount() && TheGame().radio.setVolume(level); };
    // version 5
    api.set_control_yaw = [](int enabled, float yaw) {
        Game::ControlYaw& c = TheGame().controlYaw;
        c.on = enabled && std::isfinite(yaw);
        if (c.on) c.yaw = angle(yaw);
    };
    api.set_camera_fov = [](float degrees) {
        TheGame().fovOverride = std::isfinite(degrees) && degrees > 0.f ? std::clamp(degrees, 20.f, 120.f) : 0.f;
    };
    api.world_line = worldLine;
    api.weapon_type_count = [] { return Weapons_Ok() ? 25 : 0; };
    api.weapon_name = [](int type) -> const char* {
        static std::string names[25];
        if (!Weapons_Ok() || type < 0 || type >= 25) return nullptr;
        if (names[type].empty()) names[type] = Weapons_Name(type);
        return names[type].c_str();
    };
    api.weapon_givable = [](int type) { return Weapons_Ok() && type >= 1 && type <= 23 ? 1 : 0; };
    api.give_weapon = [](int type, int ammo) -> int {
        if (!Weapons_Ok() || type < 1 || type > 23) return 0;
        Game& g = TheGame();
        if (ammo <= 0) ammo = Weapons_DefaultAmmo(type);
        // cPlayer::GiveWeapon (as a pickup does: an existing weapon in the slot is replaced)
        return g.playerWeapons.give(type, ammo, true) ? 1 : 0;
    };
    api.draw_weapon_icon = [](int type, float x, float y, float w, float h) -> int {
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(w) || !std::isfinite(h) || w <= 0 || h <= 0) return 0;
        return WeaponHud_DrawIcon(type, x, y, w, h, 1.f) ? 1 : 0;
    };
    api.set_aim_override = [](int enabled, const float* target) {
        Game& g = TheGame();
        g.aimOverride.on = enabled && target && std::isfinite(target[0]) && std::isfinite(target[1]) && std::isfinite(target[2]);
        if (g.aimOverride.on) for (int k = 0; k < 3; ++k) g.aimOverride.target[k] = (int32_t)(std::clamp(target[k], -9000.f, 9000.f) * 4096.f);
    };
    api.screen_to_world = [](float x, float y, float* out) -> int {
        if (!out || !std::isfinite(x) || !std::isfinite(y)) return 0;
        return TheGame().screenToWorld(x, y, (int)OS_ScreenGetWidth(), (int)OS_ScreenGetHeight(), out) ? 1 : 0;
    };
    api.cycle_weapon = [](int direction) -> int {   // the next frame's Q / E (cPed::HandleWeapons)
        Game& g = TheGame();
        if (!direction || g.playerCar >= 0 || !Weapons_Ok()) return 0;
        g.weaponCycle = direction > 0 ? 1 : -1;
        return 1;
    };
    // version 13: multiplayer
    api.spawn_puppet = [](const float* xyz, float heading, int body, int upper, int legs) -> CtwPuppet {
        Game& g = TheGame();
        const int palettes = g.pedSprites.paletteCount();
        if (!vectorValid(xyz) || !std::isfinite(heading) || body < 0 || body > 1 || upper < 0 || upper >= palettes ||
            legs < 0 || legs >= palettes) return 0;
        int32_t pos[3]; fixed(xyz, pos);
        return g.peds.spawnPuppet(g, pos, angle(heading), body, upper, legs);
    };
    api.set_puppet_state = [](CtwPuppet handle, const CtwPuppetState* s) -> int {
        Game& g = TheGame();
        Pedestrians::Ped* p = g.peds.find(handle);
        if (!p || !p->puppet.on || !s || s->size < sizeof(CtwPuppetState) || !vectorValid(s->position) ||
            !vectorValid(s->velocity, 512.f) || !std::isfinite(s->heading)) return 0;
        fixed(s->position, p->puppet.target);
        fixed(s->velocity, p->puppet.velocity);
        p->puppet.heading = angle(s->heading);
        p->puppet.move = std::clamp(s->move, 0, 3);
        p->puppet.dead = (s->flags & CTW_PUPPET_DEAD) != 0;
        p->puppet.vehicle = s->vehicle && vehicle(s->vehicle) ? s->vehicle : 0;
        return 1;
    };
    api.remove_puppet = [](CtwPuppet handle) -> int { return TheGame().peds.removePuppet(TheGame(), handle) ? 1 : 0; };
    api.take_puppet_damage = [](CtwPuppet handle) -> int {
        Pedestrians::Ped* p = TheGame().peds.find(handle);
        if (!p || !p->puppet.on) return 0;
        const int damage = p->puppet.damage;
        p->puppet.damage = 0;
        return damage;
    };
    api.world_to_screen = [](const float* xyz, float* out) -> int {
        if (!vectorValid(xyz) || !out) return 0;
        return TheGame().worldToScreen(xyz, (int)OS_ScreenGetWidth(), (int)OS_ScreenGetHeight(), out) ? 1 : 0;
    };
    api.get_player_vitals = [](int* health, int* armour, int* move) -> int {
        const Game& g = TheGame();
        if (health) *health = g.player.health;
        if (armour) *armour = g.player.armour;
        if (move) *move = g.playerCar >= 0 ? 0 : std::clamp(g.player.level(), 0, 3);
        return 1;
    };
    api.damage_player = [](int amount) -> int { return TheGame().damagePlayer(amount) ? 1 : 0; };
    api.get_vehicle_pose = [](CtwVehicle handle, float* xyz, float* quat, float* velocity) -> int {
        const Vehicle* v = vehicle(handle);
        if (!v) return 0;
        float q[4];
        v->orientation(q);
        for (int k = 0; k < 3; ++k) {
            if (xyz) xyz[k] = v->pos[k] / 4096.f;
            if (velocity) velocity[k] = v->vel[k] / 4096.f;
        }
        if (quat) for (int k = 0; k < 4; ++k) quat[k] = q[k];
        return 1;
    };
    api.set_vehicle_pose = [](CtwVehicle handle, const float* xyz, const float* quat, const float* velocity) -> int {
        Game& g = TheGame();
        Vehicle* v = vehicle(handle);
        if (!v || v->dead() || indexOf(handle) == g.playerCar || (g.task.op != Game::CarTask::None && indexOf(handle) == g.task.car) ||
            !vectorValid(xyz) || !quat || !vectorValid(velocity, 512.f)) return 0;
        for (int k = 0; k < 4; ++k) if (!std::isfinite(quat[k])) return 0;
        int32_t pos[3], vel[3];
        fixed(xyz, pos); fixed(velocity, vel);
        g.traffic.forget(handle);
        v->setPose(pos, quat, vel);
        return 1;
    };
    api.set_prop_physics = [](int full) { TheGame().propDynamics.setFullPhysics(full != 0); };
    api.get_prop_physics = [] { return TheGame().propDynamics.fullPhysics() ? 1 : 0; };
    api.get_player_weapon = [](int* type, int* ammo, int* clip) -> int {
        const Weapon* w = TheGame().playerWeapons.current();
        if (!w) return 0;
        if (type) *type = w->type;
        if (ammo) *ammo = w->ammoCount();
        if (clip) *clip = w->ammoInClip();
        return 1;
    };
    api.ped_count = [] { return (int)TheGame().peds.peds.size(); };
    api.get_ped_state = [](int index, CtwPedState* out) -> int {
        const auto& list = TheGame().peds.peds;
        if (index < 0 || index >= (int)list.size() || !out || out->size < sizeof(CtwPedState)) return 0;
        const Pedestrians::Ped& p = list[index];
        CtwPedState s{};
        s.size = sizeof s; s.id = p.uid; s.ped_type = p.type; s.ped_subtype = p.subtype;
        for (int k = 0; k < 3; ++k) s.position[k] = p.body.pos[k] / 4096.f;
        s.heading = degrees(p.body.heading());
        s.flags = (p.deadFrames >= 0 ? CTW_PED_KNOCKED_DOWN : 0) | (p.male ? CTW_PED_MALE : 0);
        *out = s;
        return 1;
    };
    api.set_render_style = [](const CtwRenderStyle* s) -> int {
        Game::RenderStyle r;
        if (!s) { TheGame().renderStyle = r; return 1; }
        if (s->size < sizeof(CtwRenderStyle)) return 0;
        const float v[] = {s->light_pools, s->headlight_pools, s->tint[0], s->tint[1], s->tint[2], s->sepia,
                           s->saturation, s->contrast, s->brightness, s->vignette};
        for (float f : v) if (!std::isfinite(f)) return 0;
        r.lightPools = std::clamp(s->light_pools, 0.f, 2.f);
        r.headlightPools = std::clamp(s->headlight_pools, 0.f, 2.f);
        for (int k = 0; k < 3; ++k) r.tint[k] = std::clamp(s->tint[k], 0.f, 2.f);
        r.sepia = std::clamp(s->sepia, 0.f, 1.f);
        r.saturation = std::clamp(s->saturation, 0.f, 2.f);
        r.contrast = std::clamp(s->contrast, 0.5f, 2.f);
        r.brightness = std::clamp(s->brightness, -0.5f, 0.5f);
        r.vignette = std::clamp(s->vignette, 0.f, 1.f);
        TheGame().renderStyle = r;
        return 1;
    };
    api.get_ped_density = [] { return TheGame().peds.densityScale; };
    api.set_ped_density = [](float scale) -> int {
        if (!std::isfinite(scale) || scale < 0 || scale > 4) return 0;
        TheGame().peds.densityScale = scale; return 1;
    };
    api.get_camera_shake = [](float out[3]) { TheGame().cameraShake(out); };
}
