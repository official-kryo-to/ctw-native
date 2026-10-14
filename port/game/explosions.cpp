// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "explosions.h"
#include "game.h"
#include "fixedmath.h"
#include "random.h"
#include "weapons.h"
#include <algorithm>
#include <cmath>

ExplosionBlast::ExplosionBlast(const int32_t origin[3], int kind, int16_t scale, int weaponDamage)
    // cExplosionGeneric::Process: AffectEntities(7 x scale, 14 x scale, 320, a5, a6, 0) with a5 = scale x
    // ((damage << 11) & 0xFFFFF000) >> 12 (the vehicles and the props) and a6 = scale x (damage << 12) >> 12 (the
    // pedestrians): the grenade's damage, halved for things and whole for people.
    : damageRadius((int32_t)(7 * (int64_t)std::max<int>(scale, 0))), forceRadius((int32_t)(14 * (int64_t)std::max<int>(scale, 0))),
      damageQ12((int32_t)(((int64_t)std::max<int>(scale, 0) * (int32_t)(((uint32_t)weaponDamage << 11) & 0xFFFFF000u)) >> 12)),
      pedDamageQ12((int32_t)(((int64_t)std::max<int>(scale, 0) * (int32_t)((uint32_t)weaponDamage << 12)) >> 12)), type(kind) {
    std::copy(origin, origin + 3, pos);
    const int32_t queryRadius = damageRadius + 6 * 4096;
    Collision::cellOfPos(pos[0] - queryRadius, pos[1] - queryRadius, minCellX, minCellY);
    Collision::cellOfPos(pos[0] + queryRadius, pos[1] + queryRadius, maxCellX, maxCellY);
}

ExplosionBlast::ExplosionBlast(const int32_t origin[3], int kind, int32_t damage, int32_t force, int32_t damageQ,
                               int32_t pedDamageQ)
    : damageRadius(damage), forceRadius(force), damageQ12(damageQ), pedDamageQ12(pedDamageQ), type(kind) {
    std::copy(origin, origin + 3, pos);
    const int32_t queryRadius = std::max(damageRadius, forceRadius) + 6 * 4096;
    Collision::cellOfPos(pos[0] - queryRadius, pos[1] - queryRadius, minCellX, minCellY);
    Collision::cellOfPos(pos[0] + queryRadius, pos[1] + queryRadius, maxCellX, maxCellY);
}

bool ExplosionBlast::includesCell(const int32_t point[3]) const {
    int x, y; Collision::cellOfPos(point[0], point[1], x, y);
    return x >= minCellX && x <= maxCellX && y >= minCellY && y <= maxCellY;
}

int64_t ExplosionBlast::distanceSquared(const int32_t a[3], const int32_t b[3]) {
    int64_t result = 0;
    for (int k = 0; k < 3; ++k) { const int64_t d = (int64_t)a[k] - b[k]; result += d * d; }
    return result;
}

int32_t ExplosionBlast::falloff(int64_t squared, int32_t radius) {
    // AffectEntities quantises the Q32 reciprocal before multiplying by squared distance.
    const int64_t denominator = std::max<int64_t>(((int64_t)radius * radius) >> 12, 4096);
    const int64_t reciprocal = (0x100000000000LL / denominator) & 0x1FFFFFF00000LL;
    return (int32_t)((0x100000000000LL - reciprocal * (squared >> 12)) >> 32);
}

bool ExplosionBlast::pedHit(const int32_t point[3], bool player, int& damage) const {
    damage = 0;
    if (!forceRadius || !includesCell(point)) return false;
    const int64_t squared = distanceSquared(point, pos) >> 12;
    int64_t reach = ((int64_t)forceRadius * forceRadius) >> 12;
    if (player) reach = (reach * 0x5C2) >> 12;
    if (squared > reach) return false;
    if (squared <= ((int64_t)damageRadius * damageRadius) >> 12) {
        int32_t amount = (int32_t)(((int64_t)falloff(squared * 4096, damageRadius) * pedDamageQ12) >> 12);
        if (player) amount >>= 1;
        damage = amount >> 12;
        // cPlayer::Damage halves kind 7 again, but not vehicle-blast kind 6.
        if (player && (type == 0 || type == 1 || type == 2 || type == 3 || type == 5) && damage > 0)
            damage = std::max(1, damage / 2);   // damage kind 7: types 0..3 and 5 (qword_481768)
    }
    return true; // zero-damage fringe hits still receive effect 2's knockdown
}

int ExplosionBlast::vehicleDamage(int32_t distance, int32_t bound, bool occupiedByPlayer) const {
    if (!damageRadius || distance > damageRadius + bound) return 0;
    if (distance > damageRadius) distance = std::max(0, distance - bound);
    int32_t amount = (int32_t)(((int64_t)falloff((int64_t)distance * distance, damageRadius) * damageQ12) >> 12);
    amount = (int32_t)(((int64_t)amount * (type == 4 || !occupiedByPlayer ? 0x1400 : 0x800)) >> 12);
    return amount >> 12;
}

int32_t ExplosionBlast::force(int64_t squared) const {
    return falloff(squared, forceRadius) * 320;
}

void Vehicle::explosionHit(const ExplosionBlast& blast, bool occupiedByPlayer) {
    if (!blast.forceRadius || !blast.includesCell(pos)) return;
    const int64_t squared = ExplosionBlast::distanceSquared(pos, blast.pos);
    const int32_t distance = (int32_t)std::sqrt((double)squared);
    const int32_t bound = boundRadius();
    if (distance >= blast.forceRadius + bound) return;
    int32_t direction[3];
    if (squared <= 0x63F) {
        direction[0] = (int32_t)Rand32Critical(0x332) - 0x199;
        direction[1] = (int32_t)Rand32Critical(0x332) - 0x199;
        direction[2] = 4096;
    } else {
        for (int k = 0; k < 3; ++k) direction[k] = pos[k] - blast.pos[k];
        Normalise3(direction); direction[2] = 4096; Normalise3(direction);
    }
    int32_t force[3], point[3]; worldCG(point);
    const int32_t magnitude = blast.force(squared);
    for (int k = 0; k < 3; ++k) force[k] = (int32_t)(((int64_t)direction[k] * magnitude) >> 12);
    point[0] += (int32_t)Rand32Critical(0x2000) - 4096;
    point[1] += (int32_t)Rand32Critical(0x2000) - 4096;
    applyWorldForce(point, force, 16);
    // Explosions currently run after vehicle integration. Consume the impulse here so next Act's settling
    // cannot discard a blast on a parked car before the next integrator pass.
    recalcKinematics();
    const bool previousDriver = playerDriving_;
    playerDriving_ = occupiedByPlayer;
    damage(blast.vehicleDamage(distance, bound, occupiedByPlayer));
    playerDriving_ = previousDriver;
}

void Game::affectExplosion(const Explosion& explosion) {
    // cExplosionMolotov::Process: AffectEntities(6, 10, 320, half, whole, 7) with the molotov's damage. Types 0, 1, 3 and
    // 4 are all cExplosionGeneric (Explosion::AddExplosion) and keep the scale-based radii above.
    const int molotov = Weapons_Info(21).damage;
    const ExplosionBlast blast = explosion.type == 2
        ? ExplosionBlast(explosion.pos, 2, 24576, 40960, (molotov << 11) & ~0xFFF, molotov << 12)
        : ExplosionBlast(explosion.pos, explosion.type, explosion.scale, Weapons_Info(20).damage);
    int damage;
    if (playerCar < 0 && !player.dead && !player.hidden && !player.attached && blast.pedHit(player.pos, true, damage)) {
        int32_t direction[3]; for (int k = 0; k < 3; ++k) direction[k] = player.pos[k] - blast.pos[k];
        player.explosionHit(damage, direction);
        task = CarTask{}; enterPressed = false;
        if (player.dead) {
            camera.position(deathEye_); deathYaw_ = (uint16_t)camera.yaw; deathPitch_ = camera.pitch();
            deathFrames_ = 0; deathCameraSettled_ = false;
        }
    }
    peds.explosion(*this, blast);
    for (Vehicle& car : cars) car.explosionHit(blast, playerCar >= 0 && &car == &cars[playerCar]);
    propDynamics.explosion(*this, blast);
}

bool Game::queueExplosion(const Explosion& explosion) {
    if (explosionRadiusCheck_) {
        // AddExplosion queries a half-unit cell rectangle, then applies this raw Q24 distance cutoff.
        // Retained final-state explosions still suppress additions. The currently ported types are generic
        // stationary blasts; the specialised class exclusions / moving input will accompany their ports.
        int minX,minY,maxX,maxY;
        Collision::cellOfPos(explosion.pos[0]-0x800,explosion.pos[1]-0x800,minX,minY);
        Collision::cellOfPos(explosion.pos[0]+0x800,explosion.pos[1]+0x800,maxX,maxY);
        for (const Explosion& existing : explosions_) {
            int x,y; Collision::cellOfPos(existing.pos[0],existing.pos[1],x,y);
            if (x < minX || x > maxX || y < minY || y > maxY) continue;
            if (ExplosionBlast::distanceSquared(existing.pos,explosion.pos) <= 0x45FFFFFFLL) return false;
        }
    }
    explosions_.push_back(explosion);
    return true;
}
