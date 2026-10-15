// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "projectiles.h"
#include "taser.h"
#include "fixedmath.h"
#include "game.h"
#include "sound.h"
#include "weapons.h"
#include "gfx/assets.h"
#include "gfx/model.h"
#include "os/os.h"
#include "os/pak.h"
#include "world/props.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>

namespace {
int fastsin(int a) { return (int)(sinf((float)a * 9.587378e-05f) * 4096.f); }

int16_t ATan(int32_t x) {   // ATan(cFixed): atanf(x / 4096) x 10430, clamped to a short
    const int v = (int)(float)(atanf((float)x * 0.00024414f) * 10430.0f);
    return (int16_t)std::clamp(v, -32768, 0x7FFF);
}

// cBaseProjectile's globals (set by its static initialiser)
constexpr int32_t kRange = 0xE000, kSpeedAtMaxRange = 0xF000, kSpeedAtMinRange = 0x23000;

// cBaseProjectile::CalculateProjectileInitialVelocity: dir (Q12, horizontal), power Q12 (0..1 of the range)
void initialVelocity(const int32_t dir[3], int32_t power, int32_t out[3]) {
    const int64_t v5 = (int32_t)((uint32_t)std::sqrt((double)(uint64_t)(1204200LL * kRange)) + kSpeedAtMaxRange +
                                 (int32_t)(((int64_t)(kSpeedAtMinRange - kSpeedAtMaxRange) * (4096 - power)) >> 12));
    const int64_t t = (int64_t)((0x125FE800000LL * (int32_t)(((int64_t)kRange * power) >> 12)) & (int64_t)0xFFFFFFFF00000000ULL) /
                      (v5 * v5);
    int v7;
    if ((t & 0xFFFFFFFF00LL) == 0x100000) {
        v7 = 0x2000;
    } else {
        const int64_t q = (int32_t)(t >> 8);
        const int64_t root = (int32_t)(uint32_t)std::sqrt((double)(uint64_t)(0x1000000 - q * q));
        const int32_t v13 = root ? (int32_t)((uint64_t)((int64_t)((t << 24) & (int64_t)0xFFFFFFFF00000000ULL) / root) >> 20) : 0;
        v7 = (int)((uint32_t)(0x8000 - ATan(v13)) >> 1);
    }
    const int64_t v11 = (v5 * fastsin(v7 + 0x4000)) >> 12;
    out[0] = (int32_t)((v11 * dir[0]) >> 12);
    out[1] = (int32_t)((v11 * dir[1]) >> 12);
    out[2] = (int32_t)((v5 * fastsin(v7)) >> 12);
}

// CCollision::GetSphereCollision against the statics (meshes 0x100, boxes 0x200, cylinders 0x400, the ground slab
// 0x800): the earliest contact on the move a -> b; the normal points from the contact to the sphere's centre there.
bool sphereStatics(Collision& col, const int32_t a[3], const int32_t b[3], int32_t r, bool skipFlag4,
                   int32_t contact[3], int32_t n[3], bool& water) {
    const int64_t mx = b[0] - a[0], my = b[1] - a[1], mz = b[2] - a[2];
    const int32_t R = (int32_t)std::sqrt((double)(mx * mx + my * my + mz * mz)) + r;
    Collision::Candidates cand;
    col.candidates(a, R, true, cand);
    int32_t best = 0x7fffffff;
    const Collision::Box* bestBox = nullptr;
    int32_t hit[3], nn[3], t;
    for (const Collision::Box* box : cand.boxes) {
        if (skipFlag4 && (box->flags & 4)) continue;
        if (Collision::sweptSphereVBox(a, b, r, *box, hit, t) && t < best) {
            best = t; bestBox = box; std::copy(hit, hit + 3, contact);
        }
    }
    for (const Collision::Cyl* c : cand.cyls)
        if (Collision::sweptSphereVCylinder(a, b, r, *c, hit, nn, t) && t < best) {
            best = t; bestBox = nullptr; std::copy(hit, hit + 3, contact);
        }
    for (const Collision::TriRef& tr : cand.tris)
        if (Collision::sweptSphereVTri(a, b, r, tr, hit, nn, t) && t < best) {
            best = t; bestBox = nullptr; std::copy(hit, hit + 3, contact);
        }
    if (best == 0x7fffffff) return false;
    water = false;
    if (bestBox == &cand.slab)   // the surface type of the ground under the move (2 = water)
        water = col.ground(a[0] / 4096.f, a[1] / 4096.f, a[2] / 4096.f).surface == 2;
    for (int i = 0; i < 3; ++i) n[i] = (a[i] - contact[i]) + (int32_t)(((int64_t)(b[i] - a[i]) * best) >> 12);
    if (!n[0] && !n[1] && !n[2]) n[2] = 0x1000;
    Normalise3(n);
    return true;
}

// a sphere against a vehicle's collision box (GetWorldCollisionPos: the box centre is hz above its position)
bool sphereVehicle(const Vehicle& c, const int32_t p[3], int32_t r, int32_t n[3]) {
    const int64_t q[3] = {(int64_t)p[0] - c.pos[0], (int64_t)p[1] - c.pos[1], (int64_t)p[2] - (c.pos[2] + c.hz)};
    int32_t l[3] = {(int32_t)((q[0] * c.right[0] + q[1] * c.right[1] + q[2] * c.right[2]) >> 12),
                    (int32_t)((q[0] * c.fwd[0] + q[1] * c.fwd[1] + q[2] * c.fwd[2]) >> 12),
                    (int32_t)((q[0] * c.up[0] + q[1] * c.up[1] + q[2] * c.up[2]) >> 12)};
    const int32_t h[3] = {c.hx, c.hy, c.hz};
    int32_t d[3];
    int64_t d2 = 0;
    for (int k = 0; k < 3; ++k) { d[k] = l[k] - std::clamp(l[k], -h[k], h[k]); d2 += (int64_t)d[k] * d[k]; }
    if (d2 > (int64_t)r * r) return false;
    if (!d2) d[2] = 0x1000;   // the centre is inside: push up
    int32_t w[3];
    for (int k = 0; k < 3; ++k)
        w[k] = (int32_t)(((int64_t)d[0] * c.right[k] + (int64_t)d[1] * c.fwd[k] + (int64_t)d[2] * c.up[k]) >> 12);
    if (!w[0] && !w[1] && !w[2]) w[2] = 0x1000;
    Normalise3(w);
    std::copy(w, w + 3, n);
    return true;
}

// a sphere against a ped (a vertical cylinder of its bound radius from the feet, 2 units high)
bool spherePed(const int32_t ped[3], const int32_t p[3], int32_t r) {
    const int64_t dx = p[0] - ped[0], dy = p[1] - ped[1];
    const int64_t rr = (int64_t)r + Player::kRadius;
    if (dx * dx + dy * dy > rr * rr) return false;
    return p[2] + r >= ped[2] && p[2] - r <= ped[2] + 0x2000;
}

std::map<int, std::unique_ptr<Model>>& modelCache() { static std::map<int, std::unique_ptr<Model>> m; return m; }
const Model* model(int id) {
    if (id == 0xFFFF) return nullptr;
    auto& cache = modelCache();
    auto it = cache.find(id);
    if (it != cache.end()) return it->second.get();
    auto m = std::make_unique<Model>();
    std::vector<uint8_t> raw;
    if (!Assets_Pak().read(id, raw) || !m->parse(raw)) m.reset();
    const Model* r = m.get();
    cache[id] = std::move(m);
    return r;
}
}   // namespace

bool Projectiles_SphereStatics(Collision& col, const int32_t a[3], const int32_t b[3], int32_t r, int32_t contact[3],
                               int32_t n[3]) {
    bool water = false;
    return sphereStatics(col, a, b, r, false, contact, n, water);
}

void Projectiles::clear() { list_.clear(); lastMine_ = 0; }
void Projectiles::launchVelocity(const int32_t dir[3], int32_t power, int32_t out[3]) { initialVelocity(dir, power, out); }

void Projectiles::loadModels() const {
    auto* self = const_cast<Projectiles*>(this);
    self->modelsLoaded_ = true;
    // cAssetManager::LoadAssets: resource gGameDir[11] -> gGlobalAssetMgr +0x23C: u16 count, then the models
    std::vector<uint8_t> dir, d;
    uint16_t gd[28];
    if (!Assets_Pak().read(0, dir) || dir.size() < sizeof gd) return;
    memcpy(gd, dir.data(), sizeof gd);
    if (!Assets_Pak().read(gd[11], d) || d.size() < 8) return;
    for (int i = 0; i < 3; ++i) self->models_[i] = (uint16_t)(d[2 + 2 * i] | d[3 + 2 * i] << 8);
}

namespace {
Projectiles::Projectile make(int kind, const int32_t at[3], const int32_t launch[3], uint32_t id) {
    // cBaseProjectile::cBaseProjectile: the velocity is 30 x the launcher's, the orientation its direction
    Projectiles::Projectile p;
    p.kind = kind;
    p.id = id;
    std::copy(at, at + 3, p.pos);
    std::copy(at, at + 3, p.prev);
    for (int k = 0; k < 3; ++k) p.vel[k] = launch[k] * 30;
    int32_t d[3] = {launch[0], launch[1], launch[2]};
    if ((int)(uint32_t)std::sqrt((double)(uint64_t)((int64_t)d[0] * d[0] + (int64_t)d[1] * d[1] + (int64_t)d[2] * d[2])) >= 41)
        Normalise3(d);
    if (d[0] || d[1] || d[2]) for (int k = 0; k < 3; ++k) p.dir[k] = (int16_t)d[k];
    return p;
}
}   // namespace

void Projectiles::throwGrenade(Game& g, int kind, int power) {
    // cGrenadeLauncher / cMolotovLauncher / cFlashBangLauncher::CreateBullet (gbUseNewProjectileMethod)
    if (kind == Grenade) TheSound().playerEvent(7, 60, 200);           // FireSfx
    else if (kind == Molotov) TheSound().playerEvent(8, 100, 1000);
    else TheSound().playerEvent(7, 100, 1000);
    const int32_t p = 32 * std::clamp(power, 0, 128);                   // StandardConvertFromPower (the player)
    const int32_t dir[3] = {g.player.fireDir[0], g.player.fireDir[1], g.player.fireDir[2]};
    int32_t v[3];
    initialVelocity(dir, p, v);
    int32_t launch[3];
    const int mul = kind == Molotov ? 136 : 17, shift = kind == Molotov ? 12 : 9;   // both are v / 30
    for (int k = 0; k < 3; ++k) launch[k] = (int32_t)((uint64_t)((int64_t)mul * v[k]) >> shift);
    if (kind == Molotov) for (int k = 0; k < 3; ++k) launch[k] >>= 1;   // cMolotov: half the velocity
    const int32_t at[3] = {g.player.pos[0], g.player.pos[1], g.player.pos[2] + 0x2000};
    Projectile pr = make(kind, at, launch, nextId_++);
    if (kind == Molotov) pr.radius = 2048;
    list_.push_back(pr);
}

void Projectiles::dropMine(Game& g) {   // cMineLauncher::CreateBullet: at the ped's position, lifetime 300
    TheSound().playerEvent(7, 100, 1000);
    const int32_t zero[3] = {0, 0, 0};
    Projectile p = make(Mine, g.player.pos, zero, nextId_++);
    p.lifetime = 300;
    p.collide = 0;            // ClearCollideAgainstFlags(0xFFF)
    p.radius = 20480;         // SetCollisionPrimitiveSphere(5)
    p.dir[0] = 0; p.dir[1] = 0x1000; p.dir[2] = 0;
    lastMine_ = p.id;
    list_.push_back(p);
}

bool Projectiles::mineWaiting() const {
    for (const Projectile& p : list_) if (p.id == lastMine_ && !p.dead) return p.lifetime >= 298;
    return false;
}

void Projectiles::fireRocket(Game& g) {   // cRocketLauncher::CreateBullet
    TheSound().playerEvent(44, 100, 1000);   // FireSfx
    int32_t dir[3] = {g.player.fireDir[0], g.player.fireDir[1], g.player.fireDir[2]};
    // TweakBulletOrigin: 1.5 units out from 2 units up
    int32_t muzzle[3] = {g.player.pos[0], g.player.pos[1], g.player.pos[2] + 0x2000};
    {
        int32_t n[3] = {dir[0], dir[1], dir[2]};
        Normalise3(n);
        for (int k = 0; k < 3; ++k) muzzle[k] += (int32_t)((uint64_t)(3LL * n[k]) >> 1);
    }
    const int32_t launch[3] = {6 * dir[0], 6 * dir[1], 6 * dir[2]};   // cRocketLauncher::mLaunchSpeed
    Projectile p = make(Rocket, muzzle, launch, nextId_++);
    p.collide = 0xE;
    // SetCollisionPrimitiveBox(1, 3, 1): half size (0.5, 1.5, 0.5), bound radius |half size|
    p.radius = (int32_t)(uint32_t)std::sqrt((double)(2048LL * 2048 + 6144LL * 6144 + 2048LL * 2048));
    // the move from the ped to the muzzle is checked first (GetSphereCollision 0xF00, radius 0.5)
    const int32_t from[3] = {g.player.pos[0], g.player.pos[1], muzzle[2]};
    int32_t contact[3], n[3];
    bool water = false;
    list_.push_back(p);
    if (sphereStatics(g.collision, from, muzzle, 2048, false, contact, n, water))
        onCollision(g, list_.back(), n, -1, water);
}

uint32_t Projectiles::fireTazer(Game& g, const int32_t from[3], const int32_t launch[3]) {
    (void)g;
    // cTazerCartridge: cBaseProjectile(owner, pos, launch, 300, -1, 0), colliding with peds as well
    Projectile p = make(Tazer, from, launch, nextId_++);
    p.lifetime = 300;
    p.collide |= 2;
    list_.push_back(p);
    return p.id;
}

const Projectiles::Projectile* Projectiles::find(uint32_t id) const {
    if (!id) return nullptr;
    for (const Projectile& p : list_) if (p.id == id && !p.dead) return &p;
    return nullptr;
}

void Projectiles::expire(uint32_t id) {
    for (Projectile& p : list_) if (p.id == id) p.lifetime = 0;
}

void Projectiles::tazerRetract(Game& g, Projectile& p) {
    p.retracting = true;
    int32_t d[3] = {g.player.pos[0] - p.pos[0], g.player.pos[1] - p.pos[1], g.player.pos[2] - p.pos[2] + 0x2000};
    if ((int64_t)d[0] * d[0] + (int64_t)d[1] * d[1] + (int64_t)d[2] * d[2] > 0xFFFFFF) {
        Normalise3(d);
        for (int k = 0; k < 3; ++k) p.vel[k] = d[k] << 6;   // 64 units a second
    } else {
        p.dead = true;
    }
}

void Projectiles::destroy(Game& g, Projectile& p) {   // the Destroy of each class: the explosion
    if (p.dead) return;
    p.dead = true;
    switch (p.kind) {
        case Grenade:   // cGrenade::Destroy (water: type 10, not ported)
            if (!p.inWater) g.addWeaponExplosion(p.pos, 0, 4096);
            break;
        case Molotov:   // cMolotov::Destroy
            if (p.inWater) break;
            if (p.hitCar) g.addWeaponExplosion(p.pos, 0, 4096);
            else { const int32_t up[3] = {p.pos[0], p.pos[1], p.pos[2] + 4096}; g.addWeaponExplosion(up, 2, 4096); }
            break;
        case FlashBang: g.addWeaponExplosion(p.pos, 5, 4096); break;
        case Mine: g.addWeaponExplosion(p.pos, 1, 4096); break;
        case Rocket: g.addWeaponExplosion(p.pos, 3, 4096); break;
    }
}

// OnCollision of each class. entity: -1 statics, >= 0 a vehicle index, -2 a ped
void Projectiles::onCollision(Game& g, Projectile& p, const int32_t n[3], int entity, bool water) {
    if (p.dead) return;
    auto bounce = [&](int keepNum, int keepShift) {
        const int32_t* v = p.vel;
        const int32_t dot = (int32_t)(((int64_t)v[0] * n[0] + (int64_t)v[1] * n[1] + (int64_t)v[2] * n[2]) >> 12);
        if (dot > 0) return;
        const int32_t d2 = 2 * dot;
        int32_t r[3];
        for (int k = 0; k < 3; ++k) {
            const int32_t s = v[k] - (int32_t)(((int64_t)n[k] * d2) >> 12);
            r[k] = keepShift ? (int32_t)(((int64_t)keepNum * s) >> keepShift) : s >> 1;
        }
        if ((int)(uint32_t)std::sqrt((double)(uint64_t)((int64_t)r[0] * r[0] + (int64_t)r[1] * r[1] + (int64_t)r[2] * r[2])) >= 36864)
            TheSound().objectEvent(p.id, p.pos, 107, 100, 400);
        std::copy(r, r + 3, p.vel);
    };
    switch (p.kind) {
        case Grenade:
            if (water) {   // explosion type 9 (the splash) is not ported
                p.inWater = true;
                for (int k = 0; k < 3; ++k) p.vel[k] >>= 5;
                return;
            }
            bounce(0, 0);
            return;
        case FlashBang:
            bounce(3277, 12);
            return;
        case Molotov:
            if (water) { p.inWater = true; p.lifetime = 0; return; }
            if (entity >= 0) p.hitCar = true;   // cVehicle::AttachFire (the fire is not ported)
            p.vel[0] = p.vel[1] = p.vel[2] = 0;
            p.lifetime = 0;
            return;
        case Mine:
            destroy(g, p);
            return;
        case Rocket:   // the direct hit's damage (sDamageInfo type 10) is not ported: the explosion follows
            destroy(g, p);
            return;
        case Tazer:    // cTazerCartridge::OnCollision: anything but a ped pulls the dart back
            tazerRetract(g, p);
            return;
    }
}

void Projectiles::process(Game& g) {
    for (size_t i = 0; i < list_.size(); ++i) {
        Projectile& p = list_[i];
        if (p.dead) continue;
        std::copy(p.pos, p.pos + 3, p.prev);
        // the class's Process
        if (p.kind == Grenade || p.kind == FlashBang) {
            if (!p.inWater || p.kind == FlashBang) p.vel[2] -= 1338 * 30;
        } else if (p.kind == Molotov) {
            p.vel[2] -= (1370112 * 30) >> 12;
        }
        // cBaseProjectile::Process: the lifetime (HelperExplosionCallbacks at 60 is not ported)
        p.lifetime = (int16_t)(p.lifetime - 2);
        if (p.lifetime <= 0) {
            p.lifetime = 0;
            destroy(g, p);
            continue;
        }
        if (p.kind == Tazer) {   // cTazerCartridge::Process: pulled back beyond 1.155 x the range, gone when home
            const int64_t dx = p.pos[0] - g.player.pos[0], dy = p.pos[1] - g.player.pos[1], dz = p.pos[2] - g.player.pos[2];
            const int64_t d2 = dx * dx + dy * dy + dz * dz;
            const int64_t reach = (int32_t)(((int64_t)Weapons_Info(19).range * 0x127A00000LL) >> 32);
            if (d2 >= reach * reach) tazerRetract(g, p);
            if (!p.dead && d2 < 0xA000001 && p.retracting) p.dead = true;
            if (p.dead) continue;
        }
        if (p.kind == Mine) {   // cProximityMine::Process
            if (p.lifetime >= 298) {
                p.lifetime = 300;
                const int64_t dx = p.pos[0] - g.player.pos[0], dy = p.pos[1] - g.player.pos[1],
                              dz = p.pos[2] - g.player.pos[2];
                if (g.player.dead || dx * dx + dy * dy + dz * dz >= 1677721601LL) p.lifetime = 298;
            }
            const uint32_t ms = (uint32_t)(OS_TimeAccurate() * 1000.0);
            if ((uint16_t)((uint16_t)ms - (uint16_t)p.beepMs) >= 251) {
                p.beepMs = (uint16_t)ms;
                TheSound().scriptSfx(p.pos, 696, 127, 300);
            }
            if (!p.collide && p.lifetime <= 297) p.collide = 0xA;
        }
        TheSound().objectTick(p.id, p.pos);
        // Integrate
        if (p.kind != Mine) {
            int32_t to[3];
            for (int k = 0; k < 3; ++k) to[k] = p.pos[k] + (int32_t)((17LL * p.vel[k]) >> 9);
            const bool rocket = p.kind == Rocket;
            const int32_t r = rocket ? p.radius >> 1 : p.radius;
            int32_t contact[3], n[3];
            bool water = false;
            if (sphereStatics(g.collision, p.pos, to, r, rocket && (p.collide & 0x1000), contact, n, water)) {
                if (rocket) {
                    std::copy(contact, contact + 3, p.pos);
                    onCollision(g, p, n, -1, water);
                } else {
                    onCollision(g, p, n, -1, water);
                    for (int k = 0; k < 3; ++k) p.pos[k] = contact[k] + (int32_t)((16 * r * n[k]) >> 16);
                }
            } else {
                std::copy(to, to + 3, p.pos);
            }
            if (p.dead) continue;
        }
        // entity collisions (the collide-against flags): vehicles 8, peds 2; never the owner or his vehicle
        if (p.collide & 8) {
            for (size_t c = 0; c < g.cars.size() && !p.dead; ++c) {
                const Vehicle& car = g.cars[c];
                // cBaseProjectile::StandardAllowedToCollide keeps a projectile off its owner's vehicle; the mine's
                // OnCollision does not call it (that is what the 10-unit arming distance is for)
                if (((int)c == g.playerCar && p.kind != Mine) || car.dead()) continue;
                int32_t n[3];
                if (sphereVehicle(car, p.pos, p.radius, n)) onCollision(g, p, n, (int)c, false);
            }
        }
        if ((p.collide & 2) && !p.dead) {
            if (p.kind == Mine && g.playerCar < 0 && !g.player.dead && !g.player.hidden && spherePed(g.player.pos, p.pos, p.radius)) {
                const int32_t up[3] = {0, 0, 0x1000};
                onCollision(g, p, up, -2, false);
                continue;
            }
            for (const Pedestrians::Ped& ped : g.peds.peds) {
                if (ped.vehicle || ped.deadFrames >= 0 || ped.body.dead) continue;
                if (!spherePed(ped.body.pos, p.pos, p.radius)) continue;
                if (p.kind == Tazer) {   // cTazerCartridge::OnCollision with a ped
                    if (!p.retracting) Tazer_CartridgeHitPed(g, ped.uid, p.vel);
                    tazerRetract(g, p);
                    break;
                }
                const int32_t up[3] = {0, 0, 0x1000};
                onCollision(g, p, up, -2, false);
                break;
            }
        }
    }
    list_.erase(std::remove_if(list_.begin(), list_.end(), [](const Projectile& p) { return p.dead; }), list_.end());
}

void Projectiles::render(Game& g) const {
    if (list_.empty()) return;
    if (!modelsLoaded_) loadModels();
    const float t = g.interpolate ? g.renderAlpha : 1.f;
    for (const Projectile& p : list_) {
        if (p.kind == Tazer) continue;   // the dart is drawn as the taser's wire
        const int which = p.kind == Molotov ? 1 : p.kind == Rocket ? 2 : 0;
        const Model* m = model(models_[which]);
        if (!m) continue;
        // the orientation from the launch direction: forward = dir, up as near world up as possible
        float f[3] = {p.dir[0] / 4096.f, p.dir[1] / 4096.f, p.dir[2] / 4096.f};
        const float fl = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
        if (fl < 1e-4f) { f[0] = 0; f[1] = 1; f[2] = 0; } else for (float& x : f) x /= fl;
        float r[3] = {f[1], -f[0], 0.f};
        float rl = std::sqrt(r[0] * r[0] + r[1] * r[1]);
        if (rl < 1e-4f) { r[0] = 1; r[1] = 0; rl = 1; }
        for (float& x : r) x /= rl;
        const float u[3] = {r[1] * f[2] - r[2] * f[1], r[2] * f[0] - r[0] * f[2], r[0] * f[1] - r[1] * f[0]};
        float pos[3];
        for (int k = 0; k < 3; ++k) pos[k] = (p.prev[k] + (p.pos[k] - p.prev[k]) * t) / 4096.f;
        const float M[16] = {r[0], r[1], r[2], 0, f[0], f[1], f[2], 0, u[0], u[1], u[2], 0, pos[0], pos[1], pos[2], 1};
        PropLibrary::drawModel(*m, M);
    }
}
