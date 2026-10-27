// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#define SDL_MAIN_HANDLED
#include "ctw_mod.h"
#include "ctw_plugin.h"
#include "ctw_ui.h"
#include <SDL.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <cmath>
#include <algorithm>
#include <iterator>
static int failures;
static void check(bool ok, const char* why) { if (!ok) { std::fprintf(stderr,"FAIL: %s\n",why); ++failures; } }
static std::string modsDir;
static CtwCallback hudHook, tickHook;
static CtwKeyCallback keyHook;
static CtwGameEventCallback eventHook;
static CtwCharacterCallback characterHook;
static void* characterUser;
static CtwCharacterModel released;
static void *hudUser, *tickUser, *keyUser, *eventUser;
static float mx, my;
static int clicked, wheelConsumed;
static int textureLoads, textureRestores;
static bool fakeInput = true;
static std::map<std::string, std::pair<float, float>> drawn;   // text -> where the menu drew it last

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    SDL_Init(0);
    const auto dir = std::filesystem::temp_directory_path() / ("ctw-sdk-test-" + std::to_string(SDL_GetTicks64()));
    std::filesystem::create_directories(dir / "Probe"); modsDir = dir.string() + "/";
    std::filesystem::create_directories(dir / "Probe/textures");
    { std::ofstream png(dir / "Probe/textures/42.png"); png << "synthetic host accepts this texture path"; }
    std::filesystem::copy_file(argv[2],dir / "Probe/probe.dll");
    { std::ofstream ini(dir / "Probe/mod.ini"); ini << "name = Probe\ndll = probe.dll\n"; }
    void* menu = SDL_LoadObject(argv[1]);
    void* probe = SDL_LoadObject((dir / "Probe/probe.dll").string().c_str());
    if (!menu || !probe) { std::fprintf(stderr,"DLL load failed: %s\n",SDL_GetError()); return 2; }
    const auto init = (CtwPluginInitFn)SDL_LoadFunction(menu,"ctw_plugin_init");
    const auto shutdown = (CtwPluginShutdownFn)SDL_LoadFunction(menu,"ctw_plugin_shutdown");
    const auto getApi = (const CtwApi* (*)())SDL_LoadFunction(probe,"ctw_test_api");
    const auto events = (int (*)())SDL_LoadFunction(probe,"ctw_test_events");
    const auto toggles = (int (*)())SDL_LoadFunction(probe,"ctw_test_toggles");
    const auto ticks = (int (*)())SDL_LoadFunction(probe,"ctw_test_ticks");
    const auto enabled = (int (*)())SDL_LoadFunction(probe,"ctw_test_enabled");
    const auto probeMod = (CtwMod* (*)())SDL_LoadFunction(probe,"ctw_test_mod");
    if (!init || !shutdown || !getApi || !events || !toggles || !ticks || !enabled || !probeMod) return 2;
    CtwHostApi host{}; host.version = 13; host.size = sizeof host;
    host.weapon_type_count = [] { return 25; };
    host.weapon_name = [](int t) -> const char* { return t == 5 ? "Pistol" : nullptr; };
    host.weapon_givable = [](int t) { return t == 5 ? 1 : 0; };
    host.give_weapon = [](int t, int ammo) { return t == 5 && ammo == 0 ? 1 : 0; };
    host.get_player_weapon = [](int* t, int* a, int* c) { if (t) *t = 5; if (a) *a = 30; if (c) *c = 17; return 1; };
    host.cycle_weapon = [](int d) { return d ? 1 : 0; };
    static int propPhysics = 0;
    host.set_prop_physics = [](int full) { propPhysics = full; };
    host.get_prop_physics = [] { return propPhysics; };
    host.spawn_puppet = [](const float*, float, int body, int, int) -> CtwPuppet { return body == 1 ? 77u : 0u; };
    host.take_puppet_damage = [](CtwPuppet p) { return p == 77 ? 12 : 0; };
    host.damage_player = [](int amount) { return amount > 0 ? 1 : 0; };
    host.mods_dir = [] { return modsDir.c_str(); }; host.log = [](const char*) {};
    host.override_texture_png = [](int id,const char*) -> int { if (id != 42) return 0; ++textureLoads; return 1; };
    host.restore_texture = [](int id) { if (id == 42) ++textureRestores; };
    host.on_draw_hud = [](CtwCallback f,void* u) { hudHook=f; hudUser=u; };
    host.on_tick = [](CtwCallback f,void* u) { tickHook=f; tickUser=u; };
    host.on_key = [](CtwKeyCallback f,void* u) { keyHook=f; keyUser=u; };
    host.on_game_event = [](CtwGameEventCallback f,void* u) { eventHook=f; eventUser=u; };
    host.get_render_distance = [] { return 120.f; };
    host.get_capabilities = [] { return (uint32_t)CTW_CAP_LIVE_VEHICLES; };
    host.live_vehicle_at = [](int i) -> CtwVehicle { return i == 2 ? 57 : 0; };
    host.spawn_vehicle_at = [](int model,const float*,float,int) -> CtwVehicle { return model == 5 ? 99 : 0; };
    host.get_vehicle_state = [](CtwVehicle v,CtwVehicleState* s) -> int {
        if (v != 57 || !s || s->size < sizeof *s) return 0;
        s->vehicle = v; s->health = 123; return 1;
    };
    host.get_mouse = [](float* x,float* y) { *x=mx; *y=my; };
    host.mouse_down = [](int) { return clicked; }; host.mouse_clicked = [](int) { return clicked; };
    host.mouse_wheel = [] { return 1.f; }; host.set_mouse_captured = [](int) {};
    host.consume_mouse_wheel = [] { ++wheelConsumed; };
    host.set_game_input = [](int on) { fakeInput = on != 0; };
    host.screen_width = [] { return 1280; }; host.screen_height = [] { return 720; };
    host.line_height = [](float s) { return 16.f*s; };
    host.text_width = [](float s,const char* t) { return t ? (float)std::char_traits<char>::length(t)*8*s : 0; };
    host.draw_text = [](float x,float y,float,uint32_t,const char* t) { if (t) drawn[t] = {x, y}; };
    host.draw_rect = [](float,float,float,float,uint32_t) {};
    host.set_clip = [](float,float,float,float) {};
    host.frame_count = [] { return 1u; }; host.text_input = [] { return ""; };
    host.world_line = [](const float*,const float*,float* f) { if (f) *f = 0.25f; return 1; };
    host.set_render_style = [](const CtwRenderStyle* s) { return s && s->sepia == 0.5f ? 1 : 0; };
    host.get_camera_shake = [](float* out) { if (out) { out[0]=1; out[1]=2; out[2]=3; } };
    host.on_character_render = [](CtwCharacterCallback f, void* u) { characterHook=f; characterUser=u; };
    host.load_character_model = [](const char*, const char*, const char*) -> CtwCharacterModel { return 73; };
    host.release_character_model = [](CtwCharacterModel model) { released=model; };
    host.draw_character_model = [](CtwCharacterModel model, const CtwCharacterPose*, float, const char*, float, int) -> int { return model == 73; };
    host.get_character_bounds = [](CtwCharacterModel model, const char*, float, CtwCharacterBounds* b) -> int {
        if (model != 73 || !b || b->size < sizeof *b) return 0;
        b->maximum[2] = 2; b->duration = 1.5f; return 1;
    };
    host.draw_character_model_at_time = [](CtwCharacterModel model, const CtwCharacterPose*, float, const char*, float seconds, int loop, int ground) -> int {
        return model == 73 && seconds == .75f && !loop && ground;
    };
    host.draw_character_model_attached = [](CtwCharacterModel model, const CtwCharacterPose*, float, const char*, float phase, int loop, int bone) -> int {
        return model == 73 && phase == .25f && loop && bone == 1;
    };
    check(init(&host) == 0, "v9 host loads the mod kit");
    check(textureLoads == 1, "the same mod folder loads its textures and its DLL");
    const CtwApi* api = getApi();
    check(api && api->version == 13 && api->size == sizeof(CtwApi), "a mod's DLL receives the complete v13 table");
    if (api) {
        int t = 0, a = 0, c = 0;
        check(CTW_MOD_HAS(api, give_weapon) && api->give_weapon(5, 0) && !api->give_weapon(6, 0) &&
              api->weapon_type_count() == 25 && api->weapon_name(5) && api->weapon_givable(5) &&
              api->get_player_weapon(&t, &a, &c) && t == 5 && a == 30 && c == 17, "weapon functions forward to the host");
        check(CTW_MOD_HAS(api, cycle_weapon) && api->cycle_weapon(1) && !api->cycle_weapon(0), "v11 weapon switch forwards");
        check(CTW_MOD_HAS(api, set_prop_physics) && (api->set_prop_physics(1), api->get_prop_physics() == 1),
              "v12 prop physics forwards");
        const float at[3] = {1, 2, 3};
        check(CTW_MOD_HAS(api, spawn_puppet) && api->spawn_puppet(at, 0, 1, 4, 5) == 77 && api->take_puppet_damage(77) == 12 &&
              api->damage_player(5) && !CTW_MOD_HAS(api, set_puppet_state), "v13 multiplayer functions forward (and only those the host has)");
    }
    if (api) {
        CtwCharacterPose pose{}; pose.size=sizeof pose; pose.id=42;
        check(api->load_character_model("model", "clips", "textures") == 73 &&
              api->draw_character_model(73, &pose, 1, "idle", 0, 1), "character model functions forward to host");
        CtwCharacterBounds bounds{}; bounds.size = sizeof bounds;
        check(api->get_character_bounds(73, "idle", 0, &bounds) && bounds.maximum[2] == 2 && bounds.duration == 1.5f &&
              api->draw_character_model_at_time(73, &pose, 1, "fall", .75f, 0, 1),
              "v8 bounds, elapsed time and fallen-body grounding forward to host");
        check(api->draw_character_model_attached(73,&pose,1,"rider",.25f,1,1),"v9 native bone attachments forward to host");
        api->release_character_model(73);
        check(released == 73 && characterHook && characterHook(&pose, characterUser), "models release and enabled character hook overrides sprites");
        float shake[3]{}; api->get_camera_shake(shake);
        check(shake[0] == 1 && shake[1] == 2 && shake[2] == 3, "custom-camera shake forwards through the mod menu");
        check(api->live_vehicle_at(2) == 57 && api->spawn_vehicle_at(5,nullptr,0,0) == 99, "new handles and spawning forward to host");
        CtwVehicleState state{}; state.size = sizeof state;
        check(api->get_vehicle_state(57,&state) && state.health == 123, "state output forwards through mod menu");
        float fraction = 0;
        check(api->world_line(nullptr,nullptr,&fraction) == 1 && fraction == 0.25f, "v5 world query forwards to host");
        CtwRenderStyle style{}; style.size = sizeof style; style.sepia = 0.5f;
        check(api->set_render_style(&style) == 1, "render style forwards to host");
        CtwMod* mod = probeMod();
        check(api->get_setting(mod,"speed",1.5f) == 1.5f, "missing settings return the fallback");
        api->set_setting(mod,"speed",2.25f); api->set_setting(mod,"bad key=",3.f);
        check(api->get_setting(mod,"speed",0) == 2.25f && api->get_setting(mod,"bad key=",7.f) == 7.f,
              "settings round-trip and reject keys that would corrupt the file");
    }
    CtwGameEvent e{sizeof e,CTW_EVENT_VEHICLE_REMOVED,57,5,0};
    eventHook(&e,eventUser); tickHook(tickUser);
    check(events() == 1 && ticks() == 1, "enabled mod receives gameplay events and ticks");
    keyHook(61,keyUser); // F4
    hudHook(hudUser);
    check(wheelConsumed == 1 && fakeInput, "F4 consumes the wheel while preserving driving input");
    // Select the mod in the list, then click its "Enabled" switch, both found by the text the menu drew.
    auto clickText = [&](const char* label) {
        auto it = drawn.find(label);
        if (it == drawn.end()) return false;
        mx = it->second.first + 4; my = it->second.second + 4; clicked = 1;
        hudHook(hudUser); clicked = 0; hudHook(hudUser);
        return true;
    };
    check(clickText("Probe") && clickText("Enabled"), "the menu shows the mod and its switch");
    check(toggles() == 1 && enabled() == 0, "F4 disable delivers a cleanup callback");
    check(textureRestores == 1, "disabling a mixed mod removes its textures as well as stopping its code callbacks");
    eventHook(&e,eventUser); tickHook(tickUser);
    check(events() == 1 && ticks() == 1, "disabled mod receives no gameplay events or ticks");
    CtwCharacterPose pose{}; pose.size=sizeof pose; pose.id=42;
    check(characterHook && !characterHook(&pose, characterUser), "disabled mod falls back to original character sprites");
    check(clickText("Disabled"), "the switch now reads Disabled");
    check(toggles() == 2 && enabled() == 1, "reenabling notifies the same loaded mod");
    check(textureLoads == 2, "reenabling the same mixed mod reapplies its textures");
    eventHook(&e,eventUser); check(events() == 2, "reenabling resumes event delivery");
    check(characterHook && characterHook(&pose, characterUser), "reenabling resumes character rendering");
    shutdown();
    { std::ifstream saved(dir / "Probe/settings.ini"); std::string text((std::istreambuf_iterator<char>(saved)), {});
      check(text.find("speed=2.25") != std::string::npos && text.find("bad") == std::string::npos, "settings are written at shutdown"); }
    host.version = 12; host.size = offsetof(CtwHostApi,spawn_puppet);
    check(init(&host) == 0, "truncated v12 host remains supported");
    api = getApi();
    check(api && api->version == 12 && CTW_MOD_HAS(api,set_prop_physics) && !CTW_MOD_HAS(api,spawn_puppet),
          "v12 host cannot expose v13 puppets");
    shutdown();
    host.version = 11; host.size = offsetof(CtwHostApi,set_prop_physics);
    check(init(&host) == 0, "truncated v11 host remains supported");
    api = getApi();
    check(api && api->version == 11 && CTW_MOD_HAS(api,cycle_weapon) && !CTW_MOD_HAS(api,set_prop_physics),
          "v11 host cannot expose v12 prop physics");
    shutdown();
    host.version = 10; host.size = offsetof(CtwHostApi,cycle_weapon);
    check(init(&host) == 0, "truncated v10 host remains supported");
    api = getApi();
    check(api && api->version == 10 && CTW_MOD_HAS(api,give_weapon) && !CTW_MOD_HAS(api,cycle_weapon),
          "v10 host cannot expose the v11 weapon switch");
    shutdown();
    host.version = 9; host.size = offsetof(CtwHostApi,weapon_type_count);
    check(init(&host) == 0, "truncated v9 host remains supported");
    api = getApi();
    check(api && api->version == 9 && CTW_MOD_HAS(api,draw_character_model_attached) && !CTW_MOD_HAS(api,give_weapon),
          "v9 host cannot expose the v10 weapon tail");
    shutdown();
    host.version = 8; host.size = offsetof(CtwHostApi,draw_character_model_attached);
    check(init(&host) == 0, "truncated v8 host remains supported");
    api = getApi();
    check(api && api->version == 8 && CTW_MOD_HAS(api,draw_character_model_at_time) &&
          !CTW_MOD_HAS(api,draw_character_model_attached), "v8 host cannot expose the v9 attachment tail");
    shutdown();
    host.version = 7; host.size = offsetof(CtwHostApi,get_character_bounds);
    check(init(&host) == 0, "truncated v7 host remains supported");
    api = getApi();
    check(api && api->version == 7 && CTW_MOD_HAS(api,draw_character_model) &&
          !CTW_MOD_HAS(api,get_character_bounds) && !CTW_MOD_HAS(api,draw_character_model_at_time),
          "v7 host keeps phase-based drawing without reading the v8 tail");
    shutdown();
    host.version = 6; host.size = offsetof(CtwHostApi,load_character_model); characterHook=nullptr;
    check(init(&host) == 0, "truncated v6 host remains supported");
    api = getApi();
    check(api && api->version == 6 && CTW_MOD_HAS(api,get_camera_shake) && !CTW_MOD_HAS(api,load_character_model) &&
          !characterHook, "v6 host does not read or advertise the character tail");
    shutdown();
    host.version = 5; host.size = offsetof(CtwHostApi,get_camera_shake);
    check(init(&host) == 0, "truncated v5 host remains supported");
    api = getApi();
    check(api && api->version == 5 && CTW_MOD_HAS(api,world_line) && !CTW_MOD_HAS(api,get_camera_shake),
          "v5 host keeps its camera functions without reading the new shake tail");
    shutdown();
    // A v4 game: the mod table keeps its menu-side v5 functions but not the game's camera functions.
    host.version = 4; host.size = offsetof(CtwHostApi,set_control_yaw);
    check(init(&host) == 0, "v4 host remains supported");
    api = getApi();
    check(api && api->version == 4 && !CTW_MOD_HAS(api,world_line) && !CTW_MOD_HAS(api,set_control_yaw) &&
          CTW_MOD_HAS(api,get_setting) && CTW_MOD_HAS(api,get_capabilities), "v4 host leaves the v5 game functions empty");
    if (api) check(api->get_setting(probeMod(),"speed",0) == 2.25f, "settings are read back in the next session");
    shutdown();
    // A genuinely truncated v3 table must still load and must not advertise/read its v4 tail.
    host.version = 3; host.size = offsetof(CtwHostApi,get_capabilities); eventHook = nullptr;
    check(init(&host) == 0, "v3 host prefix remains supported");
    api = getApi();
    check(api && api->version == 3 && api->size == offsetof(CtwApi,get_capabilities) &&
          !CTW_MOD_HAS(api,get_capabilities) && !eventHook, "v3 mod table stops exactly before optional gameplay fields");
    if (api) check(api->get_render_distance() == 120.f, "legacy API forwards correctly on v3");
    shutdown(); SDL_UnloadObject(probe); SDL_UnloadObject(menu);
    std::filesystem::remove_all(dir); SDL_Quit();
    return failures ? 1 : 0;
}
