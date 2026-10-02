// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "ctw_mod.h"
static const CtwApi* api;
static CtwMod* self;
static int events, toggles, ticks, enabled;
static void event(const CtwGameEvent* e, void* user) { (void)user; if (e->type == CTW_EVENT_VEHICLE_REMOVED) ++events; }
static void toggle(int on, void* user) { (void)user; ++toggles; enabled = on; }
static void tick(void* user) { (void)user; ++ticks; }
CTW_MOD_EXPORT int ctw_mod_init(CtwMod* mod, const CtwApi* a) {
    api = a; self = mod; events = toggles = ticks = 0; enabled = 1;
    if (CTW_MOD_HAS(a, on_game_event)) a->on_game_event(mod, event, 0);
    if (CTW_MOD_HAS(a, on_enabled_changed)) a->on_enabled_changed(mod, toggle, 0);
    a->on_tick(mod, tick, 0);
    return 0;
}
CTW_MOD_EXPORT const CtwApi* ctw_test_api(void) { return api; }
CTW_MOD_EXPORT int ctw_test_events(void) { return events; }
CTW_MOD_EXPORT int ctw_test_toggles(void) { return toggles; }
CTW_MOD_EXPORT int ctw_test_ticks(void) { return ticks; }
CTW_MOD_EXPORT int ctw_test_enabled(void) { return enabled; }
CTW_MOD_EXPORT CtwMod* ctw_test_mod(void) { return self; }
