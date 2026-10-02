/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kryo.to
 * See LICENSE in the repository root.
 *
 * PSP lighting: the look of the PSP version, which lights the city dynamically. At night street lights throw pools
 * of light on the pavement and headlights light the road ahead; the picture has the PSP's warm, sepia-leaning colour
 * with a little more contrast, and nights are darker and cooler between the lights. An approximation drawn with the
 * game's render style, not the PSP's own renderer.
 */
#include "enhancements.h"

static int enabled = 1;
static float pools = 1.f, headlights = 1.f, sepia = 0.35f, vignette = 0.3f;
static int available, applied;

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

/* 0 by day, 1 at night, blended over the hour around dusk (19:00-20:30) and dawn (05:30-07:00) */
static float nightness(float h) {
    if (h >= 20.5f || h < 5.5f) return 1.f;
    if (h >= 19.f) return (h - 19.f) / 1.5f;
    if (h < 7.f) return 1.f - (h - 5.5f) / 1.5f;
    return 0.f;
}

static void release(void) {
    if (!applied) return;
    applied = 0;
    api->set_render_style(0);
}

static void frame(float dt) {
    (void)dt;
    if (!available) return;
    if (!enabled) { release(); return; }
    const float n = nightness(api->get_time_of_day());
    CtwRenderStyle s = {0};
    s.size = sizeof s;
    s.light_pools = pools;
    s.headlight_pools = headlights;
    s.sepia = sepia * (1.f - 0.4f * n);
    s.saturation = 0.92f - 0.1f * n;
    s.contrast = 1.08f + 0.06f * n;
    s.brightness = -0.05f * n;
    s.tint[0] = 1.04f - 0.10f * n;
    s.tint[1] = 1.00f - 0.04f * n;
    s.tint[2] = 0.92f + 0.12f * n;
    s.vignette = clampf(vignette, 0.f, 1.f);
    applied = api->set_render_style(&s);
}

static void setup(void) {
    available = CTW_MOD_HAS(api, set_render_style) && CTW_MOD_HAS(api, get_time_of_day);
    if (!available) {
        api->menu_add_label(self, "Needs a newer game");
        return;
    }
    enh_saved_int("psp_lighting", &enabled);
    enh_saved_float("psp_pools", &pools);
    enh_saved_float("psp_headlights", &headlights);
    enh_saved_float("psp_sepia", &sepia);
    enh_saved_float("psp_vignette", &vignette);
    api->menu_add_toggle(self, "PSP lighting", &enabled);
    api->menu_add_slider(self, "Street light pools", &pools, 0.f, 2.f, 0.1f);
    api->menu_add_slider(self, "Headlight beams", &headlights, 0.f, 2.f, 0.1f);
    api->menu_add_slider(self, "Sepia tone", &sepia, 0.f, 1.f, 0.05f);
    api->menu_add_slider(self, "Vignette", &vignette, 0.f, 1.f, 0.05f);
}

const CtwFeature psp_lighting = {"PSP LIGHTING", setup, frame, 0, 0, release};
