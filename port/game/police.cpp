// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "game.h"
#include "fixedmath.h"
#include "sound.h"
#include "os/os.h"
#include <algorithm>
#include <cmath>

namespace {
int64_t distance2(const int32_t a[3], const int32_t b[3]) {
    int64_t result = 0;
    for (int k = 0; k < 3; ++k) { const int64_t d = (int64_t)a[k] - b[k]; result += d*d; }
    return result;
}
int64_t speed2(const int32_t velocity[3]) {
    const int32_t zero[3]{};
    return distance2(velocity, zero);
}
int16_t facing(const int32_t from[3], const int32_t to[3]) {
    return (int16_t)(std::atan2((double)to[0]-from[0], (double)to[1]-from[1]) * 10430.37835047);
}
}

// Enforcer and arrest tasks' supported single-player branches (native 0x707004 / 0x6ffdc8).
// This uses the existing on-foot goto and weapon controller. The full cKill task graph (cover, collective
// orders, layered interiors and specialised weapons) is still separate work; do not count that task as ported.
bool Pedestrians::policeStep(Game& g, Ped& p) {
    if (p.type != 10) return false;
    auto& task = p.enforcer;
    auto release = [&] {
        if (g.arrestingCop == p.uid) {
            g.arrestingCop = 0;
            g.arrestGrabbed = g.arrestStopVehicle = false;
        }
        task.pursuing = false;
        task.state = 0;
        task.arrestTimer = 20;
        task.animation = false;
        p.body.upperAim = false;
    };
    const int level = g.wanted.level();
    if (g.player.dead || (!level && !g.arrested())) { release(); return false; }
    int32_t target[3]; g.focus(target);
    const int64_t distance = distance2(p.body.pos, target);
    if (!task.pursuing) {
        // FindCrime: at one star the actor must be strictly within twenty units; higher levels have no
        // distance gate. The port currently has only the outdoor world, without the native interior flag.
        if (level == 1 && distance >= 0x190000000LL) return false;
        task.pursuing = true;
        p.flee = {};
        if (p.talk.role != Ped::Talk::None) finishTalk(p);
    }
    auto wait = [&](int upper = -1, int legs = -1) {
        Player::Input input;
        input.maxLevel = 0;
        input.moving = true;
        input.heading = facing(p.body.pos, target);
        input.taskAnimation = upper >= 0;
        p.body.act(input, &g.pedSprites);
        if (upper >= 0 && !task.animation) {
            p.body.playOneShot(upper, legs, false, true);
            task.animation = true;
        }
        p.movementPrepared = true;
    };
    if (g.arrested()) {
        if (g.arrestingCop != p.uid) { wait(); return true; }
        // The arresting officer moves one unit to the player's left after the arrest, then holds the guard pose.
        const int32_t aside[2] = {g.player.pos[0]-4096, g.player.pos[1]};
        const int64_t dx = (int64_t)p.body.pos[0]-aside[0], dy = (int64_t)p.body.pos[1]-aside[1];
        if (dx*dx + dy*dy > (int64_t)1024*1024) gotoTarget(g, p, aside, 2);
        else wait(110, 111);
        return true;
    }

    const bool inVehicle = g.playerCar >= 0 && g.playerCar < (int)g.cars.size();
    const Vehicle* car = inVehicle ? &g.cars[g.playerCar] : nullptr;
    // cArrestOnFoot::AbortKillingOrder: ordinary cops arrest at levels one/two. On foot the player's health
    // must be below 21; at two stars he must also be stopped or already being arrested. In a vehicle, one star
    // permits arrest while moving; at two stars its speed must be below one unit/second.
    const bool canArrest = g.policeRestartsAvailable() && level < 3 && (car ? (level == 1 || speed2(car->vel) < 0x1000000) :
        g.player.health < 21 && (level == 1 || g.arrestingCop != 0 || speed2(g.player.vel) == 0));
    if (!canArrest) {
        if (g.arrestingCop == p.uid) { g.arrestingCop = 0; g.arrestGrabbed = g.arrestStopVehicle = false; }
        task.state = 0; task.arrestTimer = 20; task.animation = false;
        // The arrest task selects fists at one star, and a pistol for the ordinary armed response.
        // Subtype-specific weapons and the remaining cKill tactical subtasks are not invented here.
        const int weapon = level == 1 ? 0 : 5;
        if (task.weapon.type != weapon) {
            task.weapon = Weapon(weapon);
            task.weapon.ammo = -1;
            task.weapon.reload();
        }
        const auto& info = Weapons_Info(weapon);
        const int32_t range = Weapons_Ok() ? std::max(info.range, 4096) : 0;
        int32_t aim[3] = {target[0]-p.body.pos[0], target[1]-p.body.pos[1], 0};
        Normalise3(aim);
        for (int k = 0; k < 3; ++k) p.body.fireDir[k] = (int16_t)aim[k];
        const bool inRange = range && distance <= (int64_t)range*range;
        if (!inRange) gotoTarget(g, p, target, 2);
        else wait();
        p.body.upperAim = weapon >= 5 && inRange;
        Weapons_NpcFrame(g, p.body, task.weapon, inRange, p.uid);
        return true;
    }

    p.body.upperAim = false;
    // The nearest eligible cop owns the arrest. Handles survive vector moves;
    // a removed/dead officer cannot leave input locked or prevent a nearer living cop taking over.
    Ped* previous = nullptr;
    for (Ped& other : peds) if (other.uid == g.arrestingCop && other.type == 10 && !other.body.dead) previous = &other;
    if (previous && previous != &p && distance >= distance2(previous->body.pos, target)) { wait(); return true; }
    if (g.arrestingCop != p.uid) {
        if (previous) { previous->enforcer.state = 0; previous->enforcer.arrestTimer = 20; }
        g.arrestingCop = p.uid;
        g.arrestGrabbed = g.arrestStopVehicle = false;
        task.arrestTimer = 20;
        TheSound().copWantsArrest(g, p.uid);
    }
    if (inVehicle) {
        task.state = 4;
        int32_t local[3], door[3];
        car->doorSpawnPoint(0, local); car->localToWorld(local, door);
        if (distance2(p.body.pos, door) > (int64_t)0x2000*0x2000) {
            gotoTarget(g, p, door, 2);
            return true;
        }
        // Native arrest state five orders direct control 11 for 120 ticks when below fifteen units/s.
        // Reuse the game's exit task to detach/animate its player rather than replacing seat ownership by hand.
        if (speed2(car->vel) < 3774873600LL) {
            g.arrestStopVehicle = true;
            if (g.task.op == Game::CarTask::None) g.startExit();
        }
        wait();
        return true;
    }
    g.arrestStopVehicle = false;
    // Native entity targets can complete on a swept crossing. The port's symmetric dynamic body solver keeps
    // actors apart; accepting physical contact is its equivalent arrival boundary (no reach through walls). That
    // contact is swept too: a running cop meets the player up to a step before the spheres overlap, and once
    // stopped there he holds the arrest until the player moves away.
    constexpr int32_t reach = 2 * Player::kRadius;
    const int32_t start[3] = {p.body.pos[0], p.body.pos[1], p.body.pos[2]+0x1000};
    const int32_t end[3] = {target[0], target[1], target[2]+0x1000};
    const bool clear = !g.collision.staticLine(start, end, 0x40001E00);
    const bool holding = g.arrestGrabbed && distance <= task.grabDistance;
    if (!clear || (distance > (int64_t)reach*reach && !holding && !Pedestrians::touching(p.body, g.player))) {
        g.arrestGrabbed = false;
        task.state = 3; task.arrestTimer = 20;
        gotoTarget(g, p, target, 2);
        return true;
    }
    if (!g.arrestGrabbed) task.grabDistance = std::max(distance, (int64_t)reach*reach);
    task.state = 5;
    g.arrestGrabbed = true;
    wait();
    if (task.arrestTimer) --task.arrestTimer;
    if (!task.arrestTimer && g.beginArrest(p.uid)) { task.state = 2; task.animation = false; }
    return true;
}

bool Game::beginArrest(uint32_t cop) {
    if (arrested_ || player.dead || playerCar >= 0 || player.attached || !cop || restart_.police.empty()) return false;
    bool valid = false;
    for (const auto& p : peds.peds) valid |= p.uid == cop && p.type == 10 && !p.body.dead && !p.vehicle;
    if (!valid) return false;
    arrested_ = arrestGrabbed = true;
    bustedDisplay_ = true;
    arrestStopVehicle = false;
    arrestingCop = cop;
    task = {};
    enterPressed = false;
    player.playOneShot(22, 23, false);
    std::fill(player.vel, player.vel+3, 0);
    scope.active = false;
    deathFrames_ = 0;
    deathStarted_ = false;
    camera.position(deathEye_);
    deathYaw_ = (uint16_t)camera.yaw;
    deathPitch_ = camera.pitch();
    deathCameraSettled_ = false;
    return true;
}
