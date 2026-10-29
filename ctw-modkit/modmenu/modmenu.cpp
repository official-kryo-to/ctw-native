// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// ModMenu.dll - the mod loader and in-game mod menu (F4) for the GTA: Chinatown Wars PC port.
//
// A plugin (ctw_plugin.h): the game loads it from its mods folder. It finds mods in
// mods/<ModName>/ folders (mod.ini), applies their textures/<resource id>.png and loads their optional
// DLLs using ctw_mod.h. Each folder can contain both; the API is implemented on top of the plugin interface.
#include "ctw_mod.h"
#include "ctw_plugin.h"
#include "ctw_ui.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace fs = std::filesystem;

enum { SC_RETURN = 40, SC_ESCAPE = 41, SC_BACKSPACE = 42, SC_SPACE = 44, SC_F1 = 58, SC_F4 = 61, SC_F12 = 69, SC_DOWN = 81, SC_UP = 82 };   // SDL scancodes

struct CtwMod {   // the opaque handle mods receive
    std::string folder, dir, name, author, version, description, dllName;
    bool enabled = true;
    bool codeLoaded = false;       // the DLL is running (it only stops completely after a restart)
    bool expanded = false;         // menu: options shown
    int textureFiles = 0;          // textures/*.png shipped with the mod
    std::vector<int> textures;     // texture overrides and patches currently applied
    void* lib = nullptr;
    CtwModShutdownFn shutdown = nullptr;
    struct Item {
        enum Kind { Toggle, Slider, Button, Choice, Label } kind = Label;
        std::string label;
        int* ival = nullptr;
        float* fval = nullptr;
        float mn = 0, mx = 1, step = 0.1f;
        CtwCallback fn = nullptr;
        void* user = nullptr;
        std::vector<std::string> options;
    };
    std::vector<Item> items;
    std::map<std::string, float> settings;   // settings.ini, read on first use
    bool settingsLoaded = false, settingsDirty = false;
    std::string error;
};

namespace {
const CtwHostApi* H = nullptr;
std::string g_modsDir;
std::vector<std::unique_ptr<CtwMod>> g_mods;
template <class Fn> struct Hook { CtwMod* mod; Fn fn; void* user; };
std::vector<Hook<CtwCallback>> g_tickHooks, g_hudHooks, g_frameHooks;
std::vector<Hook<CtwCharacterCallback>> g_characterHooks;
std::vector<Hook<CtwKeyCallback>> g_keyHooks;
std::vector<Hook<CtwGameEventCallback>> g_eventHooks;
std::vector<Hook<CtwEnabledCallback>> g_enabledHooks;

bool g_menuOpen = false;
bool g_modsWantInput = true;   // the last set_game_input of a mod
bool g_searchFocused = false;
char g_search[48];
CtwUi g_ui;
CtwUiScroll g_listScroll, g_detailScroll;
int g_selected = 0;   // the mod shown on the right
float g_listH = 0.f;  // the list's visible height (keyboard choice scrolls the chosen row into view)
void chooseNext(int step);
CtwMod* selectedMod();

bool running(const CtwMod& m) { return m.enabled && m.codeLoaded; }
void log(const std::string& s) { H->log(s.c_str()); }
void applyGameInput() { H->set_game_input(g_modsWantInput && !g_searchFocused); }

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

std::map<std::string, std::string> readIni(const fs::path& p) {
    std::map<std::string, std::string> kv;
    std::ifstream f(p);
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == ';' || line[0] == '#' || line[0] == '[') continue;
        size_t eq = line.find('=');
        if (eq != std::string::npos) kv[trim(line.substr(0, eq))] = trim(line.substr(eq + 1));
    }
    return kv;
}

void saveEnabled() {
    if (g_mods.empty()) return;   // nothing to remember: keep the mods folder clean
    std::ofstream f(fs::u8path(g_modsDir) / "enabled.ini");
    f << "; which mods are switched on (1) or off (0); edited by the mod menu (F4)\n";
    for (auto& m : g_mods) f << m->folder << "=" << (m->enabled ? 1 : 0) << "\n";
}

// ------------------------------------------------------------------------------------------------ settings
bool validKey(const char* key) {
    if (!key || !*key || strlen(key) > 64) return false;
    for (const char* c = key; *c; ++c)
        if (!isalnum((unsigned char)*c) && *c != '_' && *c != '.') return false;
    return true;
}

void loadSettings(CtwMod& m) {
    if (m.settingsLoaded) return;
    m.settingsLoaded = true;
    for (auto& kv : readIni(fs::u8path(m.dir) / "settings.ini")) {
        char* end = nullptr;
        const float v = strtof(kv.second.c_str(), &end);
        if (validKey(kv.first.c_str()) && end && *end == 0 && std::isfinite(v)) m.settings[kv.first] = v;
    }
}

void flushSettings(CtwMod& m) {
    if (!m.settingsDirty) return;
    m.settingsDirty = false;
    std::ofstream f(fs::u8path(m.dir) / "settings.ini");
    f << "; " << m.name << " settings, written by the mod\n";
    char value[32];
    for (auto& kv : m.settings) {
        snprintf(value, sizeof value, "%.9g", kv.second);
        f << kv.first << "=" << value << "\n";
    }
}

void flushAllSettings() { for (auto& m : g_mods) flushSettings(*m); }

// ------------------------------------------------------------------------------------------------ mod API
void api_log(CtwMod* mod, const char* text) { log(std::string(mod ? "[" + mod->name + "] " : "") + (text ? text : "")); }
const char* api_mod_dir(CtwMod* mod) { return mod ? mod->dir.c_str() : ""; }

CtwMod::Item* addItem(CtwMod* m, CtwMod::Item::Kind kind, const char* label) {
    if (!m) return nullptr;
    CtwMod::Item item;
    item.kind = kind;
    item.label = label ? label : "";
    m->items.push_back(std::move(item));
    return &m->items.back();
}
void api_menu_toggle(CtwMod* m, const char* label, int* v) {
    if (v) addItem(m, CtwMod::Item::Toggle, label)->ival = v;
}
void api_menu_slider(CtwMod* m, const char* label, float* v, float mn, float mx, float step) {
    if (!v) return;
    CtwMod::Item* it = addItem(m, CtwMod::Item::Slider, label);
    if (it) { it->fval = v; it->mn = mn; it->mx = mx; it->step = step; }
}
void api_menu_button(CtwMod* m, const char* label, CtwCallback fn, void* user) {
    if (!fn) return;
    CtwMod::Item* it = addItem(m, CtwMod::Item::Button, label);
    if (it) { it->fn = fn; it->user = user; }
}
void api_menu_choice(CtwMod* m, const char* label, int* v, const char* const* options, int count) {
    if (!v || !options || count <= 0) return;
    CtwMod::Item* it = addItem(m, CtwMod::Item::Choice, label);
    if (!it) return;
    it->ival = v;
    for (int i = 0; i < count; ++i) it->options.push_back(options[i] ? options[i] : "");
}
void api_menu_label(CtwMod* m, const char* text) { addItem(m, CtwMod::Item::Label, text); }

float api_get_setting(CtwMod* m, const char* key, float fallback) {
    if (!m || !validKey(key)) return fallback;
    loadSettings(*m);
    auto it = m->settings.find(key);
    return it == m->settings.end() ? fallback : it->second;
}
void api_set_setting(CtwMod* m, const char* key, float value) {
    if (!m || !validKey(key) || !std::isfinite(value)) return;
    loadSettings(*m);
    auto it = m->settings.find(key);
    if (it != m->settings.end() && it->second == value) return;
    m->settings[key] = value;
    m->settingsDirty = true;   // written within a second, and when the game closes
}

void api_on_tick(CtwMod* m, CtwCallback fn, void* user) { if (fn) g_tickHooks.push_back({m, fn, user}); }
// a mod's pause-screen buttons: the game's, shown while the mod runs
struct PauseButtonHook { CtwMod* mod; CtwCallback fn; void* user; int id; std::string label; };
std::vector<std::unique_ptr<PauseButtonHook>> g_pauseButtons;
void api_add_pause_button(CtwMod* m, const char* label, CtwCallback fn, void* user) {
    if (!m || !fn || !label) return;
    for (auto& h : g_pauseButtons)   // a mod switched off and on again adds its button again: keep the one
        if (h->mod == m && h->label == label) { h->fn = fn; h->user = user; return; }
    auto hook = std::make_unique<PauseButtonHook>(PauseButtonHook{m, fn, user, 0, label});
    hook->id = H->add_pause_button(label, [](void* u) {
        auto* h = static_cast<PauseButtonHook*>(u);
        h->fn(h->user);
    }, hook.get());
    if (hook->id) g_pauseButtons.push_back(std::move(hook));
}
// a mod's radar dots: one host id per (mod, id), taken off while the mod is not running
struct RadarBlip { CtwMod* mod; int id, hostId; };
std::vector<RadarBlip> g_radarBlips;
int g_nextRadarBlip = 1;
void api_set_radar_blip(CtwMod* m, int id, const float* xyz, uint32_t rgba) {
    if (!m || !xyz || !running(*m)) return;
    RadarBlip* b = nullptr;
    for (auto& x : g_radarBlips) if (x.mod == m && x.id == id) b = &x;
    if (!b) {
        if (g_nextRadarBlip > 0xFFFF) return;
        g_radarBlips.push_back({m, id, g_nextRadarBlip++});
        b = &g_radarBlips.back();
    }
    H->set_radar_blip(b->hostId, xyz, rgba);
}
void api_remove_radar_blip(CtwMod* m, int id) {
    for (size_t i = 0; i < g_radarBlips.size(); ++i)
        if (g_radarBlips[i].mod == m && g_radarBlips[i].id == id) {
            H->remove_radar_blip(g_radarBlips[i].hostId);
            g_radarBlips.erase(g_radarBlips.begin() + (long)i);
            return;
        }
}
void api_set_radar_blip_label(CtwMod* m, int id, const char* label) {
    if (!m || !running(*m)) return;
    for (const auto& b : g_radarBlips) if (b.mod == m && b.id == id) {
        H->set_radar_blip_label(b.hostId, label);
        return;
    }
}
void api_on_hud(CtwMod* m, CtwCallback fn, void* user) { if (fn) g_hudHooks.push_back({m, fn, user}); }
void api_on_frame(CtwMod* m, CtwCallback fn, void* user) { if (m && fn) g_frameHooks.push_back({m, fn, user}); }
void api_on_character(CtwMod* m,CtwCharacterCallback fn,void* user) {if(m && fn)g_characterHooks.push_back({m,fn,user});}
int onCharacter(const CtwCharacterPose* pose,void*) {
    const auto hooks=g_characterHooks;
    for(const auto& h:hooks)if(h.mod->enabled && h.mod->codeLoaded && h.fn(pose,h.user))return 1;
    return 0;
}
void onFrame(void*) {
    const auto hooks = g_frameHooks;
    for (const auto& h : hooks) if (h.mod->enabled && h.mod->codeLoaded) h.fn(h.user);
}
void api_on_key(CtwMod* m, CtwKeyCallback fn, void* user) { if (fn) g_keyHooks.push_back({m, fn, user}); }
void api_on_event(CtwMod* m, CtwGameEventCallback fn, void* user) { if (m && fn) g_eventHooks.push_back({m, fn, user}); }
void api_on_enabled(CtwMod* m, CtwEnabledCallback fn, void* user) { if (m && fn) g_enabledHooks.push_back({m, fn, user}); }

std::string modPath(CtwMod* m, const char* file) {
    std::string path = file;
    if (m && fs::u8path(path).is_relative()) path = m->dir + path;
    return path;
}
void remember(CtwMod* m, int id) {
    if (m && std::find(m->textures.begin(), m->textures.end(), id) == m->textures.end()) m->textures.push_back(id);
}
int api_override_texture(CtwMod* m, int id, const char* png) {
    if (!png || !H->override_texture_png(id, modPath(m, png).c_str())) return 0;
    remember(m, id);
    return 1;
}
int api_patch_texture(CtwMod* m, int id, float x, float y, float w, float h, const char* png) {
    if (!png || !H->patch_texture_png(id, x, y, w, h, modPath(m, png).c_str())) return 0;
    remember(m, id);
    return 1;
}
int api_menu_is_open() { return g_menuOpen ? 1 : 0; }
void api_set_captured(int on) { H->set_mouse_captured(on && !g_menuOpen); }   // the open menu keeps the cursor
void api_set_game_input(int on) { g_modsWantInput = on != 0; applyGameInput(); }

CtwApi g_api = {
    CTW_MOD_API_VERSION, sizeof(CtwApi),
    api_log, api_mod_dir, [] { return H->frame_count(); },
    [] { return H->get_time_of_day(); }, [](float h) { H->set_time_of_day(h); },
    [] { return H->get_clock_running(); }, [](int r) { H->set_clock_running(r); },
    [] { return H->get_weather(); }, [](int w) { H->set_weather(w); },
    [](float o[3]) { H->get_player_position(o); }, [](const float p[3]) { H->set_player_position(p); },
    [] { return H->get_player_heading(); },
    [] { return H->get_camera_height(); }, [](float u) { H->set_camera_height(u); },
    api_menu_toggle, api_menu_slider, api_menu_button,
    api_on_tick, api_on_hud,
    [](float x, float y, float s, uint32_t c, const char* t) { H->draw_text(x, y, s, c, t); },
    [](float x, float y, float w, float h, uint32_t c) { H->draw_rect(x, y, w, h, c); },
    [] { return H->screen_width(); }, [] { return H->screen_height(); },
    api_override_texture,
    [](int sc) { return H->key_down(sc); }, [](int sc) { return H->key_pressed(sc); },
    // version 2
    api_on_key, api_set_game_input, [](float s, const char* t) { return H->text_width(s, t); },
    [](float s) { return H->line_height(s); },
    [] { return H->vehicle_count(); }, [](int id) { return H->vehicle_name(id); },
    [](int id) { return H->vehicle_spawnable(id); }, [](int id) { return H->spawn_vehicle(id); },
    [](float s) { H->set_speed_scale(s); }, [] { return H->get_speed_scale(); },
    [](float s) { H->set_game_speed(s); }, [] { return H->get_game_speed(); },
    [](int on, const float eye[3], float yaw, float pitch) { H->set_free_camera(on, eye, yaw, pitch); },
    api_patch_texture,
    // version 3
    [](float* x, float* y) { H->get_mouse(x, y); }, [](int b) { return H->mouse_down(b); },
    [](int b) { return H->mouse_clicked(b); }, [] { return H->mouse_wheel(); },
    api_set_captured, [](float* dx, float* dy) { H->mouse_delta(dx, dy); }, [] { return H->text_input(); },
    [](int id, float x, float y, float w, float h, float yaw) { H->draw_vehicle(id, x, y, w, h, yaw); },
    [](float x, float y, float w, float h) { H->set_clip(x, y, w, h); },
    [](float eye[3], float* yaw, float* pitch) { H->get_camera(eye, yaw, pitch); },
    api_menu_is_open, [] { return H->time_seconds(); },
    [](float u) { H->set_render_distance(u); }, [] { return H->get_render_distance(); },
    // versions 4 and 5: filled in by ctw_plugin_init from what the running game offers
};

// Game functions that the mod table passes on unchanged (version 4, then version 5).
#define CTW_FORWARDED_V4(X) X(get_capabilities) X(live_vehicle_count) X(live_vehicle_at) X(get_vehicle_state) \
    X(spawn_vehicle_at) X(remove_vehicle) X(set_vehicle_transform) X(set_vehicle_velocity) X(set_vehicle_palette) \
    X(set_vehicle_persistent) X(set_vehicle_engine) X(set_vehicle_door) X(damage_vehicle) X(repair_vehicle) \
    X(get_player_state) X(set_player_heading) X(set_player_appearance) X(player_enter_vehicle) \
    X(player_exit_vehicle) X(get_ground) X(line_hits_world_boxes) X(get_traffic_density) X(set_traffic_density) \
    X(radio_station_count) X(radio_station_name) X(radio_station_available) X(get_radio_station) \
    X(set_radio_station) X(get_radio_volume) X(set_radio_volume) X(consume_mouse_wheel)
#define CTW_FORWARDED_V5(X) X(set_control_yaw) X(set_camera_fov) X(world_line) X(ped_count) X(get_ped_state) \
    X(get_ped_density) X(set_ped_density) X(set_render_style)

// ------------------------------------------------------------------------------------------------ loading
// mod.ini names a mod's library as built on Windows ("x.dll"). On Linux and macOS the same mod builds to "x.so" /
// "x.dylib"; that file is used when the named one is not there.
std::string libFile(const std::string& path) {
#ifdef _WIN32
    return path;
#else
    std::error_code ec;
    if (fs::exists(fs::u8path(path), ec)) return path;
    const size_t dot = path.rfind('.');
    if (dot == std::string::npos || path.compare(dot, std::string::npos, ".dll") != 0) return path;
#ifdef __APPLE__
    return path.substr(0, dot) + ".dylib";
#else
    return path.substr(0, dot) + ".so";
#endif
#endif
}
void* loadLib(const std::string& path) {
#ifdef _WIN32
    return (void*)LoadLibraryW(fs::u8path(path).wstring().c_str());
#else
    return dlopen(libFile(path).c_str(), RTLD_NOW);
#endif
}
void* libSym(void* lib, const char* name) {
#ifdef _WIN32
    return (void*)GetProcAddress((HMODULE)lib, name);
#else
    return dlsym(lib, name);
#endif
}
void freeLib(void* lib) {
#ifdef _WIN32
    FreeLibrary((HMODULE)lib);
#else
    dlclose(lib);
#endif
}

// Texture changes from switching a mod on or off are applied a few at a time (applyTextureJobs, each display
// frame), so a mod with dozens of textures does not stall a single frame. At start-up they are all applied at once.
struct TextureJob { CtwMod* mod; int id; std::string path; };   // an empty path restores the game's texture
std::vector<TextureJob> g_textureJobs;

void applyTextureJobs(double budgetMs) {
    const auto start = std::chrono::steady_clock::now();
    size_t done = 0;
    while (done < g_textureJobs.size()) {
        const TextureJob job = g_textureJobs[done++];
        if (job.path.empty()) H->restore_texture(job.id);
        else api_override_texture(job.mod, job.id, job.path.c_str());
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        if (budgetMs >= 0 && ms >= budgetMs) break;
    }
    g_textureJobs.erase(g_textureJobs.begin(), g_textureJobs.begin() + (std::ptrdiff_t)done);
}

// textures/<resource id>.png: an opaque PNG replaces the texture; one with transparency is a layer drawn over it
// (the game does the difference, see override_texture_png). With apply unset this only counts them.
int scanTextures(CtwMod& m, bool apply) {
    int count = 0;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(fs::u8path(m.dir) / "textures", ec)) {
        if (e.path().extension() != ".png") continue;
        char* end = nullptr;
        const std::string stem = e.path().stem().string();
        const long id = strtol(stem.c_str(), &end, 10);
        if (!end || *end || id < 0) continue;
        ++count;
        if (apply) g_textureJobs.push_back({&m, (int)id, e.path().u8string()});
    }
    return count;
}

void removeTextures(CtwMod& m) {   // texture changes are removed when their mod is switched off
    // changes still waiting are dropped; the ones already made are undone in turn
    g_textureJobs.erase(std::remove_if(g_textureJobs.begin(), g_textureJobs.end(),
                                       [&](const TextureJob& j) { return j.mod == &m && !j.path.empty(); }),
                        g_textureJobs.end());
    for (int id : m.textures) g_textureJobs.push_back({&m, id, std::string()});
    m.textures.clear();
}

void fail(CtwMod& m, const std::string& why) {
    m.error = why;
    log("[" + m.name + "] " + why);
}

void loadMod(CtwMod& m) {
    const int textures = scanTextures(m, true);
    if (!m.dllName.empty() && !m.codeLoaded && m.error.empty()) {
        m.lib = loadLib(m.dir + m.dllName);
        if (!m.lib) return fail(m, "could not load " + m.dllName);
        auto init = (CtwModInitFn)libSym(m.lib, "ctw_mod_init");
        m.shutdown = (CtwModShutdownFn)libSym(m.lib, "ctw_mod_shutdown");
        if (!init) return fail(m, "no ctw_mod_init in " + m.dllName);
        const int r = init(&m, &g_api);
        if (r != 0) return fail(m, "ctw_mod_init returned " + std::to_string(r));
        m.codeLoaded = true;
    }
    log("loaded " + m.name + " " + m.version + (textures ? " (" + std::to_string(textures) + " textures)" : ""));
}

void setEnabled(CtwMod& m, bool on) {
    if (m.enabled == on) return;
    m.enabled = on;
    if (on) loadMod(m);        // textures now; its DLL starts now if it never ran
    else removeTextures(m);    // code stops getting callbacks now and is unloaded at the next start
    const auto hooks = g_enabledHooks;
    if (m.codeLoaded) for (const auto& h : hooks) if (h.mod == &m) h.fn(on ? 1 : 0, h.user);
    saveEnabled();
}

// ------------------------------------------------------------------------------------------------ menu
void setSearchFocus(bool focused) {
    if (g_searchFocused == focused) return;
    g_searchFocused = focused;
    applyGameInput();   // typing in the search box must not move the player
}

void setOpen(bool open) {
    g_menuOpen = open;
    if (open) H->set_mouse_captured(0);
    else { setSearchFocus(false); flushAllSettings(); }
}

int onKey(int k, void*) {
    if (k == SC_ESCAPE && g_searchFocused) { setSearchFocus(false); return 1; }
    // Esc (Start on a controller) or Backspace (B) closes the menu instead of pausing the game
    if (k == SC_F4 || (g_menuOpen && (k == SC_ESCAPE || (k == SC_BACKSPACE && !g_searchFocused)))) {
        setOpen(!g_menuOpen);
        return 1;
    }
    if (g_searchFocused && (k < SC_F1 || k > SC_F12)) return 1;   // the key was typed into the search box
    if (g_menuOpen && !g_mods.empty()) {   // Up / Down (the D-pad) choose a mod, Enter / Space (X) switch it on or off
        if (k == SC_UP || k == SC_DOWN) { chooseNext(k == SC_DOWN ? 1 : -1); return 1; }
        if (k == SC_RETURN || k == SC_SPACE) {
            if (CtwMod* m = selectedMod()) setEnabled(*m, !m->enabled);
            return 1;
        }
    }
    const auto hooks = g_keyHooks;
    for (const auto& h : hooks)
        if (running(*h.mod) && h.fn(k, h.user)) return 1;
    return 0;   // everything else goes to the game: the player can move with the menu open
}

void onTick(void*) {
    const auto hooks = g_tickHooks;
    for (const auto& h : hooks)
        if (running(*h.mod)) h.fn(h.user);
    if (H->frame_count() % 30 == 0) flushAllSettings();
}

void onGameEvent(const CtwGameEvent* event, void*) {
    const auto hooks = g_eventHooks;
    for (const auto& h : hooks) if (running(*h.mod)) h.fn(event, h.user);
}

bool matchesSearch(const CtwMod& m) {
    if (!g_search[0]) return true;
    auto has = [](const std::string& text) {
        auto it = std::search(text.begin(), text.end(), g_search, g_search + strlen(g_search),
                              [](char a, char b) { return tolower((unsigned char)a) == tolower((unsigned char)b); });
        return it != text.end();
    };
    return has(m.name) || has(m.author) || has(m.folder) || has(m.description);
}

// The Porter's mod picker (porter/src/App.tsx, ModPicker): a card per mod, kryo-radius border p-3, its name in
// text-sm and its byline in text-xs (gap-1). Section labels are its <Label>, mb-3 under a pt-5 gap.
float listRowHeight(const CtwUi&) { return 12.f + 20.f + 4.f + 16.f + 12.f; }
float labelHeight(const CtwUi&) { return 20.f + 16.f + 12.f; }

std::string byline(const CtwMod& m) {
    std::string s;
    if (!m.version.empty()) s = std::string(isdigit((unsigned char)m.version[0]) ? "v" : "") + m.version;
    if (!m.author.empty()) s += (s.empty() ? "by " : "  \xC2\xB7  by ") + m.author;
    return s;
}

std::string note(const CtwMod& m) {
    if (!m.error.empty()) return m.error;
    if (!m.enabled && m.codeLoaded) return "Switched off. It stops completely after a restart.";
    return "";
}

CtwMod* selectedMod() {
    if (g_selected >= (int)g_mods.size()) g_selected = (int)g_mods.size() - 1;
    return g_selected >= 0 ? g_mods[g_selected].get() : nullptr;
}

std::vector<int> shownMods() {
    std::vector<int> shown;
    for (int i = 0; i < (int)g_mods.size(); ++i) if (matchesSearch(*g_mods[i])) shown.push_back(i);
    return shown;
}

// Keyboard and controller: the next or previous mod in the (searched) list, scrolled into view.
void chooseNext(int step) {
    const std::vector<int> shown = shownMods();
    if (shown.empty()) return;
    int at = 0;
    for (int i = 0; i < (int)shown.size(); ++i) if (shown[i] == g_selected) at = i;
    at = std::clamp(at + step, 0, (int)shown.size() - 1);
    if (shown[at] != g_selected) { g_selected = shown[at]; g_detailScroll.pos = g_detailScroll.target = 0.f; }
    const float rh = listRowHeight(g_ui), gap = 6.f, top = at * (rh + gap);
    if (top < g_listScroll.target) g_listScroll.target = top;
    if (g_listH > 0.f && top + rh > g_listScroll.target + g_listH) g_listScroll.target = top + rh - g_listH;
}

// The left column: the Porter's mod cards. The selected one is the card the Porter draws chosen (border-primary
// bg-primary/10); the others border-border, border-foreground/40 under the pointer. The box is the mod's switch.
void drawList(CtwUi& ui, float x, float y, float w, float h) {
    const std::vector<int> shown = shownMods();
    const float rh = listRowHeight(ui), gap = 8.f, cw = w - 14.f;
    g_listH = h;
    ctw_ui_scroll_begin(&ui, 1, &g_listScroll, x, y, w, h, shown.size() * (rh + gap));
    if (g_mods.empty())
        ctw_ui_type_wrap(&ui, x, y, CTW_UI_TEXT_XS, 1.625f, CTW_UI_MUTED_FG,
                         "No mods installed.\n\nPut each mod in its own folder inside the game's mods folder, then start "
                         "the game again.", cw, 1);
    else if (shown.empty())
        ctw_ui_type_wrap(&ui, x, y, CTW_UI_TEXT_XS, 1.625f, CTW_UI_MUTED_FG, "No mod matches the search.", cw, 1);
    const bool inList = ctw_ui_in(&ui, x, y, cw, h);
    float ry = y - g_listScroll.pos;
    for (int i : shown) {
        CtwMod& m = *g_mods[i];
        if (ry + rh >= y && ry <= y + h) {
            const bool selected = i == g_selected, hot = inList && ctw_ui_in(&ui, x, ry, cw, rh);
            if (selected) ctw_ui_round_rect(&g_api, x, ry, cw, rh, CTW_UI_RADIUS, ctw_ui_alpha(CTW_UI_PRIMARY, 0.1f));
            ctw_ui_round_frame(&g_api, x, ry, cw, rh, CTW_UI_RADIUS, 1.f,
                               selected ? CTW_UI_PRIMARY : hot ? ctw_ui_alpha(CTW_UI_FOREGROUND, 0.4f) : CTW_UI_BORDER);
            const float bx = x + 12.f, by = ry + 12.f, tx = bx + 16.f + 8.f;
            CtwUi box = ui;   // the box switches the mod; the rest of the card selects it
            box.clicked = inList && ui.clicked;
            int on = m.enabled ? 1 : 0;
            if (ctw_ui_check(&box, bx, by, 16.f, 20.f, nullptr, &on, 0)) setEnabled(m, on != 0);
            else if (hot && ui.clicked) { g_selected = i; g_detailScroll.pos = g_detailScroll.target = 0.f; }
            float tagW = 0.f;
            if (!m.error.empty()) tagW = ctw_ui_type_w(&ui, 10.f, 0.025f, CTW_UI_UPPER, "error") + 14.f;
            const float nameW = x + cw - 12.f - tx - (tagW > 0.f ? tagW + 8.f : 0.f);
            ctw_ui_type_fit(&ui, tx, by + (20.f - 21.f) * 0.5f, CTW_UI_TEXT_SM, 0.f, 0,
                            m.enabled ? CTW_UI_FOREGROUND : CTW_UI_MUTED_FG, m.name.c_str(), nameW);
            if (tagW > 0.f) ctw_ui_badge(&ui, x + cw - 12.f - tagW, by + (20.f - 21.f) * 0.5f, CTW_UI_DESTRUCTIVE, "error");
            const std::string by2 = byline(m);
            ctw_ui_type_fit(&ui, tx, by + 20.f + 4.f + (16.f - 18.f) * 0.5f, CTW_UI_TEXT_XS, 0.f, 0, CTW_UI_MUTED_FG,
                            by2.c_str(), x + cw - 12.f - tx);
        }
        ry += rh + gap;
    }
    ctw_ui_scroll_end(&ui);
}

// The right column, as the Porter's ready screen sets out a choice: the name (text-sm bold) and a caption, the tags,
// the mod's switch (a Check), its description (text-sm leading-relaxed), any notice, then its options. Returns the
// height of the contents; draws them when draw is set, so measuring and drawing can never disagree.
float detail(CtwUi& u, CtwUi& inert, CtwMod& m, float x, float y, float w, bool draw) {
    float ry = y;
    if (draw) ctw_ui_type_fit(&u, x, ry + (20.f - 21.f) * 0.5f, CTW_UI_TEXT_SM, 0.f, CTW_UI_BOLD, CTW_UI_FOREGROUND,
                              m.name.c_str(), w);
    ry += 20.f + 2.f;
    std::string caption = byline(m);
    caption += (caption.empty() ? "" : "  \xC2\xB7  ") + std::string("mods/") + m.folder;
    if (draw) ctw_ui_type_fit(&u, x, ry, 10.f, 0.05f, CTW_UI_UPPER, CTW_UI_MUTED_FG, caption.c_str(), w);
    ry += 15.f;
    if (!m.dllName.empty() || m.textureFiles > 0 || !m.error.empty()) {   // what the mod contains (its Tags)
        ry += 8.f;
        float bx = x;
        char tex[32];
        snprintf(tex, sizeof tex, "%d texture%s", m.textureFiles, m.textureFiles == 1 ? "" : "s");
        if (draw) {
            if (!m.dllName.empty()) bx += ctw_ui_tag(&u, bx, ry, "code") + 4.f;
            if (m.textureFiles > 0) bx += ctw_ui_tag(&u, bx, ry, tex) + 4.f;
            if (!m.error.empty()) ctw_ui_badge(&u, bx, ry, CTW_UI_DESTRUCTIVE, "error");
        }
        ry += 21.f;
    }
    ry += 20.f;
    if (draw) {
        int on = m.enabled ? 1 : 0;
        if (ctw_ui_check(&u, x, ry, w, 20.f, "Enabled", &on, 0)) setEnabled(m, on != 0);
    }
    ry += 20.f + 20.f;
    if (!m.description.empty())
        ry += ctw_ui_type_wrap(&u, x, ry, CTW_UI_TEXT_SM, 1.625f, CTW_UI_FOREGROUND, m.description.c_str(), w, draw) + 20.f;
    const std::string n = note(m);
    if (!n.empty()) ry += ctw_ui_notice(&u, x, ry, w, m.error.empty() ? CTW_UI_WARNING : CTW_UI_DESTRUCTIVE, n.c_str(), draw) + 20.f;
    if (m.items.empty()) {
        if (draw && m.codeLoaded) ctw_ui_caption(&u, x, ry, "This mod has no options.");
        return ry + 15.f - y;
    }
    CtwUi& iu = running(m) ? u : inert;   // a stopped mod's options do not react
    const float top = ry;
    int sliderId = 1000;
    std::vector<const char*> names;
    for (auto& it : m.items) {
        const float h = it.kind == CtwMod::Item::Slider ? ctw_ui_slider_h(&iu)
                      : it.kind == CtwMod::Item::Label ? labelHeight(iu) : ctw_ui_row_h(&iu);
        if (draw) switch (it.kind) {
        case CtwMod::Item::Toggle: ctw_ui_check(&iu, x, ry, w, h, it.label.c_str(), it.ival, 0); break;
        case CtwMod::Item::Slider:
            ctw_ui_slider(&iu, sliderId++, x - 8.f, ry, w + 16.f, h, it.label.c_str(), it.fval, it.mn, it.mx, it.step);
            break;
        case CtwMod::Item::Button:
            if (ctw_ui_button(&iu, x, ry + 2.f, ctw_ui_button_w(&iu, CTW_UI_MD, it.label.c_str()), h - 4.f, it.label.c_str()))
                it.fn(it.user);
            break;
        case CtwMod::Item::Choice:
            names.clear();
            for (auto& o : it.options) names.push_back(o.c_str());
            ctw_ui_choice(&iu, x, ry, w, h, it.label.c_str(), it.ival, names.data(), (int)names.size());
            break;
        case CtwMod::Item::Label: ctw_ui_label(&iu, x, ry + 20.f + (16.f - 18.f) * 0.5f, it.label.c_str()); break;
        }
        ry += h;
    }
    if (draw && !running(m) && ry > top) g_api.draw_rect(x - 8.f, top, w + 16.f, ry - top, ctw_ui_alpha(CTW_UI_CARD, 0.6f));   // dimmed while off
    return ry - y + 8.f;
}

void drawMenu() {
    CtwUi& ui = g_ui;
    ctw_ui_begin(&ui, &g_api);
    const CtwUiRect area = ctw_ui_menu_area(&g_api);
    const float px = area.x, py = area.y, pw = area.w, ph = area.h, row = ctw_ui_row_h(&ui);
    int on = 0;
    for (auto& m : g_mods) on += m->enabled ? 1 : 0;
    char count[48];
    snprintf(count, sizeof count, "%d of %d on", on, (int)g_mods.size());
    // the game stays visible and playable beside the menu, a little darker so the menu reads clearly
    g_api.draw_rect(0.f, 0.f, (float)g_api.screen_width(), (float)g_api.screen_height(), 0x0000003Cu);
    if (ctw_ui_panel(&ui, px, py, pw, ph, "Mods", g_mods.empty() ? nullptr : count)) { setOpen(false); return; }

    // the card's p-6
    const float top = py + ctw_ui_title_h(&ui) + 24.f, bottom = py + ph - ctw_ui_footer_h(&ui) - 24.f;
    const float listW = std::max(200.f, std::min(300.f, pw * 0.38f)), lx = px + 24.f;
    // left: search, then the list
    float ly = top;
    if (!g_mods.empty()) {
        int focused = g_searchFocused ? 1 : 0;
        if (ctw_ui_textbox(&ui, lx, ly, listW, row, g_search, sizeof g_search, &focused, "Search mods\xE2\x80\xA6"))
            g_listScroll.target = 0.f;
        setSearchFocus(focused != 0);
        ly += row + 16.f;
    }
    drawList(ui, lx, ly, listW, bottom - ly);
    // right: the selected mod
    const float dx = lx + listW + 24.f, dw = px + pw - 24.f - dx;
    g_api.draw_rect(dx - 13.f, top, 1.f, bottom - top, CTW_UI_BORDER);
    if (CtwMod* m = selectedMod()) {
        CtwUi inert = ui;   // rows scrolled out of view, and options of stopped mods, must not react
        inert.clicked = 0;
        inert.wheel = 0.f;
        const float cw = dw - 14.f, content = detail(ui, inert, *m, 0, 0, cw - 8.f, false);
        ctw_ui_scroll_begin(&ui, 2, &g_detailScroll, dx, top, dw, bottom - top, content);
        CtwUi& u = ctw_ui_in(&ui, dx, top, dw, bottom - top) ? ui : inert;
        detail(u, inert, *m, dx + 4.f, top - g_detailScroll.pos, cw - 8.f, true);
        ui.drag = u.drag;   // keep a slider drag that started in this frame
        ui.wheel_used = u.wheel_used;
        ctw_ui_scroll_end(&ui);
    }
    ctw_ui_footer(&ui, px, py, pw, ph, CTW_UI_DIM,
                  g_mods.empty() ? "F4 / Esc: close"
                                 : "Up / Down: choose   Enter: switch on or off   F4 / Esc: close");
}

void onHud(void*) {
    if (!g_textureJobs.empty()) applyTextureJobs(3.0);   // about 3 ms of texture work per frame
    for (const auto& b : g_pauseButtons) H->set_pause_button_visible(b->id, running(*b->mod));
    for (size_t i = 0; i < g_radarBlips.size();)   // a mod switched off takes its dots with it
        if (!running(*g_radarBlips[i].mod)) {
            if (CTW_HOST_HAS(H, remove_radar_blip)) H->remove_radar_blip(g_radarBlips[i].hostId);
            g_radarBlips.erase(g_radarBlips.begin() + (long)i);
        } else ++i;
    const auto hooks = g_hudHooks;
    for (const auto& h : hooks)
        if (running(*h.mod)) h.fn(h.user);
    if (!g_menuOpen) return;
    if (CTW_MOD_HAS(&g_api, consume_mouse_wheel)) g_api.consume_mouse_wheel();
    drawMenu();
}

void scanMods() {
    const auto enabled = readIni(fs::u8path(g_modsDir) / "enabled.ini");
    std::vector<fs::path> dirs;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(fs::u8path(g_modsDir), ec))
        if (e.is_directory() && fs::exists(e.path() / "mod.ini")) dirs.push_back(e.path());
    std::sort(dirs.begin(), dirs.end());
    for (auto& d : dirs) {
        auto ini = readIni(d / "mod.ini");
        auto m = std::make_unique<CtwMod>();
        m->folder = d.filename().u8string();
        m->dir = d.u8string() + "/";
        m->name = ini.count("name") ? ini["name"] : m->folder;
        m->author = ini["author"];
        m->version = ini["version"];
        m->description = ini["description"];
        m->dllName = ini["dll"];
        m->textureFiles = scanTextures(*m, false);
        auto en = enabled.find(m->folder);
        m->enabled = en == enabled.end() || en->second != "0";
        g_mods.push_back(std::move(m));
    }
}
}  // namespace

extern "C" CTW_PLUGIN_EXPORT int ctw_plugin_init(const CtwHostApi* host) {
    // The v3 prefix still serves old mods on old games. Never read an optional tail before checking its size.
    if (!host || host->version < 3 || !CTW_HOST_HAS(host, get_render_distance)) return 1;
    H = host;
    const bool gameplay = H->version >= 4 && H->size >= offsetof(CtwHostApi, set_control_yaw);
    g_api.version = gameplay ? std::min<uint32_t>(H->version, CTW_MOD_API_VERSION) : 3;
    g_api.size = gameplay ? sizeof(CtwApi) : offsetof(CtwApi, get_capabilities);
#define FORWARD_V4(name) g_api.name = gameplay ? H->name : nullptr;
#define FORWARD_V5(name) g_api.name = gameplay && CTW_HOST_HAS(H, name) ? H->name : nullptr;
    CTW_FORWARDED_V4(FORWARD_V4)
    CTW_FORWARDED_V5(FORWARD_V5)
    g_api.get_camera_shake = gameplay && CTW_HOST_HAS(H,get_camera_shake) ? H->get_camera_shake : nullptr;
    g_api.load_character_model = gameplay && CTW_HOST_HAS(H,load_character_model) ? H->load_character_model : nullptr;
    g_api.release_character_model = gameplay && CTW_HOST_HAS(H,release_character_model) ? H->release_character_model : nullptr;
    g_api.draw_character_model = gameplay && CTW_HOST_HAS(H,draw_character_model) ? H->draw_character_model : nullptr;
    g_api.get_character_bounds = gameplay && CTW_HOST_HAS(H,get_character_bounds) ? H->get_character_bounds : nullptr;
    g_api.draw_character_model_at_time = gameplay && CTW_HOST_HAS(H,draw_character_model_at_time) ? H->draw_character_model_at_time : nullptr;
    g_api.draw_character_model_attached = gameplay && CTW_HOST_HAS(H,draw_character_model_attached) ? H->draw_character_model_attached : nullptr;
    g_api.on_character_render = gameplay && CTW_HOST_HAS(H,on_character_render) ? api_on_character : nullptr;
    g_api.weapon_type_count = gameplay && CTW_HOST_HAS(H,weapon_type_count) ? H->weapon_type_count : nullptr;
    g_api.weapon_name = gameplay && CTW_HOST_HAS(H,weapon_name) ? H->weapon_name : nullptr;
    g_api.weapon_givable = gameplay && CTW_HOST_HAS(H,weapon_givable) ? H->weapon_givable : nullptr;
    g_api.give_weapon = gameplay && CTW_HOST_HAS(H,give_weapon) ? H->give_weapon : nullptr;
    g_api.get_player_weapon = gameplay && CTW_HOST_HAS(H,get_player_weapon) ? H->get_player_weapon : nullptr;
    g_api.draw_weapon_icon = gameplay && CTW_HOST_HAS(H,draw_weapon_icon) ? H->draw_weapon_icon : nullptr;
    g_api.set_aim_override = gameplay && CTW_HOST_HAS(H,set_aim_override) ? H->set_aim_override : nullptr;
    g_api.screen_to_world = gameplay && CTW_HOST_HAS(H,screen_to_world) ? H->screen_to_world : nullptr;
    g_api.cycle_weapon = gameplay && CTW_HOST_HAS(H,cycle_weapon) ? H->cycle_weapon : nullptr;
    g_api.set_prop_physics = gameplay && CTW_HOST_HAS(H,set_prop_physics) ? H->set_prop_physics : nullptr;
    g_api.get_prop_physics = gameplay && CTW_HOST_HAS(H,get_prop_physics) ? H->get_prop_physics : nullptr;
    g_api.spawn_puppet = gameplay && CTW_HOST_HAS(H,spawn_puppet) ? H->spawn_puppet : nullptr;
    g_api.set_puppet_state = gameplay && CTW_HOST_HAS(H,set_puppet_state) ? H->set_puppet_state : nullptr;
    g_api.remove_puppet = gameplay && CTW_HOST_HAS(H,remove_puppet) ? H->remove_puppet : nullptr;
    g_api.take_puppet_damage = gameplay && CTW_HOST_HAS(H,take_puppet_damage) ? H->take_puppet_damage : nullptr;
    g_api.world_to_screen = CTW_HOST_HAS(H,world_to_screen) ? H->world_to_screen : nullptr;
    g_api.get_player_vitals = gameplay && CTW_HOST_HAS(H,get_player_vitals) ? H->get_player_vitals : nullptr;
    g_api.damage_player = gameplay && CTW_HOST_HAS(H,damage_player) ? H->damage_player : nullptr;
    g_api.get_vehicle_pose = gameplay && CTW_HOST_HAS(H,get_vehicle_pose) ? H->get_vehicle_pose : nullptr;
    g_api.set_vehicle_pose = gameplay && CTW_HOST_HAS(H,set_vehicle_pose) ? H->set_vehicle_pose : nullptr;
    g_api.add_pause_button = CTW_HOST_HAS(H,add_pause_button) && CTW_HOST_HAS(H,set_pause_button_visible) ? api_add_pause_button : nullptr;
    g_api.set_radar_blip = CTW_HOST_HAS(H,set_radar_blip) && CTW_HOST_HAS(H,remove_radar_blip) ? api_set_radar_blip : nullptr;
    g_api.remove_radar_blip = CTW_HOST_HAS(H,set_radar_blip) && CTW_HOST_HAS(H,remove_radar_blip) ? api_remove_radar_blip : nullptr;
    g_api.set_radar_blip_label = CTW_HOST_HAS(H,set_radar_blip_label) && g_api.set_radar_blip ? api_set_radar_blip_label : nullptr;
    g_api.set_multiplayer_world = gameplay && CTW_HOST_HAS(H,set_multiplayer_world) ? H->set_multiplayer_world : nullptr;
    g_api.get_world_vehicle = gameplay && CTW_HOST_HAS(H,get_world_vehicle) ? H->get_world_vehicle : nullptr;
    g_api.set_world_vehicle = gameplay && CTW_HOST_HAS(H,set_world_vehicle) ? H->set_world_vehicle : nullptr;
    g_api.world_vehicle_handle = gameplay && CTW_HOST_HAS(H,world_vehicle_handle) ? H->world_vehicle_handle : nullptr;
    g_api.world_vehicle_id = gameplay && CTW_HOST_HAS(H,world_vehicle_id) ? H->world_vehicle_id : nullptr;
    g_api.remove_world_vehicle = gameplay && CTW_HOST_HAS(H,remove_world_vehicle) ? H->remove_world_vehicle : nullptr;
    g_api.get_world_ped = gameplay && CTW_HOST_HAS(H,get_world_ped) ? H->get_world_ped : nullptr;
    g_api.set_world_ped = gameplay && CTW_HOST_HAS(H,set_world_ped) ? H->set_world_ped : nullptr;
    g_api.remove_world_ped = gameplay && CTW_HOST_HAS(H,remove_world_ped) ? H->remove_world_ped : nullptr;
    g_api.take_world_explosion = gameplay && CTW_HOST_HAS(H,take_world_explosion) ? H->take_world_explosion : nullptr;
    g_api.add_world_explosion = gameplay && CTW_HOST_HAS(H,add_world_explosion) ? H->add_world_explosion : nullptr;
    g_api.get_player_seat = gameplay && CTW_HOST_HAS(H,get_player_seat) ? H->get_player_seat : nullptr;
    g_api.set_player_seat = gameplay && CTW_HOST_HAS(H,set_player_seat) ? H->set_player_seat : nullptr;
    g_api.set_puppet_seat = gameplay && CTW_HOST_HAS(H,set_puppet_seat) ? H->set_puppet_seat : nullptr;
    g_api.take_world_ped_damage = gameplay && CTW_HOST_HAS(H,take_world_ped_damage) ? H->take_world_ped_damage : nullptr;
    g_api.damage_world_ped = gameplay && CTW_HOST_HAS(H,damage_world_ped) ? H->damage_world_ped : nullptr;
    g_api.vehicle_seat_count = gameplay && CTW_HOST_HAS(H,vehicle_seat_count) ? H->vehicle_seat_count : nullptr;
    g_api.adopt_world_vehicle = gameplay && CTW_HOST_HAS(H,adopt_world_vehicle) ? H->adopt_world_vehicle : nullptr;
    g_api.get_vehicle_visual = gameplay && CTW_HOST_HAS(H,get_vehicle_visual) ? H->get_vehicle_visual : nullptr;
    g_api.set_vehicle_visual = gameplay && CTW_HOST_HAS(H,set_vehicle_visual) ? H->set_vehicle_visual : nullptr;
    g_api.get_puppet_position = gameplay && CTW_HOST_HAS(H,get_puppet_position) ? H->get_puppet_position : nullptr;
    g_api.get_player_vehicle_task = gameplay && CTW_HOST_HAS(H,get_player_vehicle_task) ? H->get_player_vehicle_task : nullptr;
    g_api.claim_mouse_rect = CTW_HOST_HAS(H,claim_mouse_rect) ? H->claim_mouse_rect : nullptr;
    g_api.take_world_skid = gameplay && CTW_HOST_HAS(H,take_world_skid) ? H->take_world_skid : nullptr;
    g_api.add_world_skid = gameplay && CTW_HOST_HAS(H,add_world_skid) ? H->add_world_skid : nullptr;
    g_api.get_world_pickup = gameplay && CTW_HOST_HAS(H,get_world_pickup) ? H->get_world_pickup : nullptr;
    g_api.set_world_pickup = gameplay && CTW_HOST_HAS(H,set_world_pickup) ? H->set_world_pickup : nullptr;
    g_api.remove_world_pickup = gameplay && CTW_HOST_HAS(H,remove_world_pickup) ? H->remove_world_pickup : nullptr;
    g_api.take_world_pickup_claim = gameplay && CTW_HOST_HAS(H,take_world_pickup_claim) ? H->take_world_pickup_claim : nullptr;
    g_api.claim_world_pickup = gameplay && CTW_HOST_HAS(H,claim_world_pickup) ? H->claim_world_pickup : nullptr;
    g_api.grant_world_pickup = gameplay && CTW_HOST_HAS(H,grant_world_pickup) ? H->grant_world_pickup : nullptr;
    g_api.take_puppet_world_damage = gameplay && CTW_HOST_HAS(H,take_puppet_world_damage) ? H->take_puppet_world_damage : nullptr;
    g_api.get_view = gameplay && CTW_HOST_HAS(H,get_view) ? H->get_view : nullptr;
    g_api.set_puppet_view = gameplay && CTW_HOST_HAS(H,set_puppet_view) ? H->set_puppet_view : nullptr;
    g_api.draw_ui_text = CTW_HOST_HAS(H,draw_ui_text) ? H->draw_ui_text : nullptr;
    g_api.ui_text_width = CTW_HOST_HAS(H,ui_text_width) ? H->ui_text_width : nullptr;
#undef FORWARD_V4
#undef FORWARD_V5
    g_api.on_game_event = gameplay && H->on_game_event ? api_on_event : nullptr;
    g_api.on_enabled_changed = gameplay ? api_on_enabled : nullptr;
    g_api.get_setting = gameplay ? api_get_setting : nullptr;
    g_api.set_setting = gameplay ? api_set_setting : nullptr;
    g_api.menu_add_choice = gameplay ? api_menu_choice : nullptr;
    g_api.menu_add_label = gameplay ? api_menu_label : nullptr;
    g_api.on_frame_begin = gameplay && CTW_HOST_HAS(H, on_frame_begin) ? api_on_frame : nullptr;
    g_modsDir = H->mods_dir();
    scanMods();
    for (auto& m : g_mods)
        if (m->enabled) loadMod(*m);
    applyTextureJobs(-1);   // the enabled mods' textures before the first frame
    if (!g_mods.empty()) log("mod menu: " + std::to_string(g_mods.size()) + " mods, API version " + std::to_string(g_api.version));
    H->on_key(onKey, nullptr);
    H->on_tick(onTick, nullptr);
    H->on_draw_hud(onHud, nullptr);
    if (gameplay && H->on_game_event) H->on_game_event(onGameEvent, nullptr);
    if (g_api.on_frame_begin) H->on_frame_begin(onFrame, nullptr);
    if (g_api.on_character_render) H->on_character_render(onCharacter,nullptr);
    return 0;
}

extern "C" CTW_PLUGIN_EXPORT void ctw_plugin_shutdown(void) {
    flushAllSettings();
    for (auto& m : g_mods) {
        if (m->codeLoaded && m->shutdown) m->shutdown();
        if (m->lib) freeLib(m->lib);
    }
    g_textureJobs.clear();
    g_mods.clear();
    g_tickHooks.clear();
    g_hudHooks.clear();
    g_frameHooks.clear();
    g_characterHooks.clear();
    g_keyHooks.clear();
    g_eventHooks.clear();
    g_enabledHooks.clear();
    g_menuOpen = g_searchFocused = false;
    g_modsWantInput = true;
    g_search[0] = 0;
    g_selected = 0;
    H = nullptr;
}
