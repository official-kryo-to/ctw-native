// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#define SDL_MAIN_HANDLED
#include "game.h"
#include "plugins.h"
#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include "os/os.h"
#include "skidmarks.h"

struct PedsTestAccess {
    static void drop(Game& g, Pedestrians::Ped& p) { g.peds.dropGoodies(g, p); }
};

struct VehicleTestAccess {
    static bool smoking(const Vehicle& v) { return v.smoke_ != nullptr; }
    static bool burning(const Vehicle& v) { return v.fire_ != nullptr; }
};
struct SkidmarksTestAccess {
    static int points(const Skidmarks& s) { int n=0; for(const auto& m:s.marks_) n+=m.count; return n; }
    static int active(const Skidmarks& s) { int n=0; for (const auto& m:s.marks_) n+=m.id && m.active; return n; }
    static bool equalGeometry(const Skidmarks& a, const Skidmarks& b) {
        const Skidmarks::Mark *x=nullptr, *y=nullptr;
        for (const auto& m : a.marks_) if (m.id) x = &m;
        for (const auto& m : b.marks_) if (m.id) y = &m;
        return x && y && x->count == y->count && !std::memcmp(x->origin, y->origin, sizeof x->origin) &&
            !std::memcmp(x->pts, y->pts, x->count * sizeof x->pts[0]);
    }
};

static int failures;
static void check(bool ok, const char* why) { if (!ok) { std::fprintf(stderr, "FAIL: %s\n", why); ++failures; } }

int main() {
    SDL_Init(0);
    {
        auto source = std::make_unique<Skidmarks>(); auto observer = std::make_unique<Skidmarks>(); source->setShared(true); observer->setShared(true);
        int32_t p[3] = {1000, 2000, 3000}, d[3] = {4096, 0, 0};
        uint32_t strip = 0, sequence = 0;
        check(source->addPoint(4, p, d, 4, &strip, &sequence) && observer->addRemote(4, 0, 1, strip, sequence, p, d, 4), "first burnout point captured and replayed");
        p[1] += 100;
        check(!source->addPoint(4, p, d, 4, &strip, &sequence), "held burnout point is not captured");
        for (int i=0; i<3; ++i) source->process();
        check(source->addPoint(4, p, d, 4, &strip, &sequence), "burnout accepts point once hold expires");
        for (int i=0; i<10; ++i) observer->process();
        check(observer->addRemote(4, 0, 1, strip, sequence, p, d, 4) && SkidmarksTestAccess::equalGeometry(*source, *observer), "replay preserves exact source strip vertices");
        check(!observer->addRemote(4, 0, 1, strip, sequence, p, d, 4), "replay rejects duplicate sequence");
        check(!observer->endRemote(4, 0, 1, strip, sequence + 2), "end before the missing point is rejected");
        for (int i=0; i<5; ++i) source->process();
        Skidmarks::Ended ended{};
        check(source->takeEnded(ended) && ended.id == 4 && ended.strip == strip && ended.sequence == sequence + 1,
              "source emits one sequenced end when the strip stops");
        check(!source->takeEnded(ended), "source does not duplicate strip ends");
        check(observer->endRemote(4, 0, 1, strip, sequence + 1) && !SkidmarksTestAccess::active(*observer),
              "end starts normal fade immediately on the observer");
        check(!observer->endRemote(4, 0, 1, strip, sequence + 1), "duplicate end is rejected");
        check(!observer->addRemote(4, 0, 1, strip, sequence + 2, p, d, 4), "ended strip cannot be restarted by a late packet");
        for (int i=0; i<80; ++i) observer->process();
        check(!SkidmarksTestAccess::points(*observer), "ended strip fades and is freed");
        auto many = std::make_unique<Skidmarks>(); many->setShared(true);
        for (uint32_t i=1; i<=128; ++i) check(many->addPoint(i, p, d, 1), "shared skid capacity covers 32 cars");
    }
    Game& g = TheGame();
    const auto& api = Plugins_HostApi();
    check(CTW_HOST_HAS(&api, set_multiplayer_world) && CTW_HOST_HAS(&api, damage_world_ped), "shared-world API is available");
    g.vehicleInfos.resize(1);
    std::memset(g.vehicleInfos.data(), 0, sizeof(VehicleInfo));
    g.vehicleInfos[0].raw[0x10C] = 4;
    auto info16 = [&](int offset, int16_t value) { std::memcpy(g.vehicleInfos[0].raw + offset, &value, sizeof value); };
    auto info32 = [&](int offset, int32_t value) { std::memcpy(g.vehicleInfos[0].raw + offset, &value, sizeof value); };
    info16(0x28, 2 * 4096); info16(0x2A, 4 * 4096); info16(0x2C, 4096);
    for (int s = 0; s < 4; ++s) {
        info32(0x98 + s * 8, (s & 1 ? 2 : -2) * 4096);
        info32(0x9C + s * 8, (s < 2 ? 1 : -1) * 4096);
    }
    const float origin[3] = {0, 0, 1};
    const auto localCar = api.spawn_vehicle_at(0, origin, 0, 4);
    Pedestrians::Ped localPed; localPed.uid = 99; g.peds.peds.push_back(localPed);
    check(localCar && api.set_multiplayer_world(CTW_MULTIPLAYER_CLIENT) && g.cars.empty() && g.peds.peds.empty(),
          "joining discards independently generated cars and NPCs");
    CtwWorldVehicle car{}; car.size = sizeof car; car.id = 40; car.model = 0; car.palette = 4;
    car.health = 255; car.position[2] = 1.f; car.quaternion[3] = 1.f;
    check(api.set_world_vehicle(&car), "host vehicle snapshot creates one shared car");
    const auto handle = api.world_vehicle_handle(car.id);
    check(api.vehicle_seat_count(handle) == 4 && api.vehicle_seat_count(0) == 0,
          "seat arbitration can query actual vehicle capacity");
    car.position[0] = 5.f;
    g.multiplayerWorld.setVehicle(g, &car);
    g.multiplayerWorld.advanceVehicles(g, OS_TimeAccurate()+.12);
    // Host destruction must survive a driver's subsequent motion update until its blast is processed.
    Vehicle authority;
    authority.damage(255);
    check(authority.dead() && authority.justDied, "lethal authority damage queues destruction");
    const uint32_t wreckState = authority.destructionState();
    car.health = 100; car.flags = (20u << 21) | (5u << 9) | (3u << 14) | (1u << 19);
    check(api.set_world_vehicle(&car) && g.cars[0].health() == 100 &&
          g.cars[0].destructionState() == (car.flags & 0x7FFFFE00u),
          "authoritative snapshots synchronize damaged parts, burst tyres and burn timer");
    g.cars[0].processDamage(0, false);
    check(VehicleTestAccess::smoking(g.cars[0]), "replicas display smoke for damaged engines");
    car.health = 30; car.flags = 2u << 21;
    api.set_world_vehicle(&car);
    for (int frame = 0; frame < 8; ++frame) g.cars[0].processDamage(frame, false);
    check(!g.cars[0].dead() && g.cars[0].destructionState() == car.flags && VehicleTestAccess::burning(g.cars[0]),
          "replicas display fire without advancing the authority's destruction timer");
    car.health = 0; car.flags = CTW_VEHICLE_DEAD | wreckState | (3u << 14);
    api.set_world_vehicle(&car);
    check(g.cars[0].dead() && !g.cars[0].justDied && VehicleTestAccess::smoking(g.cars[0]) &&
          VehicleTestAccess::burning(g.cars[0]) && g.cars[0].destructionState() == (car.flags & 0x7FFFFE00u),
          "a host wreck initializes matching destruction effects and parts without a second explosion");
    car.health = 255; car.flags = 480u << 21;
    api.set_world_vehicle(&car); g.cars[0].processDamage(0, false);
    check(!g.cars[0].dead() && !VehicleTestAccess::smoking(g.cars[0]) && !VehicleTestAccess::burning(g.cars[0]),
          "authoritative restored state clears speculative wreck effects and missing parts");
    check(api.set_world_vehicle(&car) && g.cars.size() == 1 && api.world_vehicle_handle(car.id) == handle &&
          api.world_vehicle_id(handle) == car.id && g.cars[0].pos[0] == 5 * 4096,
          "updating a host vehicle retains its stable local identity");
    car.position[0] = std::numeric_limits<float>::infinity();
    check(!api.set_world_vehicle(&car) && g.cars.size() == 1, "invalid authoritative state cannot mutate the world");
    car.position[0] = 5.f;
    check(api.set_player_seat(handle, 0) && api.get_player_seat() == 0, "driver occupies the shared driver's seat");
    car.position[0] = 2.f; car.health = 200;
    check(api.set_world_vehicle(&car) && g.cars[0].pos[0] == 5 * 4096 && g.cars[0].health() == 200,
          "host state updates health without snapping the local driver's predicted pose");
    const int32_t remotePos[3] = {5 * 4096, 0, 4096};
    const auto passenger = g.peds.spawnPuppet(g, remotePos, 0, 1, 0, 0);
    CtwPuppetState puppet{}; puppet.size = sizeof puppet; puppet.vehicle = handle;
    check(api.set_puppet_seat(passenger, 1) && api.set_puppet_state(passenger, &puppet), "another player requests a passenger seat");
    g.peds.update(g);
    check(g.cars.size() == 1 && g.cars[0].seatUser[0] == -2 && g.cars[0].seatUser[1] == (int)passenger &&
          g.peds.find(passenger)->vehicle == handle, "two players occupy one vehicle, with no duplicated car");
    check(!api.set_player_seat(handle, 1), "a seat already occupied by another player cannot be overwritten");
    check(api.set_player_seat(0, -1), "driver can leave while passenger remains in the shared car");
    api.remove_puppet(passenger);
    const auto driver = g.peds.spawnPuppet(g, remotePos, 0, 1, 0, 0);
    api.set_puppet_state(driver, &puppet); g.peds.update(g);
    g.player.dead = false; std::copy(remotePos, remotePos + 3, g.player.pos);
    g.startEnter(0);
    check(g.task.car == 0 && g.task.seat == 1 && !g.task.shuffle,
          "normal entry into a remote player's car selects a passenger seat");
    g.task = Game::CarTask{};
    g.player.pos[0] = g.cars[0].pos[0] - 3 * 4096; g.player.pos[1] = g.cars[0].pos[1];
    g.startEnter(0);
    check(g.task.seat == 2 && !g.task.waypointCount && !g.task.shuffle,
          "approaching a shared four-seat car from the left selects the nearby rear-left passenger door");
    g.task = Game::CarTask{}; api.remove_puppet(driver);
    g.vehicleInfos[0].raw[0x10C] = 2;
    const float twoSeatPosition[3] = {20, 0, 1};
    const auto twoSeatCar = api.spawn_vehicle_at(0, twoSeatPosition, 0, 4);
    const auto twoSeatDriver = g.peds.spawnPuppet(g, remotePos, 0, 1, 0, 0);
    puppet.vehicle = twoSeatCar; api.set_puppet_state(twoSeatDriver, &puppet); g.peds.update(g);
    g.player.pos[0] = g.cars[1].pos[0] - 3 * 4096; g.player.pos[1] = g.cars[1].pos[1];
    g.startEnter(1);
    int32_t approach[3]; g.carDoorTarget(approach);
    check(g.task.seat == 1 && g.task.waypointCount == 3 && approach[0] < g.cars[1].pos[0] &&
          std::abs(approach[1] - g.cars[1].pos[1]) > g.cars[1].hy,
          "a shared two-seat car routes a left-side passenger around a bumper to its right-side door");
    for (int i = 0; i < 4; ++i) { g.carDoorTarget(approach); std::copy(approach, approach + 3, g.player.pos); }
    check(g.task.waypointIndex == g.task.waypointCount && g.tickTask(false, 0) && g.task.op != Game::CarTask::GotoDoor,
          "completing the approach waypoints begins entry instead of walking into the car forever");
    g.task = Game::CarTask{}; g.player.attached = false;
    api.remove_puppet(twoSeatDriver); api.remove_vehicle(twoSeatCar);
    g.vehicleInfos[0].raw[0x10C] = 4;

    CtwWorldPed npc{}; npc.size = sizeof npc; npc.id = 100; npc.type = 10; npc.body = 1;
    npc.health = 127; npc.seat = -1;
    check(api.set_world_ped(&npc), "host police NPC appears on the client");
    const auto npcHandle = g.peds.peds.back().uid;
    const int32_t testDirection[3] = {0, 4096, 0};
    g.peds.bulletHit(g, *g.peds.find(npcHandle), 20, testDirection, 0, testDirection, 1, false);
    check(!api.take_puppet_world_damage(npcHandle) && !g.peds.find(npcHandle)->puppet.worldDamage,
          "client NPC replicas never queue host NPC damage");
    npc.position[0] = 3.f;
    check(api.set_world_ped(&npc) && g.peds.peds.size() == 1 && g.peds.peds[0].uid == npcHandle,
          "police snapshots update the same NPC");
    g.peds.find(npcHandle)->puppet.damage = 17;
    check(api.take_world_ped_damage(npc.id) == 17 && api.take_world_ped_damage(npc.id) == 0,
          "client NPC hits drain once for forwarding to the host");
    CtwWorldExplosion explosion{}; explosion.size = sizeof explosion; explosion.type = 0; explosion.scale = 1.f;
    check(api.add_world_explosion(&explosion) && !api.take_world_explosion(&explosion),
          "a replayed host explosion does not feed back as another outgoing event");
    g.multiplayerWorld.captureExplosion(3, remotePos, 1.f);
    check(api.take_world_explosion(&explosion) && explosion.type == 3 && explosion.position[0] == 5.f &&
          !api.take_world_explosion(&explosion), "locally generated explosions drain once with their position");
    check(api.remove_world_ped(npc.id) && g.peds.peds.empty() && api.remove_world_vehicle(car.id) && g.cars.empty(),
          "host removals release shared entities");
    const auto modCar = api.spawn_vehicle_at(0, origin, 0, 4);
    check(api.set_player_seat(modCar, 0), "client can enter a newly mod-spawned car before its spawn acknowledgment");
    car.id = 41; car.health = 255;
    check(api.set_world_vehicle(&car) && g.cars.size() == 2, "a snapshot arriving before its spawn acknowledgment creates a temporary replica");
    const auto temporary = api.world_vehicle_handle(41);
    const auto friendHandle = g.peds.spawnPuppet(g, remotePos, 0, 1, 0, 0);
    puppet.vehicle = temporary;
    api.set_puppet_seat(friendHandle, 1); api.set_puppet_state(friendHandle, &puppet); g.peds.update(g);
    check(api.adopt_world_vehicle(modCar, 41) && g.cars.size() == 1 && api.world_vehicle_handle(41) == modCar &&
          g.playerCar == 0 && g.cars[0].seatUser[0] == -2 && g.cars[0].seatUser[1] == (int)friendHandle &&
          g.peds.find(friendHandle)->vehicle == modCar, "spawn acknowledgment merges the temporary replica while preserving the original driver and passenger");
    check(!api.adopt_world_vehicle(modCar, 42), "one local car cannot acquire two different host identities");
    api.remove_world_vehicle(41); api.remove_puppet(friendHandle);
    CtwWorldVehicle moving = car; moving.id = 5000; moving.flags = 480u << 21; moving.position[0] = 0;
    moving.velocity[0] = 100;
    g.multiplayerWorld.setVehicle(g, &moving, 10.);
    const auto movingHandle = api.world_vehicle_handle(5000);
    moving.position[0] = 10; g.multiplayerWorld.setVehicle(g, &moving, 10.1);
    g.multiplayerWorld.advanceVehicles(g, 10.17);
    CtwVehicleState smooth{}; smooth.size = sizeof smooth; api.get_vehicle_state(movingHandle, &smooth);
    check(std::abs(smooth.position[0] - 5.f) < .01f, "network motion blends between buffered packets at high speed");
    g.multiplayerWorld.advanceVehicles(g, 10.18); api.get_vehicle_state(movingHandle, &smooth);
    check(std::abs(smooth.position[0] - 6.f) < .01f, "passenger target advances steadily between network packets");
    moving.position[0] = 20; g.multiplayerWorld.setVehicle(g, &moving, 10.2);
    api.get_vehicle_state(movingHandle, &smooth);
    check(std::abs(smooth.position[0] - 6.f) < .01f, "receiving a packet during HUD drawing cannot move the rendered vehicle");
    CtwVehicleVisual visual{}; visual.size = sizeof visual; visual.flags = 63; visual.controls = 0x106;
    visual.tyres = 63; visual.burnout = 3; visual.steer = 100; visual.bike_lean = -500;
    visual.bike_pitch = 800; visual.bike_kick = 900; visual.doors_open = 4; visual.door_angles[2] = 60; visual.frame = 77; visual.source = 100;
    check(api.set_vehicle_visual(movingHandle, &visual), "remote vehicle accepts lights, tyre slip, burnout and door state");
    CtwVehicleVisual shown{}; shown.size = sizeof shown; api.get_vehicle_visual(movingHandle, &shown);
    check(!std::memcmp(&shown, &visual, sizeof visual), "the actual vehicle retains the complete presentation for lights, tracks and bike particles");
    const int marksBefore=SkidmarksTestAccess::points(TheSkidmarks());
    for(int i=0;i<2;++i) { ++g.frame; Vehicles_AddSkidmarks(g,g.cars[0]); }
    check(SkidmarksTestAccess::points(TheSkidmarks()) == marksBefore,
          "remote wheel slip does not synthesize a second tyre track");
    CtwSkidPoint skid{}; skid.size = sizeof skid; skid.vehicle = 5000; skid.strip = 1; skid.sequence = 1; skid.hold = 1; skid.direction[0] = 4096;
    check(api.add_world_skid(&skid), "remote contact point accepted");
    for (int i = 0; i < 10; ++i) TheSkidmarks().process();
    skid.position[1] = 4096; ++skid.sequence;
    check(api.add_world_skid(&skid) && SkidmarksTestAccess::points(TheSkidmarks()) == marksBefore + 4,
          "points arriving across render frames form one strip");
    check(!api.add_world_skid(&skid), "duplicate skid point rejected");
    skid.vehicle = 999999; skid.strip = 2; skid.sequence = 1;
    check(api.add_world_skid(&skid), "authenticated geometry survives a missing or already removed vehicle snapshot");
    api.set_player_seat(movingHandle, 1);
    int32_t cameraEye[3]; g.carCam.position(cameraEye);
    check(std::abs(cameraEye[1]-g.player.pos[1])<40*4096,
          "a new authoritative passenger assignment initializes the camera at the car instead of the world origin");
    g.multiplayerWorld.advanceVehicles(g, 10.19);
    api.get_vehicle_state(movingHandle, &smooth);
    check(std::abs(g.player.pos[0]/4096.f - smooth.position[0]) < .001f, "passenger and camera target share the smoothed vehicle position");
    api.set_player_seat(0,-1); api.remove_world_vehicle(5000);
    check(api.set_multiplayer_world(CTW_MULTIPLAYER_OFF) && !g.multiplayerWorld.client(), "leaving restores local simulation");

    api.set_multiplayer_world(CTW_MULTIPLAYER_HOST);
    CtwWorldVehicle hostCar = car; hostCar.id = api.spawn_vehicle_at(0, origin, 0, 4);
    auto& destroyedHostCar = g.cars.back(); destroyedHostCar.damage(255);
    hostCar.health = 255; hostCar.flags = 0;
    check(api.set_world_vehicle(&hostCar) && destroyedHostCar.dead() && destroyedHostCar.justDied &&
          destroyedHostCar.health() == 0,
          "remote driver pose updates cannot resurrect a host wreck or erase its pending explosion");
    api.remove_vehicle(hostCar.id);
    {   // the host carries another player's car forward from its newest state (never plays it back late)
        CtwWorldVehicle driven = car; driven.id = api.spawn_vehicle_at(0, origin, 0, 4);
        const int index = (int)g.cars.size() - 1;
        const auto remoteDriver = g.peds.spawnPuppet(g, remotePos, 0, 1, 0, 0);
        CtwPuppetState seated{}; seated.size = sizeof seated; seated.vehicle = driven.id;
        api.set_puppet_seat(remoteDriver, 0); api.set_puppet_state(remoteDriver, &seated); g.peds.update(g);
        check(g.cars[index].seatUser[0] == (int)remoteDriver && g.multiplayerWorld.simulatesVehicle(g, index),
              "a puppet's car is simulated on the host until its player's states arrive (mission and mod drivers)");
        driven.flags = 0; driven.position[0] = 0; driven.velocity[0] = 10;
        g.multiplayerWorld.setVehicle(g, &driven, 20.);
        driven.position[0] = 1; g.multiplayerWorld.setVehicle(g, &driven, 20.1);
        g.multiplayerWorld.advanceVehicles(g, 20.15);
        CtwVehicleState now{}; now.size = sizeof now; api.get_vehicle_state(driven.id, &now);
        check(std::abs(now.position[0] - 1.5f) < .01f && !g.multiplayerWorld.simulatesVehicle(g, index),
              "the host's copy of a remote car is where its driver is now");
        api.set_puppet_seat(remoteDriver, -1); seated.vehicle = 0; api.set_puppet_state(remoteDriver, &seated);
        g.peds.update(g);
        g.multiplayerWorld.advanceVehicles(g, 20.2);
        check(g.cars[index].seatUser[0] != (int)remoteDriver && g.multiplayerWorld.simulatesVehicle(g, index),
              "a car its remote driver got out of is the host's to simulate again");
        api.remove_puppet(remoteDriver);
        api.remove_vehicle(driven.id);
    }
    const int32_t distant[3] = {1000 * 4096, 1000 * 4096, 0};
    const auto distantPlayer = g.peds.spawnPuppet(g, distant, 0, 1, 0, 0);
    const int32_t hitDirection[3] = {0, 4096, 0};
    auto* distantPuppet = g.peds.find(distantPlayer);
    g.peds.bulletHit(g, *distantPuppet, 20, hitDirection, 0, distant, 1, false);
    g.peds.meleeHit(g, *distantPuppet, 10, 0, distant, 0, false);
    check(api.take_puppet_world_damage(distantPlayer) == 30 && api.take_puppet_world_damage(distantPlayer) == 0 &&
          api.take_puppet_damage(distantPlayer) == 0, "host NPC hit queue drains exactly once independently of PvP");
    check(g.multiplayerWorld.interestCenters(g).size() == 2 && g.multiplayerWorld.nearPlayer(g, distant, 100 * 4096),
          "host interests include distant players");
    {   // what a distant player's camera sees is on screen on the host as well (cBaseCam::CanSee with their view)
        CtwView view{}; view.size = sizeof view;
        for (int k = 0; k < 3; ++k) view.eye[k] = distant[k];
        view.eye[2] += 10 * 4096; view.yaw = 0x2000; view.pitch = 0xF000; view.width = 1280; view.height = 720;
        CameraView same; same.set(view.eye, 0x2000, 0xF000, 1280, 720);
        int32_t ahead[3], behind[3];
        for (int k = 0; k < 3; ++k) {
            ahead[k] = view.eye[k] - same.m[6 + k] * 20;
            behind[k] = view.eye[k] + same.m[6 + k] * 20;
        }
        check(same.canSee(ahead, 4096) && !same.canSee(behind, 4096), "test view setup");
        check(!g.multiplayerWorld.seenByOthers(g, ahead, 4096), "no remote view, nothing remote is on screen");
        check(api.set_puppet_view(distantPlayer, &view) && g.multiplayerWorld.seenByOthers(g, ahead, 4096) &&
              !g.multiplayerWorld.seenByOthers(g, behind, 4096), "a remote player's view counts as on screen on the host");
        const float carAt[3] = {ahead[0] / 4096.f, ahead[1] / 4096.f, ahead[2] / 4096.f};
        const auto seenCar = api.spawn_vehicle_at(0, carAt, 0, 4);
        g.cars.back().screenTimer = 0;
        g.updateWanted();
        check(seenCar && g.cars.back().screenTimer == 0xFF, "a car in a remote player's view keeps its on-screen timer");
        api.remove_vehicle(seenCar);
        const int32_t eye[3] = {4096, 2 * 4096, 3 * 4096};
        g.view.set(eye, 0x1234, 0xF100, 800, 600);
        CtwView got{}; got.size = sizeof got;
        check(api.get_view(&got) && got.width > 0 && got.eye[1] == 2 * 4096 && got.yaw == 0x1234 &&
              got.pitch == 0xF100 && got.width == 800 && got.height == 600,
              "the game's own view can be read for sending");
        check(api.set_puppet_view(distantPlayer, nullptr) && !g.multiplayerWorld.seenByOthers(g, ahead, 4096),
              "a forgotten view no longer counts");
        api.set_puppet_view(distantPlayer, &view);
    }
    const int32_t originalPlayer[3] = {g.player.pos[0], g.player.pos[1], g.player.pos[2]};
    g.updateSectors();
    check(g.sectors.loadedAt(distant[0], distant[1]), "distant players retain their city sectors on the host");
    g.updatePopulation(); g.updatePopulation();
    check(!g.populationOverride_ && !std::memcmp(originalPlayer, g.player.pos, sizeof originalPlayer),
          "rotating population interests never relocates the host player");
    api.remove_puppet(distantPlayer); g.updateSectors(); g.sectors.collect();
    check(!g.sectors.loadedAt(distant[0], distant[1]), "departed players release distant sectors");
    WorldSectors sectors;
    sectors.hold(0, 0);
    std::vector<std::array<int32_t, 3>> areas{{distant[0], distant[1], 0}, {distant[0], distant[1], 0}};
    sectors.setAdditionalAreas(areas); sectors.collect();
    areas.pop_back(); sectors.setAdditionalAreas(areas); sectors.collect();
    check(sectors.loadedAt(distant[0], distant[1]), "overlapping remote interests retain one sector reference");
    sectors.setAdditionalAreas({}); sectors.collect();
    check(!sectors.loadedAt(distant[0], distant[1]) && sectors.loadedAt(0, 0), "clearing remote interests preserves the local held sector");
    api.set_multiplayer_world(CTW_MULTIPLAYER_OFF);
    check(api.set_multiplayer_world(CTW_MULTIPLAYER_HOST), "start host pickup authority");
    g.peds.peds.clear();
    Pedestrians::Ped victim; victim.uid = 0xABCDEF; victim.moneyTier = 3;
    victim.dropWeapon = false; victim.dropMoney = true; victim.body.health = 127;
    std::copy(g.player.pos, g.player.pos + 3, victim.body.pos);
    victim.body.pos[0] += 20 * 4096;
    g.peds.peds.push_back(victim);
    check(api.damage_world_ped(victim.uid, 255) && g.peds.find(victim.uid)->body.dead,
          "lethal shared-world damage kills the host NPC");
    g.peds.update(g);
    CtwWorldPickup drop{}; drop.size = sizeof drop;
    check(api.get_world_pickup(0, &drop) && drop.type == Pickups::Money && drop.amount == 60,
          "updating the killed NPC creates its shared money pickup");
    const int32_t far[3] = {drop.position[0] + 200 * 4096, drop.position[1], drop.position[2]};
    CtwWorldPickup accepted{}; accepted.size = sizeof accepted;
    check(!api.claim_world_pickup(drop.id, far, 0, &accepted), "remote pickup claim checks distance");
    const int32_t near[3] = {drop.position[0], drop.position[1], drop.position[2]};
    check(api.claim_world_pickup(drop.id, near, 0, &accepted) && accepted.amount == 60 &&
          !api.claim_world_pickup(drop.id, near, 0, &accepted), "pickup has exactly one successful claimant");
    check(api.set_multiplayer_world(CTW_MULTIPLAYER_CLIENT), "start client pickup replica");
    const int32_t moneyBefore = g.money;
    check(api.set_world_pickup(&drop) && api.grant_world_pickup(&drop) && g.money == moneyBefore + 60,
          "client sees the host drop and receives its money");
    check(api.remove_world_pickup(drop.id) && g.pickups.list().empty(), "host removal deletes client replica");
    victim.dropPending = true; PedsTestAccess::drop(g, victim);
    check(g.pickups.list().empty(), "client NPC replicas never create independent drops");
    api.set_multiplayer_world(CTW_MULTIPLAYER_OFF);
    SDL_Quit();
    if (!failures) std::puts("PASS: authoritative multiplayer world");
    return failures ? 1 : 0;
}
