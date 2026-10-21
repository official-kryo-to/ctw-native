// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// Attached and loose fire behaviour over synthetic land and water.
#define SDL_MAIN_HANDLED
#include "game.h"
#include <SDL.h>
#include <algorithm>
#include <cstdio>
#include <memory>

static int failures;
static void check(bool ok, const char* why) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", why); ++failures; }
}

struct CollisionTestAccess {
    static void ground(Collision& collision, bool water) {
        collision.cells_.clear();
        auto cell = std::make_unique<Collision::Cell>();
        cell->loaded = true;
        // At the origin, cell (70, 50) and the first ground-map square are used.
        cell->groundMap.assign(400, water ? 0xAA : 0x55); // type 2 water or type 1 land
        collision.cells_[70 * 100 + 50] = std::move(cell);
    }
};

static void reset(Game& g, bool water) {
    g.fires.clear();
    g.peds.clear();
    g.cars.clear();
    g.player = Player{};
    std::fill(g.player.pos, g.player.pos + 3, 0);
    g.player.health = 127;
    g.player.dead = false;
    g.player.hidden = false;
    CollisionTestAccess::ground(g.collision, water);
    g.frame = 0;
}

static void addPed(Game& g, uint32_t uid) {
    Pedestrians::Ped p;
    p.uid = uid;
    p.body.playerControlled = false;
    p.body.pos[0] = p.body.pos[1] = p.body.pos[2] = 0;
    p.body.health = 127;
    g.peds.peds.push_back(std::move(p));
}

static void addCar(Game& g, uint32_t uid) {
    Vehicle c;
    c.uid = uid;
    c.pos[0] = c.pos[1] = c.pos[2] = 0;
    c.right[0] = 0x1000; c.right[1] = c.right[2] = 0;
    c.fwd[0] = c.fwd[2] = 0; c.fwd[1] = 0x1000;
    c.up[0] = c.up[1] = 0; c.up[2] = 0x1000;
    g.cars.push_back(std::move(c));
}

int main() {
    SDL_Init(0);
    Game& g = TheGame();

    reset(g, false);
    const int32_t at[3] = {0, 0, 0};
    g.fires.start(g, at, Fires::None, 0, 20, 0, false, false);
    g.fires.process(g);
    check(g.fires.burning(Fires::None, 0), "a loose fire over land remains eligible for burning");
    check(!g.fires.list().empty(), "a loose fire over land is not extinguished on its first process");

    reset(g, true);
    g.fires.start(g, at, Fires::None, 0, 20, 0, false, false);
    g.fires.process(g);
    check(g.fires.list().empty(), "a loose fire over water keeps its first-process extinguish behaviour");

    reset(g, false);
    addPed(g, 7);
    g.fires.start(g, at, Fires::Ped, 7, -1, 1, false, true);
    g.fires.process(g);
    check(g.fires.burning(Fires::Ped, 7), "an attached pedestrian fire remains active on land");
    check(g.peds.peds[0].body.health == 126, "an attached pedestrian fire damages its owner on land");

    reset(g, true);
    addPed(g, 7);
    g.fires.start(g, at, Fires::Ped, 7, -1, 1, false, true);
    g.fires.process(g);
    check(!g.fires.burning(Fires::Ped, 7), "a pedestrian fire is extinguished over water");
    check(g.peds.peds[0].body.health == 127, "a pedestrian fire over water causes no damage");

    reset(g, true);
    g.fires.start(g, at, Fires::Player, 0, -1, 1, false, true);
    g.fires.process(g);
    check(!g.fires.burning(Fires::Player, 0) && g.player.health == 127,
          "a player fire is extinguished over water before player damage");

    reset(g, true);
    addCar(g, 9);
    g.fires.start(g, at, Fires::Car, 9, 300, 2, false, true);
    g.fires.process(g);
    check(!g.fires.burning(Fires::Car, 9), "a vehicle fire is extinguished over water");

    g.fires.clear();
    SDL_Quit();
    return failures ? 1 : 0;
}
