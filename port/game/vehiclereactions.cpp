// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// How the people in a vehicle react to it being damaged: the end of cVehicle::Damage, cSimpleMover::GetAction,
// cVehicle::GetReaction and cPed::HandleReaction for a driver.
//
// The port's traffic always has a real driver (the original's virtual occupants, created by ReactionCreatePed when
// they react, do not exist here) and no passengers, so only the driver's seat is looked at.
#include "game.h"

namespace {
// cSimpleMover::GetAction for damage to a vehicle (a collision, type 8, only counts as coming from a ped in a vehicle
// when the victim is a ped: action 5 here)
int vehicleAction(int type) {
    switch (type) {
    case 0: case 1: case 2: case 3: case 5: case 10: case 11: case 12: case 13: case 21: return 0;
    case 8: case 9: return 5;
    case 14: return 6;
    case 15: return 7;
    case 17: return 4;
    case 18: return 8;
    default: return 3;
    }
}
// cSimpleMover::ReactionCheck: fleeing only for the first occupant asked
bool reactionCheck(int reaction, bool first) {
    return (reaction >= 2 && reaction <= 5) || reaction == 7 || (reaction == 1 && first);
}
}   // namespace

void Game::vehicleReaction(Vehicle& car, int type, int damage, uint32_t sourceCar, const int32_t at[3]) {
    if (car.dead()) return;
    copCarCrime(car, type, damage, sourceCar);   // cVehicle::HandlePlayerCrime comes first in Damage
    if (type == 8 && damage < 12) return;   // gAggBigHit
    const int action = vehicleAction(type);
    Pedestrians::Ped* driver = nullptr;
    for (Pedestrians::Ped& p : peds.peds)
        if (p.vehicle == car.uid && p.seat == 0 && car.seatUser[0] == (int)p.uid) { driver = &p; break; }
    if (!driver || driver->exiting || driver->deadFrames >= 0) return;
    const bool fromPlayer = sourceCar == 0 || (playerCar >= 0 && cars[playerCar].uid == sourceCar);
    int reaction = peds.reactionFor(*driver, action, fromPlayer);   // GetReactionStatic
    if (!reactionCheck(reaction, true)) return;
    if (reaction == Pedestrians::VehicleReaction) {
        // a slow vehicle's driver (first-gear acceleration below 11) runs for it instead (the threat is never
        // inside this vehicle here)
        if (traffic.drivingProps().maxAcceleration(car.infoId, 1) < 0xB000) reaction = Pedestrians::Flee;
    }
    // cPed::HandleReaction for a ped in a vehicle: women flee instead of attacking, getting in or rolling; rolling is
    // nothing in a vehicle. Attacking (cKill) is not ported: the driver flees instead, as peds on foot do.
    if (driver->body.bodySet == 0 &&
        (reaction == Pedestrians::Attack || reaction == Pedestrians::FleeOrGetIn || reaction == Pedestrians::Roll))
        reaction = Pedestrians::Flee;
    switch (reaction) {
    case Pedestrians::Flee:
    case Pedestrians::Attack:   // cFleeOnFoot (it gets out first)
        peds.fleeFromVehicle(*this, *driver, car, at, sourceCar);
        break;
    case Pedestrians::VehicleReaction:   // cVehicle::HandleReaction: drive away
        traffic.driveAway(*this, car);
        break;
    default:   // FleeOrGetIn: the driver is already in (cNewGetInVehicle); Roll: nothing in a vehicle
        break;
    }
}
