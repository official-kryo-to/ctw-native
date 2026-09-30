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

#define CTW_PLUGIN_API_VERSION 3

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
} CtwHostApi;

/* Exported by a plugin DLL. Return 0 from ctw_plugin_init to stay loaded. */
typedef int (*CtwPluginInitFn)(const CtwHostApi* host);
typedef void (*CtwPluginShutdownFn)(void);

#ifdef __cplusplus
}
#endif

#endif /* CTW_PLUGIN_H */
