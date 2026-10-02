// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// A stand-in for the game for test_modmenu_layout.py: loads a native build of ModMenu, feeds it keys and clicks,
// and writes everything it draws (and the game's own clock and help line) to a JSON file.
//
//   host <ModMenu library> <mods dir> <W> <H> <line height> <out.json> [key:<scancode> | click:<text>]...
//
// click:<text> clicks the middle of the text drawn last frame, so the scenarios survive layout changes.
#include "ctw_plugin.h"
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static int W = 1280, H = 720;
static float LH = 16.f;
static std::string modsDir;
static FILE* out;   // nullptr: draw without recording
static uint32_t frame;
static float mx, my, wheel;
static int clicked, down;
static std::vector<std::pair<CtwCallback, void*>> huds, ticks;
static std::vector<std::pair<CtwKeyCallback, void*>> keys;
struct Drawn { std::string text; float x, y, w, h; };
static std::vector<Drawn> lastTexts, texts;

static std::string esc(const char* s) {
    std::string r;
    for (; *s; ++s) { if (*s == '"' || *s == '\\') r += '\\'; r += *s; }
    return r;
}
// Proportional widths like the game's Helvetica-style font: narrow i/l/punctuation, wide M/W, capitals wider.
static float advance(unsigned char c) {
    if (strchr("il.,:;'|!", c)) return 3.5f;
    if (strchr("fjrt ()[]/-", c)) return 4.5f;
    if (strchr("mwMW@", c)) return 11.5f;
    if (c >= 'A' && c <= 'Z') return 9.f;
    return 7.5f;
}
static float tw(float s, const char* t) {
    float w = 0;
    for (; t && *t; ++t) w += advance((unsigned char)*t);
    return w * s * (LH / 16.f);
}
static void text(float x, float y, float s, uint32_t c, const char* t) {
    texts.push_back({t, x, y, tw(s, t), LH * s});
    if (out) fprintf(out, "{\"t\":\"text\",\"x\":%.1f,\"y\":%.1f,\"s\":%.3f,\"c\":%u,\"w\":%.1f,\"h\":%.1f,\"str\":\"%s\"},\n",
                     x, y, s, c, tw(s, t), LH * s, esc(t).c_str());
}
static void rect(float x, float y, float w, float h, uint32_t c) {
    if (out) fprintf(out, "{\"t\":\"rect\",\"x\":%.1f,\"y\":%.1f,\"w\":%.1f,\"h\":%.1f,\"c\":%u},\n", x, y, w, h, c);
}
static void clip(float x, float y, float w, float h) {
    if (out) fprintf(out, "{\"t\":\"clip\",\"x\":%.1f,\"y\":%.1f,\"w\":%.1f,\"h\":%.1f},\n", x, y, w, h);
}
static const char* names[] = {"Infernus", "Banshee", "Cheetah", "Sabre Turbo", "Police Cruiser With A Very Long Name",
                              "Faggio", "Pony", "Taxi"};

static void frameHud() {
    texts.clear();
    // what the game itself draws before the plugins (game.cpp, F3): the clock and the help line
    text(W - 20 - tw(1.5f, "12:00"), 16, 1.5f, 0xFFFFFFFFu, "12:00");
    text(16, H - 30, 1.f, 0xFFFFFFB0u, "WASD: move, Shift: sprint, Ctrl: walk, F / Enter: get in, F5: spawn car, F3: debug off");
    for (auto& h : huds) h.first(h.second);
    for (auto& t : ticks) t.first(t.second);
    lastTexts = texts;
    ++frame;
    clicked = 0;
    wheel = 0;
}
static void settle() { for (int i = 0; i < 30; ++i) frameHud(); }
static void key(int k) { for (auto& h : keys) if (h.first(k, h.second)) return; }
static bool click(const std::string& label) {
    for (auto it = lastTexts.rbegin(); it != lastTexts.rend(); ++it) {
        if (it->text != label) continue;
        mx = it->x + it->w * 0.5f;
        my = it->y + it->h * 0.5f;
        clicked = down = 1;
        frameHud();
        down = 0;
        settle();
        return true;
    }
    fprintf(stderr, "nothing drawn reads \"%s\"\n", label.c_str());
    return false;
}

static void* openLibrary(const char* path) {
#ifdef _WIN32
    return (void*)LoadLibraryA(path);
#else
    return dlopen(path, RTLD_NOW);
#endif
}
static void* symbol(void* lib, const char* name) {
#ifdef _WIN32
    return (void*)GetProcAddress((HMODULE)lib, name);
#else
    return dlsym(lib, name);
#endif
}

int main(int argc, char** argv) {
    if (argc < 7) return 1;
    modsDir = argv[2]; W = atoi(argv[3]); H = atoi(argv[4]); LH = (float)atof(argv[5]);
    CtwHostApi host{};
    host.version = CTW_PLUGIN_API_VERSION;
    host.size = sizeof host;
    host.mods_dir = [] { return modsDir.c_str(); };
    host.log = [](const char*) {};
    host.frame_count = [] { return frame; };
    host.get_time_of_day = [] { return 12.f; };
    host.set_time_of_day = [](float) {};
    host.get_clock_running = [] { return 1; };
    host.set_clock_running = [](int) {};
    host.get_weather = [] { return 0; };
    host.set_weather = [](int) {};
    host.get_player_position = [](float o[3]) { o[0] = o[1] = o[2] = 0; };
    host.set_player_position = [](const float*) {};
    host.get_player_heading = [] { return 0.f; };
    host.get_camera_height = [] { return 10.f; };
    host.set_camera_height = [](float) {};
    host.draw_text = text;
    host.draw_rect = rect;
    host.text_width = tw;
    host.line_height = [](float s) { return LH * s; };
    host.screen_width = [] { return W; };
    host.screen_height = [] { return H; };
    host.override_texture_png = [](int, const char*) { return 1; };
    host.restore_texture = [](int) {};
    host.key_down = [](int) { return 0; };
    host.key_pressed = [](int) { return 0; };
    host.on_tick = [](CtwCallback f, void* u) { ticks.push_back({f, u}); };
    host.on_draw_hud = [](CtwCallback f, void* u) { huds.push_back({f, u}); };
    host.on_key = [](CtwKeyCallback f, void* u) { keys.push_back({f, u}); };
    host.set_game_input = [](int) {};
    host.vehicle_count = [] { return 8; };
    host.vehicle_name = [](int id) { return id >= 0 && id < 8 ? names[id] : (const char*)nullptr; };
    host.vehicle_spawnable = [](int) { return 1; };
    host.spawn_vehicle = [](int) { return 1; };
    host.set_speed_scale = [](float) {};
    host.get_speed_scale = [] { return 1.f; };
    host.set_game_speed = [](float) {};
    host.get_game_speed = [] { return 1.f; };
    host.set_free_camera = [](int, const float*, float, float) {};
    host.patch_texture_png = [](int, float, float, float, float, const char*) { return 1; };
    host.get_mouse = [](float* x, float* y) { *x = mx; *y = my; };
    host.mouse_down = [](int) { return down; };
    host.mouse_clicked = [](int) { return clicked; };
    host.mouse_wheel = [] { return wheel; };
    host.set_mouse_captured = [](int) {};
    host.mouse_delta = [](float* x, float* y) { *x = *y = 0; };
    host.text_input = [] { return ""; };
    host.draw_vehicle = [](int, float x, float y, float w, float h, float) { rect(x, y, w, h, 0x3060A0FFu); };
    host.set_clip = clip;
    host.get_camera = [](float e[3], float* y, float* p) { e[0] = e[1] = e[2] = 0; *y = *p = 0; };
    host.time_seconds = [] { return frame / 60.0; };
    host.set_render_distance = [](float) {};
    host.get_render_distance = [] { return 120.f; };
    host.get_capabilities = [] { return 0u; };
    host.get_player_state = [](CtwPlayerState* s) { return s && s->size >= sizeof *s ? 1 : 0; };
    host.get_ground = [](const float*, CtwGround*) { return 0; };
    host.consume_mouse_wheel = [] {};
    host.set_control_yaw = [](int, float) {};
    host.set_camera_fov = [](float) {};
    host.world_line = [](const float*, const float*, float*) { return 0; };

    void* lib = openLibrary(argv[1]);
    if (!lib) { fprintf(stderr, "cannot load %s\n", argv[1]); return 1; }
    auto init = (CtwPluginInitFn)symbol(lib, "ctw_plugin_init");
    if (!init || init(&host)) return 2;
    mx = W / 2.f; my = H / 2.f;
    settle();
    for (int i = 7; i < argc; ++i) {
        const std::string action = argv[i];
        if (action.rfind("key:", 0) == 0) { key(atoi(action.c_str() + 4)); settle(); }
        else if (action.rfind("click:", 0) == 0 && !click(action.substr(6))) return 3;
    }
    mx = W - 100.f; my = H - 100.f;   // nothing hovered in the recorded frame
    settle();
    out = fopen(argv[6], "w");
    fprintf(out, "{\"W\":%d,\"H\":%d,\"LH\":%.1f,\"ops\":[\n", W, H, LH);
    frameHud();
    fprintf(out, "{\"t\":\"end\"}]}\n");
    fclose(out);
    return 0;
}
