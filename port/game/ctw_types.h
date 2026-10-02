// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
/* Shared C data types. Keep identical to port/game/ctw_types.h. */
#ifndef CTW_TYPES_H
#define CTW_TYPES_H
#include <stdint.h>

/* Runtime vehicle handle, independent of its model id and array index. 0 = invalid.
   Handles expire on removal and are never reused during a game session. */
typedef uint32_t CtwVehicle;

enum CtwVehicleFlags {
    CTW_VEHICLE_DEAD = 1, CTW_VEHICLE_ENGINE_ON = 2, CTW_VEHICLE_PERSISTENT = 4,
    CTW_VEHICLE_BIKE = 8, CTW_VEHICLE_PLAYER = 16, CTW_VEHICLE_PHYSICS = 32
};
enum CtwPlayerFlags { CTW_PLAYER_DEAD = 1, CTW_PLAYER_IN_VEHICLE = 2, CTW_PLAYER_ENTERING_EXITING = 4 };
enum CtwCapabilities {
    CTW_CAP_LIVE_VEHICLES = 1, CTW_CAP_PLAYER_APPEARANCE = 2, CTW_CAP_WORLD_QUERIES = 4,
    CTW_CAP_TRAFFIC = 8, CTW_CAP_RADIO = 16, CTW_CAP_GAME_EVENTS = 32, CTW_CAP_PEDESTRIANS = 64
};

/* Set size = sizeof(your struct) before a get_* call. Undersized outputs are rejected.
   Positions: world units, z up. Velocities: units/second. Headings: degrees CCW from +y.
   Game-owned strings remain valid until their subsystem is reloaded/shut down. */
typedef struct CtwVehicleState {
    uint32_t size;
    CtwVehicle vehicle;
    int32_t model_id;               /* vehicle catalogue id (vehicle_count), not a model resource id */
    float position[3], velocity[3], heading;
    int32_t health;                 /* 0..255; a dead wreck cannot be repaired */
    int32_t palette;                /* 0..26 */
    uint32_t flags;
    int32_t radio_station;
} CtwVehicleState;

typedef struct CtwPlayerState {
    uint32_t size;
    float position[3], velocity[3], heading;
    int32_t body_set, palette_upper, palette_legs;
    uint32_t flags;
    CtwVehicle vehicle;
} CtwPlayerState;

/* A random pedestrian. Ids are never reused in a game session; the list index can change between ticks. */
enum CtwPedFlags { CTW_PED_KNOCKED_DOWN = 1, CTW_PED_MALE = 2 };
typedef struct CtwPedState {
    uint32_t size;
    uint32_t id;
    int32_t ped_type, ped_subtype;  /* pedinfo.bin type (1 civilian, 10 cop, gangs...) and subtype */
    float position[3], heading;
    uint32_t flags;
} CtwPedState;

/* How the picture is drawn (a PC addition). All fields at their neutral value (zero, or one for the scales) is
   the game's own look. */
typedef struct CtwRenderStyle {
    uint32_t size;
    float light_pools;              /* 0..2: street lights light the ground under them at night */
    float headlight_pools;          /* 0..2: car headlights light the road ahead at night */
    float tint[3];                  /* multiplied into the picture; 1, 1, 1 = none */
    float sepia;                    /* 0..1 */
    float saturation;               /* 0..2, 1 = unchanged */
    float contrast;                 /* 0.5..2, 1 = unchanged */
    float brightness;               /* -0.5..0.5, 0 = unchanged */
    float vignette;                 /* 0..1: darker corners */
} CtwRenderStyle;

typedef struct CtwGround {
    uint32_t size;
    float height, normal[3];
    int32_t surface;                /* 0 solid, 2 water */
} CtwGround;

enum CtwGameEventType {
    CTW_EVENT_VEHICLE_SPAWNED = 1, CTW_EVENT_VEHICLE_REMOVED = 2,
    CTW_EVENT_VEHICLE_DESTROYED = 3, CTW_EVENT_PLAYER_ENTERED_VEHICLE = 4,
    CTW_EVENT_PLAYER_EXITED_VEHICLE = 5, CTW_EVENT_RADIO_CHANGED = 6,
    CTW_EVENT_PED_KNOCKED_DOWN = 7   /* vehicle = the car, value = the ped's id */
};
typedef struct CtwGameEvent {
    uint32_t size, type;
    CtwVehicle vehicle;
    int32_t model_id;
    int32_t value;                  /* RADIO_CHANGED: station id; PED_KNOCKED_DOWN: ped id; otherwise 0 */
} CtwGameEvent;
typedef void (*CtwGameEventCallback)(const CtwGameEvent* event, void* user);
#endif
