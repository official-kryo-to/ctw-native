// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// Synthetic damage values exercise the native armour and health arithmetic without game data.
#include "player.h"
#include "vehicle.h"
#include <cstdio>
#include <cstdlib>

static int failures;
static void check(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); ++failures; }
}

int main() {
    Player p;
    check(p.health == 127 && p.armour == 0 && p.damagePercentScale() == 100, "reset health and armour defaults");
    p.setArmour(500);
    check(p.armour == 127, "armour capacity is 127");
    check(!p.takeDamage(25) && p.health == 127 && p.armour == 102, "a protected hit consumes armour first");
    p.setArmour(10);
    check(!p.takeDamage(25) && p.health == 112 && p.armour == 0, "only damage beyond the remaining armour reaches health");
    p.setArmour(15);
    check(!p.takeDamage(15) && p.health == 112 && p.armour == 0, "exactly depleted armour protects health");
    p.giveArmour(50);
    p.giveArmour(100);
    check(p.armour == 127, "armour pickups saturate");
    p.setDamagePercentScale(50);
    p.setArmour(10);
    check(!p.takeDamage(25) && p.armour == 0 && p.health == 110, "damage scale is rounded down before armour absorption");
    p.setDamagePercentScale(0);
    check(!p.takeDamage(5) && p.health == 109, "a nonzero hit has a one-point minimum even at zero scale");
    p.setDamagePercentScale(256 + 100);
    check(p.damagePercentScale() == 100, "damage scale follows the native byte storage");
    p.setArmour(10);
    check(!p.takeDamage(0) && !p.takeDamage(-20) && p.health == 109 && p.armour == 10,
          "nonpositive damage cannot heal or alter armour");

    p.resetDamageState();
    p.setArmour(20);
    p.setHasDoubleArmour(true);
    check(p.reduceArmour(5) == 0 && p.armour == 18, "double armour halves odd hits with integer rounding");
    check(p.reduceArmour(1) == 0 && p.armour == 18, "the first single-point double-armour hit is skipped");
    check(p.reduceArmour(1) == 0 && p.armour == 17, "the second single-point hit is applied");
    p.setHasDoubleArmour(true);
    check(p.reduceArmour(1) == 0 && p.armour == 17, "setting double armour resets its alternation");
    p.setArmour(2);
    check(!p.takeDamage(10) && p.armour == 0 && p.health == 124, "double armour also halves overflow to health");
    p.setHasDoubleArmour(false);
    p.setArmour(2);
    check(p.reduceArmour(10) == 8 && p.armour == 0, "ReduceArmour returns damage left for health");

    Player npc;
    npc.playerControlled = false;
    npc.setArmour(40);
    npc.setDamagePercentScale(50);
    check(!npc.takeDamage(21) && npc.health == 117 && npc.armour == 40,
          "the base cPed path scales damage without player armour absorption");

    p.resetDamageState();
    p.setArmour(127);
    p.setDamagePercentScale(50);
    check(!p.takeDamage(15, true) && p.health == 112 && p.armour == 127,
          "run-over damage bypasses both armour and damage percent scaling");
    VehicleInfo info{};
    const int32_t at[3] = {0, 0, 0};
    Vehicle car; car.init(info, 0, at, 0, 0);
    car.hx = 4096; car.hy = 8192;
    car.vel[1] = 40 * 4096;
    p.pos[0] = car.pos[0]; p.pos[1] = car.pos[1]; p.pos[2] = car.pos[2];
    const int32_t normal[3] = {0, 4096, 0};
    check(p.vehicleImpact(car, normal, true) && p.health == 97 && p.armour == 127,
          "the actual vehicle impact path preserves armour");

    p.resetDamageState();
    p.setArmour(30);
    check(p.explosionHit(20, normal) && p.health == 127 && p.armour == 10 && p.reacting(),
          "an armoured blast still knocks the player down");
    p.resetDamageState();
    p.setArmour(5);
    check(p.takeDamage(200) && p.health == 0 && p.armour == 0 && p.dead && p.reacting(),
          "fatal overflow starts the death reaction");
    check(!p.takeDamage(1) && p.health == 0, "dead characters ignore further damage");
    p.setDamagePercentScale(20);
    p.setHasDoubleArmour(true);
    p.resetDamageState();
    check(p.health == 127 && p.armour == 0 && p.damagePercentScale() == 100 && !p.dead && !p.reacting(),
          "reset restores health, clears armour and death, and restores scale");
    check(!p.takeDamage(10) && p.health == 117, "reset clears the double-armour reward too");
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
