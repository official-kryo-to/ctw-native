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
    CtwHostApi host{}; host.version = 5; host.size = sizeof host;
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
    check(init(&host) == 0, "v4 host loads the mod kit");
    check(textureLoads == 1, "the same mod folder loads its textures and its DLL");
    const CtwApi* api = getApi();
    check(api && api->version == 5 && api->size == sizeof(CtwApi), "a mod's DLL receives the complete v5 table");
    if (api) {
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
    check(clickText("Disabled"), "the switch now reads Disabled");
    check(toggles() == 2 && enabled() == 1, "reenabling notifies the same loaded mod");
    check(textureLoads == 2, "reenabling the same mixed mod reapplies its textures");
    eventHook(&e,eventUser); check(events() == 2, "reenabling resumes event delivery");
    shutdown();
    { std::ifstream saved(dir / "Probe/settings.ini"); std::string text((std::istreambuf_iterator<char>(saved)), {});
      check(text.find("speed=2.25") != std::string::npos && text.find("bad") == std::string::npos, "settings are written at shutdown"); }
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
