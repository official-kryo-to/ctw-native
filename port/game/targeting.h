// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// The player's auto-aim: ports of cPlayer::HandleAiming (on foot, touch controls), cPlayerSensorCone (Update ->
// CheckTarget / FillUp / HandleTarget, GetBestTarget), cSensorCone (Validate, IsTargetInFireCone, GetSensorBand,
// GetSensorPriority, DirectionToTarget) and cPlayer::RenderTarget.
//
// The cone (cSensorCone::sConeInfo): from the player's position along his facing, pulled back 2.5 units
// (AVERANGE_PULLBACK_VALUE); its reach is the weapon range for target detection (cPed +0x367, 0 -> 40 units) plus the
// pullback. Validate: an entity within the reach of the apex whose direction from it is within PLAYER_FOV_CONE
// (dot >= 2048, +-60 degrees). The fire cone is HalfFireConeDp (dot > 2896, +-45 degrees) and always includes
// anything within 5 units.
//
// Every frame Update keeps (CheckTarget) or replaces (FillUp -> HandleTarget) the target: peds, and vehicles while a
// gun (slot >= 2) is held. A lower GetSensorPriority wins, then the nearer of three distance bands (thirds of the
// squared reach). While fire is held GetBestTarget searches the bands in order for the best priority (the RPG never
// auto-targets: HandleStrafe sets the yoke's +0x94 for weapon 17). A target in the fire cone sets the fire direction
// (cPed +0x32A) towards it; the direction then turns a quarter of the remaining angle a frame (cPlayer +0x1188).
//
// Priorities here: peds 5 (cops, ped type 10, 7 while the player is not wanted), driven vehicles 6, cop cars 8,
// empty vehicles 9. The original's 0..4 (players, attackers, mission entities) have no counterpart in the port yet.
#pragma once
#include <cstdint>

class Game;

class PlayerTargeting {
public:
    struct Ref {   // a weak pointer by uid: 1 ped, 2 vehicle
        int kind = 0;
        uint32_t uid = 0;
        explicit operator bool() const { return kind != 0; }
        bool operator==(const Ref& o) const { return kind == o.kind && uid == o.uid; }
    };
    Ref target;          // cPlayerSensorCone +0x10
    Ref targeted;        // cPlayer +0x1120 (SetTargetedEntity): the lock, held while fire is held
    bool turning = false;   // cPlayer +0x1188
    bool enabled = true;    // cPlayer +0x1184 bit 0x40 (cPlayer::Reset sets it)
    void reset();
    // cPlayer::HandleAiming on foot: updates g.player.fireDir; true while a target is in the fire cone
    bool handleAiming(Game& g, bool fireHeld);
    void render(Game& g) const;   // cPlayer::RenderTarget
    // the target's position and health (0..127), false if it is gone
    bool targetInfo(const Game& g, int32_t pos[3], int& health) const;
    bool locked = false;          // the target was in the fire cone this frame
};
