#include "plugins.h"
#include "ctw_plugin.h"
#include "game.h"
#include "hud.h"
#include "gfx/assets.h"
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
int h_key_down(int sc) { return sc >= 0 && sc < (int)g_curKeys.size() && g_curKeys[sc]; }
int h_key_pressed(int sc) {
    return sc >= 0 && sc < (int)g_curKeys.size() && g_curKeys[sc] && !(sc < (int)g_prevKeys.size() && g_prevKeys[sc]);
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
    for (auto& k : g_hud) k.fn(k.user);
}

bool Plugins_Key(int sc) {
    for (auto& k : g_keys)
        if (k.fn(sc, k.user)) return true;
    return false;
}

bool Plugins_GameInput() { return g_gameInput; }
