// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// cWeaponManager / cWeapon behaviour on a synthetic weapon table (arbitrary values, never the game's data).
#include "weapons.h"
#include "player.h"
#include "projectiles.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

static int failures = 0;
static void check(bool ok, const char* description) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", description); ++failures; }
}

int main() {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "ctw-weapons-test";
    fs::create_directories(dir);
    WeaponTables t{};
    for (int i = 0; i < 28; ++i) {
        WeaponInfo& w = t.info[i];
        w.maxAmmo = i <= 4 ? -1 : 500;
        w.slot = i == 0 ? 0 : i <= 4 ? 1 : i <= 7 ? 2 : i <= 9 ? 4 : i <= 12 ? 3 : i <= 14 ? 5 : i <= 17 ? 6 : 7;
        w.fireDelay = 10;
        w.reloadTime = 20;
        w.damage = 10;
        w.range = 30 << 12;
    }
    t.info[5].clip = 3;   // a "pistol" with a three-round clip
    for (int& life : t.traceLife) life = 3;
    {
        std::ofstream f(dir / "weapon_tables.bin", std::ios::binary);
        f.write("CTWWPN01", 8);
        f.write((const char*)t.info, sizeof t.info);
        f.write((const char*)t.defaultAmmo, sizeof t.defaultAmmo);
        f.write((const char*)t.traceLife, sizeof t.traceLife);
    }
    check(Weapons_Init(dir.string()), "synthetic weapon table loads");

    WeaponManager m;
    check(m.cur == 0 && m.current() && m.current()->type == 0, "a new manager holds the fists");
    check(m.give(5, 10, true) && m.cur == 2 && m.current()->type == 5, "a weapon in a higher slot is switched to");
    Weapon& p = *m.current();
    check(p.ammo == 10 && p.clip == 3, "AddAmmo fills the clip from the ammo");
    check(m.give(5, 600, true) && p.ammo == 500, "ammo is capped at the maximum");
    check(!m.give(5, 1, true), "a full weapon is not picked up again");
    check(m.give(9, 20, true) && m.cur == 4, "a higher slot again");
    m.cyclePrev(true);
    check(m.cur == 2, "CycleToPrevWeapon goes down to the next slot with ammo");
    m.cyclePrev(true);
    check(m.cur == 0, "then to the fists");
    m.cycleNext(true);
    check(m.cur == 2, "CycleToNextWeapon skips empty slots");

    // the state machine: fired -> fire delay -> ready, an empty clip reloads
    Weapon w(5);
    w.addAmmo(4);
    check(w.clip == 3 && w.ammo == 4, "four rounds, three in the clip");
    w.setState(1);
    check(w.state == 1 && w.timer == 10 && (w.flags & 2), "SetState(1) uses the fire delay");
    w.clip = 0;
    w.state = 2;
    w.timer = 2;
    w.unload();
    check(w.state == 3 && w.timer == 4 && w.clip == 0, "Unload starts the reload countdown");
    w.reload();
    check(w.clip == 3, "Reload refills the clip from the ammo");
    check(w.reduceAmmo(2) == 2 && w.clip == 1 && w.ammo == 2, "ReduceAmmo takes from the clip and the total");
    w.addAmmo(-1);
    check(w.ammo == -1, "negative ammo means infinite");

    BulletTraces traces;
    const int32_t from[3] = {0, 0, 0}, vec[3] = {3 << 12, 0, 0};
    traces.add(from, vec, 0);
    traces.update();
    traces.update();
    traces.update();   // three frames: gone
    Player victim;   // cPed::Damage's health part: 127 at cPed::Reset, dead at 0
    check(!victim.takeDamage(25) && victim.health == 102 && !victim.dead, "a pistol round takes 25 health");
    check(victim.takeDamage(500) && victim.health == 0 && victim.dead, "health 0 kills");
    check(!victim.takeDamage(10), "the dead take no more damage");
    m.clearAll();
    check(m.cur == 0 && !m.inSlot(2), "clearAll leaves only the fists");
    fs::remove_all(dir);
    {   // cBaseProjectile::CalculateProjectileInitialVelocity: a lob that lands power x 14 units away
        const int32_t dir[3] = {0, 4096, 0};
        int32_t v[3];
        Projectiles::launchVelocity(dir, 0, v);
        check(std::abs(v[0]) < 8 && std::abs(v[1]) < 64 && v[2] > 0, "no power: straight up");
        for (int32_t power : {2048, 4096}) {
            Projectiles::launchVelocity(dir, power, v);
            int64_t y = 0, z = 0, vz = v[2];
            for (int f = 0; f < 300 && z >= 0; ++f) {   // cGrenade: Integrate v x 17 / 512, gravity 1338 x 30
                vz -= 1338 * 30;
                y += (17LL * v[1]) >> 9;
                z += (17LL * vz) >> 9;
            }
            const double landed = y / 4096.0, wanted = 14.0 * power / 4096;
            check(std::fabs(landed - wanted) < 1.0, "the throw lands at power x gProjectileRange");
            check(v[2] > v[1], "the throw is the high lob");
        }
    }
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
