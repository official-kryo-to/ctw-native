// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// ModMenu.dll - the mod loader and in-game mod menu (F4) for the GTA: Chinatown Wars PC port.
//
// A plugin (ctw_plugin.h): the game loads it from its mods folder. It finds mods in
// mods/<ModName>/ folders (mod.ini), applies texture mods (textures/<resource id>.png), loads code
// mods (DLLs using ctw_mod.h) and gives them the mod API, which it implements on top of the plugin interface.
// The menu is used with the mouse; the game keeps its keyboard, so the player can walk while it is open.
#include "ctw_mod.h"
#include "ctw_plugin.h"
#include "ctw_ui.h"
#include <algorithm>
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

enum { SC_ESCAPE = 41, SC_F4 = 61 };   // SDL scancodes

struct CtwMod {   // the opaque handle mods receive
    std::string folder, dir, name, author, version, description, dllName;
    bool enabled = true;
    bool codeLoaded = false;       // the DLL is running (it only stops completely after a restart)
    bool expanded = false;         // menu: options shown
    std::vector<int> textures;     // texture overrides and patches currently applied
    void* lib = nullptr;
    CtwModShutdownFn shutdown = nullptr;
    struct Item { enum Kind { Toggle, Slider, Button } kind; std::string label; int* ival = nullptr; float* fval = nullptr;
                  float mn = 0, mx = 1, step = 0.1f; CtwCallback fn = nullptr; void* user = nullptr; };
    std::vector<Item> items;
    std::string error;
};

namespace {
const CtwHostApi* H = nullptr;
std::string g_modsDir;
std::vector<std::unique_ptr<CtwMod>> g_mods;
struct Hook { CtwMod* mod; CtwCallback fn; void* user; };
struct KeyHook { CtwMod* mod; CtwKeyCallback fn; void* user; };
std::vector<KeyHook> g_keyHooks;
std::vector<Hook> g_tickHooks, g_hudHooks;

bool g_menuOpen = false;
CtwUi g_ui;
CtwUiScroll g_scroll;

void log(const std::string& s) { H->log(s.c_str()); }

std::map<std::string, std::string> readIni(const fs::path& p) {
    std::map<std::string, std::string> kv;
    std::ifstream f(p);
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == ';' || line[0] == '#' || line[0] == '[') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        auto trim = [](std::string s) {
            size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t");
            return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
        };
        kv[trim(line.substr(0, eq))] = trim(line.substr(eq + 1));
    }
    return kv;
}

void saveEnabled() {
    if (g_mods.empty()) return;   // nothing to remember: keep the mods folder clean
    std::ofstream f(fs::path(g_modsDir) / "enabled.ini");
    f << "; which mods are switched on (1) or off (0); edited by the mod menu (F4)\n";
    for (auto& m : g_mods) f << m->folder << "=" << (m->enabled ? 1 : 0) << "\n";
}

// ------------------------------------------------------------------------------------------------ mod API
void api_log(CtwMod* mod, const char* text) { log(std::string(mod ? "[" + mod->name + "] " : "") + (text ? text : "")); }
const char* api_mod_dir(CtwMod* mod) { return mod ? mod->dir.c_str() : ""; }
uint32_t api_frame_count() { return H->frame_count(); }
float api_get_time() { return H->get_time_of_day(); }
void api_set_time(float h) { H->set_time_of_day(h); }
int api_get_clock() { return H->get_clock_running(); }
void api_set_clock(int r) { H->set_clock_running(r); }
int api_get_weather() { return H->get_weather(); }
void api_set_weather(int w) { H->set_weather(w); }
void api_get_player_pos(float o[3]) { H->get_player_position(o); }
void api_set_player_pos(const float p[3]) { H->set_player_position(p); }
float api_get_player_heading() { return H->get_player_heading(); }
float api_get_cam_height() { return H->get_camera_height(); }
void api_set_cam_height(float u) { H->set_camera_height(u); }
void api_menu_toggle(CtwMod* m, const char* label, int* v) {
    if (m && v) m->items.push_back({CtwMod::Item::Toggle, label ? label : "", v});
}
void api_menu_slider(CtwMod* m, const char* label, float* v, float mn, float mx, float step) {
    if (m && v) { CtwMod::Item it{CtwMod::Item::Slider, label ? label : ""}; it.fval = v; it.mn = mn; it.mx = mx; it.step = step; m->items.push_back(it); }
}
void api_menu_button(CtwMod* m, const char* label, CtwCallback fn, void* user) {
    if (m && fn) { CtwMod::Item it{CtwMod::Item::Button, label ? label : ""}; it.fn = fn; it.user = user; m->items.push_back(it); }
}
void api_on_tick(CtwMod* m, CtwCallback fn, void* user) { if (fn) g_tickHooks.push_back({m, fn, user}); }
void api_on_hud(CtwMod* m, CtwCallback fn, void* user) { if (fn) g_hudHooks.push_back({m, fn, user}); }
void api_draw_text(float x, float y, float s, uint32_t rgba, const char* t) { H->draw_text(x, y, s, rgba, t); }
void api_draw_rect(float x, float y, float w, float h, uint32_t rgba) { H->draw_rect(x, y, w, h, rgba); }
int api_screen_w() { return H->screen_width(); }
int api_screen_h() { return H->screen_height(); }
std::string modPath(CtwMod* m, const char* file) {
    std::string path = file;
    if (m && fs::path(path).is_relative()) path = m->dir + path;
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
void api_on_key(CtwMod* m, CtwKeyCallback fn, void* user) { if (fn) g_keyHooks.push_back({m, fn, user}); }
int api_key_down(int sc) { return H->key_down(sc); }
int api_key_pressed(int sc) { return H->key_pressed(sc); }
int api_menu_is_open() { return g_menuOpen ? 1 : 0; }
void api_set_captured(int on) { H->set_mouse_captured(on && !g_menuOpen); }   // the open menu keeps the cursor

const CtwApi g_api = {
    CTW_MOD_API_VERSION, sizeof(CtwApi),
    api_log, api_mod_dir, api_frame_count,
    api_get_time, api_set_time, api_get_clock, api_set_clock, api_get_weather, api_set_weather,
    api_get_player_pos, api_set_player_pos, api_get_player_heading,
    api_get_cam_height, api_set_cam_height,
    api_menu_toggle, api_menu_slider, api_menu_button,
    api_on_tick, api_on_hud,
    api_draw_text, api_draw_rect, api_screen_w, api_screen_h,
    api_override_texture,
    api_key_down, api_key_pressed,
    // version 2
    api_on_key, [](int on) { H->set_game_input(on); }, [](float s, const char* t) { return H->text_width(s, t); },
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
};

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

// The asset part: textures/<resource id>.png. An opaque PNG replaces the texture; one with transparency is a layer
// drawn over it (the game does the difference, see override_texture_png).
void applyTextures(CtwMod& m) {
    std::error_code ec;
    for (auto& e : fs::directory_iterator(fs::u8path(m.dir) / "textures", ec)) {
        if (e.path().extension() != ".png") continue;
        char* end = nullptr;
        std::string stem = e.path().stem().string();
        long id = strtol(stem.c_str(), &end, 10);
        if (end && *end == 0 && id >= 0) api_override_texture(&m, (int)id, e.path().u8string().c_str());
    }
}

void removeTextures(CtwMod& m) {   // switching an asset mod off takes effect right away
    for (int id : m.textures) H->restore_texture(id);
    m.textures.clear();
}

void loadMod(CtwMod& m) {
    applyTextures(m);
    if (!m.dllName.empty() && !m.codeLoaded && m.error.empty()) {
        std::string path = m.dir + m.dllName;
        m.lib = loadLib(path);
        if (!m.lib) { m.error = "could not load " + m.dllName; log("[" + m.name + "] " + m.error); return; }
        auto init = (CtwModInitFn)libSym(m.lib, "ctw_mod_init");
        m.shutdown = (CtwModShutdownFn)libSym(m.lib, "ctw_mod_shutdown");
        if (!init) { m.error = "no ctw_mod_init in " + m.dllName; log("[" + m.name + "] " + m.error); return; }
        int r = init(&m, &g_api);
        if (r != 0) { m.error = "ctw_mod_init returned " + std::to_string(r); log("[" + m.name + "] " + m.error); return; }
        m.codeLoaded = true;
    }
    log("loaded " + m.name + " " + m.version + (m.textures.empty() ? "" : " (" + std::to_string(m.textures.size()) + " textures)"));
}

void setEnabled(CtwMod& m, bool on) {
    m.enabled = on;
    if (on) loadMod(m);        // textures now; code mods start now if they never ran
    else removeTextures(m);    // code stops getting callbacks now and is unloaded at the next start
    saveEnabled();
}

// ------------------------------------------------------------------------------------------------ menu
void setOpen(bool open) {
    g_menuOpen = open;
    if (open) H->set_mouse_captured(0);
}

int onKey(int k, void*) {
    if (k == SC_F4 || (k == SC_ESCAPE && g_menuOpen)) {   // Esc closes the menu instead of quitting
        setOpen(!g_menuOpen);
        return 1;
    }
    for (auto& h : g_keyHooks)
        if (h.mod->enabled && h.fn(k, h.user)) return 1;
    return 0;   // everything else goes to the game: the player can move with the menu open
}

void onTick(void*) {
    for (auto& h : g_tickHooks)
        if (h.mod->enabled) h.fn(h.user);
}

const float kRow = 36.f, kSlider = 48.f, kHeader = 52.f;

float modHeight(const CtwMod& m) {
    float h = kHeader;
    if (!m.expanded) return h;
    h += 26.f;                                                      // description
    if (!m.error.empty() || (!m.enabled && m.codeLoaded)) h += 24.f;
    for (auto& it : m.items) h += it.kind == CtwMod::Item::Slider ? kSlider : kRow;
    return h + 10.f;
}

void drawMenu() {
    CtwUi& ui = g_ui;
    ctw_ui_begin(&ui, &g_api);
    const float W = (float)H->screen_width(), Hh = (float)H->screen_height();
    const float pw = std::min(420.f, W - 40.f), px = 20.f, py = 20.f, ph = Hh - 40.f;
    char count[48];
    snprintf(count, sizeof count, "%d installed", (int)g_mods.size());
    if (ctw_ui_panel(&ui, px, py, pw, ph, "MODS", count)) { setOpen(false); return; }

    const float lx = px + 10.f, ly = py + 50.f, lw = pw - 20.f, lh = ph - 50.f - 34.f;
    float content = 0.f;
    for (auto& m : g_mods) content += modHeight(*m) + 8.f;
    if (g_mods.empty()) content = 0.f;
    ctw_ui_scroll_begin(&ui, 1, &g_scroll, lx, ly, lw, lh, content);
    const float cw = lw - 14.f;   // room for the scrollbar
    float y = ly - g_scroll.pos;
    if (g_mods.empty()) {
        ctw_ui_text(&ui, lx + 10.f, ly + 10.f, 1.f, CTW_UI_DIM, "No mods installed.");
        ctw_ui_text(&ui, lx + 10.f, ly + 34.f, 1.f, CTW_UI_DIM, "Put each mod in its own folder");
        ctw_ui_text(&ui, lx + 10.f, ly + 56.f, 1.f, CTW_UI_DIM, "inside the game's mods folder.");
    }
    int index = 0;
    const bool inList = ctw_ui_in(&ui, lx, ly, lw, lh);
    CtwUi clickless = ui;   // rows scrolled out of view must not react
    clickless.clicked = 0;
    for (auto& mp : g_mods) {
        CtwMod& m = *mp;
        const float mh = modHeight(m);
        int id = 1000 + 64 * index++;   // slider ids, stable while scrolling
        if (y + mh < ly || y > ly + lh) { y += mh + 8.f; continue; }
        CtwUi& u = inList ? ui : clickless;
        H->draw_rect(lx, y, cw, mh, 0xFFFFFF0Cu);
        // header: expand on click, switch on the right
        const bool hot = ctw_ui_in(&u, lx, y, cw - 70.f, kHeader);
        if (hot) H->draw_rect(lx, y, cw, kHeader, CTW_UI_HOVER);
        if (hot && u.clicked) m.expanded = !m.expanded;
        H->draw_text(lx + 12.f, ctw_ui_text_y(&u, y, kHeader, 1.1f), 1.1f, CTW_UI_DIM, m.expanded ? "-" : "+");
        std::string title = m.name + (m.version.empty() ? "" : "  " + m.version);
        ctw_ui_text_fit(&u, lx + 32.f, y + 9.f, 1.15f, CTW_UI_TEXT, title.c_str(), cw - 120.f);
        const char* kind = !m.error.empty() ? "error" : m.dllName.empty() ? "asset mod" : "code mod";
        ctw_ui_text(&u, lx + 32.f, y + 31.f, 0.85f, !m.error.empty() ? 0xE05050FFu : CTW_UI_DIM, kind);
        int on = m.enabled ? 1 : 0;
        if (ctw_ui_switch(&u, lx + cw - 70.f, y, 70.f, kHeader, "", &on)) setEnabled(m, on != 0);
        float ry = y + kHeader;
        if (m.expanded) {
            std::string about = (m.author.empty() ? "" : "by " + m.author + " - ") + m.description;
            ctw_ui_text_fit(&u, lx + 12.f, ry + 4.f, 0.9f, CTW_UI_DIM, about.c_str(), cw - 24.f);
            ry += 26.f;
            if (!m.error.empty()) { ctw_ui_text(&u, lx + 12.f, ry + 2.f, 0.9f, 0xE05050FFu, m.error.c_str()); ry += 24.f; }
            else if (!m.enabled && m.codeLoaded) { ctw_ui_text(&u, lx + 12.f, ry + 2.f, 0.9f, CTW_UI_DIM, "Fully off after a restart."); ry += 24.f; }
            CtwUi& iu = m.enabled && m.codeLoaded ? u : clickless;
            for (auto& it : m.items) {
                if (it.kind == CtwMod::Item::Toggle) {
                    ctw_ui_switch(&iu, lx + 6.f, ry, cw - 12.f, kRow, it.label.c_str(), it.ival);
                    ry += kRow;
                } else if (it.kind == CtwMod::Item::Slider) {
                    ctw_ui_slider(&iu, id++, lx + 6.f, ry, cw - 12.f, kSlider - 4.f, it.label.c_str(), it.fval, it.mn, it.mx, it.step);
                    ry += kSlider;
                } else {
                    if (ctw_ui_button(&iu, lx + 16.f, ry + 3.f, cw - 32.f, kRow - 6.f, it.label.c_str())) it.fn(it.user);
                    ry += kRow;
                }
            }
        }
        ui.drag = u.drag;   // keep a slider drag that started in this frame
        y += mh + 8.f;
    }
    ctw_ui_scroll_end(&ui);
    ctw_ui_text(&ui, px + 14.f, py + ph - 26.f, 0.9f, CTW_UI_DIM, "Click a mod for its options.  F4: close");
}

void onHud(void*) {
    for (auto& h : g_hudHooks)
        if (h.mod->enabled) h.fn(h.user);
    if (g_menuOpen) drawMenu();
}
}  // namespace

extern "C" CTW_PLUGIN_EXPORT int ctw_plugin_init(const CtwHostApi* host) {
    if (!host || host->version < CTW_PLUGIN_API_VERSION || host->size < sizeof(CtwHostApi)) return 1;
    H = host;
    g_modsDir = H->mods_dir();
    auto enabled = readIni(fs::u8path(g_modsDir) / "enabled.ini");
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
        auto en = enabled.find(m->folder);
        m->enabled = en == enabled.end() || en->second != "0";
        g_mods.push_back(std::move(m));
    }
    for (auto& m : g_mods)
        if (m->enabled) loadMod(*m);
    if (!g_mods.empty()) log("mod menu: " + std::to_string(g_mods.size()) + " mods, API version " + std::to_string(CTW_MOD_API_VERSION));
    H->on_key(onKey, nullptr);
    H->on_tick(onTick, nullptr);
    H->on_draw_hud(onHud, nullptr);
    return 0;
}

extern "C" CTW_PLUGIN_EXPORT void ctw_plugin_shutdown(void) {
    for (auto& m : g_mods) {
        if (m->codeLoaded && m->shutdown) m->shutdown();
        if (m->lib) freeLib(m->lib);
    }
    g_mods.clear();
    g_tickHooks.clear();
    g_hudHooks.clear();
    g_keyHooks.clear();
}
