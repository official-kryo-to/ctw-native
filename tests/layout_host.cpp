// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// A stand-in for the game for test_modmenu_layout.py: loads a Linux build of ModMenu, feeds it keys and clicks,
// and writes everything it draws (and the game's own clock and help line) to a JSON file.
#include "ctw_plugin.h"
#include <dlfcn.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
static int W = 1280, H = 720; static float LH = 16.f;
static std::string modsDir; static FILE* out; static uint32_t frame;
static float mx, my, wheel; static int clicked, down;
static std::vector<std::pair<CtwCallback, void*>> huds, ticks; static std::vector<std::pair<CtwKeyCallback, void*>> keys;
static std::string esc(const char* s) { std::string r; for (; *s; ++s) { if (*s == '"' || *s == '\\') r += '\\'; r += *s; } return r; }
// Proportional widths like the game's Helvetica-style font: narrow i/l/punctuation, wide M/W, capitals wider.
static float advance(unsigned char c) {
    if (strchr("il.,:;'|!", c)) return 3.5f;
    if (strchr("fjrt ()[]/-", c)) return 4.5f;
    if (strchr("mwMW@", c)) return 11.5f;
    if (c >= 'A' && c <= 'Z') return 9.f;
    return 7.5f;
}
static float tw(float s, const char* t) { float w = 0; for (; t && *t; ++t) w += advance((unsigned char)*t); return w * s * (LH / 16.f); }
static void text(float x, float y, float s, uint32_t c, const char* t) { fprintf(out, "{\"t\":\"text\",\"x\":%.1f,\"y\":%.1f,\"s\":%.3f,\"c\":%u,\"w\":%.1f,\"h\":%.1f,\"str\":\"%s\"},\n", x, y, s, c, tw(s, t), LH * s, esc(t).c_str()); }
static void rect(float x, float y, float w, float h, uint32_t c) { fprintf(out, "{\"t\":\"rect\",\"x\":%.1f,\"y\":%.1f,\"w\":%.1f,\"h\":%.1f,\"c\":%u},\n", x, y, w, h, c); }
static void clip(float x, float y, float w, float h) { fprintf(out, "{\"t\":\"clip\",\"x\":%.1f,\"y\":%.1f,\"w\":%.1f,\"h\":%.1f},\n", x, y, w, h); }
static const char* names[] = {"Infernus", "Banshee", "Cheetah", "Sabre Turbo", "Police Cruiser With A Very Long Name", "Faggio", "Pony", "Taxi"};
static float fzero() { return 0.f; } static void fset(float) {} static int izero() { return 0; } static void iset(int) {}
static void vget(float o[3]) { o[0] = o[1] = o[2] = 0; } static void vset(const float*) {}
static const CtwHostApi host = {
    CTW_PLUGIN_API_VERSION, sizeof(CtwHostApi),
    [] { return modsDir.c_str(); }, [](const char*) {}, [] { return frame; },
    [] { return 12.f; }, fset, [] { return 1; }, iset, izero, iset,
    vget, vset, fzero, [] { return 10.f; }, fset,
    text, rect, tw, [](float s) { return LH * s; }, [] { return W; }, [] { return H; },
    [](int, const char*) { return 1; }, [](int) {},
    [](int) { return 0; }, [](int) { return 0; },
    [](CtwCallback f, void* u) { ticks.push_back({f, u}); }, [](CtwCallback f, void* u) { huds.push_back({f, u}); },
    [](CtwKeyCallback f, void* u) { keys.push_back({f, u}); }, iset,
    [] { return 8; }, [](int id) { return id >= 0 && id < 8 ? names[id] : (const char*)nullptr; }, [](int) { return 1; }, [](int) { return 1; },
    fset, [] { return 1.f; }, fset, [] { return 1.f; }, [](int, const float*, float, float) {},
    [](int, float, float, float, float, const char*) { return 1; },
    [](float* x, float* y) { *x = mx; *y = my; }, [](int) { return down; }, [](int) { return clicked; }, [] { return wheel; },
    iset, [](float* x, float* y) { *x = *y = 0; }, [] { return ""; },
    [](int, float x, float y, float w, float h, float) { rect(x, y, w, h, 0x3060A0FFu); },
    clip, [](float e[3], float* y, float* p) { e[0] = e[1] = e[2] = 0; *y = *p = 0; }, [] { return frame / 60.0; },
    fset, [] { return 120.f; },
};
static void key(int k) { for (auto& h : keys) if (h.first(k, h.second)) return; }
static void draw(const char* file) {
    out = fopen(file, "w"); fprintf(out, "{\"W\":%d,\"H\":%d,\"LH\":%.1f,\"ops\":[\n", W, H, LH);
    // what the game itself draws before the plugins (game.cpp): the clock and the help line
    text(W - 20 - tw(1.5f, "12:00"), 16, 1.5f, 0xFFFFFFFFu, "12:00");
    text(16, H - 30, 1.f, 0xFFFFFFB0u, "Gameplay prototype.  WASD: move, Shift: sprint, Ctrl: walk, F / Enter: get in a car, F5: spawn car, F3: collision, Esc: quit");
    for (auto& h : huds) h.first(h.second);
    fprintf(out, "{\"t\":\"end\"}]}\n"); fclose(out); ++frame; clicked = 0; wheel = 0;
}
static void settle() { for (int i = 0; i < 30; ++i) { for (auto& h : huds) { out = fopen("/dev/null", "w"); h.first(h.second); fclose(out); } ++frame; clicked = 0; } }
static void click(float x, float y) { mx = x; my = y; clicked = 1; down = 1; out = fopen("/dev/null", "w"); for (auto& h : huds) h.first(h.second); fclose(out); ++frame; clicked = 0; down = 0; settle(); }
int main(int argc, char** argv) {
    // host <ModMenu.so> <modsdir> <W> <H> <LH> <scenario> <out.json> [clickx clicky]...
    modsDir = argv[2]; W = atoi(argv[3]); H = atoi(argv[4]); LH = atof(argv[5]); std::string sc = argv[6];
    void* lib = dlopen(argv[1], RTLD_NOW); if (!lib) { fprintf(stderr, "%s\n", dlerror()); return 1; }
    auto init = (int (*)(const CtwHostApi*))dlsym(lib, "ctw_plugin_init"); if (init(&host)) return 2;
    mx = W / 2.f; my = H / 2.f;
    if (sc.find("menu") != std::string::npos) key(61);          // F4
    for (int i = 8; i + 1 < argc; i += 2) click(atof(argv[i]), atof(argv[i + 1]));
    if (sc.find("spawner") != std::string::npos) key(64);       // F7
    if (sc.find("freecam") != std::string::npos) key(65);       // F8
    if (sc.find("hover") != std::string::npos) { mx = W - 100.f; my = 200.f; }
    settle(); draw(argv[7]); return 0;
}
