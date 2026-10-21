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

struct VehicleTestAccess {
    static int32_t pendingLift(Vehicle& c) { c.recalcKinematics(); return c.vel[2]; }
    static void launched(Vehicle& c) { c.burnout_ = 3; c.playerDriving_ = true; c.hx = 0x1000; }
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

    // cFountainStream::OnCollision: a person in the jet is knocked into state 14 and given the reference
    // velocity (the fixed 7-unit diagonal and four times the upward jet); no health is lost.
    {
        reset(g, false);
        g.player.pos[0] = 0x1000 + Player::kRadius + 1;   // just clear of the 1 x 1 box
        addPed(g, 11);
        const int16_t up[3] = {0, 0, 0x2000};
        Fountain fountain(at, up);
        fountain.process(g);
        const Pedestrians::Ped& p = g.peds.peds[0];
        check(p.body.reacting() && p.deadFrames == 0, "a fountain jet knocks a standing pedestrian down");
        check(p.body.vel[0] == 0x7000 && p.body.vel[1] == 0x7000 && p.body.vel[2] == 0x8000,
              "a fountain jet sets the reference push velocity");
        check(p.body.health == 127, "a fountain jet does no damage");
        check(!g.player.reacting() && g.player.vel[0] == 0, "a fountain does not reach a player clear of its box");
        g.player.pos[0] = 0x1000 + Player::kRadius;
        fountain.process(g);
        check(g.player.reacting(), "a fountain reaches the player touching its box");
    }
    // Vehicles over the jet receive eight times its velocity as a type-1 force at the fountain's base.
    {
        reset(g, false);
        g.player.pos[0] = 50 * 4096;
        addCar(g, 12);
        const int16_t up[3] = {0, 0, 0x2000};
        Fountain fountain(at, up);
        fountain.process(g);
        check(VehicleTestAccess::pendingLift(g.cars[0]) > 0, "a fountain jet lifts a vehicle standing over it");
    }
    // CWaterCannon::PutOutFires: a fire under the stream loses two health a frame and goes out at zero.
    {
        reset(g, false);
        g.player.pos[0] = 50 * 4096;
        const int32_t beside[3] = {0x800, 0, 0};
        g.fires.start(g, beside, Fires::None, 0, -1, 0, false, false);
        const int16_t up[3] = {0, 0, 0x2000};
        Fountain fountain(at, up);
        int frames = 0;
        for (; frames < 100 && !g.fires.list().empty(); ++frames) {
            fountain.process(g);
            g.fires.process(g);
            ++g.frame;
        }
        check(g.fires.list().empty() && frames > 60 && frames < 70,
              "a fire beside a fountain is put out after its 127 health is worn down two at a time");
    }

    // cWheeledVehicle::AddSkidmarks: a launched burnout drops a short fire at each back wheel every frame
    {
        reset(g, false);
        addCar(g, 13);
        VehicleTestAccess::launched(g.cars[0]);
        Vehicles_AddSkidmarks(g, g.cars[0]);
        check(g.fires.list().size() == 2 && g.fires.list()[0].style == 5 && g.fires.list()[0].life >= 20 &&
              g.fires.list()[0].life < 40, "a launched burnout leaves a style-5 flame at each back wheel");
    }

    g.fires.clear();
    SDL_Quit();
    return failures ? 1 : 0;
}
