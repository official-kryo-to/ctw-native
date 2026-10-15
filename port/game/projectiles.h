// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// Projectiles: ports of cBaseProjectile and the player's cGrenade, cMolotov, cFlashBang, cProximityMine and cRocket,
// with the launchers' CreateBullet (cGrenadeLauncher, cMolotovLauncher, cFlashBangLauncher, cMineLauncher,
// cRocketLauncher) and cBaseProjectile::CalculateProjectileInitialVelocity.
//
// cBaseProjectile (a cObject): velocity (cSimpleMover +0xB8) is in units per second, 30 x what the launcher passes;
// Integrate moves it by v x 17 / 512 a frame and sweeps its sphere (+0x68, the bound radius) against the statics
// (CCollision::GetSphereCollision 0xF00); on a hit the projectile is put at the contact plus the normal x radius and
// OnCollision runs. Lifetime (+0x110, -1 = 90) drops by 2 a frame; at 60 HelperExplosionCallbacks warns the peds
// (not ported); at 0 the projectile is destroyed, which is when the fused ones explode. Entity collisions follow the
// collide-against flags: 2 peds, 4 dynamic props (not ported), 8 vehicles.
//
//   cGrenade     gravity 1338 x 30 a frame, bounces at half speed (event 107 when still fast), water: no explosion
//                (types 9 / 10 are not ported); explodes (type 0, cExplosionGeneric) when its fuse runs out.
//   cMolotov     half the launch velocity, gravity 1370112 x 30 >> 12; stops on the first thing it hits and bursts
//                the next frame: a vehicle -> type 0, anything else -> type 2 (cExplosionMolotov) one unit up.
//   cFlashBang   grenade gravity, bounces at 0.8; type 5 (cExplosionFlashBang) when the fuse runs out.
//   cProximityMine  dropped at the feet with lifetime 300 held while its owner is within 10 units; once he walks
//                away it arms (collides with peds and vehicles, 5-unit sphere), beeps (sfx 696 every 251 ms) and
//                explodes (type 1, cExplosionGeneric) on contact or after ~5 seconds.
//   cRocket      6 units a frame from the muzzle, no gravity, a 1.66-unit sweep; explodes on whatever it
//                hits (type 3, cExplosionGeneric), or after its 90-frame lifetime.
//
// Throw power (cPed +0x1E0, cIPhonePad +0x638): 0..128; StandardConvertFromPower gives the player 32 x power (Q12
// fraction of gProjectileRange = 14 units). With the new projectile method (gbUseNewProjectileMethod = 1) the launch
// speed is lerped from 35 (power 0) to 15 + sqrt(range x 294) (power 1) and pitched to land at that distance.
#pragma once
#include <cstdint>
#include <vector>

class Game;
struct Model;

class Projectiles {
public:
    enum Kind { Grenade = 20, Molotov = 21, FlashBang = 22, Mine = 23, Rocket = 17, Tazer = 19 };
    void clear();
    // the launchers' CreateBullet for the player (fireDir: cPed::FireDirection; power 0..128)
    void throwGrenade(Game& g, int kind, int power);
    void dropMine(Game& g);
    void fireRocket(Game& g);
    // cTazer::CreateBullet: a cTazerCartridge from `from` at `launch` a frame; returns its id
    uint32_t fireTazer(Game& g, const int32_t from[3], const int32_t launch[3]);
    bool alive(uint32_t id) const { return find(id) != nullptr; }
    void expire(uint32_t id);   // lifetime 0: gone on its next Process
    bool mineWaiting() const;   // cMineLauncher::Reload: the last mine is not armed yet (IsMineActive)
    // cBaseProjectile::CalculateProjectileInitialVelocity: dir Q12 (horizontal), power Q12 (0..1 of the range)
    static void launchVelocity(const int32_t dir[3], int32_t power, int32_t out[3]);
    void process(Game& g);      // Process + Integrate of every projectile, once a frame
    void render(Game& g) const;
    struct Projectile {
        int kind = Grenade;
        uint32_t id = 0;
        int32_t pos[3] = {}, prev[3] = {}, vel[3] = {};   // vel: units per second (cSimpleMover +0xB8)
        int16_t dir[3] = {0, 0x1000, 0};                  // the launch direction (the model's facing)
        int16_t lifetime = 90;                            // +0x110
        int32_t radius = 2048;                            // the bound radius (+0x68)
        uint32_t collide = 0xF0C;                         // collide-against flags
        bool inWater = false;                             // +0x64 bit 1
        bool hitCar = false;                              // cMolotov +0x113
        bool dead = false;
        uint32_t beepMs = 0;                              // cProximityMine +0x114
        bool retracting = false;                          // cTazerCartridge +0x113
    };
    const Projectile* find(uint32_t id) const;
    const std::vector<Projectile>& list() const { return list_; }
private:
    std::vector<Projectile> list_;
    uint32_t nextId_ = 1, lastMine_ = 0;
    uint16_t models_[3] = {0xFFFF, 0xFFFF, 0xFFFF};       // gGlobalAssetMgr +0x23E (gGameDir[11]): grenade, molotov, rocket
    bool modelsLoaded_ = false;
    void loadModels() const;
    void destroy(Game& g, Projectile& p);
    void onCollision(Game& g, Projectile& p, const int32_t normal[3], int entity, bool water);
    void tazerRetract(Game& g, Projectile& p);   // the dart flies back to the shooter, or goes when close
};

// CCollision::GetSphereCollision against the statics (0xF00): the earliest contact of a sphere moving a -> b.
bool Projectiles_SphereStatics(class Collision& col, const int32_t a[3], const int32_t b[3], int32_t r,
                               int32_t contact[3], int32_t n[3]);
