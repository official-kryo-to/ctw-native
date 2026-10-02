/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kryo.to
 * See LICENSE in the repository root.
 *
 * Third-person camera, in the style of GTA IV: a camera over the player's shoulder that the mouse turns. On foot,
 * WASD moves relative to where it looks. In a vehicle it swings back behind the vehicle once the mouse rests, pulls
 * back and widens a little with speed, and C looks behind. It keeps out of walls and the ground, and gives the view
 * back to the game while the player is dead (the original death camera) or the feature is off.
 *
 * The camera is placed in on_frame_begin, before the world is drawn, from the interpolated positions the frame shows,
 * so it never lags a frame behind the vehicle. Not part of the original game, which only has its overhead cameras.
 * The player and pedestrians are layered sprites made for viewing from above, so the pitch is limited.
 */
#include "enhancements.h"
#include <math.h>

enum { SC_C = 6, SC_V = 25 };
#define PI 3.14159265f
#define MIN_PITCH -82.f
#define MAX_PITCH -12.f
#define DEFAULT_PITCH -34.f

/* options (saved) */
static int enabled = 0;
static float distance_foot = 6.f, distance_car = 10.f;
static float fov = 62.f, sensitivity = 0.15f, view_distance = 240.f, shoulder = 0.8f;
static int invert_y = 0, recenter = 1;
static const char* const recenter_names[] = {"Off", "Normal", "Quick"};

/* state */
static int available;          /* the game has the camera functions this needs */
static int active;             /* this feature is showing the view */
static float yaw, pitch = DEFAULT_PITCH, zoom = 1.f;
static float distance;         /* current camera distance after wall checks */
static float want_distance;    /* on foot or vehicle distance, blended when getting in and out */
static float side;             /* current shoulder offset, blended */
static float pivot[3];         /* smoothed point the camera orbits */
static float idle;             /* seconds since the mouse last turned the camera */
static float speed_fov;        /* extra field of view from speed, smoothed */
static float game_render_distance, applied_render_distance;

static float wrap(float degrees) {
    while (degrees > 180.f) degrees -= 360.f;
    while (degrees < -180.f) degrees += 360.f;
    return degrees;
}
static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static float ease(float rate, float dt) { return 1.f - expf(-rate * dt); }   /* frame-rate independent smoothing */

static void release(void) {
    if (!active) return;
    active = 0;
    api->set_free_camera(0, 0, 0.f, 0.f);
    api->set_control_yaw(0, 0.f);
    api->set_camera_fov(0.f);
    api->set_mouse_captured(0);
    api->set_render_distance(game_render_distance);
}

/* What the camera follows: the player or the driven vehicle. */
typedef struct Target { float pos[3], heading, speed; int in_vehicle; } Target;

static int target(Target* t) {
    CtwPlayerState p = {0};
    p.size = sizeof p;
    if (!api->get_player_state(&p) || (p.flags & CTW_PLAYER_DEAD)) return 0;
    t->in_vehicle = 0;
    t->heading = p.heading;
    t->speed = sqrtf(p.velocity[0] * p.velocity[0] + p.velocity[1] * p.velocity[1]);
    for (int i = 0; i < 3; ++i) t->pos[i] = p.position[i];
    if (p.vehicle && (p.flags & CTW_PLAYER_IN_VEHICLE)) {
        CtwVehicleState v = {0};
        v.size = sizeof v;
        if (api->get_vehicle_state(p.vehicle, &v)) {
            for (int i = 0; i < 3; ++i) t->pos[i] = v.position[i];
            t->heading = v.heading;
            t->speed = sqrtf(v.velocity[0] * v.velocity[0] + v.velocity[1] * v.velocity[1]);
            t->in_vehicle = 1;
        }
    }
    t->pos[2] += t->in_vehicle ? 1.8f : 1.6f;
    return 1;
}

static void begin(const Target* t) {
    float eye[3], game_pitch;
    api->get_camera(eye, &yaw, &game_pitch);   /* start facing the way the game camera faces: no jump */
    pitch = DEFAULT_PITCH;
    zoom = 1.f;
    want_distance = t->in_vehicle ? distance_car : distance_foot;
    distance = 0.f;                            /* eases out from the player */
    side = 0.f;
    for (int i = 0; i < 3; ++i) pivot[i] = t->pos[i];
    idle = speed_fov = 0.f;
    game_render_distance = api->get_render_distance();
    applied_render_distance = 0.f;
    active = 1;
}

static void look(float dt, const Target* t) {
    const int looking = !api->menu_is_open();   /* the F4 menu needs the cursor */
    api->set_mouse_captured(looking);
    float dx = 0.f, dy = 0.f;
    if (looking) {
        api->mouse_delta(&dx, &dy);
        const float wheel = api->mouse_wheel();
        if (wheel != 0.f) {
            zoom = clampf(zoom * (wheel > 0.f ? 0.9f : 1.1f), 0.4f, 2.5f);
            api->consume_mouse_wheel();
        }
    }
    yaw = wrap(yaw - dx * sensitivity);
    pitch = clampf(pitch - dy * sensitivity * (invert_y ? -1.f : 1.f), MIN_PITCH, MAX_PITCH);
    idle = dx != 0.f || dy != 0.f ? 0.f : idle + dt;
    /* in a moving vehicle, swing back behind it once the mouse has rested */
    if (t->in_vehicle && recenter && idle > 0.8f && t->speed > 2.f) {
        const float rate = (recenter == 1 ? 2.5f : 6.f) * clampf(t->speed / 15.f, 0.3f, 1.f);
        yaw = wrap(yaw + wrap(t->heading - yaw) * ease(rate, dt));
        pitch += (DEFAULT_PITCH + 6.f - pitch) * ease(rate * 0.5f, dt);
    }
}

static void place_camera(float dt, const Target* t) {
    /* the pivot follows the target closely: firmer on foot, a little lag in vehicles */
    const float follow = ease(t->in_vehicle ? 14.f : 22.f, dt);
    for (int i = 0; i < 3; ++i) {
        pivot[i] += (t->pos[i] - pivot[i]) * follow;
        if (fabsf(t->pos[i] - pivot[i]) > 4.f) pivot[i] = t->pos[i] + (pivot[i] > t->pos[i] ? 4.f : -4.f);
    }
    const int behind = t->in_vehicle && api->key_down(SC_C) && !api->menu_is_open();
    const float view_yaw = behind ? wrap(t->heading + 180.f) : yaw;
    const float yr = view_yaw * PI / 180.f, pr = pitch * PI / 180.f;
    const float fwd[3] = {-sinf(yr) * cosf(pr), cosf(yr) * cosf(pr), sinf(pr)};
    const float right[3] = {cosf(yr), sinf(yr), 0.f};
    const float base = t->in_vehicle ? distance_car * (1.f + clampf(t->speed / 50.f, 0.f, 0.35f)) : distance_foot;
    want_distance += (base - want_distance) * ease(4.f, dt);   /* smooth when getting in and out */
    side += ((t->in_vehicle ? 0.f : shoulder) - side) * ease(6.f, dt);
    const float want = want_distance * zoom;
    const float at[3] = {pivot[0] + right[0] * side, pivot[1] + right[1] * side, pivot[2]};
    /* stop in front of walls at once; move back out smoothly */
    const float far_eye[3] = {at[0] - fwd[0] * want, at[1] - fwd[1] * want, at[2] - fwd[2] * want};
    float fraction = 1.f, room = want;
    if (api->world_line(at, far_eye, &fraction) == 1) room = want * fraction - 0.4f;
    room = clampf(room, 0.6f, want);
    distance = room < distance ? room : distance + (room - distance) * ease(5.f, dt);
    float eye[3] = {at[0] - fwd[0] * distance, at[1] - fwd[1] * distance, at[2] - fwd[2] * distance};
    CtwGround ground = {0};
    ground.size = sizeof ground;
    const float probe[3] = {eye[0], eye[1], at[2]};
    if (api->get_ground(probe, &ground) && eye[2] < ground.height + 0.5f) eye[2] = ground.height + 0.5f;
    api->set_free_camera(1, eye, view_yaw, pitch);
    api->set_control_yaw(1, yaw);   /* on foot, "forward" is where the camera looks */
    speed_fov += ((t->in_vehicle ? clampf(t->speed * 0.25f, 0.f, 10.f) : 0.f) - speed_fov) * ease(3.f, dt);
    api->set_camera_fov(fov + speed_fov);
}

static void frame(float dt) {
    Target t;
    if (!available || !enabled || !target(&t)) {
        release();   /* off, or the player is dead: the game's own camera shows */
        return;
    }
    if (!active) begin(&t);
    look(dt, &t);
    place_camera(dt, &t);
    if (view_distance != applied_render_distance) {   /* a low camera sees much further than the overhead one */
        api->set_render_distance(view_distance > game_render_distance ? view_distance : game_render_distance);
        applied_render_distance = view_distance;
    }
}

static int key(int scancode) {
    if (scancode == SC_C && active) return 1;   /* look behind is read while held */
    if (scancode != SC_V || !available) return 0;
    enabled = !enabled;
    return 1;
}

static void setup(void) {
    available = CTW_MOD_HAS(api, set_control_yaw) && CTW_MOD_HAS(api, set_camera_fov) &&
                CTW_MOD_HAS(api, world_line) && CTW_MOD_HAS(api, get_player_state) && CTW_MOD_HAS(api, get_ground);
    if (!available) {
        api->menu_add_label(self, "Needs a newer game (camera API version 5)");
        return;
    }
    enh_saved_int("third_person.enabled", &enabled);
    enh_saved_float("third_person.distance_foot", &distance_foot);
    enh_saved_float("third_person.distance_vehicle", &distance_car);
    enh_saved_float("third_person.shoulder", &shoulder);
    enh_saved_float("third_person.fov", &fov);
    enh_saved_float("third_person.sensitivity", &sensitivity);
    enh_saved_int("third_person.invert_y", &invert_y);
    enh_saved_int("third_person.recenter", &recenter);
    enh_saved_float("third_person.view_distance", &view_distance);
    api->menu_add_toggle(self, "Third-person camera (V)", &enabled);
    api->menu_add_slider(self, "Distance on foot", &distance_foot, 3.f, 20.f, 0.5f);
    api->menu_add_slider(self, "Distance in vehicles", &distance_car, 5.f, 30.f, 0.5f);
    api->menu_add_slider(self, "Over-the-shoulder offset", &shoulder, 0.f, 2.f, 0.1f);
    api->menu_add_slider(self, "Field of view", &fov, 40.f, 100.f, 1.f);
    api->menu_add_slider(self, "Mouse sensitivity", &sensitivity, 0.03f, 0.5f, 0.01f);
    api->menu_add_toggle(self, "Invert mouse Y", &invert_y);
    api->menu_add_choice(self, "Follow vehicle", &recenter, recenter_names, 3);
    api->menu_add_slider(self, "View distance", &view_distance, 120.f, 720.f, 60.f);
}

const CtwFeature third_person = {"THIRD-PERSON CAMERA", setup, frame, 0, key, release};
