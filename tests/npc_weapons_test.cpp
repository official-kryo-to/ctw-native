// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#define SDL_MAIN_HANDLED
#include "game.h"
#include "sound.h"
#include <SDL.h>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>

static int failures;
static void check(bool ok, const char* what) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
}
struct CollisionTestAccess {
    static void wall(Collision& collision, int y = 0) {
        collision.cells_.clear();
        if (!y) return;
        auto cell = std::make_unique<Collision::Cell>();
        cell->loaded = true;
        cell->boxes.push_back({0, y * 4096, 8192, 8 * 4096, 512, 8192, 0, 0});
        collision.cells_[70 * 100 + 50] = std::move(cell);
    }
};
struct GameTestAccess { static void arrested(Game& game, bool on) { game.arrested_ = on; } };
struct SoundTestAccess {
    static void prepare() {
        Sound& sound = TheSound(); sound.res_.data.resize(1);
        sound.people_.clear(); sound.stopSlots(sound.ped_);
        sound.tables_.events[0].mode = sound.tables_.events[73].mode = sound.tables_.events[99].mode = 2;
    }
    static bool npcEvent(uint32_t uid, int event, const int32_t pos[3]) {
        const Sound& sound = TheSound();
        auto found = sound.people_.find(uid);
        if (found == sound.people_.end() || !std::equal(pos, pos + 3, found->second.pos)) return false;
        for (const auto& slot : found->second.s) if (slot.event == event) return true;
        return false;
    }
    static bool playerSilent() {
        for (const auto& slot : TheSound().ped_.s) if (slot.event != 0x9C) return false;
        return true;
    }
};

static void tables(const std::filesystem::path& dir) {
    WeaponTables table{};
    for (auto& weapon : table.info) weapon.scale[0] = weapon.scale[1] = 4096;
    for (int type : {0, 1, 5}) {
        auto& weapon = table.info[type]; weapon.maxAmmo = type < 5 ? -1 : 100;
        weapon.range = (type < 5 ? 3 : 30) * 4096; weapon.damage = type < 5 ? 10 : 20;
        weapon.fireDelay = 6; weapon.reloadTime = 8; weapon.effect = type;
    }
    table.info[5].clip = 2; table.info[5].slot = 2;
    for (int32_t& life : table.traceLife) life = 3;
    std::ofstream weapons(dir / "weapon_tables.bin", std::ios::binary);
    weapons.write("CTWWPN01", 8); weapons.write((const char*)table.info, sizeof table.info);
    weapons.write((const char*)table.defaultAmmo, sizeof table.defaultAmmo);
    weapons.write((const char*)table.traceLife, sizeof table.traceLife);
    std::ofstream wanted(dir / "wanted_tables.bin", std::ios::binary);
    wanted.write("CTWWANT1", 8);
    for (uint16_t threshold : {0, 40, 150, 500, 1000, 2000, 4000, 8000}) {
        const uint16_t chances = 0;
        wanted.write((const char*)&threshold, 2); wanted.write((const char*)&chances, 2);
    }
    uint16_t crimes[26]; std::fill_n(crimes, 26, 60);
    wanted.write((const char*)crimes, sizeof crimes);
    int32_t carCrimes[13]; std::fill_n(carCrimes, 13, 16);
    wanted.write((const char*)carCrimes, sizeof carCrimes);
    const int32_t takeDowns[7] = {0, 1, 1, 2, 2, 3, 3}, subtract[7] = {0, 10, 20, 30, 40, 50, 60}, zero[7]{};
    wanted.write((const char*)takeDowns, sizeof takeDowns); wanted.write((const char*)subtract, sizeof subtract);
    for (int i = 0; i < 10; ++i) wanted.write((const char*)zero, sizeof zero);
    const int32_t failed = 100; wanted.write((const char*)&failed, 4);
}
static Player& scene(Game& game) {
    game.player = Player{}; game.player.pos[0] = game.player.pos[2] = 0; game.player.pos[1] = 10 * 4096;
    game.playerCar = -1; game.peds.clear(); game.cars.clear(); game.peds.peds.reserve(4);
    game.peds.peds.emplace_back(); auto& cop = game.peds.peds.back();
    cop.uid = 41; cop.type = 10; cop.body.playerControlled = false;
    std::fill_n(cop.body.pos, 3, 0);
    cop.body.setHeading(0); cop.body.fireDir[0] = cop.body.fireDir[2] = 0; cop.body.fireDir[1] = 4096;
    game.sectors.reset(); game.sectors.hold(0, 0); game.stats.data() = {};
    game.wanted.reset(); GameTestAccess::arrested(game, false);
    CollisionTestAccess::wall(game.collision); SoundTestAccess::prepare(); game.muzzleFlashes.beginTick();
    return cop.body;
}
static Weapon pistol() { Weapon weapon(5); weapon.addAmmo(4); return weapon; }

int main() {
    SDL_Init(0);
    const auto dir = std::filesystem::temp_directory_path() / "ctw-npc-weapons-test";
    std::filesystem::create_directories(dir); tables(dir);
    check(Weapons_Init(dir.string()), "synthetic weapon tables load");
    Game game; check(game.wanted.init(dir.string()), "synthetic wanted table makes accidental player crimes observable");
    Player& cop = scene(game);
    game.playerWeapons.give(5, 17, true);
    Weapon& playerWeapon = *game.playerWeapons.current();
    const int playerAmmo = playerWeapon.ammo, playerState = playerWeapon.state, playerTimer = playerWeapon.timer;
    Weapon gun = pistol();
    Weapons_NpcFrame(game, cop, gun, true, 41);
    check(game.player.health == 107 && cop.health == 127, "NPC bullet damages the player while excluding the shooter");
    check(gun.ammo == 3 && gun.clip == 1 && gun.state == 1 && cop.weaponType == 5 && cop.upperAim,
          "NPC owns its ammunition, fire state and aimed weapon animation");
    check(playerWeapon.ammo == playerAmmo && playerWeapon.state == playerState && playerWeapon.timer == playerTimer,
          "NPC fire never borrows the player's weapon manager or timing");
    check(SoundTestAccess::npcEvent(41, 0, cop.pos) && SoundTestAccess::playerSilent(),
          "gunfire uses the NPC's positional sound entity, independently of player sounds");
    check(game.muzzleFlashes.count() == 1, "NPC gunfire produces the existing muzzle flash");
    MuzzleFlashes::Quad flash{};
    check(game.muzzleFlashes.quad(0, flash) && flash.centre[1] < 10 && flash.centre[2] == 2,
          "muzzle flash originates from the NPC two units above its feet");
    Weapons_NpcFrame(game, cop, gun, true, 41);
    check(game.player.health == 107 && gun.ammo == 3, "fire held during native fire delay cannot shoot every tick");
    for (int i = 0; i < 2; ++i) Weapons_NpcFrame(game, cop, gun, true, 41);
    check(game.player.health == 87 && gun.ammo == 2 && gun.clip == 0, "the next shot occurs when native fire delay expires");
    bool reload = false;
    for (int i = 0; i < 20 && (gun.state || !gun.clip); ++i) {
        Weapons_NpcFrame(game, cop, gun, false, 41); reload |= gun.state == 3 || gun.state == 4;
    }
    check(reload && gun.state == 0 && gun.clip == 2 && gun.ammo == 2 && SoundTestAccess::npcEvent(41, 73, cop.pos),
          "empty clips traverse reload delay and reload time, with positional reload sound");

    scene(game); gun = pistol(); CollisionTestAccess::wall(game.collision, 4);
    Weapons_NpcFrame(game, game.peds.peds[0].body, gun, true, 41);
    check(game.player.health == 127 && gun.ammo == 3, "static walls stop NPC bullets while a shot still consumes ammunition");
    scene(game); gun = pistol(); CollisionTestAccess::wall(game.collision, 4); game.player.pos[1] = 3 * 4096;
    Weapons_NpcFrame(game, game.peds.peds[0].body, gun, true, 41);
    check(game.player.health == 107, "a wall behind the target cannot absorb a bullet before it reaches the target");
    scene(game); gun = pistol(); CollisionTestAccess::wall(game.collision, 4); game.player.pos[1] = 5 * 4096;
    Weapons_NpcFrame(game, game.peds.peds[0].body, gun, true, 41);
    check(game.player.health == 127, "native two-unit hit-search margin cannot extend NPC bullets through a wall");

    scene(game); gun = pistol(); game.player.pos[1] = 40 * 4096;
    Weapons_NpcFrame(game, game.peds.peds[0].body, gun, true, 41);
    check(game.player.health == 127, "targets outside the native weapon range are not hit");

    scene(game); gun = pistol();
    game.peds.peds.emplace_back(); auto& victim = game.peds.peds.back();
    victim.uid = 42; victim.body.pos[0] = victim.body.pos[2] = 0; victim.body.pos[1] = 5 * 4096; victim.body.health = 5;
    Weapons_NpcFrame(game, game.peds.peds[0].body, gun, true, 41);
    check(victim.body.dead && victim.deadFrames == 0 && game.player.health == 127,
          "the nearest collateral pedestrian receives the hit before a player further away");
    check(game.stats.data().bulletsThatHit == 0 && game.stats.data().peopleKilled == 0 &&
          game.stats.data().weaponKills[5] == 0 && game.wanted.rating() == 0,
          "NPC collateral kills do not increase player accuracy, kill statistics or wanted rating");

    scene(game); gun = pistol();
    VehicleInfo info{}; const int32_t carPosition[3] = {0, 7 * 4096, 0};
    game.cars.emplace_back(); game.cars[0].init(info, 0, carPosition, 0, 0);
    game.cars[0].uid = 7; game.cars[0].hx = game.cars[0].hy = 4096;
    game.cars[0].lastDamagedBy = Game::kPlayerOnFoot; game.playerCar = 0;
    game.peds.peds.emplace_back(); auto& driver = game.peds.peds.back();
    driver.uid = 42; driver.type = 10; driver.vehicle = 7; driver.seat = 0; game.cars[0].seatUser[0] = 42;
    const int before = game.cars[0].health();
    Weapons_NpcFrame(game, game.peds.peds[0].body, gun, true, 41);
    check(game.cars[0].health() == before - 5 && game.player.health == 127 &&
          game.cars[0].lastDamagedBy == 0 && game.wanted.rating() == 0,
          "NPC shots hit the player's vehicle with quarter damage without inherited player attribution");
    gun = pistol(); Weapons_NpcFrame(game, game.peds.peds[0].body, gun, true, 41);
    check(game.wanted.rating() == 0, "repeated NPC damage to a cop's vehicle cannot register a player crime");

    for (int type : {0, 1}) {
        scene(game); game.player.pos[1] = 2 * 4096; Weapon melee(type);
        Weapons_NpcFrame(game, game.peds.peds[0].body, melee, true, 41);
        check(game.player.health == 117 && game.peds.peds[0].body.health == 127,
              "NPC fists and nightstick use melee damage without self damage");
        scene(game); game.player.pos[0] = 3 * 4096; game.player.pos[1] = 0; melee = Weapon(type);
        Weapons_NpcFrame(game, game.peds.peds[0].body, melee, true, 41);
        check(game.player.health == 127, "NPC melee respects the native facing cone");
        scene(game); game.player.pos[1] = 2 * 4096; CollisionTestAccess::wall(game.collision, 1); melee = Weapon(type);
        Weapons_NpcFrame(game, game.peds.peds[0].body, melee, true, 41);
        check(game.player.health == 127, "NPC melee cannot reach through a wall");
        scene(game); melee = Weapon(type); game.peds.peds.emplace_back();
        auto& bystander = game.peds.peds.back(); bystander.uid = 42; bystander.body.health = 5;
        bystander.body.pos[0] = bystander.body.pos[2] = 0; bystander.body.pos[1] = 2 * 4096;
        Weapons_NpcFrame(game, game.peds.peds[0].body, melee, true, 41);
        check(bystander.body.dead && game.stats.data().peopleKilled == 0 && game.stats.data().meleeKills == 0 &&
              game.wanted.rating() == 0, "NPC melee collateral kills also preserve player statistics and wanted rating");
    }
    scene(game); gun = pistol(); game.player.setArmour(10);
    Weapons_NpcFrame(game, game.peds.peds[0].body, gun, true, 41);
    check(game.player.armour == 0 && game.player.health == 117, "NPC shots pass through the player's normal armour rules");
    scene(game); gun = pistol(); GameTestAccess::arrested(game, true);
    Weapons_NpcFrame(game, game.peds.peds[0].body, gun, true, 41);
    check(game.player.health == 127 && gun.ammo == 4, "arrested players are protected from new NPC attacks");
    scene(game); gun = pistol(); game.peds.peds[0].body.dead = true;
    Weapons_NpcFrame(game, game.peds.peds[0].body, gun, true, 41);
    check(game.player.health == 127 && gun.ammo == 4, "dead NPCs cannot fire");
    scene(game); Weapon unsupported(17); unsupported.ammo = 5;
    Weapons_NpcFrame(game, game.peds.peds[0].body, unsupported, true, 41);
    check(game.player.health == 127 && unsupported.ammo == 5 && game.projectiles.list().empty(),
          "unsupported NPC weapon types cannot invoke player projectile paths");
    SoundTestAccess::prepare(); std::filesystem::remove_all(dir); SDL_Quit();
    return failures ? 1 : 0;
}
