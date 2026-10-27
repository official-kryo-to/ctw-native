/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kryo.to
 * See LICENSE in the repository root.
 *
 * Real prop physics: in the original many props stay where they stand when hit. Lamp posts and signs bend to a
 * fixed angle, hydrants tilt about their base and benches or kiosks only tilt or break. With this on, anything that
 * breaks comes loose and tumbles, bounces and rolls instead; a knocked-off hydrant still sprays from its pipe.
 */
#include "enhancements.h"

static int enabled = 1, available, applied = -1;

static void release(void) {
    if (!available || applied == 0) return;
    api->set_prop_physics(0);
    applied = 0;
}

static void frame(float dt) {
    (void)dt;
    if (!available || applied == enabled) return;
    api->set_prop_physics(enabled);
    applied = enabled;
}

static void setup(void) {
    available = CTW_MOD_HAS(api, set_prop_physics);
    if (!available) {
        api->menu_add_label(self, "Needs a newer game");
        return;
    }
    enh_saved_int("prop_physics", &enabled);
    api->menu_add_toggle(self, "Props come loose when hit", &enabled);
}

const CtwFeature prop_physics = {"PROP PHYSICS", setup, frame, 0, 0, release};
