// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#pragma once
#include "doors.h"

class Game;

// cSafehouseManager and cSafehouseHandler for the safehouse the port has: the first, the apartment KEN_A01 gives
// (cSafehouseManager::Init's own entry: its marker at (377.63, 22.86), facing -90 degrees; kena01's
// cCutscene_AtSafehouse_Sequence::TidyUpEndOfSafehouse buys it, WorldImpl::PurchaseSafehouse). Once it is owned and
// no mission runs, its door has a marker (SetupDoorMarker: HUDImpl::AddBlip(door, representation 5) pointed in,
// radius 3, ChangeBlipStyle palette 11 (green), style 2). Walking into it on foot, standing and facing the door
// (within 60 degrees) saves (Save): the cutscene starts, the marker turns style 3, cDoorHandler walks the player in,
// DoSave makes it the current safehouse and saves, WalkoutDelay walks him out again (cDoorHandler, Leave) and
// SaveFinished fades the screen in over 30 frames. A loaded game starts there the same way (ForcePlayerStart: put at
// the door, PutPlayerAtSafehouseDoor, then the walk out).
// Not ported: the PDA's safehouse app (PDAImpl::ActivateSafeHouseApp) that DoSave opens, where the player picks
// the slot (cPauseLoadSaveDelApp::SaveSlot) - the save goes to the slot used last, else slot 0, with what SaveSlot
// does in a safehouse (health to 0x7F, no wanted level, the clock 6 hours on); the zone's name the walk out prints;
// the other safehouses, their garages, the for-sale markers and buying.
class Safehouses {
public:
    void tick(Game& game);                  // cSafehouseManager::Update / the handler's states, once a frame
    void forcePlayerStart(Game& game);      // cSafehouseHandler::ForcePlayerStart: a loaded game starts here
    bool busy() const { return state_ != kIdle && state_ != kWatching; }
    void reset();                           // cSafehouseManager::Reset

private:
    enum State { kIdle, kWatching, kEntering, kSaving, kLeaving };
    void setupDoorMarker(Game& game);       // SetupDoorMarker
    bool inArea(const Game& game) const;    // Save's rectangle about the marker
    bool saveConditions(Game& game) const;  // Save's checks
    void save(Game& game, bool forced);     // Save
    void doSave(Game& game, bool forced);   // DoSave and the app's SaveSlot
    void walkOut(Game& game);               // WalkoutDelay .. SaveFinished

    State state_ = kIdle;
    bool forced_ = false;   // +0x2A8
    int waitFrames_ = 0;
    int checkWait_ = 0;     // SaveCheck's 10 frames
    DoorHandler door_;
};
