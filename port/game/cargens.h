// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// Parked cars from the map's car generators: a port of cCarGenManager.
//
// Every 50-unit sector of world.bin carries its car generators (section +0x128, see world/collision.h). When a
// sector streams in (cWorldSector::DataLoaded, the 3 x 3 sectors around the player), SpawnAllCarGensInSector puts
// each generator that has nothing within 8 units into one of 8 creation slots (AddVehicleToCreationList: a chance
// roll, optional random heading / position / vehicle). cCarGenManager::Process then creates the car once its model is
// loaded, unless the spot got taken, the sector went away, the slot got too old (255 frames), or - for generators
// flagged 0x40 - a player can see the spot.
#pragma once
#include <cstdint>
#include <set>
#include <vector>
#include "world/collision.h"
#include "random.h"

class Game;

class CarGenManager {
public:
    void reset();
    void update(Game& g);   // streaming check + cCarGenManager::Process, once per game frame

    // cVehicleInfoManager's list of loaded vehicle models (RandomVehicle picks from it)
    std::vector<int> loadedVehicles;
    void noteLoaded(int infoId);

private:
    struct Slot { int32_t pos[3]; int16_t heading; uint8_t age, vehicle, unseenOnly, chance, colour; bool used; };
    Slot slots_[8] = {};
    std::set<int> sectors_;   // the streamed-in sectors (x * 100 + y)
    void spawnAllInSector(Game& g, int cx, int cy);
    void addToCreationList(Game& g, const Collision::CarGen& r);
    bool spotTaken(const Game& g, const int32_t p[3]) const;
    int randomVehicle(const Game& g, bool sea);
};
