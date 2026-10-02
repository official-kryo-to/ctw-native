// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
/*
 * Cheat Example - cheats, and a tour of the C API:
 *   - vehicle spawner (F7) and free camera (F8)
 *   - vehicle cheats: repair, invincibility, nitro (hold Shift while driving), flip upright, new paint
 *   - world cheats: time of day, frozen clock, weather, traffic and pedestrian density, clearing the area
 *   - speed, time speed and render distance
 * Uses ctw_ui.h for the window, the search field and the scrolling list.
 */
#include "ctw_mod.h"
#include "ctw_ui.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* SDL scancodes */
enum {
    SC_A = 4, SC_D = 7, SC_E = 8, SC_Q = 20, SC_S = 22, SC_W = 26, SC_RETURN = 40, SC_ESCAPE = 41, SC_SPACE = 44,
    SC_F1 = 58, SC_F7 = 64, SC_F8 = 65, SC_F12 = 69, SC_PAGEUP = 75, SC_PAGEDOWN = 78, SC_LCTRL = 224, SC_LSHIFT = 225
};

static const CtwApi* api;

/* ---- speed changer and time speed multiplier ---------------------------------------------------------- */
static float speed_scale = 1.f, applied_speed = 1.f;
static float game_speed = 1.f, applied_game_speed = 1.f;
static float render_distance = 120.f, applied_render_distance = 120.f;
static float ped_density = 1.f, applied_ped_density = 1.f;   /* needs a version 5 game */

/* ---- free camera -------------------------------------------------------------------------------------- */
static int cam_on = 0, cam_was_on = 0;
static float cam_eye[3], cam_yaw, cam_pitch, cam_speed = 30.f;
static double last_time;

/* ---- vehicle spawner ---------------------------------------------------------------------------------- */
#define MAX_VEHICLES 512
static int spawner_open = 0, search_focused = 0;
static char filter[32];
static int shown[MAX_VEHICLES], shown_count;
static CtwUi ui;
static CtwUiScroll scroll;
static float spin;                 /* the hovered car turns */
static char message[96];
static uint32_t message_until;

static int contains_nocase(const char* text, const char* what) {
    size_t n = strlen(what);
    if (!n) return 1;
    for (; *text; ++text) {
        size_t i = 0;
        while (i < n && text[i] && tolower((unsigned char)text[i]) == tolower((unsigned char)what[i])) ++i;
        if (i == n) return 1;
    }
    return 0;
}

static void refilter(void) {
    int count = api->vehicle_count();
    shown_count = 0;
    for (int id = 0; id < count && shown_count < MAX_VEHICLES; ++id) {
        if (!api->vehicle_spawnable(id)) continue;
        const char* name = api->vehicle_name(id);
        char number[16];
        snprintf(number, sizeof number, "%d", id);
        if (contains_nocase(name ? name : "", filter) || strcmp(number, filter) == 0) shown[shown_count++] = id;
    }
}

static void say(const char* text) {
    snprintf(message, sizeof message, "%s", text);
    message_until = api->frame_count() + 75;
}

/* ---- cheats ----------------------------------------------------------------------------------------------- */
static int invincible = 0, nitro = 0, freeze_clock = 0, applied_freeze = 0, weather = 0, applied_weather = -1;
static float time_of_day = 12.f, applied_time = -1.f, traffic_density = 1.f, applied_traffic = 1.f;
static const char* const weather_names[] = {"1", "2", "3", "4", "5", "6", "7", "8"};

static int has_gameplay(void) { return CTW_MOD_HAS(api, get_player_state) && CTW_MOD_HAS(api, get_vehicle_state); }

/* The vehicle the player drives, 0 when on foot. */
static CtwVehicle driven(void) {
    CtwPlayerState p = {0};
    p.size = sizeof p;
    return has_gameplay() && api->get_player_state(&p) && (p.flags & CTW_PLAYER_IN_VEHICLE) ? p.vehicle : 0;
}

static void repair_button(void* user) {
    (void)user;
    const CtwVehicle v = driven();
    say(v && api->repair_vehicle(v) ? "Vehicle repaired" : "Get in a vehicle first");
}

static void flip_button(void* user) {   /* upright where it is, a little above the ground */
    (void)user;
    const CtwVehicle v = driven();
    CtwVehicleState s = {0};
    s.size = sizeof s;
    if (!v || !api->get_vehicle_state(v, &s)) { say("Get in a vehicle first"); return; }
    s.position[2] += 1.f;
    api->set_vehicle_transform(v, s.position, s.heading);
    say("Back on its wheels");
}

static void paint_button(void* user) {
    (void)user;
    const CtwVehicle v = driven();
    CtwVehicleState s = {0};
    s.size = sizeof s;
    if (!v || !api->get_vehicle_state(v, &s)) { say("Get in a vehicle first"); return; }
    api->set_vehicle_palette(v, (s.palette + 1 + (int)(api->frame_count() % 25)) % 27);
    say("Fresh paint");
}

static void clear_area_button(void* user) {   /* every car but the player's that can be removed */
    (void)user;
    const CtwVehicle mine = driven();
    int removed = 0;
    if (!has_gameplay()) return;
    for (int i = api->live_vehicle_count() - 1; i >= 0; --i) {
        const CtwVehicle v = api->live_vehicle_at(i);
        if (v && v != mine && api->remove_vehicle(v)) ++removed;
    }
    char text[64];
    snprintf(text, sizeof text, "Cleared %d vehicles", removed);
    say(text);
}

static void explode_button(void* user) {   /* wreck every car within 40 units, except the player's */
    (void)user;
    CtwPlayerState p = {0};
    int wrecked = 0;
    p.size = sizeof p;
    if (!has_gameplay() || !api->get_player_state(&p)) return;
    for (int i = 0; i < api->live_vehicle_count(); ++i) {
        CtwVehicleState s = {0};
        s.size = sizeof s;
        const CtwVehicle v = api->live_vehicle_at(i);
        if (!v || v == p.vehicle || !api->get_vehicle_state(v, &s) || (s.flags & CTW_VEHICLE_DEAD)) continue;
        const float dx = s.position[0] - p.position[0], dy = s.position[1] - p.position[1];
        if (dx * dx + dy * dy < 40.f * 40.f && api->damage_vehicle(v, 255)) ++wrecked;
    }
    char text[64];
    snprintf(text, sizeof text, "Boom: %d vehicles", wrecked);
    say(text);
}

static void cheat_tick(void) {
    const CtwVehicle v = driven();
    if (v && invincible) {   /* keep the driven vehicle at full health */
        CtwVehicleState s = {0};
        s.size = sizeof s;
        if (api->get_vehicle_state(v, &s) && s.health < 255 && !(s.flags & CTW_VEHICLE_DEAD)) api->repair_vehicle(v);
    }
    if (v && nitro && api->key_down(SC_LSHIFT)) {   /* push along the direction of travel, up to 60 units/s */
        CtwVehicleState s = {0};
        s.size = sizeof s;
        if (api->get_vehicle_state(v, &s)) {
            const float sp = sqrtf(s.velocity[0] * s.velocity[0] + s.velocity[1] * s.velocity[1]);
            if (sp > 2.f && sp < 60.f) {
                const float k = (sp + 1.2f) / sp;
                const float vel[3] = {s.velocity[0] * k, s.velocity[1] * k, s.velocity[2]};
                api->set_vehicle_velocity(v, vel);
            }
        }
    }
    if (time_of_day != applied_time) { api->set_time_of_day(time_of_day); applied_time = time_of_day; }
    if (freeze_clock != applied_freeze) { api->set_clock_running(!freeze_clock); applied_freeze = freeze_clock; }
    if (!freeze_clock) time_of_day = applied_time = api->get_time_of_day();   /* the slider follows the clock */
    if (weather != applied_weather) { api->set_weather(weather); applied_weather = weather; }
    if (traffic_density != applied_traffic && CTW_MOD_HAS(api, set_traffic_density)) {
        api->set_traffic_density(traffic_density);
        applied_traffic = traffic_density;
    }
}

static void spawn(int id) {
    const char* name = api->vehicle_name(id);
    char text[96];
    snprintf(text, sizeof text, api->spawn_vehicle(id) ? "Spawned %s" : "Could not spawn %s", name ? name : "vehicle");
    say(text);
}

static void update_game_input(void) { api->set_game_input(!(search_focused && spawner_open) && !cam_on); }

static void set_spawner(int open) {
    spawner_open = open;
    search_focused = open;          /* start typing right away; click the world to walk again */
    if (open) { filter[0] = 0; refilter(); scroll.pos = scroll.target = 0.f; }
    update_game_input();
}

static void open_spawner_button(void* user) { (void)user; set_spawner(1); }

static void reset_button(void* user) {
    (void)user;
    speed_scale = 1.f;
    game_speed = 1.f;
    render_distance = 120.f;
    ped_density = 1.f;
    traffic_density = 1.f;
    invincible = nitro = freeze_clock = 0;
    cam_on = 0;
}

static int on_key(int key, void* user) {
    (void)user;
    if (key == SC_F7) { set_spawner(!spawner_open); return 1; }
    if (key == SC_F8) { cam_on = !cam_on; return 1; }
    if (cam_on && (key == SC_PAGEUP || key == SC_PAGEDOWN)) {
        render_distance += key == SC_PAGEUP ? 120.f : -120.f;
        if (render_distance < 120.f) render_distance = 120.f;
        if (render_distance > 720.f) render_distance = 720.f;
        return 1;
    }
    if (spawner_open && key == SC_ESCAPE) { set_spawner(0); return 1; }
    if (spawner_open && search_focused) {
        if (key == SC_RETURN) {     /* Enter spawns the first match */
            if (shown_count) spawn(shown[0]);
            search_focused = 0;
            update_game_input();
        }
        return key < SC_F1 || key > SC_F12;   /* typing must not reach the game; F-keys still work */
    }
    return 0;
}

/* ---- free camera ---------------------------------------------------------------------------------------- */
static void camera_frame(void) {
    if (cam_on && !cam_was_on) {    /* start where the game camera is */
        api->get_camera(cam_eye, &cam_yaw, &cam_pitch);
    }
    if (!cam_on && cam_was_on) {
        api->set_free_camera(0, 0, 0.f, 0.f);
        api->set_mouse_captured(0);
    }
    cam_was_on = cam_on;
    update_game_input();
    if (!cam_on) return;
    const int looking = !spawner_open && !api->menu_is_open();
    api->set_mouse_captured(looking);
    if (looking) {
        float dx, dy;
        api->mouse_delta(&dx, &dy);
        cam_yaw -= dx * 0.15f;
        cam_pitch -= dy * 0.15f;
        if (cam_pitch > 89.f) cam_pitch = 89.f;
        if (cam_pitch < -89.f) cam_pitch = -89.f;
        float wheel = api->mouse_wheel();
        if (wheel != 0.f) {
            cam_speed *= wheel > 0 ? 1.25f : 0.8f;
            if (cam_speed < 2.f) cam_speed = 2.f;
            if (cam_speed > 400.f) cam_speed = 400.f;
        }
    }
    api->set_free_camera(1, cam_eye, cam_yaw, cam_pitch);
}

static void camera_move(float dt) {  /* every rendered frame, with real time: also works while time is paused */
    if (!cam_on || (spawner_open && search_focused)) return;
    float yr = cam_yaw * 3.14159265f / 180.f, pr = cam_pitch * 3.14159265f / 180.f;
    float fwd[3] = {-sinf(yr) * cosf(pr), cosf(yr) * cosf(pr), sinf(pr)};
    float right[3] = {cosf(yr), sinf(yr), 0.f};
    float f = (float)(api->key_down(SC_W) - api->key_down(SC_S));
    float r = (float)(api->key_down(SC_D) - api->key_down(SC_A));
    float u = (float)(api->key_down(SC_SPACE) + api->key_down(SC_E) - api->key_down(SC_LCTRL) - api->key_down(SC_Q));
    float step = cam_speed * (api->key_down(SC_LSHIFT) ? 4.f : 1.f) * dt;
    for (int i = 0; i < 3; ++i) cam_eye[i] += (fwd[i] * f + right[i] * r) * step;
    cam_eye[2] += u * step;
    if (cam_eye[2] < 1.f) cam_eye[2] = 1.f;
}

static void tick(void* user) {
    (void)user;
    if (speed_scale != applied_speed) { api->set_speed_scale(speed_scale); applied_speed = speed_scale; }
    if (game_speed != applied_game_speed) { api->set_game_speed(game_speed); applied_game_speed = game_speed; }
    if (has_gameplay()) cheat_tick();
    if (ped_density != applied_ped_density && CTW_MOD_HAS(api, set_ped_density)) {
        api->set_ped_density(ped_density);
        applied_ped_density = ped_density;
    }
}

static void apply_render_distance(void) {   /* also while the game is paused: called every drawn frame */
    if (render_distance != applied_render_distance) {
        api->set_render_distance(render_distance);
        applied_render_distance = api->get_render_distance();
        render_distance = applied_render_distance;
    }
}

/* ---- the spawner window ---------------------------------------------------------------------------------- */
static void draw_spawner(void) {
    /* On the right, below the game's clock; beside the F4 menu while that is open. */
    const CtwUiRect r = ctw_ui_side_area(api, 640.f);
    const float px = r.x, py = r.y, pw = r.w, ph = r.h, row = ctw_ui_row_h(&ui);
    char line[64];
    snprintf(line, sizeof line, "%d", shown_count);
    if (ctw_ui_panel(&ui, px, py, pw, ph, "VEHICLES", line)) { set_spawner(0); return; }

    const float top = py + ctw_ui_title_h(&ui) + 12.f;
    int was_focused = search_focused;
    if (ctw_ui_textbox(&ui, px + 14.f, top, pw - 28.f, row, filter, sizeof filter, &search_focused,
                       "Search by name or ID...")) {
        refilter();
        scroll.target = 0.f;
    }
    if (was_focused != search_focused) update_game_input();

    /* grid of cards: the picture, then the name and the ID under it */
    const float gx = px + 14.f, gy = top + row + 12.f, gw = pw - 28.f, gh = py + ph - ctw_ui_footer_h(&ui) - 10.f - gy;
    const int cols = gw >= 520.f ? 3 : gw >= 300.f ? 2 : 1;
    const float gap = 10.f, cw = (gw - 14.f - gap * (cols - 1)) / cols;
    const float name_h = ctw_ui_lh(&ui, 1.05f), id_h = ctw_ui_lh(&ui, 0.85f), text_h = 8.f + name_h + 2.f + id_h + 8.f;
    const float pic_h = cw * 0.55f, ch = 4.f + pic_h + text_h;
    const int rows = (shown_count + cols - 1) / cols;
    ctw_ui_scroll_begin(&ui, 2, &scroll, gx, gy, gw, gh, rows * (ch + gap));
    const int in_grid = ctw_ui_in(&ui, gx, gy, gw - 14.f, gh);
    for (int i = 0; i < shown_count; ++i) {
        const float cx = gx + (i % cols) * (cw + gap), cy = gy + (i / cols) * (ch + gap) - scroll.pos;
        if (cy + ch < gy || cy > gy + gh) continue;       /* only what is on screen */
        const int id = shown[i];
        const int hot = in_grid && ctw_ui_in(&ui, cx, cy, cw, ch);
        api->draw_rect(cx, cy, cw, ch, hot ? 0xE0B02040u : CTW_UI_ROW);
        if (hot) api->draw_rect(cx, cy + ch - 3.f, cw, 3.f, CTW_UI_ACCENT);
        api->draw_vehicle(id, cx + 4.f, cy + 4.f, cw - 8.f, pic_h, hot ? spin : 35.f);
        api->set_clip(gx, gy, gw, gh);                   /* the picture changes the drawing state */
        const char* name = api->vehicle_name(id);
        const float ty = cy + 4.f + pic_h + 8.f;
        ctw_ui_text_fit(&ui, cx + 10.f, ty, 1.05f, CTW_UI_TEXT, name ? name : "?", cw - 20.f);
        snprintf(line, sizeof line, "ID %d", id);
        api->draw_text(cx + 10.f, ty + name_h + 2.f, 0.85f, CTW_UI_DIM, line);
        if (hot && ui.clicked) spawn(id);
    }
    if (!shown_count) ctw_ui_text(&ui, gx + 6.f, gy + 10.f, 1.f, CTW_UI_DIM, "No vehicle matches.");
    ctw_ui_scroll_end(&ui);
    /* what just happened, else how it works */
    if (message[0] && api->frame_count() < message_until) ctw_ui_footer(&ui, px, py, pw, ph, CTW_UI_ACCENT, message);
    else ctw_ui_footer(&ui, px, py, pw, ph, CTW_UI_DIM, "Click to spawn.   Enter: first match.   F7 or Esc: close");
}

/* The free camera's keys, at the bottom above the game's help line. Only while nothing else is open: with a
 * window open the camera does not move anyway. Same rounded panel style as the mod menu. */
static void draw_camera_help(void) {
    const float W = (float)api->screen_width(), bottom = ctw_ui_screen_bottom(api);
    const float x = CTW_UI_GAP, bw = W - 2.f * CTW_UI_GAP, w = bw - 40.f;
    char speeds[128];
    snprintf(speeds, sizeof speeds, "Wheel: speed %.0f      Page Up / Page Down: render distance %.0f", cam_speed, render_distance);
    const char* keys = "Mouse: look      WASD: fly      Space / Ctrl: up and down      Shift: faster      F8: back";
    const float h1 = ctw_ui_lh(&ui, 1.1f), h2 = ctw_ui_text_wrap(&ui, 0, 0, 0.9f, 0, keys, w, 0);
    const float h3 = ctw_ui_text_wrap(&ui, 0, 0, 0.9f, 0, speeds, w, 0), h = 12.f + h1 + 6.f + h2 + h3 + 12.f;
    const float y = bottom - h;
    ctw_ui_round_rect(api, x, y, bw, h, 12.f, CTW_UI_PANEL);
    api->draw_rect(x + 12.f, y, bw - 24.f, 2.f, CTW_UI_ACCENT);
    api->draw_text(x + 20.f, y + 12.f, 1.1f, CTW_UI_TEXT, "FREE CAMERA");
    ctw_ui_text_wrap(&ui, x + 20.f, y + 18.f + h1, 0.9f, CTW_UI_TEXT, keys, w, 1);
    ctw_ui_text_wrap(&ui, x + 20.f, y + 18.f + h1 + h2, 0.9f, CTW_UI_DIM, speeds, w, 1);
}

static void hud(void* user) {
    (void)user;
    const double now = api->time_seconds();
    float dt = (float)(now - last_time);
    last_time = now;
    if (dt < 0.f || dt > 0.1f) dt = 0.1f;
    spin += 90.f * dt;
    ctw_ui_begin(&ui, api);
    apply_render_distance();
    camera_move(dt);
    camera_frame();
    if (cam_on && !spawner_open && !api->menu_is_open()) draw_camera_help();
    if (spawner_open) draw_spawner();
}

CTW_MOD_EXPORT int ctw_mod_init(CtwMod* mod, const CtwApi* a) {
    api = a;
    if (api->version < 3 || !CTW_MOD_HAS(api, get_render_distance)) {
        api->log(mod, "needs a newer game");
        return 1;
    }
    speed_scale = applied_speed = api->get_speed_scale();
    render_distance = applied_render_distance = api->get_render_distance();
    game_speed = applied_game_speed = api->get_game_speed();
    weather = applied_weather = api->get_weather();
    time_of_day = applied_time = api->get_time_of_day();
    const int labels = CTW_MOD_HAS(api, menu_add_label), choices = CTW_MOD_HAS(api, menu_add_choice);
    if (labels) api->menu_add_label(mod, "VEHICLES");
    api->menu_add_button(mod, "Vehicle spawner (F7)", open_spawner_button, 0);
    if (has_gameplay()) {
        api->menu_add_button(mod, "Repair vehicle", repair_button, 0);
        api->menu_add_button(mod, "Flip vehicle upright", flip_button, 0);
        api->menu_add_button(mod, "New paint job", paint_button, 0);
        api->menu_add_toggle(mod, "Invincible vehicle", &invincible);
        api->menu_add_toggle(mod, "Nitro (hold Shift)", &nitro);
    }
    if (labels) api->menu_add_label(mod, "WORLD");
    api->menu_add_slider(mod, "Time of day", &time_of_day, 0.f, 23.75f, 0.25f);
    api->menu_add_toggle(mod, "Freeze clock", &freeze_clock);
    if (choices) api->menu_add_choice(mod, "Weather", &weather, weather_names, 8);
    if (CTW_MOD_HAS(api, set_traffic_density))
        api->menu_add_slider(mod, "Traffic density", &traffic_density, 0.f, 4.f, 0.25f);
    if (CTW_MOD_HAS(api, set_ped_density))
        api->menu_add_slider(mod, "Pedestrian density", &ped_density, 0.f, 4.f, 0.25f);
    if (has_gameplay()) {
        api->menu_add_button(mod, "Clear all other vehicles", clear_area_button, 0);
        api->menu_add_button(mod, "Wreck nearby vehicles", explode_button, 0);
    }
    if (labels) api->menu_add_label(mod, "PLAYER AND CAMERA");
    api->menu_add_toggle(mod, "Free camera (F8)", &cam_on);
    api->menu_add_slider(mod, "Speed multiplier", &speed_scale, 0.25f, 10.f, 0.25f);
    api->menu_add_slider(mod, "Time speed multiplier", &game_speed, 0.f, 8.f, 0.25f);
    api->menu_add_slider(mod, "Render distance", &render_distance, 120.f, 720.f, 60.f);
    api->menu_add_button(mod, "Reset everything", reset_button, 0);
    api->on_key(mod, on_key, 0);
    api->on_tick(mod, tick, 0);
    api->on_draw_hud(mod, hud, 0);
    return 0;
}

CTW_MOD_EXPORT void ctw_mod_shutdown(void) {
    if (!api) return;
    api->set_speed_scale(1.f);
    api->set_game_speed(1.f);
    api->set_render_distance(120.f);
    if (CTW_MOD_HAS(api, set_ped_density)) api->set_ped_density(1.f);
    api->set_free_camera(0, 0, 0.f, 0.f);
    api->set_mouse_captured(0);
    api->set_game_input(1);
}
