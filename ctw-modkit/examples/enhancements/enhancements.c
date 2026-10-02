/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kryo.to
 * See LICENSE in the repository root.
 *
 * Enhancements - the entry point: the feature list, saved options and the mod's lifecycle.
 */
#include "enhancements.h"

const CtwApi* api;
CtwMod* self;

static const CtwFeature* const features[] = {&third_person, &psp_lighting};
enum { FEATURES = sizeof features / sizeof features[0], MAX_SAVED = 64 };

static int mod_on = 1, frame_hook = 0;
static double last_frame, last_hud;

/* ---- saved options -------------------------------------------------------------------------------------------- */
typedef struct Saved { const char* key; int* i; float* f; float last; } Saved;
static Saved saved[MAX_SAVED];
static int saved_count;

static void track(const char* key, int* i, float* f) {
    if (saved_count == MAX_SAVED) return;
    Saved* s = &saved[saved_count++];
    const float current = i ? (float)*i : *f;
    const float value = api->get_setting(self, key, current);
    s->key = key; s->i = i; s->f = f; s->last = value;
    if (i) *i = (int)value;
    else *f = value;
}

void enh_saved_int(const char* key, int* value) { track(key, value, 0); }
void enh_saved_float(const char* key, float* value) { track(key, 0, value); }

static void save_changes(void) {
    for (int n = 0; n < saved_count; ++n) {
        Saved* s = &saved[n];
        const float now = s->i ? (float)*s->i : *s->f;
        if (now != s->last) { api->set_setting(self, s->key, now); s->last = now; }
    }
}

/* ---- lifecycle ------------------------------------------------------------------------------------------------- */
static void stop_all(void) {
    for (int n = 0; n < FEATURES; ++n) if (features[n]->stop) features[n]->stop();
}

static float elapsed(double* last) {
    const double now = api->time_seconds();
    float dt = (float)(now - *last);
    *last = now;
    return dt < 0.f || dt > 0.1f ? 0.1f : dt;
}

static void frame(void* user) {   /* before the world is drawn */
    (void)user;
    const float dt = elapsed(&last_frame);
    if (!mod_on) return;
    for (int n = 0; n < FEATURES; ++n) if (features[n]->frame) features[n]->frame(dt);
}

static void hud(void* user) {
    (void)user;
    save_changes();
    if (!frame_hook) frame(0);   /* an older game without on_frame_begin: one frame late, but working */
    const float dt = elapsed(&last_hud);
    if (!mod_on) return;
    for (int n = 0; n < FEATURES; ++n) if (features[n]->hud) features[n]->hud(dt);
}

static int key(int scancode, void* user) {
    (void)user;
    for (int n = 0; n < FEATURES; ++n)
        if (features[n]->key && features[n]->key(scancode)) return 1;
    return 0;
}

static void enabled(int on, void* user) {
    (void)user;
    mod_on = on;
    if (!on) stop_all();   /* a switched-off mod stops getting callbacks: leave the game as it was */
}

CTW_MOD_EXPORT int ctw_mod_init(CtwMod* mod, const CtwApi* a) {
    if (!a || a->version < 4 || !CTW_MOD_HAS(a, menu_add_label) || !CTW_MOD_HAS(a, get_setting)) {
        if (a) a->log(mod, "needs a newer game (mod API version 5)");
        return 1;
    }
    api = a;
    self = mod;
    last_frame = last_hud = api->time_seconds();
    for (int n = 0; n < FEATURES; ++n) {
        api->menu_add_label(mod, features[n]->name);
        features[n]->setup();
    }
    frame_hook = CTW_MOD_HAS(api, on_frame_begin);
    if (frame_hook) api->on_frame_begin(mod, frame, 0);
    api->on_draw_hud(mod, hud, 0);
    api->on_key(mod, key, 0);
    api->on_enabled_changed(mod, enabled, 0);
    return 0;
}

CTW_MOD_EXPORT void ctw_mod_shutdown(void) {
    if (!api) return;
    save_changes();
    stop_all();
}
