// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "plugins.h"
#include "ctw_plugin.h"
#include "game.h"
#include "hud.h"
#include "gfx/assets.h"
#include "os/os.h"
#include <glad/gl.h>
#include <map>
#include <memory>
#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

namespace {
struct Plugin { std::string name; void* lib = nullptr; CtwPluginShutdownFn shutdown = nullptr; };
std::vector<Plugin> g_plugins;
std::string g_modsDir;
FILE* g_log = nullptr;
struct Hook { CtwCallback fn; void* user; };
struct KeyHook { CtwKeyCallback fn; void* user; };
std::vector<Hook> g_tick, g_hud;
std::vector<KeyHook> g_keys;
std::vector<uint8_t> g_prevKeys, g_curKeys;
bool g_gameInput = true;
int g_w = 1280, g_h = 720;
// input collected once per rendered frame (Plugins_DrawHud)
int g_clicks = 0;
float g_wheel = 0, g_dx = 0, g_dy = 0;
std::string g_text;
bool g_captured = false;
float g_clip[4] = {0, 0, 0, 0};   // the mods' clip rectangle (w <= 0: none)

const char* h_mods_dir() { return g_modsDir.c_str(); }
void h_log(const char* t) { Plugins_Log(t ? t : ""); }
uint32_t h_frame() { return TheGame().frame; }
float h_get_time() { return TheGame().world.timeCycle().time() / 4096.f; }
void h_set_time(float h) {
    h = std::fmod(std::max(0.f, h), 24.f);
    TheGame().world.timeCycle().setTime((uint32_t)(h * 4096.f));
    TheGame().world.timeCycle().evaluate();
}
int h_get_clock() { return TheGame().clockRunning ? 1 : 0; }
void h_set_clock(int r) { TheGame().clockRunning = r != 0; }
int h_get_weather() { return TheGame().weather; }
void h_set_weather(int w) {
    TheGame().weather = w & 7;
    TheGame().world.timeCycle().setWeather(w & 7);
    TheGame().world.timeCycle().evaluate();
}
void h_get_pos(float o[3]) { TheGame().player.posf(o); }
void h_set_pos(const float p[3]) {
    Player& pl = TheGame().player;
    for (int i = 0; i < 3; ++i) pl.pos[i] = (int32_t)(p[i] * 4096.f);
    pl.vel[0] = pl.vel[1] = pl.vel[2] = 0;
    TheGame().camera.reset(pl.pos, pl.heading());
}
float h_heading() { return -TheGame().player.heading() * 360.f / 65536.f; }
float h_get_cam() { return TheGame().camera.height / 4096.f; }
void h_set_cam(float u) { TheGame().camera.height = (int32_t)(std::max(2.f, std::min(u, 140.f)) * 4096.f); }
void h_text(float x, float y, float s, uint32_t c, const char* t) { if (t) Hud_Text(x, y, s, c, t); }
void h_rect(float x, float y, float w, float h, uint32_t c) { Hud_Rect(x, y, w, h, c); }
float h_text_w(float s, const char* t) { return t ? Hud_TextWidth(t, s) : 0.f; }
float h_line_h(float s) { return Hud_LineHeight(s); }
int h_sw() { return g_w; }
int h_sh() { return g_h; }
int h_override(int id, const char* png) { return png && Assets_OverrideTexturePNG(id, png) ? 1 : 0; }
void h_restore(int id) { Assets_RestoreTexture(id); }
int h_key_down(int sc) {   // live, so it also works while the game is paused
    int n = 0;
    const Uint8* ks = SDL_GetKeyboardState(&n);
    return sc >= 0 && sc < n && ks[sc];
}
int h_key_pressed(int sc) {
    return sc >= 0 && sc < (int)g_curKeys.size() && g_curKeys[sc] && !(sc < (int)g_prevKeys.size() && g_prevKeys[sc]);
}
int h_vehicle_count() { return (int)TheGame().vehicleInfos.size(); }
std::string g_vehName;
const char* h_vehicle_name(int id) {
    if (id < 0 || id >= h_vehicle_count()) return nullptr;
    g_vehName = TheGame().vehicleInfos[id].name();
    const size_t dot = g_vehName.rfind(".vehicle");
    if (dot != std::string::npos) g_vehName.erase(dot);   // "Admiral.vehicle" -> "Admiral"
    if (!g_vehName.empty()) g_vehName[0] = (char)toupper((unsigned char)g_vehName[0]);
    return g_vehName.c_str();
}
int h_vehicle_spawnable(int id) { return id >= 0 && id < h_vehicle_count() && TheGame().vehicleInfos[id].type() <= 1; }
int h_spawn_vehicle(int id) {
    Game& g = TheGame();
    if (!h_vehicle_spawnable(id)) return 0;
    int32_t at[3] = {g.player.pos[0] + g.player.fwd[0] * 6, g.player.pos[1] + g.player.fwd[1] * 6, 0};
    if (g.playerCar >= 0) {
        const Vehicle& c = g.cars[g.playerCar];
        at[0] = c.pos[0] + c.fwd[0] * 10; at[1] = c.pos[1] + c.fwd[1] * 10; at[2] = 0;
    }
    return g.spawnCar(id, at, g.playerCar >= 0 ? g.cars[g.playerCar].heading() : g.player.heading()) >= 0 ? 1 : 0;
}
void h_set_speed(float s) { TheGame().speedScale = std::max(0.1f, std::min(s, 10.f)); }
float h_get_speed() { return TheGame().speedScale; }
void h_set_game_speed(float s) { TheGame().gameSpeed = std::max(0.f, std::min(s, 8.f)); }
float h_get_game_speed() { return TheGame().gameSpeed; }
void h_free_cam(int on, const float eye[3], float yaw, float pitch) {
    Game& g = TheGame();
    if (on && !g.freeCam.on) {   // keep the game camera's lens
        WorldCamera current;
        g.viewCamera(current);
        g.freeCam.cam = current;
    }
    g.freeCam.on = on != 0;
    if (!on || !eye) return;
    for (int i = 0; i < 3; ++i) g.freeCam.cam.eye[i] = eye[i];
    g.freeCam.cam.setYawPitch(yaw, pitch);
}
int h_patch(int id, float x, float y, float w, float h, const char* png) {
    return png && Assets_PatchTexturePNG(id, x, y, w, h, png) ? 1 : 0;
}
void h_get_mouse(float* x, float* y) {
    int mx = 0, my = 0;
    Host_GetMouse(&mx, &my);
    if (x) *x = (float)mx;
    if (y) *y = (float)my;
}
int h_mouse_down(int b) { return b >= 0 && b < 3 && Host_MouseDown(b) ? 1 : 0; }
int h_mouse_clicked(int b) { return b >= 0 && b < 3 && (g_clicks >> b & 1) ? 1 : 0; }
float h_mouse_wheel() { return g_wheel; }
void h_set_captured(int on) { g_captured = on != 0; Host_SetRelativeMouse(g_captured); }
void h_mouse_delta(float* dx, float* dy) {
    if (dx) *dx = g_dx;
    if (dy) *dy = g_dy;
}
const char* h_text_input() { return g_text.c_str(); }

// Vehicle pictures: the model of a parked car of that type, drawn with its own light into a HUD rectangle.
std::map<int, std::unique_ptr<Vehicle>> g_previews;
void h_draw_vehicle(int id, float x, float y, float w, float h, float yaw) {
    Game& g = TheGame();
    if (id < 0 || id >= (int)g.vehicleInfos.size() || w < 2 || h < 2) return;
    const Model* m = g.carModel(id);
    if (!m) return;
    std::unique_ptr<Vehicle>& v = g_previews[id];
    if (!v) {
        const VehicleInfo& info = g.vehicleInfos[id];
        std::vector<int> palettes;   // one of its paint jobs (cVehicleInfo::RandomPalette: bits 0..24, else 26)
        for (int bit = 0; bit < 25; ++bit)
            if (info.paletteMask() >> bit & 1) palettes.push_back(bit);
        const int palette = palettes.empty() ? 26 : palettes[(size_t)id * 7 % palettes.size()];
        const int32_t at[3] = {0, 0, 0};
        v = std::make_unique<Vehicle>();
        v->init(info, id, at, 0, palette);
    }
    // a camera orbiting the model's bounding box, looking slightly down
    float c[3], r = 0;
    for (int i = 0; i < 3; ++i) {
        c[i] = (m->bboxMin[i] + m->bboxMax[i]) * 0.5f;
        r = std::max(r, (m->bboxMax[i] - m->bboxMin[i]) * 0.5f);
    }
    if (r <= 0) r = 3;
    const float fov = 30.f, dist = r / std::sin(fov * 0.5f * 3.14159265f / 180.f) * 0.95f;
    const float yr = yaw * 3.14159265f / 180.f, up = 28.f * 3.14159265f / 180.f;
    WorldCamera cam;   // yaw 0 looks at the front of the car (models face +y)
    cam.eye[0] = c[0] + std::sin(yr) * std::cos(up) * dist;
    cam.eye[1] = c[1] + std::cos(yr) * std::cos(up) * dist;
    cam.eye[2] = c[2] + std::sin(up) * dist;
    cam.lookAt(c);
    const int vx = (int)x, vy = g_h - (int)(y + h), vw = (int)w, vh = (int)h;
    glViewport(vx, vy, vw, vh);
    float cx0 = x, cy0 = y, cx1 = x + w, cy1 = y + h;   // inside the mods' clip rectangle too
    if (g_clip[2] > 0 && g_clip[3] > 0) {
        cx0 = std::max(cx0, g_clip[0]); cy0 = std::max(cy0, g_clip[1]);
        cx1 = std::min(cx1, g_clip[0] + g_clip[2]); cy1 = std::min(cy1, g_clip[1] + g_clip[3]);
    }
    if (cx1 <= cx0 || cy1 <= cy0) { Hud_Begin(g_w, g_h); return; }
    glEnable(GL_SCISSOR_TEST);
    glScissor((int)cx0, g_h - (int)cy1, (int)(cx1 - cx0), (int)(cy1 - cy0));
    glClear(GL_DEPTH_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    const float n = 0.1f, t = n * std::tan(fov * 3.14159265f / 360.f), a = w / h;
    glFrustum(-t * a, t * a, -t, t, n, dist + r * 4);
    glMatrixMode(GL_MODELVIEW);
    const float* R = cam.right; const float* U = cam.up; const float* F = cam.fwd; const float* E = cam.eye;
    const float view[16] = {R[0], U[0], -F[0], 0, R[1], U[1], -F[1], 0, R[2], U[2], -F[2], 0,
                            -(R[0] * E[0] + R[1] * E[1] + R[2] * E[2]), -(U[0] * E[0] + U[1] * E[1] + U[2] * E[2]),
                            F[0] * E[0] + F[1] * E[1] + F[2] * E[2], 1};
    glLoadMatrixf(view);
    const float sun[4] = {0.4f, -0.5f, 0.8f, 0.f}, dif[4] = {0.75f, 0.75f, 0.72f, 1.f}, amb[4] = {0.55f, 0.55f, 0.58f, 1.f},
                zero[4] = {0, 0, 0, 1};
    glLightfv(GL_LIGHT0, GL_POSITION, sun);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, dif);
    glLightfv(GL_LIGHT0, GL_AMBIENT, zero);
    glLightfv(GL_LIGHT0, GL_SPECULAR, zero);
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, amb);
    glEnable(GL_LIGHT0);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
    glEnable(GL_COLOR_MATERIAL);
    glEnable(GL_DEPTH_TEST);
    v->render(m);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    glDisable(GL_CULL_FACE);
    glDisable(GL_ALPHA_TEST);
    glDepthMask(GL_TRUE);
    Hud_Begin(g_w, g_h);   // back to HUD drawing, with the mods' clip rectangle
    if (g_clip[2] > 0 && g_clip[3] > 0) glScissor((int)g_clip[0], g_h - (int)(g_clip[1] + g_clip[3]), (int)g_clip[2], (int)g_clip[3]);
    else glDisable(GL_SCISSOR_TEST);
}

void h_set_clip(float x, float y, float w, float h) {
    g_clip[0] = x; g_clip[1] = y; g_clip[2] = w; g_clip[3] = h;
    if (w <= 0 || h <= 0) { glDisable(GL_SCISSOR_TEST); return; }
    glEnable(GL_SCISSOR_TEST);
    glScissor((int)x, g_h - (int)(y + h), (int)w, (int)h);
}
void h_get_camera(float eye[3], float* yaw, float* pitch) {
    WorldCamera cam;
    TheGame().viewCamera(cam);
    if (eye) for (int i = 0; i < 3; ++i) eye[i] = cam.eye[i];
    if (yaw) *yaw = std::atan2(-cam.fwd[0], cam.fwd[1]) * 180.f / 3.14159265f;
    if (pitch) *pitch = std::asin(std::max(-1.f, std::min(1.f, cam.fwd[2]))) * 180.f / 3.14159265f;
}

void h_on_tick(CtwCallback fn, void* u) { if (fn) g_tick.push_back({fn, u}); }
void h_on_hud(CtwCallback fn, void* u) { if (fn) g_hud.push_back({fn, u}); }
void h_on_key(CtwKeyCallback fn, void* u) { if (fn) g_keys.push_back({fn, u}); }
void h_game_input(int on) { g_gameInput = on != 0; }

const CtwHostApi g_host = {
    CTW_PLUGIN_API_VERSION, sizeof(CtwHostApi),
    h_mods_dir, h_log, h_frame,
    h_get_time, h_set_time, h_get_clock, h_set_clock, h_get_weather, h_set_weather,
    h_get_pos, h_set_pos, h_heading,
    h_get_cam, h_set_cam,
    h_text, h_rect, h_text_w, h_line_h, h_sw, h_sh,
    h_override, h_restore,
    h_key_down, h_key_pressed,
    h_on_tick, h_on_hud, h_on_key, h_game_input,
    h_vehicle_count, h_vehicle_name, h_vehicle_spawnable, h_spawn_vehicle,
    h_set_speed, h_get_speed, h_set_game_speed, h_get_game_speed, h_free_cam, h_patch,
    h_get_mouse, h_mouse_down, h_mouse_clicked, h_mouse_wheel, h_set_captured, h_mouse_delta, h_text_input, h_draw_vehicle,
    h_set_clip, h_get_camera, [] { return OS_TimeAccurate(); },
    [](float u) { TheGame().setRenderDistance(u); }, [] { return TheGame().renderDistance; },
};
}  // namespace


void Plugins_Log(const std::string& line) {
    if (!g_log) g_log = fopen((fs::path(g_modsDir) / "log.txt").string().c_str(), "w");   // only once there is something to say
    if (g_log) { fprintf(g_log, "%s\n", line.c_str()); fflush(g_log); }
    printf("plugins: %s\n", line.c_str());
}

void Plugins_Init(const std::string& modsDir) {
    g_modsDir = modsDir;
    if (!g_modsDir.empty() && g_modsDir.back() != '/' && g_modsDir.back() != '\\') g_modsDir += '/';
    std::error_code ec;
    std::vector<fs::path> dlls;
    for (auto& e : fs::directory_iterator(modsDir, ec))
        if (e.is_regular_file() && e.path().extension() == ".dll") dlls.push_back(e.path());
    std::sort(dlls.begin(), dlls.end());
    for (auto& p : dlls) {
        Plugin pl;
        pl.name = p.filename().string();
        pl.lib = SDL_LoadObject(p.string().c_str());
        if (!pl.lib) { Plugins_Log("could not load " + pl.name + ": " + SDL_GetError()); continue; }
        auto init = (CtwPluginInitFn)SDL_LoadFunction(pl.lib, "ctw_plugin_init");
        pl.shutdown = (CtwPluginShutdownFn)SDL_LoadFunction(pl.lib, "ctw_plugin_shutdown");
        if (!init) { Plugins_Log(pl.name + " is not a plugin (no ctw_plugin_init)"); SDL_UnloadObject(pl.lib); continue; }
        int r = init(&g_host);
        if (r != 0) { Plugins_Log(pl.name + " refused to load (" + std::to_string(r) + ")"); SDL_UnloadObject(pl.lib); continue; }
        Plugins_Log("loaded plugin " + pl.name);
        g_plugins.push_back(pl);
    }
}

void Plugins_Shutdown() {
    for (auto it = g_plugins.rbegin(); it != g_plugins.rend(); ++it) {
        if (it->shutdown) it->shutdown();
        SDL_UnloadObject(it->lib);
    }
    g_plugins.clear();
    g_previews.clear();
    g_tick.clear(); g_hud.clear(); g_keys.clear();
    if (g_log) fclose(g_log);
    g_log = nullptr;
}

void Plugins_BeginFrameInput() {
    int n = 0;
    const Uint8* ks = SDL_GetKeyboardState(&n);
    g_prevKeys = g_curKeys;
    g_curKeys.assign(ks, ks + n);
}

void Plugins_Tick() { for (auto& h : g_tick) h.fn(h.user); }

void Plugins_DrawHud(int w, int h) {
    g_w = w; g_h = h;
    g_clicks = Host_PopClicks();
    g_wheel = 0;
    while (int notch = Host_PopWheel()) g_wheel += (float)notch;
    g_text = Host_PopText();
    Host_PopMouseDelta(&g_dx, &g_dy);
    for (auto& k : g_hud) k.fn(k.user);
    g_clip[2] = 0;
    glDisable(GL_SCISSOR_TEST);
}

bool Plugins_Key(int sc) {
    for (auto& k : g_keys)
        if (k.fn(sc, k.user)) return true;
    return false;
}

bool Plugins_GameInput() { return g_gameInput; }
