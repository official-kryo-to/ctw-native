/*
 * GTA: Chinatown Wars PC port - mod SDK (public interface)
 *
 * This header is all a mod needs. The game's source code is not part of the SDK: mods talk to the game only
 * through the function table below (plain C, so mods can be written in C, C++, or anything that can build a
 * DLL with the C calling convention).
 *
 * A mod is a folder inside the game's "mods" folder:
 *
 *   mods/<ModName>/mod.ini          required: name, author, version, description, optional dll=<file>
 *   mods/<ModName>/<file>.dll       optional: code (exports ctw_mod_init / ctw_mod_shutdown, see below)
 *   mods/<ModName>/textures/N.png   optional: replaces game texture resource N (any size, RGBA PNG)
 *
 * Mods without a DLL are "asset mods" (texture replacements). Mods are loaded by the mod menu (ModMenu.dll in
 * the game's mods folder, part of this mod kit); switching them on/off happens in the mod menu (F4) and is
 * remembered in mods/enabled.ini.
 *
 * Versioning: the game passes CTW_MOD_API_VERSION in api->version. New functions are only ever appended to the
 * end of CtwApi, so a mod built against an older header keeps working. Check api->size before using a function
 * that was added later than the version you need.
 */
#ifndef CTW_MOD_H
#define CTW_MOD_H

#include <stdint.h>

#define CTW_MOD_API_VERSION 1

#ifdef _WIN32
#define CTW_MOD_EXPORT __declspec(dllexport)
#else
#define CTW_MOD_EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CtwMod CtwMod;   /* opaque handle identifying the calling mod */

#ifndef CTW_CALLBACK_DEFINED
#define CTW_CALLBACK_DEFINED
typedef void (*CtwCallback)(void* user);
#endif

/* Colours are 0xRRGGBBAA. HUD coordinates are pixels, origin top-left of the window. */
typedef struct CtwApi {
    uint32_t version;   /* CTW_MOD_API_VERSION of the running game */
    uint32_t size;      /* sizeof(CtwApi) in the running game */

    /* --- general ------------------------------------------------------------------------------------- */
    void (*log)(CtwMod* mod, const char* text);           /* writes to the game log (mods/log.txt) */
    const char* (*mod_dir)(CtwMod* mod);                  /* this mod's folder, with a trailing slash */
    uint32_t (*frame_count)(void);                        /* game frames since start (30 per second) */

    /* --- time of day / weather ------------------------------------------------------------------------- */
    float (*get_time_of_day)(void);                       /* hours, 0..24 */
    void (*set_time_of_day)(float hours);
    int (*get_clock_running)(void);
    void (*set_clock_running)(int running);
    int (*get_weather)(void);                             /* 0..7 */
    void (*set_weather)(int weather);

    /* --- player ----------------------------------------------------------------------------------------- */
    void (*get_player_position)(float out_xyz[3]);        /* world units, z up */
    void (*set_player_position)(const float xyz[3]);
    float (*get_player_heading)(void);                    /* degrees, 0 = +y (north) */

    /* --- camera ----------------------------------------------------------------------------------------- */
    float (*get_camera_height)(void);                     /* follow-camera height above the player */
    void (*set_camera_height)(float units);

    /* --- mod menu (F4): options appear under this mod's name ------------------------------------------ */
    void (*menu_add_toggle)(CtwMod* mod, const char* label, int* value);
    void (*menu_add_slider)(CtwMod* mod, const char* label, float* value, float min, float max, float step);
    void (*menu_add_button)(CtwMod* mod, const char* label, CtwCallback fn, void* user);

    /* --- events ------------------------------------------------------------------------------------------ */
    void (*on_tick)(CtwMod* mod, CtwCallback fn, void* user);       /* every game frame (30 per second) */
    void (*on_draw_hud)(CtwMod* mod, CtwCallback fn, void* user);   /* every rendered frame, HUD drawing allowed */

    /* --- HUD drawing (only inside an on_draw_hud callback) ------------------------------------------- */
    void (*draw_text)(float x, float y, float scale, uint32_t rgba, const char* utf8);
    void (*draw_rect)(float x, float y, float w, float h, uint32_t rgba);
    int (*screen_width)(void);
    int (*screen_height)(void);

    /* --- assets ----------------------------------------------------------------------------------------- */
    int (*override_texture_png)(CtwMod* mod, int resource_id, const char* png_path);   /* 1 = ok */

    /* --- input (SDL scancodes: https://wiki.libsdl.org/SDL2/SDL_Scancode) ----------------------------- */
    int (*key_down)(int scancode);
    int (*key_pressed)(int scancode);                     /* went down since the previous game frame */
} CtwApi;

/*
 * Exported by the mod DLL:
 *   ctw_mod_init      called once after loading; keep `api` and `mod`, register menu items and events here.
 *                     Return 0 on success, anything else to refuse loading.
 *   ctw_mod_shutdown  optional; called before the game exits or the mod is unloaded.
 */
typedef int (*CtwModInitFn)(CtwMod* mod, const CtwApi* api);
typedef void (*CtwModShutdownFn)(void);

#ifdef __cplusplus
}
#endif

#endif /* CTW_MOD_H */
