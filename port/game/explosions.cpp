// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "explosions.h"
#include "game.h"
#include "fixedmath.h"
#include "random.h"
#include "weapons.h"
#include "particles.h"
#include "sound.h"
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
    // types 4 and 6 ((type | 2) == 6) hit every vehicle x1.25; others only those without the player in them
    amount = (int32_t)(((int64_t)amount * ((type | 2) == 6 || !occupiedByPlayer ? 0x1400 : 0x800)) >> 12);
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
    // The per-state effects of the specialised explosions: cExplosionFlashBang::Process (the daze) and
    // cExplosionWaterExplosion::Process (the surface splash and the underwater pulse). The generic and molotov
    // blast is below. cBaseExplosion::DazeEntities and cPlayer::Daze are modelled inline for the flash bang.
    if (explosion.type == 5) {
        // cExplosionFlashBang::Process state 0 (0x0073fb4c) runs cBaseExplosion::DazeEntities(30.0, 900.0, 250)
        // (0x0073e57c): a 31-unit world query dazing each entity (flag bit 2 at +0x77) within squared distance 900.0
        // (Q12). Only the player is handled here; peds get a stumble (cDazed) task and vehicles are never dazed,
        // neither of which is ported. The daze strength falls off linearly from 250 at the centre, but DazeEntities'
        // 2^45 base over a masked 2^44 reciprocal keeps it above 127 everywhere in range. The player's type-4 damage
        // passes through unchanged (no health loss, no knockdown) to cPlayer::Daze(damage, owner, false) =
        // cScreenFade::FadeIn(min(damage, 127), reset) (0x0088b920/0x006c2ecc): snap to full (0x10000), then fade in.
        int minX, minY, maxX, maxY;
        Collision::cellOfPos(explosion.pos[0] - 31 * 4096, explosion.pos[1] - 31 * 4096, minX, minY);
        Collision::cellOfPos(explosion.pos[0] + 31 * 4096, explosion.pos[1] + 31 * 4096, maxX, maxY);
        auto inDazeCells = [&](const int32_t at[3]) {
            int x, y; Collision::cellOfPos(at[0], at[1], x, y);
            return x >= minX && x <= maxX && y >= minY && y <= maxY;
        };
        if (playerCar < 0 && !player.dead && !player.hidden && !player.attached && inDazeCells(player.pos)) {
            const int64_t distanceQ12 = ExplosionBlast::distanceSquared(player.pos, explosion.pos) >> 12;
            if (distanceQ12 <= 900 * 4096) {
                const int64_t reciprocal = (0x100000000000LL / (900 * 4096)) & ~0xFFFFFLL;   // masked 2^44 / radius^2
                const int32_t factor = (int32_t)((0x200000000000LL - reciprocal * distanceQ12) >> 32);
                const int32_t damage = (int32_t)(((int64_t)factor * ((250 << 11) & ~0xFFF)) >> 24);
                // The port's cScreenFade has no FadeIn reset flag, so drive one FadeOut step to full before fading in.
                // cPlayer::Daze's FadeIn passes b=0, which cScreenFade::Render draws white (not the camera's black).
                screenFade.toBlack(1);
                screenFade.tick();
                screenFade.in(std::min<int>(damage, 0x7F));
                screenFade.white = true;
            }
        }
        return;
    }
    if (explosion.type == 10) {
        // cExplosionWaterExplosion::Process (0x00740894): state 0 advances to state 1; state 1 shakes the cameras,
        // plays script SFX 0x1D5 (volume 127, range 0x2BC), spawns a scale-4.0 type-0 WaterSplashEmitter ring at pos
        // and a puff of dark smoke (colour 3, from the engine's smoke pool); state 2 waits until counter >= 0x15, applies
        // AffectEntities(0, 7, 320, 0, 0, 0) at (pos.x, pos.y, -7.5), plays script SFX 0x1D6 and spawns scale-1.5
        // type-1 and type-2 WaterSplashEmitters with direction (0, 0, 0.4).
        if (explosion.state == 1) {
            shakeNearExplosion(explosion.pos);
            TheSound().scriptSfx(explosion.pos, 0x1D5, 0x7F, 0x2BC);
            const int16_t zeroDir[3] = {0, 0, 0};
            TheParticles().add<WaterSplashEmitter>(explosion.pos, zeroDir, (int16_t)0x4000, 0, false, (uint16_t)0x7F39);
            auto* smoke = TheParticles().add<SmokeEmitter>(explosion.pos, 10);
            const int32_t zeroVel[3] = {0, 0, 0};
            smoke->addParticle(zeroVel, 3);
            smoke->dying = true;
            return;
        }
        if (explosion.state != 2) return;   // states 0 and 3 only advance/destroy (driven by processWaterExplosion)
        // The underwater pulse at z = -7.5 is the only part moved below the surface; the SFX and the two rising
        // splashes stay at the surface position (the binary reads this->pos for them).
        const int32_t deepPos[3] = {explosion.pos[0], explosion.pos[1], -0x7800};
        affectBlast(ExplosionBlast(deepPos, 10, 0, 0x7000, 0, 0));
        TheSound().scriptSfx(explosion.pos, 0x1D6, 0x7F, 0x2BC);
        const int16_t upDir[3] = {0, 0, 0x666};
        TheParticles().add<WaterSplashEmitter>(explosion.pos, upDir, (int16_t)0x1800, 1, true, (uint16_t)0x7F39);
        TheParticles().add<WaterSplashEmitter>(explosion.pos, upDir, (int16_t)0x1800, 2, true, (uint16_t)0x7F39);
        return;
    }
    // cExplosionMolotov::Process: AffectEntities(6, 10, 320, half, whole, 7) with the molotov's damage. Types 0, 1, 3 and
    // 4 are all cExplosionGeneric (Explosion::AddExplosion) and keep the scale-based radii above.
    const int molotov = Weapons_Info(21).damage;
    ExplosionBlast blast = explosion.type == 2
        ? ExplosionBlast(explosion.pos, 2, 24576, 40960, (molotov << 11) & ~0xFFF, molotov << 12)
        : ExplosionBlast(explosion.pos, explosion.type, explosion.scale, Weapons_Info(20).damage);
    if (explosion.type == 2) blast.fireRadius = 0x7000;
    affectBlast(blast);
}

void Game::affectBlast(const ExplosionBlast& blast) {
    // AffectEntities sets people within the fire radius alight (cPed::SetOnFire) before it damages them
    auto inFire = [&](const int32_t at[3]) {
        if (!blast.fireRadius || !blast.includesCell(at)) return false;
        const int64_t squared = ExplosionBlast::distanceSquared(at, blast.pos) >> 12;
        return (int64_t)blast.fireRadius * blast.fireRadius > (squared << 12);
    };
    if (playerCar < 0 && !player.dead && !player.hidden && !player.attached && inFire(player.pos) &&
        !fires.burning(Fires::Player, 0))
        fires.start(*this, player.pos, Fires::Player, 0, 0x1E0, 1, true, true);
    if (!multiplayerWorld.client()) for (const Pedestrians::Ped& p : peds.peds)
        if (!p.vehicle && !p.body.dead && inFire(p.body.pos)) Fires_SetPedOnFire(*this, p.uid);
    // vehicles closer than the radius (whole-unit distance), except helicopters, catch fire (cVehicle::SetOnFire)
    if (blast.fireRadius && !multiplayerWorld.client())
        for (Vehicle& car : cars) {
            if (car.dead() || !blast.includesCell(car.pos)) continue;
            const int32_t distance = (int32_t)std::sqrt((double)ExplosionBlast::distanceSquared(car.pos, blast.pos));
            if (distance < blast.forceRadius + car.boundRadius() && distance < blast.fireRadius)
                Fires_SetVehicleOnFire(*this, car);
        }
    int damage;
    if (playerCar < 0 && !player.dead && !player.hidden && !player.attached && blast.pedHit(player.pos, true, damage)) {
        int32_t direction[3]; for (int k = 0; k < 3; ++k) direction[k] = player.pos[k] - blast.pos[k];
        player.explosionHit(damage, direction);
        task = CarTask{}; enterPressed = false;
        if (player.dead) {
            leaveFootView(deathEye_, deathYaw_, deathPitch_);
            deathFrames_ = 0; deathCameraSettled_ = false;
        }
    }
    if (!multiplayerWorld.client()) {
        peds.explosion(*this, blast);
        for (Vehicle& car : cars) car.explosionHit(blast, playerCar >= 0 && &car == &cars[playerCar]);
        propDynamics.explosion(*this, blast);
    }
}

bool Game::queueExplosion(const Explosion& explosion) {
    if (explosionRadiusCheck_ && !explosion.moving && explosion.type != 8 && explosion.type != 9 && explosion.type != 11) {
        // AddExplosion queries a half-unit cell rectangle, then applies this raw Q24 distance cutoff.
        // Retained final-state explosions still suppress additions; moving blasts and types 8, 9, 11 (ClassIds 12, 15, 17)
        // are excluded from the suppression check.
        int minX,minY,maxX,maxY;
        Collision::cellOfPos(explosion.pos[0]-0x800,explosion.pos[1]-0x800,minX,minY);
        Collision::cellOfPos(explosion.pos[0]+0x800,explosion.pos[1]+0x800,maxX,maxY);
        for (const Explosion& existing : explosions_) {
            if (existing.type == 8 || existing.type == 9 || existing.type == 11) continue;
            int x,y; Collision::cellOfPos(existing.pos[0],existing.pos[1],x,y);
            if (x < minX || x > maxX || y < minY || y > maxY) continue;
            if (ExplosionBlast::distanceSquared(existing.pos,explosion.pos) <= 0x45FFFFFFLL) return false;
        }
    }
    explosions_.push_back(explosion);
    int32_t velocity[3] = {explosion.dir[0], explosion.dir[1], explosion.dir[2]};
    multiplayerWorld.captureExplosion(explosion.type, explosion.pos, explosion.scale / 4096.f, velocity);
    if (explosion.type != 8 && explosion.type != 9) stats.incExplosions();   // Explosion::AddExplosion
    return true;
}
