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

enum { SC_ESCAPE = 41, SC_F1 = 58, SC_F4 = 61, SC_F12 = 69 };   // SDL scancodes

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
void api_on_hud(CtwMod* m, CtwCallback fn, void* user) { if (fn) g_hudHooks.push_back({m, fn, user}); }
void api_on_frame(CtwMod* m, CtwCallback fn, void* user) { if (m && fn) g_frameHooks.push_back({m, fn, user}); }
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
void* loadLib(const std::string& path) {
#ifdef _WIN32
    return (void*)LoadLibraryW(fs::u8path(path).wstring().c_str());
#else
    return dlopen(path.c_str(), RTLD_NOW);
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
        if (apply) api_override_texture(&m, (int)id, e.path().u8string().c_str());
    }
    return count;
}

void removeTextures(CtwMod& m) {   // texture changes are removed when their mod is switched off
    for (int id : m.textures) H->restore_texture(id);
    m.textures.clear();
}

void fail(CtwMod& m, const std::string& why) {
    m.error = why;
    log("[" + m.name + "] " + why);
}

void loadMod(CtwMod& m) {
    scanTextures(m, true);
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
    log("loaded " + m.name + " " + m.version + (m.textures.empty() ? "" : " (" + std::to_string(m.textures.size()) + " textures)"));
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
    if (k == SC_F4 || (k == SC_ESCAPE && g_menuOpen)) {   // Esc closes the menu instead of quitting
        setOpen(!g_menuOpen);
        return 1;
    }
    if (g_searchFocused && (k < SC_F1 || k > SC_F12)) return 1;   // the key was typed into the search box
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

// Every size follows the game font (ctw_ui.h), so rows never run into each other at any font size.
float listRowHeight(const CtwUi& ui) { return 12.f + ctw_ui_lh(&ui, 1.f) + 4.f + ctw_ui_lh(&ui, 0.8f) + 12.f; }
float labelHeight(const CtwUi& ui) { return ctw_ui_lh(&ui, 0.8f) + 18.f; }

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

uint32_t statusColour(const CtwMod& m) { return !m.error.empty() ? CTW_UI_ERROR : m.enabled ? CTW_UI_OK : 0x5A5D68FFu; }

CtwMod* selectedMod() {
    if (g_selected >= (int)g_mods.size()) g_selected = (int)g_mods.size() - 1;
    return g_selected >= 0 ? g_mods[g_selected].get() : nullptr;
}

// The left column: one row per mod (status dot, name, version and author). Clicking selects it.
void drawList(CtwUi& ui, float x, float y, float w, float h) {
    std::vector<int> shown;
    for (int i = 0; i < (int)g_mods.size(); ++i) if (matchesSearch(*g_mods[i])) shown.push_back(i);
    const float rh = listRowHeight(ui), gap = 6.f;
    ctw_ui_scroll_begin(&ui, 1, &g_listScroll, x, y, w, h, shown.size() * (rh + gap));
    if (g_mods.empty())
        ctw_ui_text_wrap(&ui, x + 8.f, y + 8.f, 0.9f, CTW_UI_DIM,
                         "No mods installed.\n\nPut each mod in its own folder inside the game's mods folder, then start "
                         "the game again.", w - 24.f, 1);
    else if (shown.empty())
        ctw_ui_text_wrap(&ui, x + 8.f, y + 8.f, 0.9f, CTW_UI_DIM, "No mod matches the search.", w - 24.f, 1);
    const bool inList = ctw_ui_in(&ui, x, y, w - 14.f, h);
    float ry = y - g_listScroll.pos;
    for (int i : shown) {
        CtwMod& m = *g_mods[i];
        if (ry + rh >= y && ry <= y + h) {
            const bool selected = i == g_selected, hot = inList && ctw_ui_in(&ui, x, ry, w - 14.f, rh);
            if (selected || hot) ctw_ui_round_rect(&g_api, x, ry, w - 14.f, rh, 10.f, selected ? 0xF2C2301Cu : CTW_UI_HOVER);
            if (selected) ctw_ui_round_rect(&g_api, x, ry + 10.f, 3.f, rh - 20.f, 1.5f, CTW_UI_ACCENT);
            if (hot && ui.clicked) { g_selected = i; g_detailScroll.pos = g_detailScroll.target = 0.f; }
            const float dot = 8.f, tx = x + 16.f + dot + 10.f, tw = w - 14.f - (tx - x) - 12.f;
            ctw_ui_round_rect(&g_api, x + 16.f, ry + 12.f + (ctw_ui_lh(&ui, 1.f) - dot) * 0.5f, dot, dot, dot * 0.5f,
                              statusColour(m));
            ctw_ui_text_fit(&ui, tx, ry + 12.f, 1.f, m.enabled ? CTW_UI_TEXT : CTW_UI_DIM, m.name.c_str(), tw);
            const std::string by = byline(m);
            ctw_ui_text_fit(&ui, tx, ry + 12.f + ctw_ui_lh(&ui, 1.f) + 4.f, 0.8f, CTW_UI_DIM, by.c_str(), tw);
        }
        ry += rh + gap;
    }
    ctw_ui_scroll_end(&ui);
}

// The right column: the selected mod's name, its switch, description, any note, then its options. Returns the height of
// the contents; draws them when draw is set, so measuring and drawing can never disagree.
float detail(CtwUi& u, CtwUi& inert, CtwMod& m, float x, float y, float w, bool draw) {
    float ry = y;
    if (draw) ctw_ui_text_fit(&u, x, ry, 1.5f, CTW_UI_TEXT, m.name.c_str(), w);
    ry += ctw_ui_lh(&u, 1.5f) + 4.f;
    const std::string by = byline(m);
    if (!by.empty()) {
        if (draw) ctw_ui_text_fit(&u, x, ry, 0.9f, CTW_UI_DIM, by.c_str(), w);
        ry += ctw_ui_lh(&u, 0.9f);
    }
    ry += 14.f;
    const float row = ctw_ui_row_h(&u);
    if (draw) {   // the on/off switch as a card
        ctw_ui_round_rect(&g_api, x, ry, w, row + 8.f, 10.f, CTW_UI_SURFACE);
        int on = m.enabled ? 1 : 0;
        if (ctw_ui_switch(&u, x, ry + 4.f, w, row, m.enabled ? "Enabled" : "Disabled", &on)) setEnabled(m, on != 0);
    }
    ry += row + 8.f + 14.f;
    if (!m.description.empty())
        ry += ctw_ui_text_wrap(&u, x, ry, 0.95f, 0xC8CAD2FFu, m.description.c_str(), w, draw) + 12.f;
    const std::string n = note(m);
    if (!n.empty())
        ry += ctw_ui_text_wrap(&u, x, ry, 0.9f, m.error.empty() ? CTW_UI_ACCENT : CTW_UI_ERROR, n.c_str(), w, draw) + 12.f;
    if (m.items.empty()) {
        if (draw && m.codeLoaded) ctw_ui_text(&u, x, ry, 0.9f, CTW_UI_DIM, "This mod has no options.");
        return ry + ctw_ui_lh(&u, 0.9f) - y;
    }
    CtwUi& iu = running(m) ? u : inert;   // a stopped mod's options do not react
    const float top = ry;
    int sliderId = 1000;
    std::vector<const char*> names;
    for (auto& it : m.items) {
        const float h = it.kind == CtwMod::Item::Slider ? ctw_ui_slider_h(&iu)
                      : it.kind == CtwMod::Item::Label ? labelHeight(iu) : ctw_ui_row_h(&iu);
        if (draw) switch (it.kind) {
        case CtwMod::Item::Toggle: ctw_ui_switch(&iu, x, ry, w, h, it.label.c_str(), it.ival); break;
        case CtwMod::Item::Slider:
            ctw_ui_slider(&iu, sliderId++, x, ry, w, h, it.label.c_str(), it.fval, it.mn, it.mx, it.step);
            break;
        case CtwMod::Item::Button:
            if (ctw_ui_button(&iu, x + 4.f, ry + 3.f, w - 8.f, h - 6.f, it.label.c_str())) it.fn(it.user);
            break;
        case CtwMod::Item::Choice:
            names.clear();
            for (auto& o : it.options) names.push_back(o.c_str());
            ctw_ui_choice(&iu, x, ry, w, h, it.label.c_str(), it.ival, names.data(), (int)names.size());
            break;
        case CtwMod::Item::Label:
            ctw_ui_text_fit(&iu, x + 12.f, ry + h - ctw_ui_lh(&iu, 0.8f) - 6.f, 0.8f, CTW_UI_ACCENT, it.label.c_str(), w - 24.f);
            g_api.draw_rect(x + 12.f, ry + h - 2.f, w - 24.f, 1.f, CTW_UI_LINE);
            break;
        }
        ry += h;
    }
    if (draw && !running(m) && ry > top) g_api.draw_rect(x, top, w, ry - top, 0x14151B90u);   // dimmed while off
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
    if (ctw_ui_panel(&ui, px, py, pw, ph, "Mods", g_mods.empty() ? nullptr : count)) { setOpen(false); return; }

    const float top = py + ctw_ui_title_h(&ui) + 14.f, bottom = py + ph - ctw_ui_footer_h(&ui) - 12.f;
    const float listW = std::max(200.f, std::min(300.f, pw * 0.38f)), lx = px + 14.f;
    // left: search, then the list
    float ly = top;
    if (!g_mods.empty()) {
        int focused = g_searchFocused ? 1 : 0;
        if (ctw_ui_textbox(&ui, lx, ly, listW, row, g_search, sizeof g_search, &focused, "Search mods..."))
            g_listScroll.target = 0.f;
        setSearchFocus(focused != 0);
        ly += row + 10.f;
    }
    drawList(ui, lx, ly, listW, bottom - ly);
    // right: the selected mod
    const float dx = lx + listW + 16.f, dw = px + pw - 16.f - dx;
    g_api.draw_rect(dx - 9.f, top, 1.f, bottom - top, CTW_UI_LINE);
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
    ctw_ui_footer(&ui, px, py, pw, ph, CTW_UI_DIM, "Select a mod to see its options.   F4: close");
}

void onHud(void*) {
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
    if (!g_mods.empty()) log("mod menu: " + std::to_string(g_mods.size()) + " mods, API version " + std::to_string(g_api.version));
    H->on_key(onKey, nullptr);
    H->on_tick(onTick, nullptr);
    H->on_draw_hud(onHud, nullptr);
    if (gameplay && H->on_game_event) H->on_game_event(onGameEvent, nullptr);
    if (g_api.on_frame_begin) H->on_frame_begin(onFrame, nullptr);
    return 0;
}

extern "C" CTW_PLUGIN_EXPORT void ctw_plugin_shutdown(void) {
    flushAllSettings();
    for (auto& m : g_mods) {
        if (m->codeLoaded && m->shutdown) m->shutdown();
        if (m->lib) freeLib(m->lib);
    }
    g_mods.clear();
    g_tickHooks.clear();
    g_hudHooks.clear();
    g_frameHooks.clear();
    g_keyHooks.clear();
    g_eventHooks.clear();
    g_enabledHooks.clear();
    g_menuOpen = g_searchFocused = false;
    g_modsWantInput = true;
    g_search[0] = 0;
    g_selected = 0;
    H = nullptr;
}
