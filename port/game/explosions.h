// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#pragma once
#include <cstdint>

// cExplosionGeneric::Process / cBaseExplosion::AffectEntities (ordinary types 0 and 4).
struct ExplosionBlast {
    ExplosionBlast(const int32_t origin[3], int type, int16_t scale, int weaponDamage);
    // AffectEntities with explicit radii and damage (cExplosionBigVehicle, cExplosionMolotov)
    ExplosionBlast(const int32_t origin[3], int type, int32_t damageRadius, int32_t forceRadius, int32_t damageQ12,
                   int32_t pedDamageQ12);
    int32_t pos[3], damageRadius, forceRadius, damageQ12, pedDamageQ12;
    int type;
    int minCellX, minCellY, maxCellX, maxCellY;
    bool includesCell(const int32_t point[3]) const;
    static int64_t distanceSquared(const int32_t a[3], const int32_t b[3]);
    static int32_t falloff(int64_t distanceSquared, int32_t radius);
    bool pedHit(const int32_t point[3], bool player, int& damage) const;
    int vehicleDamage(int32_t distance, int32_t bound, bool occupiedByPlayer) const;
    int32_t force(int64_t distanceSquared) const;
    // the sDamageInfo type AffectEntities hands out (qword_481768): 6 for types 4 and 6 to 11, otherwise 7
    int damageKind() const { return type >= 4 && type <= 11 && type != 5 ? 6 : 7; }
};
