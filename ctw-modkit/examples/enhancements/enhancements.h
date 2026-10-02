/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kryo.to
 * See LICENSE in the repository root.
 *
 * Enhancements - optional PC improvements, each a self-contained feature in its own file.
 *
 * Adding a feature: write a CtwFeature in a new .c file in this folder (the build picks up every source file), declare
 * it below and list it in enhancements.c. The entry point registers each feature's menu options, saves them between
 * sessions and calls its hooks only while the mod is switched on.
 */
#ifndef ENHANCEMENTS_H
#define ENHANCEMENTS_H

#include "ctw_mod.h"

extern const CtwApi* api;
extern CtwMod* self;

typedef struct CtwFeature {
    const char* name;                 /* heading in the F4 menu */
    void (*setup)(void);              /* once: add menu options and saved settings */
    void (*frame)(float dt);          /* every drawn frame, before the world is drawn (cameras); dt = real seconds */
    void (*hud)(float dt);            /* every drawn frame after the world, for HUD drawing */
    int (*key)(int scancode);         /* key presses while the mod is on; return 1 to take the key */
    void (*stop)(void);               /* release everything it changed: the mod is switched off or unloaded */
} CtwFeature;

/* Options that are kept between sessions (mods/Enhancements/settings.ini). Call these from setup: they load the
 * saved value into *value (or keep its default) and save it whenever it changes. */
void enh_saved_int(const char* key, int* value);
void enh_saved_float(const char* key, float* value);

extern const CtwFeature third_person;
extern const CtwFeature psp_lighting;

#endif
