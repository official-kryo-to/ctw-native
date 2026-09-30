// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// Showcase city viewer: a free-fly camera around the engine's WorldRenderer.
#include "worldview.h"
#include "world/worldrenderer.h"
#include "os/os.h"
#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {
WorldRenderer g_world;
bool g_ok = false;
float g_pos[3] = {120.f, -60.f, 70.f};
float g_yaw = 0.f, g_pitch = -55.f, g_speed = 40.f;
int g_lastX = 0, g_lastY = 0;
bool g_dragging = false;
int g_weather = 0;
bool g_timeRuns = false;          // the viewer holds the clock unless asked to let it run
double g_frameAcc = 0;            // game logic runs at 30 fps

void updateTitle() {
    WorldRenderer::Stats s = g_world.stats();
    uint32_t tm = g_world.timeCycle().time();
    char t[320];
    snprintf(t, sizeof t, "CTW world  %02u:%02u weather %d%s  pos %.0f %.0f %.0f  %zu blocks, %d objects, %zu draws  (Tab: textures, WASD/QE: fly, Shift: fast, drag: look, wheel: speed, H: +1 hour, N: weather, P: run clock, T: texture, Ctrl+W: wire, +/-: radius)",
             tm >> 12, (tm & 0xFFF) * 60 >> 12, g_weather, g_timeRuns ? " (clock running)" : "", g_pos[0], g_pos[1], g_pos[2],
             s.blocks, s.objects, s.draws);
    Host_SetTitle(t);
}
}  // namespace

bool WorldView_Init(const std::string& dataDir) { return g_ok = g_world.init(dataDir); }

void WorldView_SetTime(float hours) {
    g_world.timeCycle().setTime((uint32_t)(hours * 4096.f));
    g_world.timeCycle().evaluate();
}

void WorldView_Enter() { if (g_ok) updateTitle(); }

void WorldView_SetCamera(float x, float y, float z, float yaw, float pitch) {
    g_pos[0] = x; g_pos[1] = y; g_pos[2] = z; g_yaw = yaw; g_pitch = pitch;
}

void WorldView_LoadAllNow() { g_world.loadAllNow(g_pos[0], g_pos[1]); }

void WorldView_Key(int k) {
    TimeCycle& tc = g_world.timeCycle();
    if (k == SDL_SCANCODE_T) g_world.textured = !g_world.textured;
    else if (k == SDL_SCANCODE_W && (SDL_GetModState() & KMOD_CTRL)) g_world.wireframe = !g_world.wireframe;
    else if (k == SDL_SCANCODE_EQUALS || k == SDL_SCANCODE_KP_PLUS) g_world.setRadius(std::min(g_world.radius() + 1, 6));
    else if (k == SDL_SCANCODE_MINUS || k == SDL_SCANCODE_KP_MINUS) g_world.setRadius(std::max(g_world.radius() - 1, 0));
    else if (k == SDL_SCANCODE_H) { tc.setTime(tc.time() + (1u << 12)); tc.evaluate(); }   // +1 hour
    else if (k == SDL_SCANCODE_N) { g_weather = (g_weather + 1) & 7; tc.setWeather(g_weather); tc.evaluate(); }
    else if (k == SDL_SCANCODE_P) g_timeRuns = !g_timeRuns;
}

void WorldView_Tick() {   // one 30 fps game frame: water UVs, model animations, the clock
    g_world.tick();
    if (g_timeRuns) { g_world.timeCycle().advanceFrames(1); g_world.timeCycle().evaluate(); }
}

void WorldView_Update(float dt) {
    if (!g_ok) return;
    int mx, my;
    Host_GetMouse(&mx, &my);
    bool down = Host_MouseDown(0);
    if (down && g_dragging) {
        g_yaw -= (mx - g_lastX) * 0.25f;
        g_pitch = std::max(-89.f, std::min(89.f, g_pitch - (my - g_lastY) * 0.25f));
    }
    g_dragging = down;
    g_lastX = mx; g_lastY = my;
    while (int w = Host_PopWheel()) g_speed = std::max(5.f, std::min(1000.f, g_speed * (w > 0 ? 1.25f : 0.8f)));

    const Uint8* ks = SDL_GetKeyboardState(nullptr);
    WorldCamera cam;
    cam.setYawPitch(g_yaw, g_pitch);
    float sp = g_speed * dt * ((ks[SDL_SCANCODE_LSHIFT] || ks[SDL_SCANCODE_RSHIFT]) ? 4.f : 1.f);
    for (int i = 0; i < 3; ++i) {
        if (ks[SDL_SCANCODE_W] && !(SDL_GetModState() & KMOD_CTRL)) g_pos[i] += cam.fwd[i] * sp;
        if (ks[SDL_SCANCODE_S]) g_pos[i] -= cam.fwd[i] * sp;
        if (ks[SDL_SCANCODE_D]) g_pos[i] += cam.right[i] * sp;
        if (ks[SDL_SCANCODE_A]) g_pos[i] -= cam.right[i] * sp;
    }
    if (ks[SDL_SCANCODE_E]) g_pos[2] += sp;
    if (ks[SDL_SCANCODE_Q]) g_pos[2] -= sp;
    g_frameAcc += dt;
    for (int n = 0; g_frameAcc >= 1.0 / 30.0 && n < 4; ++n) { g_frameAcc -= 1.0 / 30.0; WorldView_Tick(); }
    if (g_frameAcc > 1.0 / 30.0) g_frameAcc = 0;
    g_world.stream(g_pos[0], g_pos[1], 2);
    updateTitle();
}

void WorldView_Render() {
    WorldCamera cam;
    cam.eye[0] = g_pos[0]; cam.eye[1] = g_pos[1]; cam.eye[2] = g_pos[2];
    cam.setYawPitch(g_yaw, g_pitch);
    g_world.render(cam, (int)OS_ScreenGetWidth(), (int)OS_ScreenGetHeight());
}
