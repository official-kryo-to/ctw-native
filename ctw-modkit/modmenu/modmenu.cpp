// ModMenu.dll - the mod loader and in-game mod menu (F4) for the GTA: Chinatown Wars PC port.
//
// A plugin (ctw_plugin.h): the game loads it from its mods folder. It then finds mods in mods/<ModName>/ folders
// (mod.ini), applies asset mods (textures/<resource id>.png), loads code mods (DLLs using ctw_mod.h) and gives
// them the mod API, which it implements on top of the game's plugin interface.
#include "ctw_mod.h"
#include "ctw_plugin.h"
#include <algorithm>
#include <cstdio>
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

// SDL scancodes used by the menu
enum { SC_RETURN = 40, SC_ESCAPE = 41, SC_BACKSPACE = 42, SC_F4 = 61, SC_RIGHT = 79, SC_LEFT = 80, SC_DOWN = 81, SC_UP = 82 };

struct CtwMod {   // the opaque handle mods receive
    std::string folder, dir, name, author, version, description, dllName;
    bool enabled = true;
    bool codeLoaded = false;       // the DLL is running (only switches off after a restart)
    std::vector<int> textures;     // texture overrides currently applied
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
std::vector<Hook> g_tickHooks, g_hudHooks;

bool g_menuOpen = false;
int g_menuMod = -1;       // -1 = mod list, else index into g_mods
int g_sel = 0;
bool g_dirty = false;     // enabled state changed (saved on close)

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
int api_override_texture(CtwMod* m, int id, const char* png) {
    if (!png) return 0;
    std::string path = png;
    if (m && fs::path(path).is_relative()) path = m->dir + path;
    if (!H->override_texture_png(id, path.c_str())) return 0;
    if (m && std::find(m->textures.begin(), m->textures.end(), id) == m->textures.end()) m->textures.push_back(id);
    return 1;
}
int api_key_down(int sc) { return H->key_down(sc); }
int api_key_pressed(int sc) { return H->key_pressed(sc); }

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
};

// ------------------------------------------------------------------------------------------------ loading
void* loadLib(const std::string& path) {
#ifdef _WIN32
    return (void*)LoadLibraryA(path.c_str());
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

void applyTextures(CtwMod& m) {   // the asset part: textures/<resource id>.png
    std::error_code ec;
    for (auto& e : fs::directory_iterator(fs::path(m.dir) / "textures", ec)) {
        if (e.path().extension() != ".png") continue;
        char* end = nullptr;
        std::string stem = e.path().stem().string();
        long id = strtol(stem.c_str(), &end, 10);
        if (end && *end == 0 && id >= 0) api_override_texture(&m, (int)id, e.path().string().c_str());
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

// ------------------------------------------------------------------------------------------------ menu
int menuCount() {
    if (g_menuMod < 0) return (int)g_mods.size();
    return 1 + (int)g_mods[g_menuMod]->items.size();   // row 0 = enabled switch
}

void adjust(CtwMod::Item& it, int dir) {
    if (it.kind == CtwMod::Item::Toggle) *it.ival = !*it.ival;
    else if (it.kind == CtwMod::Item::Slider) *it.fval = std::max(it.mn, std::min(it.mx, *it.fval + dir * it.step));
    else if (it.kind == CtwMod::Item::Button && dir == 0) it.fn(it.user);
}

void setOpen(bool open) {
    g_menuOpen = open;
    H->set_game_input(open ? 0 : 1);
    if (!open && g_dirty) { saveEnabled(); g_dirty = false; }
}

int onKey(int k, void*) {
    if (k == SC_F4) {
        setOpen(!g_menuOpen);
        g_menuMod = -1;
        g_sel = 0;
        return 1;
    }
    if (!g_menuOpen) return 0;
    int n = menuCount();
    if (k == SC_UP && n) g_sel = (g_sel + n - 1) % n;
    else if (k == SC_DOWN && n) g_sel = (g_sel + 1) % n;
    else if (k == SC_ESCAPE || k == SC_BACKSPACE) {
        if (g_menuMod >= 0) { g_sel = g_menuMod; g_menuMod = -1; }
        else setOpen(false);
    } else if (g_menuMod < 0) {
        if ((k == SC_RETURN || k == SC_RIGHT) && n) { g_menuMod = g_sel; g_sel = 0; }
    } else {
        CtwMod& m = *g_mods[g_menuMod];
        int dir = k == SC_LEFT ? -1 : k == SC_RIGHT ? 1 : (k == SC_RETURN ? 0 : 99);
        if (dir == 99) return 1;
        if (g_sel == 0) {
            m.enabled = !m.enabled;
            g_dirty = true;
            if (m.enabled) loadMod(m);     // textures now; code mods start now if they never ran
            else removeTextures(m);        // code keeps running until the game restarts
        } else if (m.codeLoaded) adjust(m.items[g_sel - 1], dir);
    }
    return 1;   // the open menu takes every key
}

void onTick(void*) {
    for (auto& h : g_tickHooks)
        if (h.mod->enabled) h.fn(h.user);
}

void onHud(void*) {
    for (auto& h : g_hudHooks)
        if (h.mod->enabled) h.fn(h.user);
    if (!g_menuOpen) return;

    int W = H->screen_width();
    auto text = [](float x, float y, float s, uint32_t c, const std::string& t) { H->draw_text(x, y, s, c, t.c_str()); };
    auto width = [](const std::string& t, float s) { return H->text_width(s, t.c_str()); };
    const float s = 1.25f, lh = H->line_height(s) + 6.f;
    float pw = std::min(760.f, W - 40.f), x = (W - pw) / 2, y = 60.f;
    int n = menuCount();
    float ph = 90.f + lh * std::max(n, 1) + 60.f;
    H->draw_rect(x, y, pw, ph, 0x000000C8u);
    H->draw_rect(x, y, pw, 4.f, 0xE0B020FFu);
    if (g_menuMod < 0) {
        text(x + 20, y + 16, 1.6f, 0xFFFFFFFFu, "MODS");
        text(x + pw - 20 - width("F4: close", 1.f), y + 22, 1.f, 0xB4B4B4FFu, "F4: close");
        if (g_mods.empty()) text(x + 20, y + 70, s, 0xB4B4B4FFu, "No mods installed. Put mods in the \"mods\" folder.");
        for (int i = 0; i < n; ++i) {
            CtwMod& m = *g_mods[i];
            float ry = y + 70 + i * lh;
            if (i == g_sel) H->draw_rect(x + 10, ry - 3, pw - 20, lh, 0xE0B02060u);
            text(x + 24, ry, s, 0xFFFFFFFFu, m.name + (m.version.empty() ? "" : "  " + m.version));
            bool restart = !m.enabled && m.codeLoaded;
            std::string st = restart ? "OFF (after restart)" : !m.enabled ? "OFF" : !m.error.empty() ? "ERROR" : "ON";
            uint32_t col = !m.enabled ? 0x808080FFu : !m.error.empty() ? 0xCD1212FFu : 0x2CCD12FFu;
            text(x + pw - 24 - width(st, s), ry, s, col, st);
        }
        text(x + 20, y + ph - 34, 1.f, 0xB4B4B4FFu, "Up/Down: choose   Enter: open   Esc: close");
    } else {
        CtwMod& m = *g_mods[g_menuMod];
        text(x + 20, y + 16, 1.6f, 0xFFFFFFFFu, m.name);
        std::string by = (m.author.empty() ? "" : "by " + m.author + "   ") + m.description;
        while (by.size() > 4 && width(by, 1.f) > pw - 40) by = by.substr(0, by.size() - 4) + "...";
        text(x + 20, y + 50, 1.f, 0xB4B4B4FFu, by);
        for (int i = 0; i < n; ++i) {
            float ry = y + 80 + i * lh;
            if (i == g_sel) H->draw_rect(x + 10, ry - 3, pw - 20, lh, 0xE0B02060u);
            std::string label, value;
            if (i == 0) {
                label = "Enabled";
                value = m.enabled ? "ON" : "OFF";
                if (!m.enabled && m.codeLoaded) value += " (after restart)";
            } else {
                CtwMod::Item& it = m.items[i - 1];
                label = it.label;
                if (it.kind == CtwMod::Item::Toggle) value = *it.ival ? "ON" : "OFF";
                else if (it.kind == CtwMod::Item::Slider) { char b[32]; snprintf(b, sizeof b, "< %.2f >", *it.fval); value = b; }
                else value = "[Enter]";
            }
            text(x + 24, ry, s, m.codeLoaded || i == 0 ? 0xFFFFFFFFu : 0x808080FFu, label);
            text(x + pw - 24 - width(value, s), ry, s, 0xE0B020FFu, value);
        }
        if (!m.error.empty()) text(x + 20, y + ph - 58, 1.f, 0xCD1212FFu, m.error);
        text(x + 20, y + ph - 34, 1.f, 0xB4B4B4FFu, "Up/Down: choose   Left/Right/Enter: change   Esc: back");
    }
}
}  // namespace

extern "C" CTW_PLUGIN_EXPORT int ctw_plugin_init(const CtwHostApi* host) {
    if (!host || host->version < 1) return 1;
    H = host;
    g_modsDir = H->mods_dir();
    log("mod menu: mod API version " + std::to_string(CTW_MOD_API_VERSION));
    auto enabled = readIni(fs::path(g_modsDir) / "enabled.ini");
    std::vector<fs::path> dirs;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(g_modsDir, ec))
        if (e.is_directory() && fs::exists(e.path() / "mod.ini")) dirs.push_back(e.path());
    std::sort(dirs.begin(), dirs.end());
    for (auto& d : dirs) {
        auto ini = readIni(d / "mod.ini");
        auto m = std::make_unique<CtwMod>();
        m->folder = d.filename().string();
        m->dir = d.string() + "/";
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
    saveEnabled();
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
}
