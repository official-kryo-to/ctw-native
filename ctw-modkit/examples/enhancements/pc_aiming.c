/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kryo.to
 * See LICENSE in the repository root.
 *
 * PC aiming (off by default): on foot, guns fire at the mouse cursor (or, with the third-person camera, at the
 * middle of the screen) instead of using the game's auto-aim, the upper body turns to the aim and a crosshair shows
 * where the shots go. Grenades and molotovs land on the aim point (as far as they can be thrown). Optional extras:
 * aim assist (the aim snaps onto a pedestrian next to the cursor) and the mouse wheel switching weapons. Movement
 * keeps working as before. Switched off, the game aims exactly as the original does.
 */
#include "enhancements.h"
#include <math.h>

static int enabled = 0, crosshair = 1, assist = 1, wheel_switch = 1;
static int available, applied, on_target;
static float aim_x, aim_y;   /* screen point being aimed at */
static float kick;           /* the crosshair opens up after a shot and closes again */
static int last_ammo = -2, last_type = -1;

static void release(void) {
    if (!applied) return;
    applied = 0;
    on_target = 0;
    api->set_aim_override(0, 0);
}

static int on_foot(void) {
    CtwPlayerState s = {0};
    s.size = sizeof s;
    if (!api->get_player_state(&s)) return 0;
    return !(s.flags & (CTW_PLAYER_DEAD | CTW_PLAYER_IN_VEHICLE | CTW_PLAYER_ENTERING_EXITING));
}

/* aim assist: the standing pedestrian nearest the aim point, within 1.5 units of it (across the ground) */
static int snap_to_ped(float target[3]) {
    if (!CTW_MOD_HAS(api, ped_count) || !CTW_MOD_HAS(api, get_ped_state)) return 0;
    const int n = api->ped_count();
    float best = 1.5f * 1.5f;
    int found = 0;
    for (int i = 0; i < n; ++i) {
        CtwPedState p = {0};
        p.size = sizeof p;
        if (!api->get_ped_state(i, &p) || (p.flags & CTW_PED_KNOCKED_DOWN)) continue;
        const float dx = p.position[0] - target[0], dy = p.position[1] - target[1];
        const float d = dx * dx + dy * dy;
        if (d >= best) continue;
        best = d;
        target[0] = p.position[0];
        target[1] = p.position[1];
        target[2] = p.position[2] + 1.f;
        found = 1;
    }
    return found;
}

static void frame(float dt) {
    kick = kick > 0.f ? kick - kick * fminf(dt * 10.f, 1.f) : 0.f;
    if (!available) return;
    if (!enabled || !on_foot() || api->menu_is_open()) { release(); return; }
    if (enh_third_person_active()) {   /* the camera looks where the shots go */
        aim_x = api->screen_width() * 0.5f;
        aim_y = api->screen_height() * 0.5f;
    } else {
        api->get_mouse(&aim_x, &aim_y);
    }
    float target[3];
    if (!api->screen_to_world(aim_x, aim_y, target)) { release(); return; }
    on_target = assist && snap_to_ped(target);
    api->set_aim_override(1, target);
    applied = 1;
    /* a shot: the held weapon's ammo went down */
    int type = -1, ammo = 0, clip = 0;
    if (api->get_player_weapon(&type, &ammo, &clip)) {
        if (type == last_type && ammo != -1 && last_ammo >= 0 && ammo < last_ammo) kick = 1.f;
        last_type = type;
        last_ammo = ammo;
    }
}

static void hud(float dt) {
    (void)dt;
    if (applied && wheel_switch && !enh_third_person_active() && CTW_MOD_HAS(api, cycle_weapon)) {
        const float w = api->mouse_wheel();   /* away from you: the next weapon */
        if (w != 0.f) {
            api->cycle_weapon(w > 0.f ? 1 : -1);
            api->consume_mouse_wheel();
        }
    }
    if (!applied || !crosshair) return;
    const float x = aim_x, y = aim_y, t = 2.f;
    const float gap = 3.f + 6.f * kick, r = gap + 7.f;
    const uint32_t shade = 0x000000A0u, colour = on_target ? 0xF04838F0u : 0xF2C230F0u;
    api->draw_rect(x - r - 1.f, y - t * 0.5f - 1.f, r - gap + 2.f, t + 2.f, shade);
    api->draw_rect(x + gap - 1.f, y - t * 0.5f - 1.f, r - gap + 2.f, t + 2.f, shade);
    api->draw_rect(x - t * 0.5f - 1.f, y - r - 1.f, t + 2.f, r - gap + 2.f, shade);
    api->draw_rect(x - t * 0.5f - 1.f, y + gap - 1.f, t + 2.f, r - gap + 2.f, shade);
    api->draw_rect(x - r, y - t * 0.5f, r - gap, t, colour);
    api->draw_rect(x + gap, y - t * 0.5f, r - gap, t, colour);
    api->draw_rect(x - t * 0.5f, y - r, t, r - gap, colour);
    api->draw_rect(x - t * 0.5f, y + gap, t, r - gap, colour);
    api->draw_rect(x - 1.f, y - 1.f, 2.f, 2.f, colour);   /* the centre dot */
}

static void setup(void) {
    available = CTW_MOD_HAS(api, set_aim_override) && CTW_MOD_HAS(api, screen_to_world) && CTW_MOD_HAS(api, get_player_state);
    if (!available) {
        api->menu_add_label(self, "Needs a newer game");
        return;
    }
    enh_saved_int("pc_aiming", &enabled);
    enh_saved_int("pc_aiming_crosshair", &crosshair);
    enh_saved_int("pc_aiming_assist", &assist);
    enh_saved_int("pc_aiming_wheel", &wheel_switch);
    api->menu_add_toggle(self, "Aim with the mouse", &enabled);
    api->menu_add_toggle(self, "Crosshair", &crosshair);
    api->menu_add_toggle(self, "Aim assist (snap to people)", &assist);
    if (CTW_MOD_HAS(api, cycle_weapon)) api->menu_add_toggle(self, "Mouse wheel switches weapons", &wheel_switch);
}

const CtwFeature pc_aiming = {"PC AIMING", setup, frame, hud, 0, release};
