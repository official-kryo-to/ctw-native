// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// Pedestrians on synthetic data: sector bridges, looks from pedinfo records, and vehicles running peds over.
#define SDL_MAIN_HANDLED
#include "game.h"
#include "explosions.h"
#include "random.h"
#include <SDL.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static int failures;
static void check(bool ok, const char* why) { if (!ok) { std::fprintf(stderr, "FAIL: %s\n", why); ++failures; } }

struct CollisionTestAccess {
    static void cell(Collision& col, int cx, int cy, Collision::PedPaths paths) {
        if (col.world_.empty()) col.world_.assign(10000 * 4, 0);
        auto c = std::make_unique<Collision::Cell>();
        c->loaded = true;
        c->groundMap.assign(400, 0x55);
        c->pedPaths = std::move(paths);
        col.cells_[cx * 100 + cy] = std::move(c);
    }
    static void floor(Collision& col, int cx, int cy, const int32_t at[3]) {
        auto c = std::make_unique<Collision::Cell>(); c->loaded = true;
        c->groundMap.assign(400,0x55);
        c->boxes.push_back({at[0],at[1],-4096,25*4096,25*4096,4096,0,0});
        col.cells_[cx*100+cy] = std::move(c);
    }
};

struct PedsTestAccess {
    static void occupant(Pedestrians& peds, Game& g, Pedestrians::Ped& p) { peds.updateOccupant(g,p); }
    static void contacts(Pedestrians& peds, Game& g) { peds.contacts(g); }
    static bool advance(Pedestrians& peds, Game& g, Pedestrians::Ped& p) { return peds.advance(g, p); }
    static void records(Pedestrians& peds, std::vector<Pedestrians::Record> records, std::vector<uint16_t> first) {
        peds.records_ = std::move(records);
        peds.firstRecord_ = std::move(first);
    }
    static void runOver(Pedestrians& peds, Game& g, Pedestrians::Ped& p) { peds.runOver(g, p); }
    static bool evade(Pedestrians& peds, Game& g, Pedestrians::Ped& p) { return peds.evade(g, p); }
    static bool roll(Pedestrians& peds, Game& g, Pedestrians::Ped& p) { return peds.roll(g, p); }
    static void flee(Pedestrians& peds, Game& g, Pedestrians::Ped& p) { peds.fleeStep(g, p); }
};

// cell (cx, cy) starts at (50 cx - 3500, 50 cy - 2500); nodes are in 1/8 units
static Collision::PathNode node(float x, float y, uint16_t first, int links, bool bridge) {
    return {first, (uint16_t)(links << 2 | (bridge ? 0x80 : 0)), (int16_t)(x * 8), (int16_t)(y * 8), 0, 0};
}

int main() {
    SDL_Init(0);
    Game& g = TheGame();
    Pedestrians& peds = g.peds;

    // Two sectors side by side, joined at x = -2950 by bridge nodes at the same point.
    // A (10, 10): 0 (-2980, -1975) <-> 1 (-2950, -1975, bridge to sector 10 * 140 + 11)
    // B (11, 10): 0 (-2950, -1975, bridge back) <-> 1 (-2920, -1975)
    Collision::PedPaths a, b;
    a.nodes = {node(-2980, -1975, 0, 1, false), node(-2950, -1975, 1, 1, true)};
    a.links = {1, 0};
    a.bridges = {{1, 10 * 140 + 11}};
    b.nodes = {node(-2950, -1975, 0, 1, true), node(-2920, -1975, 1, 1, false)};
    b.links = {1, 0};
    b.bridges = {{0, 10 * 140 + 10}};
    CollisionTestAccess::cell(g.collision, 10, 10, a);
    CollisionTestAccess::cell(g.collision, 11, 10, b);
    check(g.collision.pedPaths(10, 10) && g.collision.pedPaths(11, 10) && !g.collision.pedPaths(12, 10),
          "pavement nodes are read per sector");

    Pedestrians::Ped p;
    p.prev = {10, 10, 0};
    p.cur = {10, 10, 1};
    // cWanderPath::Process copies the old node ID before SetNext; the previous node stays in its original sector.
    check(PedsTestAccess::advance(peds, g, p) && p.cur.cx == 11 && p.cur.index == 1 &&
          p.prev.cx == 10 && p.prev.cy == 10 && p.prev.index == 1,
          "a bridge node continues in the neighbouring sector while retaining its arrival node");
    check(std::abs(p.target[0] - (-2920 * 4096)) <= 0xC00 && std::abs(p.target[1] - (-1975 * 4096)) <= 0xC00,
          "the target is the next node plus at most 0.75 units of offset");
    check(PedsTestAccess::advance(peds, g, p) && p.cur.cx == 11 && p.cur.index == 0 && p.prev.index == 1,
          "a dead end turns the ped round");

    Pedestrians::Ped lost;
    lost.prev = {11, 10, 1};
    lost.cur = {11, 10, 0};
    Collision::PedPaths alone = b;
    alone.bridges = {{0, 10 * 140 + 12}};   // its neighbour has no pavement
    CollisionTestAccess::cell(g.collision, 11, 10, alone);
    check(PedsTestAccess::advance(peds, g, lost) && lost.cur.cx == 11 && lost.cur.index == 1,
          "an unresolved bridge turns the ped round instead of leaving the network");

    // Looks: type 1 subtypes 0..1 without fixed palettes, a gang (type 0) with fixed palettes 20 / 20, cops (10).
    PedsTestAccess::records(peds, {{0, 0, 1, 0, 20, 20}, {1, 0, 0, 0, 0, 0}, {1, 1, 0, 0, 0, 0}, {10, 0, 0, 0, 0, 0},
                                   {10, 1, 0, 0, 0, 0}},
                            {0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 3});
    check(peds.record(1, 1) && !peds.record(1, 2) && !peds.record(2, 0), "pedinfo records are found by type and subtype");
    RandInit(0);
    int men = 0, women = 0;
    bool palettesOk = true;
    for (int i = 0; i < 400; ++i) {
        Pedestrians::Ped c;
        c.type = 1;
        peds.dress(c, 0);
        if (c.male) { ++men; palettesOk = palettesOk && c.body.bodySet == 1 && c.body.palUpper < 5 && c.body.palLegs < 5; }
        else { ++women; palettesOk = palettesOk && c.body.bodySet == 0 && c.body.palUpper >= 30 && c.body.palUpper < 35; }
    }
    check(palettesOk, "men use body set 1 and palettes 0..4, women body set 0 and palettes 30..34");
    check(men > 200 && women > 120, "about 60% of civilians are men (cPed::cPed: Rand16Critical(100) > 40)");
    Pedestrians::Ped woman;
    woman.type = 1;
    peds.dress(woman, 2);
    check(!woman.male, "a female zone makes civilians women");
    Pedestrians::Ped gang;
    gang.type = 0;
    peds.dress(gang, 2);
    check(gang.male && gang.body.palUpper == 19 && gang.body.palLegs == 19, "fixed record palettes are used minus one");
    Pedestrians::Ped cop;
    cop.type = 10;
    cop.subtype = 1;
    peds.dress(cop, 0);
    check(cop.male && cop.body.palUpper == 0x12, "cop subtypes use their uniform palette");

    // Vehicle contact and damage have different thresholds in the original.
    g.vehicleInfos.resize(1);
    std::memset(g.vehicleInfos.data(), 0, sizeof(VehicleInfo));
    const int32_t at[3] = {-2900 * 4096, -1975 * 4096, 0};
    const int car = g.spawnCar(0, at, 0, 0);
    check(car >= 0, "test car spawns");
    if (car >= 0) {
        Vehicle& v = g.cars[car];
        v.pos[2] = 0; // this synthetic scene's floor; the spawn cell was initially unloaded
        v.hx = 0x1000; v.hy = 0x2000; v.hz = 0x1000;
        Pedestrians::Ped walker;
        walker.body.pos[0] = v.pos[0]; walker.body.pos[1] = v.pos[1] + 0x1000; walker.body.pos[2] = v.pos[2];
        PedsTestAccess::runOver(peds, g, walker);
        check(walker.deadFrames < 0 && walker.body.pos[1] - v.pos[1] == 0x1000,
              "gentle contact does not teleport a pedestrian out of a stopped car");
        v.vel[1] = 0x8000;
        walker.body.pos[1] = v.pos[1] + 0x1000;
        PedsTestAccess::runOver(peds, g, walker);
        check(walker.deadFrames < 0 && walker.body.health == 127, "an eight-unit/s contact is below the damage threshold");
        v.vel[1] = 16 * 4096;
        walker.body.pos[1] = v.pos[1] + v.hy + 0x400;
        PedsTestAccess::runOver(peds, g, walker);
        check(walker.deadFrames == 0 && walker.body.health == 119 && !walker.body.dead && walker.body.reacting(),
              "a moderate frontal hit causes damage and a recoverable knockdown");
        check(walker.body.vel[2] == 0 && walker.body.vel[1] > 0 && walker.body.pos[2] == v.pos[2] + 2*4096,
              "Android run-over effect 2 raises the body while preserving the contact velocity");
        Pedestrians::Ped fatal;
        std::copy(v.pos, v.pos+3, fatal.body.pos); fatal.body.pos[1] += v.hy + 0x400;
        v.vel[1] = 26 * 4096;
        PedsTestAccess::runOver(peds,g,fatal);
        check(fatal.body.dead && fatal.body.health == 0 && fatal.body.vel[1] > 26*4096 && fatal.body.vel[2] == 0 &&
              fatal.body.renderPose(nullptr,0,1,0).reaction_state == 2,
              "fatal NPC damage queues the death pose first and preserves collision momentum");
        const int32_t start[3] = {fatal.body.pos[0],fatal.body.pos[1],fatal.body.pos[2]};
        fatal.body.actReaction(nullptr); fatal.body.advance(nullptr,false);
        check(fatal.body.pos[1] != start[1] && fatal.body.pos[2] == start[2], "a dead body slides instead of freezing or receiving a second launch");
        Player survivor;
        std::copy(v.pos,v.pos+3,survivor.pos);survivor.pos[1]+=v.hy+0x400;
        const int32_t front[3]={0,4096,0};
        check(survivor.vehicleImpact(v,front,true) && survivor.health == 112 && !survivor.dead &&
              survivor.vel[1] == 40*4096 && survivor.vel[2] == 20*4096,
              "a surviving high-speed player impact uses the reference launch rather than NPC lethal damage");
        CollisionTestAccess::floor(g.collision,12,10,v.pos);
        Vehicle recoveryCar=v; recoveryCar.vel[1]=16*4096;
        Player recovery; recovery.playerControlled=false;
        std::copy(v.pos,v.pos+3,recovery.pos);recovery.pos[1]+=v.hy+0x400;
        check(recovery.vehicleImpact(recoveryCar,front,false) && !recovery.dead,"recovery starts from a living run-over response");
        for (int i=0;i<90;++i) { recovery.actReaction(nullptr,true); recovery.advance(&g.collision,false); }
        check(recovery.onGround() && std::abs(recovery.pos[2]) < 0x100 && recovery.reacting(),
              "a fallen body reaches the floor and remains down while a vehicle pins it");
        check(Pedestrians::vehicleAbove(g,recovery),"the original radius-0.75 feet query detects an overhead vehicle");
        for (int i=0;i<4;++i) recovery.actReaction(nullptr,false);
        check(!recovery.reacting() && !recovery.dead && recovery.health == 119,
              "an unpinned living body completes getup and returns to locomotion");
        // dodging the player's car (cWanderPath::HandleEvasiveManeuvers)
        g.playerCar = car;
        std::copy(v.pos, v.pos + 3, g.player.pos);
        Pedestrians::Ped dodger;
        dodger.body.pos[0] = v.pos[0]; dodger.body.pos[1] = v.pos[1] + 12 * 4096; dodger.body.pos[2] = v.pos[2];
        v.vel[1] = 0x6000;   // 6 units/s: too slow to make anyone jump
        PedsTestAccess::evade(peds, g, dodger);
        check(!dodger.evading, "a slow car does not make pedestrians dive");
        v.vel[1] = 0x10000;  // 16 units/s straight at the ped
        PedsTestAccess::evade(peds, g, dodger);
        check(dodger.evading && dodger.rollFrames == 0 && std::abs(dodger.rollVel[0]) == 20 * 4096 && dodger.rollVel[1] == 0,
              "a fast car heading for a pedestrian makes it roll to one side at 20 units/s");
        const int32_t x0 = dodger.body.pos[0];
        check(PedsTestAccess::roll(peds, g, dodger) && std::abs(dodger.rollVel[0]) < 20 * 4096 &&
              dodger.body.pos[0] == x0 && dodger.movementPrepared,
              "roll drag prepares velocity without integrating ahead of contacts");
        dodger.body.advance(nullptr,false);
        check(dodger.body.pos[0]-x0 == ((int64_t)dodger.rollVel[0]*17 >> 9), "rolls use the reference 17/512 movement interval");
        check(PedsTestAccess::evade(peds, g, dodger), "a ped that rolled away keeps evading while the car is near");
        g.player.pos[0] += 30 * 4096;
        check(!PedsTestAccess::evade(peds, g, dodger), "evading ends 25 units from the player's car");
        g.player.pos[0] -= 30 * 4096;
        Pedestrians::Ped aside;
        aside.body.pos[0] = v.pos[0] + 10 * 4096; aside.body.pos[1] = v.pos[1] + 12 * 4096; aside.body.pos[2] = v.pos[2];
        PedsTestAccess::evade(peds, g, aside);
        check(!aside.evading, "pedestrians out of the car's path keep walking");
        g.playerCar = -1;
    }

    // Dynamic contact uses entity spheres (1.25) rather than static sweep spheres (0.875).
    g.playerCar = -1; g.player.hidden = g.player.dead = false;
    std::fill(g.player.pos, g.player.pos + 3, 0);
    std::fill(g.player.vel,g.player.vel+3,0);
    peds.peds.emplace_back(); auto& contact = peds.peds.back();
    contact.body.pos[0] = 2 * 4096; contact.body.pos[1] = contact.body.pos[2] = 0;
    g.player.vel[0] = 4096;
    PedsTestAccess::contacts(peds,g);
    check(contact.body.pos[0] == 2*4096 && g.player.pos[0] == 0 && g.player.vel[0] == -40,
          "dynamic radius catches contact and removes approaching velocity without teleporting positions");
    g.player.vel[0] = -4096;
    PedsTestAccess::contacts(peds,g);
    check(g.player.vel[0] == -4096, "separating contacts preserve velocity");
    contact.body.pos[0] = 0;
    PedsTestAccess::contacts(peds,g);
    check(contact.body.pos[0] == 0 && g.player.vel[0] == -4096, "coincident spheres have the reference zero normal");
    contact.body.pos[0] = 3*4096; g.player.vel[0] = 60*4096;
    PedsTestAccess::contacts(peds,g);
    check(g.player.vel[0] < 0, "a fast pedestrian sweep detects contact before crossing another body");
    peds.peds.clear();

    // Reactions (cPed::GetReactionHelper): the first record for the action or for any action (9) within its chance.
    PedsTestAccess::records(peds, {{1, 0, 0, 0, 0, 0, {{0, 3, 4}, {100, 1, 4}, {100, 1, 3}, {100, 5, 9}}}},
                            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
    Pedestrians::Ped man;
    man.type = 1; man.body.bodySet = 1;
    check(peds.reactionFor(man, 4) == Pedestrians::Flee, "a record that fails its roll passes to the next one");
    check(peds.reactionFor(man, 7) == Pedestrians::Roll, "action 9 records match any action");
    Pedestrians::Ped lady = man;
    lady.body.bodySet = 0;
    check(peds.reactionFor(lady, 7) == Pedestrians::Flee, "women flee instead of rolling or attacking");

    // Fleeing (cFleeOnFoot) along the pavement: A (10, 10) is a straight line of three nodes.
    Collision::PedPaths line;
    line.nodes = {node(-2990, -1975, 0, 1, false), node(-2975, -1975, 1, 2, false), node(-2960, -1975, 3, 1, false)};
    line.links = {1, 0, 2, 1};
    CollisionTestAccess::cell(g.collision, 10, 10, line);
    Pedestrians::Ped runner = man;
    runner.prev = {10, 10, 0};
    runner.cur = {10, 10, 1};
    runner.body.pos[0] = -2980 * 4096; runner.body.pos[1] = -1975 * 4096; runner.body.pos[2] = 0;
    const int32_t blast[3] = {-2970 * 4096, -1975 * 4096, 0};
    peds.react(g, runner, 4, blast, 0);
    check(runner.flee.on && !runner.flee.straight && runner.cur.index == 0 && runner.prev.index == 1,
          "a fleeing ped turns to the end of its link that leads away from the threat");
    runner.body.pos[0] = -2989 * 4096;
    runner.cur = {10, 10, 1}; runner.prev = {10, 10, 0};   // arriving at the middle node from the dead end
    runner.target[0] = -2975 * 4096; runner.target[1] = -1975 * 4096;
    runner.body.pos[0] = -2975 * 4096;
    runner.flee.from[0] = -2950 * 4096;
    PedsTestAccess::flee(peds, g, runner);
    check(runner.cur.index == 0, "at a node the fleeing ped takes the link pointing away from the threat");
    peds.peds.clear();
    peds.peds.push_back(man);
    peds.peds.back().body.pos[1] = blast[1];
    peds.peds.back().body.pos[0] = blast[0] + 60 * 4096;
    const ExplosionBlast generic(blast, 4, 4096, 80);
    peds.explosion(g, generic);
    check(!peds.peds.back().body.reacting(), "peds out of reach of an explosion keep walking");
    peds.peds.back().body.pos[0] = blast[0] + 5 * 4096;   // inside the 7-unit damage radius
    peds.explosion(g, generic);
    check(peds.peds.back().body.reacting() && peds.peds.back().body.health < 127 &&
          peds.peds.back().body.vel[0] == 40 * 4096 && peds.peds.back().body.vel[2] == 20 * 4096,
          "generic explosions damage nearby peds and apply the high knockdown response");
    peds.peds.clear();
    g.cars.clear(); g.cars.resize(1); g.cars[0].uid=400;
    Traffic::ZoneInfo zone{};
    check(peds.createDriver(g,g.cars[0],1,zone), "traffic can create a real civilian driver from the zone appearance records");
    check(peds.peds.size()==1 && g.cars[0].seatUser[0]==(int)peds.peds[0].uid && peds.peds[0].vehicle==400 &&
          peds.peds[0].body.hidden && peds.peds[0].body.attached, "driver and vehicle own matching seat handles");
    check(!peds.createDriver(g,g.cars[0],1,zone), "an occupied driver seat cannot acquire a duplicate pedestrian");
    const int occupant=g.cars[0].seatUser[0];
    g.cars.resize(2); std::swap(g.cars[0],g.cars[1]);
    g.cars[1].pos[0]+=4096;
    PedsTestAccess::occupant(peds,g,peds.peds[0]);
    check(peds.peds[0].vehicle==400 && peds.peds[0].body.pos[0]==g.cars[1].pos[0],
          "driver attachment survives vehicle vector reallocation and index changes");
    check(!peds.exitOccupant(g,g.cars[1],0) && peds.peds[0].exiting && !peds.peds[0].body.hidden &&
          g.cars[1].seatUser[0]==occupant, "car entry waits for the existing driver to finish the exit animation");
    for(int tick=0;tick<90 && peds.peds[0].vehicle;++tick) PedsTestAccess::occupant(peds,g,peds.peds[0]);
    check(!peds.peds[0].vehicle && !peds.peds[0].body.attached && g.cars[1].seatUser[0]==-1 && peds.peds[0].flee.on,
          "exiting driver becomes an on-foot fleeing pedestrian and frees the seat");
    check(peds.createDriver(g,g.cars[1],1,zone), "a freed driver seat can be occupied again");
    peds.releaseOccupants(g,g.cars[1],true);
    check(peds.peds.back().body.dead && peds.peds.back().body.reacting() && !peds.peds.back().body.hidden &&
          !peds.peds.back().vehicle && g.cars[1].seatUser[0]==-1,
          "vehicle destruction releases and kills its NPC occupant with an active death animation");
    check(peds.createDriver(g,g.cars[1],1,zone), "cleanup fixture creates an occupant");
    const size_t beforeRemoval=peds.peds.size();
    peds.discardOccupants(g.cars[1]);
    check(peds.peds.size()+1==beforeRemoval && g.cars[1].seatUser[0]==-1,
          "offscreen car cleanup removes only its attached occupant and clears the seat handle");
    peds.peds.clear();
    SDL_Quit();
    return failures ? 1 : 0;
}
