// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
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
 *   mods/<ModName>/textures/N.png   optional: game texture resource N. Opaque PNG: replaces it (any size).
 *                                   PNG with transparency: a layer over it (the game's art shows through).
 *
 * A folder can contain textures, a DLL, or both. Mods are loaded by the mod menu (ModMenu.dll in the game's
 * mods folder); switching them on/off happens in the mod menu (F4) and is remembered in
 * mods/enabled.ini. See README.md in the mod kit for the modding guide.
 *
 * Versioning: the game passes CTW_MOD_API_VERSION in api->version. New functions are only ever appended to the
 * end of CtwApi, so a mod built against an older header keeps working. Check api->size before using a function
 * that was added later than the version you need.
 */
#ifndef CTW_MOD_H
#define CTW_MOD_H

#include <stdint.h>
#include <stddef.h>
#include "ctw_types.h"

#define CTW_MOD_API_VERSION 5
#define CTW_MOD_HAS(api, member) ((api) && (api)->size >= offsetof(CtwApi, member) + sizeof((api)->member) && (api)->member)

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
typedef int (*CtwKeyCallback)(int scancode, void* user);   /* return 1 to swallow the key */
typedef void (*CtwEnabledCallback)(int enabled, void* user);

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
    int (*key_down)(int scancode);                        /* held right now */
    int (*key_pressed)(int scancode);                     /* went down since the previous game frame */

    /* --- version 2 (check api->size before using) ------------------------------------------------------ */
    void (*on_key)(CtwMod* mod, CtwKeyCallback fn, void* user);     /* key presses; return 1 to take the key */
    void (*set_game_input)(int enabled);                  /* 0: the player ignores the keyboard (your own UI) */
    float (*text_width)(float scale, const char* utf8);
    float (*line_height)(float scale);

    int (*vehicle_count)(void);                           /* vehicle ids are 0 .. count-1 */
    const char* (*vehicle_name)(int id);                  /* the game's internal name, NULL for a bad id */
    int (*vehicle_spawnable)(int id);                     /* 1 = a car or bike the player can drive */
    int (*spawn_vehicle)(int id);                         /* in front of the player (or their vehicle); 1 = ok */

    void (*set_speed_scale)(float scale);                 /* player speed on foot and in a vehicle, 1 = normal */
    float (*get_speed_scale)(void);
    void (*set_game_speed)(float scale);                  /* simulation speed including the clock, 1 = normal */
    float (*get_game_speed)(void);

    /* Replaces the game camera while enabled; call every frame with the pose you want. Angles in degrees:
       yaw 0 = looking along +y (north), pitch negative = looking down. */
    void (*set_free_camera)(int enabled, const float eye_xyz[3], float yaw_deg, float pitch_deg);
    /* Draws a PNG over a rectangle of a game texture (x, y, w, h are fractions 0..1 of the texture). Undone when the
       mod is switched off. Transparent PNG pixels keep the game's artwork. */
    int (*patch_texture_png)(CtwMod* mod, int resource_id, float x, float y, float w, float h, const char* png_path);

    /* --- version 3: mouse, text input, clipping, vehicle pictures (see ctw_ui.h for ready-made widgets) ----- */
    /* Input is collected once per rendered frame: read it inside an on_draw_hud callback. */
    void (*get_mouse)(float* x, float* y);                /* HUD pixels */
    int (*mouse_down)(int button);                        /* 0 left, 1 middle, 2 right */
    int (*mouse_clicked)(int button);                     /* pressed since the previous rendered frame */
    float (*mouse_wheel)(void);                           /* notches this frame, + = away from the user */
    void (*set_mouse_captured)(int captured);             /* hide and lock the cursor for mouse look */
    void (*mouse_delta)(float* dx, float* dy);            /* movement this frame while captured */
    const char* (*text_input)(void);                      /* UTF-8 typed this frame, '\b' = backspace */
    void (*draw_vehicle)(int id, float x, float y, float w, float h, float yaw_deg);   /* a vehicle picture */
    void (*set_clip)(float x, float y, float w, float h); /* HUD drawing only inside this rectangle; w <= 0 = off */
    void (*get_camera)(float eye_xyz[3], float* yaw_deg, float* pitch_deg);   /* the camera being shown */
    int (*menu_is_open)(void);                            /* the F4 mod menu is showing (it uses the mouse) */
    double (*time_seconds)(void);                         /* real time, for smooth movement at any frame rate */
    void (*set_render_distance)(float units);             /* how far the city is loaded around the camera (120+) */
    float (*get_render_distance)(void);                   /* the game's own is 120 */

    /* --- version 4: gameplay. Check version, size and get_capabilities. Mutations return
       1 on success, 0 for invalid data/handles or an unavailable subsystem. Spawn returns 0 on failure.
       Call on the game thread, in callbacks. Positions must be finite within +/-32768 units,
       velocity components within +/-512 units/s. State getters require out->size. --- */
    uint32_t (*get_capabilities)(void);
    int (*live_vehicle_count)(void);
    CtwVehicle (*live_vehicle_at)(int index);
    int (*get_vehicle_state)(CtwVehicle vehicle, CtwVehicleState* out);
    CtwVehicle (*spawn_vehicle_at)(int model_id, const float xyz[3], float heading, int palette);
    int (*remove_vehicle)(CtwVehicle vehicle); /* refuses occupied cars and active enter/exit targets */
    int (*set_vehicle_transform)(CtwVehicle vehicle, const float xyz[3], float heading); /* upright, stops motion */
    int (*set_vehicle_velocity)(CtwVehicle vehicle, const float xyz[3]); /* detaches traffic's rail controller */
    int (*set_vehicle_palette)(CtwVehicle vehicle, int palette);
    int (*set_vehicle_persistent)(CtwVehicle vehicle, int persistent); /* bypass distance cleanup */
    int (*set_vehicle_engine)(CtwVehicle vehicle, int running);
    int (*set_vehicle_door)(CtwVehicle vehicle, int seat, int open); /* seats 0..3 */
    int (*damage_vehicle)(CtwVehicle vehicle, int amount); /* original damage/fire/death behavior */
    int (*repair_vehicle)(CtwVehicle vehicle); /* living cars only; clears smoke/fire */
    int (*get_player_state)(CtwPlayerState* out);
    int (*set_player_heading)(float heading); /* on foot, outside enter/exit animations */
    int (*set_player_appearance)(int body_set, int palette_upper, int palette_legs);
    int (*player_enter_vehicle)(CtwVehicle vehicle); /* starts normal animation; within 10 units */
    int (*player_exit_vehicle)(void); /* starts normal exit/braking; rejects excessive speed */
    int (*get_ground)(const float xyz[3], CtwGround* out);
    int (*line_hits_world_boxes)(const float from[3], const float to[3]); /* -1 unavailable/invalid, 0 clear, 1 hit; boxes only */
    float (*get_traffic_density)(void);
    int (*set_traffic_density)(float scale); /* 0..4; 0 stops new moving traffic, existing cars continue */
    int (*radio_station_count)(void); /* includes radio off */
    const char* (*radio_station_name)(int station);
    int (*radio_station_available)(int station);
    int (*get_radio_station)(void);
    int (*set_radio_station)(int station); /* requires a car with a radio */
    int (*get_radio_volume)(void); /* 0..10 */
    int (*set_radio_volume)(int volume);
    void (*on_game_event)(CtwMod* mod, CtwGameEventCallback fn, void* user); /* enabled mods only; queued, next tick; data valid during callback */
    void (*consume_mouse_wheel)(void); /* inside on_draw_hud: mark this frame's wheel as used by your UI */
    void (*on_enabled_changed)(CtwMod* mod, CtwEnabledCallback fn, void* user); /* toggled in F4; called even when disabling, for cleanup */

    /* --- version 5: custom cameras, saved settings and more menu items. The three camera functions need a
       version 5 game: check them with CTW_MOD_HAS (they are NULL on an older game). --- */
    /* On foot, WASD moves relative to this yaw (degrees, as get_camera) instead of the game camera's. Turn it off
       when your camera stops. */
    void (*set_control_yaw)(int enabled, float yaw_deg);
    void (*set_camera_fov)(float degrees);                /* vertical field of view, 20..120; 0 = the game's own */
    /* Nearest static world hit (buildings, street furniture shapes, ground) on a segment of at most 256 units.
       -1 unavailable/invalid, 0 clear, 1 hit; *fraction (optional) = 0..1 along the segment. */
    int (*world_line)(const float from[3], const float to[3], float* fraction);
    /* Numbers kept between game sessions in this mod's folder (settings.ini). Keys: letters, digits, '_' and '.'. */
    float (*get_setting)(CtwMod* mod, const char* key, float fallback);
    void (*set_setting)(CtwMod* mod, const char* key, float value);
    /* A row that steps through `count` options (the strings are copied); *value is the chosen index. */
    void (*menu_add_choice)(CtwMod* mod, const char* label, int* value, const char* const* options, int count);
    void (*menu_add_label)(CtwMod* mod, const char* text);  /* a heading between this mod's options */
    /* Random pedestrians (version 5): read-only for now. */
    int (*ped_count)(void);
    int (*get_ped_state)(int index, CtwPedState* out);
    float (*get_ped_density)(void);
    int (*set_ped_density)(float scale); /* 0..4; 0 stops new pedestrians, existing ones walk on */
    /* Called at the start of every rendered frame, before the world is drawn (input for the frame is ready).
       Place custom cameras here; positions read here are the interpolated ones the frame shows. No HUD drawing. */
    void (*on_frame_begin)(CtwMod* mod, CtwCallback fn, void* user);
    /* The look of the picture: light pools, colour grade (see CtwRenderStyle). NULL restores the game's own. */
    int (*set_render_style)(const CtwRenderStyle* style);
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
