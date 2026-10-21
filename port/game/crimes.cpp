// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// The player's crimes and the cops who see them (wanted.h): cPed::HelperForRegisteringCrimesDoneToThisPed,
// cVehicle::HandlePlayerCrime, cNewGetInVehicle::HandleJackCrime, the cop part of cVehicle::ProcessAlways and the
// player's call of cWantedManager::Process.
#include "game.h"

namespace {
bool isCopCar(const Game& g, const Vehicle& car) {   // its driver is a cop (cVehicle::GetDriverPedType == 10)
    for (const Pedestrians::Ped& p : g.peds.peds)
        if (p.vehicle == car.uid && p.seat == 0 && car.seatUser[0] == (int)p.uid) return p.type == 10;
    return false;
}
}   // namespace

void Game::updateWanted() {
    // cVehicle::ProcessAlways: a cop car on screen sees the player (cPlayer::SetCopInView). Pursuit (the +0x948
    // bit 27 flag) belongs to the cop AI, which is not ported: no cop counts as pursuing yet.
    int32_t at[3];
    focus(at);
    for (Vehicle& car : cars) {
        const bool onScreen = view.canSee(car.pos, car.boundRadius());
        car.screenTimer = onScreen ? 0xFF : car.screenTimer ? (uint8_t)(car.screenTimer - 1) : 0;   // +0x99C
        if (!car.dead() && onScreen && isCopCar(*this, car)) wanted.setCopInView(frame, at, false);
    }
    wanted.process(frame, at, playerCar >= 0);   // cPlayer::Process -> cWantedManager::Process
}

void Game::pedCrime(const Pedestrians::Ped& p, int type) {   // cPed::HelperForRegisteringCrimesDoneToThisPed
    // the attacker is the player; the ped's health (+0x35F) has already been reduced
    const bool cop = p.type == 10;
    const bool killed = p.body.dead || p.body.health <= 0 || p.deadFrames >= 0;
    int crime;
    if (killed) crime = cop ? WantedManager::KillCop : WantedManager::KillPed;
    else switch (type) {
        case 1: case 2: crime = cop ? WantedManager::ShootCop : WantedManager::ShootPed; break;
        case 3: crime = cop ? WantedManager::TaserCop : WantedManager::TaserPed; break;
        case 5: case 13: return;
        case 8:   // IsRunningOverClassedAsCrime: not a cop rolling out of the way
            if (cop && p.rollFrames >= 0) return;
            crime = cop ? WantedManager::RunOverCop : WantedManager::RunOverPed;
            break;
        case 9: crime = cop ? WantedManager::RunOverCop : WantedManager::HitPed; break;
        case 11: crime = cop ? WantedManager::HitCopWith11 : WantedManager::HitPedWith11; break;
        case 12: crime = cop ? WantedManager::ChainsawCop : WantedManager::ChainsawPed; break;
        default: crime = cop ? WantedManager::HitCop : WantedManager::HitPed; break;
    }
    wanted.registerCrime(crime, frame);
}

void Game::copCarCrime(Vehicle& car, int type, int damage, uint32_t sourceCar) {   // cVehicle::HandlePlayerCrime
    // sourceCar 0: the player on foot; otherwise the vehicle that did it (a crime when the player drives it)
    const uint32_t source = sourceCar ? sourceCar : kPlayerOnFoot;
    const uint32_t last = car.lastDamagedBy;
    car.lastDamagedBy = source;   // cEntity::Damage: LastDamagedBy
    if (!isCopCar(*this, car) || last == source) return;
    if (sourceCar) {
        if (playerCar < 0 || cars[playerCar].uid != sourceCar || type == 4 || damage < 2) return;
    }
    const int crime = type >= 0 && type <= 12 ? wanted.tables().copCarCrimes[type] : WantedManager::DamageCopCar;
    wanted.registerCrime(crime, frame);
}

void Game::jackCrime(const Vehicle& car) {   // cNewGetInVehicle::HandleJackCrime
    static const int copCars[6] = {0x12, 0x22, 4, 7, 3, 0x25};
    for (int id : copCars)
        if (car.infoId == id) { wanted.registerCrime(WantedManager::JackCopCar, frame); return; }
    wanted.registerCrime(wanted.copInView(frame) ? WantedManager::JackSeenByCops : WantedManager::Jack, frame);
}
