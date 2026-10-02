// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#define SDL_MAIN_HANDLED
#include "game.h"
#include "plugins.h"
#include "os/os.h"
#include <SDL.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

static int failures;
static void check(bool ok, const char* why) { if (!ok) { std::fprintf(stderr, "FAIL: %s\n", why); ++failures; } }
static std::vector<CtwGameEvent> events;
static CtwVehicle removeDuringEvent;
static void event(const CtwGameEvent* e, void*) {
    events.push_back(*e);
    if (removeDuringEvent && e->type == CTW_EVENT_VEHICLE_SPAWNED) {
        const auto handle = removeDuringEvent; removeDuringEvent = 0;
        Plugins_HostApi().remove_vehicle(handle);
    }
}

struct CollisionTestAccess {
    static void scene(Collision& col) {
        col.world_.assign(10000 * 4, 0);
        auto cell = std::make_unique<Collision::Cell>(); cell->loaded = true;
        cell->groundMap.assign(400, 0x55); // synthetic land
        cell->boxes.push_back({-2475*4096,-1475*4096,2*4096,4096,4096,2*4096,0,0x200});
        col.cells_[2020] = std::move(cell);
    }
};

int main() {
    SDL_Init(0);
    Game& g = TheGame();
    const auto& api = Plugins_HostApi();
    check(api.version == 5 && CTW_HOST_HAS(&api, consume_mouse_wheel) && CTW_HOST_HAS(&api, world_line),
          "v5 append-only host is available");
    check(api.get_capabilities() == CTW_CAP_GAME_EVENTS, "missing game data does not advertise unsupported systems");
    api.on_game_event(event, nullptr);
    g.vehicleInfos.resize(2);
    std::memset(g.vehicleInfos.data(), 0, g.vehicleInfos.size() * sizeof(VehicleInfo));
    g.vehicleInfos[0].raw[0x94] = 1; // driver door
    g.vehicleInfos[1].raw[0] = 2; // unsupported boat
    check(api.get_capabilities() & CTW_CAP_LIVE_VEHICLES, "loaded vehicle definitions enable vehicle capability");
    const float pos[3] = {-2470,-1475,1};
    const auto first = api.spawn_vehicle_at(0,pos,90,4);
    const auto second = api.spawn_vehicle_at(0,pos,-90,5);
    check(first && second && first != second, "spawn returns unique runtime handles");
    check(api.live_vehicle_count() == 2 && api.live_vehicle_at(1) == second && api.live_vehicle_at(-1) == 0,
          "live enumeration is separate from model ids");
    check(!api.spawn_vehicle_at(1,pos,0,4), "unported vehicle types are rejected");
    CtwVehicleState s{}; s.size = sizeof s;
    check(api.get_vehicle_state(first,&s) && s.vehicle == first && s.model_id == 0 && s.palette == 4 &&
          std::abs(s.heading - 90) < 0.1, "vehicle state and CCW heading round-trip");
    check(!api.get_vehicle_state(first,nullptr), "null output rejected");
    s.size = 8; s.palette = 999;
    check(!api.get_vehicle_state(first,&s) && s.palette == 999, "undersized state output is not written");
    s.size = sizeof s;
    const float invalid[3] = {std::numeric_limits<float>::quiet_NaN(),0,0};
    const float moved[3] = {-2468,-1472,3};
    const float velocity[3] = {2,3,4};
    check(!api.spawn_vehicle_at(0,invalid,0,4) && !api.set_vehicle_transform(first,invalid,0) &&
          !api.set_vehicle_velocity(first,invalid), "non-finite input is rejected before fixed-point conversion");
    check(api.set_vehicle_velocity(first,velocity) && api.set_vehicle_transform(first,moved,180), "active physics car can be teleported");
    api.get_vehicle_state(first,&s);
    check(s.position[0] == moved[0] && s.position[1] == moved[1] && s.position[2] == moved[2] &&
          s.velocity[0] == 0 && s.velocity[1] == 0, "teleport updates centre of gravity and stops old velocity");
    check(api.set_vehicle_velocity(first,velocity), "vehicle velocity is editable");
    check(api.set_vehicle_palette(first,8) && !api.set_vehicle_palette(first,27) &&
          api.set_vehicle_engine(first,1) && api.set_vehicle_persistent(first,1), "vehicle paint, engine and persistence edits");
    api.set_vehicle_engine(first,0);
    Vehicle::Controls gas; gas.throttle = 0x1000;
    g.cars[0].act(gas,true,nullptr);
    check(!g.cars[0].soundState().gas, "engine-off control suppresses propulsion as well as audio/lights");
    api.set_vehicle_engine(first,1); g.cars[0].act(Vehicle::Controls{},false,nullptr);
    check(api.set_vehicle_door(first,0,1) && !api.set_vehicle_door(first,4,1), "valid doors work and invalid seats are rejected");
    check(api.damage_vehicle(first,80) && api.repair_vehicle(first), "live vehicle can be damaged and repaired");
    api.get_vehicle_state(first,&s);
    check(s.health == 255 && (s.flags & CTW_VEHICLE_PERSISTENT) && (s.flags & CTW_VEHICLE_ENGINE_ON), "repair preserves other entity state");
    g.playerCar = 1; g.cars[1].seatUser[0] = -2;
    check(!api.remove_vehicle(second), "occupied player car cannot be deleted");
    check(api.remove_vehicle(first) && g.playerCar == 0 && api.live_vehicle_at(0) == second,
          "removing preceding car keeps player index and stable handle intact");
    check(!api.get_vehicle_state(first,&s) && !api.set_vehicle_engine(first,1), "stale handles cannot address another car");
    CtwPlayerState p{}; p.size = sizeof p;
    check(api.get_player_state(&p) && p.vehicle == second && (p.flags & CTW_PLAYER_IN_VEHICLE), "player state identifies the live driven car");
    check(!api.set_player_heading(60), "heading edit rejects seated player");
    g.playerCar = -1; g.cars[0].seatUser[0] = -1;
    check(api.set_player_heading(60) && api.get_player_state(&p) && std::abs(p.heading-60) < 0.1,
          "on-foot heading uses the documented angle convention");
    g.task.op = Game::CarTask::GotoDoor; g.task.car = 0;
    check(!api.remove_vehicle(second) && !api.set_vehicle_transform(second,moved,0), "active vehicle task target cannot be invalidated");
    g.task = Game::CarTask{};
    check(!api.set_player_appearance(0,0,0) && !api.player_enter_vehicle(second), "missing sprite data is reported instead of starting broken animations");
    check(api.set_traffic_density(0) && api.get_traffic_density() == 0 && !api.set_traffic_density(5) &&
          !api.set_traffic_density(std::numeric_limits<float>::infinity()), "traffic density validates its documented limits");
    g.traffic.update(g); check(g.traffic.maxCars == 0, "zero density actually suppresses traffic generation");
    CtwGround ground{}; ground.size = sizeof ground;
    float fraction = 0;
    check(!api.get_ground(pos,&ground) && api.line_hits_world_boxes(pos,moved) == -1 && api.world_line(pos,moved,&fraction) == -1,
          "world queries report unavailable collision data");
    CollisionTestAccess::scene(g.collision);
    const float land[3] = {-2470,-1475,10}, a[3] = {-2480,-1475,2}, b[3] = {-2470,-1475,2};
    check(api.get_ground(land,&ground) && ground.height == 0 && ground.surface == 0, "ground query reads synthetic world data");
    check(api.line_hits_world_boxes(a,b) == 1 && api.line_hits_world_boxes(land,land) == 0, "world box query distinguishes blocked and clear segments");
    check(api.world_line(a,b,&fraction) == 1 && std::abs(fraction - 0.4f) < 0.01f, "world line reports where the box face is crossed");
    const float above[3] = {-2470,-1475,30}, far[3] = {-2100,-1475,2};
    check(api.world_line(land,above,&fraction) == 0 && fraction == 1 && api.world_line(a,far,nullptr) == -1,
          "clear lines report fraction 1 and scans beyond the query length are refused");
    check(!api.radio_station_count() && api.get_radio_station() == -1 && !api.set_radio_station(0), "unavailable radio is explicit");
    Plugins_Tick();
    check(events.size() == 3 && events[0].type == CTW_EVENT_VEHICLE_SPAWNED && events[2].type == CTW_EVENT_VEHICLE_REMOVED &&
          events[2].vehicle == first, "spawn/removal events are ordered and keep removed handle metadata");
    events.clear();
    const auto third = api.spawn_vehicle_at(0,pos,0,4); removeDuringEvent = third;
    Plugins_Tick();
    check(events.size() == 1 && events[0].vehicle == third && !api.get_vehicle_state(third,&s),
          "event callback can safely remove a just-spawned vehicle");
    Plugins_Tick();
    check(events.size() == 2 && events[1].type == CTW_EVENT_VEHICLE_REMOVED, "events produced by a callback are deferred to the following tick");
    api.remove_vehicle(second);
    const auto fourth = api.spawn_vehicle_at(0,pos,0,4);
    check(fourth != first && fourth != second && fourth != third, "expired handles are never recycled");
    g.nextUid = 0;
    check(!api.spawn_vehicle_at(0,pos,0,4), "handle exhaustion cannot recycle old identifiers");
    api.remove_vehicle(fourth);
    CtwPedState ped{}; ped.size = sizeof ped;
    check(api.ped_count() == 0 && !api.get_ped_state(0,&ped) && !(api.get_capabilities() & CTW_CAP_PEDESTRIANS),
          "no pedinfo.bin: no pedestrians are advertised");
    g.peds.peds.emplace_back(); g.peds.peds.back().uid = 77; g.peds.peds.back().type = 10; g.peds.peds.back().deadFrames = 3;
    check(api.ped_count() == 1 && api.get_ped_state(0,&ped) && ped.id == 77 && ped.ped_type == 10 &&
          (ped.flags & CTW_PED_KNOCKED_DOWN), "pedestrian state is readable by index");
    ped.size = 4; check(!api.get_ped_state(0,&ped), "undersized pedestrian state is rejected");
    g.peds.peds.clear();
    check(api.set_ped_density(0) && api.get_ped_density() == 0 && !api.set_ped_density(4.5f) && api.set_ped_density(1),
          "pedestrian density validates its limits");
    api.set_control_yaw(1,90);
    check(g.controlYaw.on && g.controlYaw.yaw == -0x4000, "control yaw uses the get_camera angle convention");
    api.set_camera_fov(500); WorldCamera view; g.viewCamera(view);
    check(g.fovOverride == 120 && view.fovY == 120, "field of view is clamped and applied to the shown camera");
    api.set_control_yaw(1,std::numeric_limits<float>::quiet_NaN());
    check(!g.controlYaw.on, "invalid control yaw falls back to the game camera");
    CtwRenderStyle style{}; style.size = sizeof style;
    style.light_pools = 5; style.headlight_pools = 1; style.tint[0] = style.tint[1] = style.tint[2] = 1;
    style.sepia = 0.5f; style.saturation = 1; style.contrast = 1;
    check(api.set_render_style(&style) && g.renderStyle.lightPools == 2 && g.renderStyle.sepia == 0.5f &&
          g.renderStyle.graded(), "render style is clamped and stored");
    style.vignette = std::numeric_limits<float>::infinity();
    check(!api.set_render_style(&style) && g.renderStyle.sepia == 0.5f, "an invalid render style is refused");
    style.size = 4; check(!api.set_render_style(&style), "an undersized render style is refused");
    api.set_control_yaw(1,0); api.set_camera_fov(70);
    Plugins_Shutdown();
    check(!g.controlYaw.on && g.fovOverride == 0 && !g.renderStyle.graded() && g.renderStyle.lightPools == 0,
          "plugin shutdown releases camera and render style overrides"); SDL_Quit();
    return failures ? 1 : 0;
}
