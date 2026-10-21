// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// Sword contact timing and geometry on synthetic data; no original weapon tables are embedded here.
#define SDL_MAIN_HANDLED
#include "game.h"
#include "sound.h"
#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

static int failures;
static void check(bool ok, const char* why) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", why); ++failures; }
}

struct CollisionTestAccess {
    static void wall(Collision& collision, bool enabled) {
        collision.cells_.clear();
        if (!enabled) return;
        auto cell = std::make_unique<Collision::Cell>();
        cell->loaded = true;
        cell->boxes.push_back({0, 4096, 8192, 8192, 512, 8192, 0, 0});
        collision.cells_[70 * 100 + 50] = std::move(cell);
    }
};

struct ParticlesTestAccess {
    static int active(const Emitter& e) { return e.alive_; }
    static const Particle* first(const Emitter& e) {
        for (const Particle& p : e.parts_) if (p.active) return &p;
        return nullptr;
    }
};

struct PlayerTestAccess {
    static void frames(Player& player,int previous,int current) {
        player.previousUpperFrame_=previous;player.frameUpper_=current<<8;
    }
};

struct SoundTestAccess {
    static void prepare() {
        Sound& sound=TheSound();sound.res_.data.resize(1);
        sound.tables_.events[152].mode=sound.tables_.events[9].mode=2;sound.stopSlots(sound.ped_);
    }
    static int swordEvents() {
        Sound& sound=TheSound();int count=0;
        for (const auto& slot:sound.ped_.s) {
            if (slot.event!=152) continue;
            check(slot.volume==127 && slot.radius==200,"sword swings use the original event volume and squared radius");
            ++count;
        }
        sound.stopSlots(sound.ped_);return count;
    }
    static int fistEvents() {
        Sound& sound=TheSound();int count=0;
        for(const auto& slot:sound.ped_.s) if(slot.event==9) {
            check(slot.volume==50 && slot.radius==200,"unarmed swings use their original quieter sound event");
            ++count;
        }
        sound.stopSlots(sound.ped_);return count;
    }
};

static void scene(Game& g) {
    g.player = Player{};
    std::fill(g.player.pos, g.player.pos + 3, 0);
    g.playerCar = -1;
    g.cars.clear();
    g.peds.clear();
    CollisionTestAccess::wall(g.collision, false);
    g.playerWeapons.clearAll();
    g.aimOverride.on = true;
    g.aimOverride.target[0] = 0; g.aimOverride.target[1] = 20 * 4096; g.aimOverride.target[2] = 0;
    Weapons_PlayerFrame(g, false, 0);
    check(g.playerWeapons.give(3, 0, true), "a sword can be equipped");
}

static void ped(Game& g, int x, int y) {
    Pedestrians::Ped p;
    p.uid = (uint32_t)g.peds.peds.size() + 1;
    p.body.playerControlled = false;
    p.body.pos[0] = x * 4096; p.body.pos[1] = y * 4096; p.body.pos[2] = 0;
    g.peds.peds.push_back(std::move(p));
}

static void swing(Game& g) {
    Weapons_PlayerFrame(g, true, 0);
    for (int i = 0; i < 6; ++i) Weapons_PlayerFrame(g, false, 0);
}

int main() {
    SDL_Init(0);
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "ctw-weapon-parity-test";
    fs::create_directories(dir);
    WeaponTables table{};
    for(auto& weapon:table.info) weapon.scale[0]=weapon.scale[1]=4096;
    table.info[0].maxAmmo = table.info[3].maxAmmo = -1;
    table.info[3].slot = 1;
    table.info[3].damage = 17;
    table.info[0].damage = 10;
    table.info[0].range = 4 * 4096;
    table.info[3].range = 4 * 4096;
    table.info[3].fireDelay = 24;
    for (int32_t& life : table.traceLife) life = 3;   // the loader rejects trace lifetimes outside 1..127
    {   // the sniper rifle: 40 units of range, 50 damage
        WeaponInfo& s = table.info[18];
        s.maxAmmo = 100; s.slot = 5; s.clip = 1; s.fireDelay = 10; s.reloadTime = 20; s.damage = 50; s.range = 40 * 4096;
    }
    {   // the taser: 12 units of range, 2 damage a shock
        WeaponInfo& t = table.info[19];
        t.maxAmmo = -1; t.slot = 4; t.fireDelay = 10; t.reloadTime = 0; t.damage = 2; t.range = 12 * 4096;
    }
    {   // the flamethrower: 10 units of range, 5 damage
        WeaponInfo& f = table.info[16];
        f.maxAmmo = 500; f.slot = 6; f.fireDelay = 0; f.reloadTime = 20; f.damage = 5; f.range = 10 * 4096;
    }
    for (int type : {5, 10}) {   // a pistol and a shotgun for the muzzle flashes
        WeaponInfo& gun = table.info[type];
        gun.maxAmmo = 500; gun.slot = type == 5 ? 2 : 3; gun.clip = 10;
        gun.fireDelay = 10; gun.reloadTime = 20; gun.damage = 10; gun.range = 30 << 12;
    }
    {
        std::ofstream file(dir / "weapon_tables.bin", std::ios::binary);
        file.write("CTWWPN01", 8);
        file.write((const char*)table.info, sizeof table.info);
        file.write((const char*)table.defaultAmmo, sizeof table.defaultAmmo);
        file.write((const char*)table.traceLife, sizeof table.traceLife);
    }
    check(Weapons_Init(dir.string()), "synthetic weapon tables load");
    Game& g = TheGame();
    scene(g);
    ped(g, 0, 3);
    ped(g, 3, 0);
    ped(g, 0, -3);
    ped(g, 0, 5);
    Vehicle car;
    VehicleInfo info{};
    const int32_t at[3] = {0, 2 * 4096, 0};
    car.init(info, 0, at, 0, 0);
    car.hx = car.hy = car.hz = 4096;
    g.cars.push_back(std::move(car));
    Weapons_PlayerFrame(g, true, 0);
    for (int i = 0; i < 5; ++i) Weapons_PlayerFrame(g, false, 0);
    check(g.peds.peds[0].body.health == 127 && g.cars[0].health() == 255,
          "a sword swing does not damage anything during its first six animation ticks");
    Weapons_PlayerFrame(g, false, 0);
    check(g.peds.peds[0].body.health == 110, "the seventh swing tick applies the weapon table's ped damage");
    check(g.cars[0].health() == 252, "sword contact takes three vehicle health points");
    check(g.peds.peds[1].body.health == 127 && g.peds.peds[2].body.health == 127 && g.peds.peds[3].body.health == 127,
          "sword contact excludes peds outside the forward cone or weapon range");
    for (int i = 0; i < 20; ++i) Weapons_PlayerFrame(g, false, 0);
    check(g.peds.peds[0].body.health == 110 && g.cars[0].health() == 252,
          "a released sword swing applies contact only once");

    scene(g);
    ped(g, 0, 3);
    Weapons_PlayerFrame(g, true, 0);
    g.peds.peds[0].body.pos[1] = 8 * 4096;
    for (int i = 0; i < 6; ++i) Weapons_PlayerFrame(g, false, 0);
    check(g.peds.peds[0].body.health == 127, "contact uses the victim's position at the delayed hit");

    scene(g);
    ped(g, 0, 3);
    CollisionTestAccess::wall(g.collision, true);
    swing(g);
    check(g.peds.peds[0].body.health == 127, "a static wall blocks sword damage");

    scene(g);
    ped(g, 0, 3);
    Weapons_PlayerFrame(g, true, 0);
    g.playerWeapons.setUse(0, false);
    for (int i = 0; i < 10; ++i) Weapons_PlayerFrame(g, false, 0);
    check(g.peds.peds[0].body.health == 127, "a sword stored in another slot does not hit");

    scene(g);SoundTestAccess::prepare();
    Weapons_PlayerFrame(g,true,0);
    check(SoundTestAccess::swordEvents()==0,"pressing fire does not prematurely play the sword's animation sound");
    for (const auto& frames: {std::pair<int,int>{6,7},{7,8},{8,8},{8,9},{10,11},{11,12},{12,12},{12,13}}) {
        PlayerTestAccess::frames(g.player,frames.first,frames.second);
        Weapons_PlayerFrame(g,false,0);
        const bool cue=(frames.first==7 && frames.second==8)||(frames.first==11 && frames.second==12);
        check(SoundTestAccess::swordEvents()==(int)cue,
              "sword sound cues fire only on the exact seven-to-eight and eleven-to-twelve frame changes");
    }
    g.player.attached=true;PlayerTestAccess::frames(g.player,7,8);
    Weapons_PlayerFrame(g,false,0);
    check(SoundTestAccess::swordEvents()==0,"attached players cannot emit an on-foot sword swing cue");
    g.player.attached=false;

    scene(g);g.playerWeapons.setUse(0,false);SoundTestAccess::prepare();
    Weapons_PlayerFrame(g,true,0);
    check(SoundTestAccess::fistEvents()==1,"an unarmed swing plays its original firing sound on its first animation tick");
    Weapons_PlayerFrame(g,false,0);
    check(SoundTestAccess::fistEvents()==0,"the unarmed swing's cooling animation does not repeat its sound");

    scene(g);
    auto& movingWeapon=const_cast<WeaponInfo&>(Weapons_Info(3));
    movingWeapon.scale[0]=2048;movingWeapon.scale[1]=0;
    Weapons_PlayerFrame(g,true,0);
    check(g.player.weaponMovementScale==0,"holding fire selects the weapon's firing movement scale, including zero");
    Player::Input moving;moving.moving=true;moving.maxLevel=2;
    for(int i=0;i<30;++i)g.player.act(moving,nullptr);
    check(g.player.level()==2 && g.player.currentSpeed()==0,"a zero firing factor stops physical movement without changing the requested gait");
    Weapons_PlayerFrame(g,false,0);
    check(g.player.weaponMovementScale==2048 && g.player.currentSpeed()==24540,
          "releasing fire restores the passive factor with native base-before-gait truncation");
    movingWeapon.scale[0]=movingWeapon.scale[1]=4096;

    // cMuzzleFlashes: one flash per shot at the trace start, lying along the fire direction (+y here)
    auto flashAfterShot = [&](int type, float length, float width) {
        scene(g);
        check(g.playerWeapons.give(type, 50, true), "a gun can be equipped");
        g.muzzleFlashes.beginTick();
        Weapons_PlayerFrame(g, true, 0);
        check(g.muzzleFlashes.count() == 1, "one muzzle flash per shot (shotguns flash only the centre pellet)");
        MuzzleFlashes::Quad q{};
        check(g.muzzleFlashes.quad(0, q), "the flash has geometry");
        const float r = 0x19A / 4096.f + 1e-3f;
        check(std::fabs(q.halfLength - length) <= r && std::fabs(q.halfWidth - 2.f * width) <= 2.f * r,
              "the flash size is the type's base size within the +-0.1 jitter");
        check(std::fabs(q.forward[1] - 1.f) < 0.01f && std::fabs(q.right[0] - 1.f) < 0.01f,
              "the flash points along the fire direction");
        check(std::fabs(q.centre[1] - (1.5f + q.halfLength)) < 0.01f && std::fabs(q.centre[2] - 2.f) < 0.01f,
              "the flash starts at the muzzle, 2 up and 1.5 out");
        MuzzleFlashes::Quad again{};
        g.muzzleFlashes.quad(0, again);
        check(again.halfLength == q.halfLength && again.halfWidth == q.halfWidth,
              "redrawing within a tick keeps the rolled size");
        g.muzzleFlashes.beginTick();
        check(g.muzzleFlashes.count() == 0, "flashes last one simulation frame");
    };
    flashAfterShot(5, 2.f, 0xCCD / 4096.f);   // pistols and SMGs: type 2
    flashAfterShot(10, 3.f, 1.25f);           // rifles and shotguns: type 0
    MuzzleFlashes pool;
    const int32_t origin[3] = {0, 0, 0};
    pool.add(origin, 0, -1);
    check(pool.count() == 0, "flash type -1 adds nothing");
    for (int i = 0; i < 9; ++i) pool.add(origin, 0, 4);
    check(pool.count() == 8, "at most eight flashes a frame");

    // cPed::Damage's blood through the local general particle controller's emitter at the player
    g.blood = TheParticles().add<BloodEmitter>(g.player.pos);
    auto bloodAfter = [&](int type, int damage, int y) {
        scene(g);
        g.blood->dying = false;
        for (int i = 0; i < 12; ++i) g.blood->process();   // let earlier drops expire
        ped(g, 0, y);
        if (type <= 4) {
            (void)damage;   // melee damage comes from the loaded table
            if (type == 0) g.playerWeapons.setUse(0, false);
            else g.playerWeapons.give(type, 0, true);
            Weapons_PlayerFrame(g, true, 0);
            for (int i = 0; i < 8; ++i) Weapons_PlayerFrame(g, false, 0);
        } else {
            const int32_t dir[3] = {0, 4096, 0}, from[3] = {0, 0, 0};
            g.peds.bulletHit(g, g.peds.peds[0], damage, dir, 0, from, type == 15 ? 2 : type == 3 ? 0 : 1);
        }
        return ParticlesTestAccess::active(*g.blood);
    };
    check(bloodAfter(5, 10, 3) == 15, "a bullet of at least five damage bleeds fifteen drops");
    check(bloodAfter(5, 4, 3) == 0, "a bullet under five damage does not bleed");
    check(bloodAfter(15, 1, 3) == 15, "a minigun round always bleeds fifteen drops");
    check(bloodAfter(5, 10, 800) == 0, "victims beyond the emitter's 16-bit range get no blood");
    check(bloodAfter(0, 10, 3) == 4, "a fist hit bleeds four drops");
    check(bloodAfter(3, 17, 3) == 1, "a sword hit (type 11) bleeds one drop");
    {
        const Particle* drop = ParticlesTestAccess::first(*g.blood);
        bloodAfter(5, 10, 3);
        drop = ParticlesTestAccess::first(*g.blood);
        check(drop && drop->colour == 0x040D && drop->alpha == 0x1D && drop->life == 0x14,
              "blood drops are dark red, nearly opaque and live ten frames");
        check(drop && drop->v[2] >= 0 && drop->v[2] <= 2 && std::abs(drop->v[0]) <= 3 && drop->angle == (uint16_t)drop->spin,
              "blood drops drift slowly upward and spin from their starting angle");
        for (int i = 0; i < 10; ++i) g.blood->process();
        check(ParticlesTestAccess::active(*g.blood) == 0, "blood drops are gone after ten frames");
    }
    (void)bloodAfter;

    // cFlameThrower: one flame a frame while held; from the second shot on the first ped in the cone burns
    scene(g);
    g.fires.clear();
    ped(g, 0, 5);
    ped(g, 4, 0);   // outside the 20-degree cone
    check(g.playerWeapons.give(16, 100, true), "a flamethrower can be equipped");
    Weapons_PlayerFrame(g, true, 0);
    check(g.playerWeapons.current()->ammoCount() == 99, "each frame of fire uses one unit of fuel");
    check(!g.fires.burning(Fires::Ped, g.peds.peds[0].uid), "the first shot only creates the flame emitter");
    Weapons_PlayerFrame(g, true, 0);
    check(g.fires.burning(Fires::Ped, g.peds.peds[0].uid), "the ped in front catches fire");
    check(!g.fires.burning(Fires::Ped, g.peds.peds[1].uid), "a ped outside the cone does not");
    check(g.playerWeapons.current()->flame.jetReady, "the pilot light is built while the flamethrower is held");
    const int before = g.peds.peds[0].body.health;
    for (int i = 0; i < 20; ++i) { ++g.frame; g.fires.process(g); }
    check(g.peds.peds[0].body.health == before - 10, "a burning ped loses a point every second frame");
    for (int i = 0; i < 400 && !g.peds.peds[0].body.dead; ++i) { ++g.frame; g.fires.process(g); }
    check(g.peds.peds[0].body.dead, "a ped set on fire by the player burns until it dies");
    check(!g.fires.burning(Fires::Ped, g.peds.peds[0].uid), "death puts the fire out");
    Weapons_PlayerFrame(g, false, 0);
    check(!g.playerWeapons.current()->flame.emitter || g.playerWeapons.current()->flame.flags != 0xFF,
          "releasing the button keeps the weapon usable");
    {   // a vehicle in the cone catches a 300-frame fire and takes type-5 damage
        scene(g);
        g.fires.clear();
        Vehicle car;
        VehicleInfo info{};
        const int32_t at[3] = {0, 5 * 4096, 0};
        car.init(info, 0, at, 0, 0);
        car.hx = car.hy = car.hz = 4096;
        car.uid = 77;
        g.cars.push_back(std::move(car));
        g.playerWeapons.give(16, 100, true);
        Weapons_PlayerFrame(g, true, 0);
        Weapons_PlayerFrame(g, true, 0);
        check(g.fires.burning(Fires::Car, 77) && g.cars[0].health() == 250, "a vehicle catches fire and takes damage");
        int frames = 0;
        while (g.fires.burning(Fires::Car, 77) && frames < 400) { ++g.frame; g.fires.process(g); ++frames; }
        check(frames >= 148 && frames <= 150, "a vehicle fire lasts 300 lifetime units at 2 a frame");
    }

    {   // cExplosionMolotov: three small loose fires flung outward, one central fire with a petrol flame
        scene(g);
        g.fires.clear();
        const int32_t at[3] = {0, 20 * 4096, 0};
        g.fires.molotov(g, at, true);
        check(g.fires.list().size() == 4, "a molotov starts four fires");
        const Fires::Fire& centre = g.fires.list().back();
        check(centre.life == 400 && centre.petrol && centre.emitter && centre.petrolEmitter,
              "the central fire burns 400 units with a fire and a petrol emitter");
        bool spread = true;
        for (size_t i = 0; i + 1 < g.fires.list().size(); ++i) {
            const Fires::Fire& f = g.fires.list()[i];
            spread = spread && f.life >= 100 && f.life < 400 && (f.vel[0] || f.vel[1]) && !f.emitter;
        }
        check(spread, "the small fires live 100 to 398 units and slide outward");
        ped(g, 0, 21);
        ++g.frame;
        g.fires.process(g);
        check(g.fires.burning(Fires::Ped, g.peds.peds.back().uid), "a ped walking into a molotov fire catches fire");
        g.fires.clear();
    }

    {   // cTazer: the dart flies, wires up the ped it hits, shocks him while the button is held
        scene(g);
        g.fires.clear();
        g.projectiles.clear();
        ped(g, 0, 6);
        check(g.playerWeapons.give(19, 0, true), "a taser can be equipped");
        Weapon& taser = *g.playerWeapons.current();
        check(taser.type == 19 && taser.ammoCount() == -1 && taser.tazer.charge == 0x65, "the taser has no ammo, full charge");
        for (int i = 0; i < 10 && taser.state != 0; ++i) Weapons_PlayerFrame(g, false, 0);   // the switch-in reload
        Weapons_PlayerFrame(g, true, 0);
        check((taser.tazer.flags & 7) == 1 && g.projectiles.alive(taser.tazer.cartridge), "firing shoots a dart");
        int frames = 0;
        while ((taser.tazer.flags & 7) != 2 && frames < 20) {
            ++g.frame;
            g.projectiles.process(g);
            Weapons_PlayerFrame(g, true, 0);
            ++frames;
        }
        check((taser.tazer.flags & 7) == 2 && taser.tazer.ped == g.peds.peds[0].uid, "the dart wires up the ped it hits");
        check(frames <= 4, "the dart crosses 6 units in a few frames (2.5 a frame)");
        check(g.peds.peds[0].body.health == 125, "the dart's hit is a jolt of the weapon's damage");
        const int charge = taser.tazer.charge;
        int shocks = 0;
        for (int i = 0; i < 300 && !Fires_PedOnFire(g, g.peds.peds[0].uid) && !g.peds.peds[0].body.dead; ++i) {
            const int before = g.peds.peds[0].body.health;
            ++g.frame;
            g.projectiles.process(g);
            Weapons_PlayerFrame(g, true, 0);
            if (g.peds.peds[0].body.health < before) ++shocks;
        }
        check(Fires_PedOnFire(g, g.peds.peds[0].uid) && shocks == 6, "the sixth shock sets the ped on fire");
        check(taser.tazer.charge < charge, "shocking drains the charge");
        check(taser.tazer.wireReady && taser.tazer.arcCount >= 10 && taser.tazer.arcCount < 30,
              "a shock draws the wire and a 10..29-point arc");
        for (int i = 0; i < 60 && (taser.tazer.flags & 7); ++i) Weapons_PlayerFrame(g, false, 0);
        check((taser.tazer.flags & 7) == 0 && taser.tazer.ped == 0, "letting go releases the ped after the shock");
        const int low = taser.tazer.charge;
        Weapons_PlayerFrame(g, false, 0);
        check(taser.tazer.charge == low + 1, "the charge comes back by one an idle frame");
        g.fires.clear();
        g.projectiles.clear();
    }
    {   // a dart that misses comes back when the button is let go
        scene(g);
        g.projectiles.clear();
        g.playerWeapons.give(19, 0, true);
        Weapon& taser = *g.playerWeapons.current();
        taser.tazer = TazerState{};
        for (int i = 0; i < 10 && taser.state != 0; ++i) Weapons_PlayerFrame(g, false, 0);
        Weapons_PlayerFrame(g, true, 0);
        const uint32_t dart = taser.tazer.cartridge;
        Weapons_PlayerFrame(g, false, 0);
        ++g.frame;
        g.projectiles.process(g);
        check(!g.projectiles.alive(dart) && (taser.tazer.flags & 7) == 0, "releasing the button drops the dart");
    }

    {   // cSniperCam: 30 units up, 4 ahead; the stick moves the aim a unit a frame (0.4 zoomed), within range
        scene(g);
        g.player.fwd[0] = 0; g.player.fwd[1] = 0x1000;
        SniperScope& s = g.scope;
        s = SniperScope{};
        s.enter(g);
        check(s.active && s.pos[0] == 0 && s.pos[1] == 4 * 4096 && s.pos[2] == 0x1E000 && s.fov == 0x1555,
              "the scope starts 4 units ahead of the player, 30 up, at 30 degrees");
        s.update(g, 0.f, 1.f);
        check(std::abs(s.pos[1] - 5 * 4096) <= 2 && std::abs(s.pos[0]) <= 2, "a full stick moves the aim a unit forward");
        s.update(g, 1.f, 0.f);
        check(std::abs(s.pos[0] - 4096) <= 2, "right on the stick moves the aim to the right of the view");
        s.zoomPressed = true;
        s.update(g, 0.f, 1.f);
        check(s.zoomed && s.fov == 0x1555 - 0x38E && std::abs(s.pos[1] - (5 * 4096 + 0x666)) <= 4,
              "zooming narrows the view by 5 degrees a frame and slows the aim to 0.4");
        for (int i = 0; i < 5; ++i) s.update(g, 0.f, 0.f);
        check(s.fov == 0xE39, "zoomed in, the view is 20 degrees");
        for (int i = 0; i < 200; ++i) s.update(g, 0.f, 1.f);
        check(std::abs(s.pos[1] - 40 * 4096) <= 8, "the aim point stays within the rifle's range");
        // the shot hits the ped whose circle holds the aim point
        s.pos[0] = 0; s.pos[1] = 20 * 4096;
        ped(g, 1, 20);   // 1 unit off the aim point: inside its 1.25-unit circle
        ped(g, 0, 23);
        g.playerWeapons.give(18, 10, true);
        Sniper_CreateBullet(g, *g.playerWeapons.current());
        check(g.peds.peds[0].body.health == 77 && g.peds.peds[1].body.health == 127,
              "a sniper shot takes the rifle's damage from the ped under the aim point only");
        s.active = false;
    }

    g.cars.clear(); g.peds.clear();
    fs::remove_all(dir);
    SDL_Quit();
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
