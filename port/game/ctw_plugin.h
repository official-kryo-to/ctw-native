// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
/*
 * GTA: Chinatown Wars PC port - plugin interface (the game's side of the contract)
 *
 * The game itself has no mod menu. On start it loads every DLL placed directly in its "mods" folder as a plugin
 * and calls ctw_plugin_init with the function table below. The mod menu (ModMenu.dll, from the mod kit) is such a
 * plugin: it finds the mods in mods/<ModName>/ folders and gives them the mod API.
 *
 * This header is shared between the game and the mod kit: keep the copies identical
 * (port/game/ctw_plugin.h and ctw-modkit/include/ctw_plugin.h).
 * Versioning: functions are only ever appended to CtwHostApi; check `size` before using a later addition.
 */
#ifndef CTW_PLUGIN_H
#define CTW_PLUGIN_H

#include <stdint.h>
#include <stddef.h>
#include "ctw_types.h"

#define CTW_PLUGIN_API_VERSION 5
#define CTW_HOST_HAS(api, member) ((api) && (api)->size >= offsetof(CtwHostApi, member) + sizeof((api)->member) && (api)->member)

#ifdef _WIN32
#define CTW_PLUGIN_EXPORT __declspec(dllexport)
#else
#define CTW_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CTW_CALLBACK_DEFINED
#define CTW_CALLBACK_DEFINED
typedef void (*CtwCallback)(void* user);
#endif
typedef int (*CtwKeyCallback)(int scancode, void* user);   /* return 1 to swallow the key */

/* Colours are 0xRRGGBBAA. HUD coordinates are pixels, origin top-left of the window. */
typedef struct CtwHostApi {
    uint32_t version;   /* CTW_PLUGIN_API_VERSION of the running game */
    uint32_t size;      /* sizeof(CtwHostApi) in the running game */

    const char* (*mods_dir)(void);                        /* the game's mods folder, with a trailing slash */
    void (*log)(const char* text);                        /* appends a line to mods/log.txt */
    uint32_t (*frame_count)(void);                        /* game frames since start (30 per second) */

    float (*get_time_of_day)(void);                       /* hours, 0..24 */
    void (*set_time_of_day)(float hours);
    int (*get_clock_running)(void);
    void (*set_clock_running)(int running);
    int (*get_weather)(void);                             /* 0..7 */
    void (*set_weather)(int weather);

    void (*get_player_position)(float out_xyz[3]);        /* world units, z up */
    void (*set_player_position)(const float xyz[3]);
    float (*get_player_heading)(void);                    /* degrees, 0 = +y (north), counter-clockwise */

    float (*get_camera_height)(void);
    void (*set_camera_height)(float units);

    /* HUD drawing - only inside an on_draw_hud callback; text uses the game's own font */
    void (*draw_text)(float x, float y, float scale, uint32_t rgba, const char* utf8);
    void (*draw_rect)(float x, float y, float w, float h, uint32_t rgba);
    float (*text_width)(float scale, const char* utf8);
    float (*line_height)(float scale);
    int (*screen_width)(void);
    int (*screen_height)(void);

    int (*override_texture_png)(int resource_id, const char* png_path);   /* 1 = ok */
    void (*restore_texture)(int resource_id);                             /* undo an override */

    int (*key_down)(int scancode);                        /* SDL scancodes */
    int (*key_pressed)(int scancode);                     /* went down since the previous game frame */

    void (*on_tick)(CtwCallback fn, void* user);          /* every game frame */
    void (*on_draw_hud)(CtwCallback fn, void* user);      /* every rendered frame */
    void (*on_key)(CtwKeyCallback fn, void* user);        /* key presses, before the game sees them */
    void (*set_game_input)(int enabled);                  /* 0: the player ignores the keyboard (menus) */

    /* --- version 2 (check `size` before using) --- */
    int (*vehicle_count)(void);                           /* vehicle ids are 0 .. count-1 */
    const char* (*vehicle_name)(int id);                  /* the game's internal name, NULL for a bad id */
    int (*vehicle_spawnable)(int id);                     /* 1 = a car or bike the player can drive */
    int (*spawn_vehicle)(int id);                         /* in front of the player; 1 = ok */
    void (*set_speed_scale)(float scale);                 /* player speed on foot and in a vehicle, 1 = normal */
    float (*get_speed_scale)(void);
    void (*set_game_speed)(float scale);                  /* simulation speed including the clock, 1 = normal */
    float (*get_game_speed)(void);
    void (*set_free_camera)(int enabled, const float eye_xyz[3], float yaw_deg, float pitch_deg);
    /* Draws a PNG over a rectangle of a game texture; x, y, w, h are fractions (0..1) of the texture.
       restore_texture undoes patches as well as overrides. */
    int (*patch_texture_png)(int resource_id, float x, float y, float w, float h, const char* png_path);

    /* --- version 3: mouse, text input and vehicle pictures. Read input inside on_draw_hud; it is collected
           once per rendered frame. --- */
    void (*get_mouse)(float* x, float* y);                /* HUD pixels */
    int (*mouse_down)(int button);                        /* 0 left, 1 middle, 2 right */
    int (*mouse_clicked)(int button);                     /* pressed since the previous rendered frame */
    float (*mouse_wheel)(void);                           /* notches this frame, + = away from the user */
    void (*set_mouse_captured)(int captured);             /* hide and lock the cursor (mouse look) */
    void (*mouse_delta)(float* dx, float* dy);            /* movement this frame while captured */
    const char* (*text_input)(void);                      /* UTF-8 typed this frame, '\b' = backspace */
    void (*draw_vehicle)(int id, float x, float y, float w, float h, float yaw_deg);   /* the model, in a HUD rectangle */
    void (*set_clip)(float x, float y, float w, float h); /* HUD drawing only inside this rectangle; w <= 0: everywhere */
    void (*get_camera)(float eye_xyz[3], float* yaw_deg, float* pitch_deg);   /* the camera being shown */
    double (*time_seconds)(void);                         /* real time, for smooth movement at any frame rate */
    void (*set_render_distance)(float units);             /* how far the city is loaded around the camera (120+) */
    float (*get_render_distance)(void);

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
    void (*on_game_event)(CtwGameEventCallback fn, void* user); /* queued, delivered on next tick; data valid during callback */
    void (*consume_mouse_wheel)(void); /* inside on_draw_hud: mark this frame's wheel as used by your UI */

    /* --- version 5: camera control for custom cameras. Check version and size. --- */
    /* On foot, WASD moves relative to this yaw (degrees, as get_camera) instead of the game camera's. */
    void (*set_control_yaw)(int enabled, float yaw_deg);
    void (*set_camera_fov)(float degrees);                /* vertical field of view, 20..120; 0 = the game's own */
    /* Nearest static world hit (buildings, props' shapes, ground) on the segment. -1 unavailable/invalid, 0 clear,
       1 hit; *fraction (optional) = 0..1 along the segment. */
    int (*world_line)(const float from[3], const float to[3], float* fraction);
    /* Random pedestrians (version 5): read-only for now. */
    int (*ped_count)(void);
    int (*get_ped_state)(int index, CtwPedState* out);
    float (*get_ped_density)(void);
    int (*set_ped_density)(float scale); /* 0..4; 0 stops new pedestrians, existing ones walk on */
    /* Called at the start of every rendered frame, before the world is drawn (input for the frame is ready).
       Place custom cameras here; positions read here are the interpolated ones the frame shows. No HUD drawing. */
    void (*on_frame_begin)(CtwCallback fn, void* user);
    /* The look of the picture (see CtwRenderStyle); NULL restores the game's own. 0 if the style is invalid. */
    int (*set_render_style)(const CtwRenderStyle* style);
} CtwHostApi;

/* Exported by a plugin DLL. Return 0 from ctw_plugin_init to stay loaded. */
typedef int (*CtwPluginInitFn)(const CtwHostApi* host);
typedef void (*CtwPluginShutdownFn)(void);

#ifdef __cplusplus
}
#endif

#endif /* CTW_PLUGIN_H */
