// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#define SDL_MAIN_HANDLED
#include "game.h"
#include <SDL.h>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>

static int failures;
static void check(bool value, const char* message) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); ++failures; }
}
struct PedsTestAccess {
    static bool police(Game& g, size_t index) { return g.peds.policeStep(g, g.peds.peds[index]); }
};
struct GameTestAccess {
    static void restarts(Game& g) {
        g.restart_.hospitals = {{{200*4096,200*4096,0},0}};
        g.restart_.police = {{{3*4096,0,0},-90},{{100*4096,100*4096,0},90}};
    }
    static void death(Game& g) { g.updateDeath(); }
};
static void wantedTable(const std::filesystem::path& path) {
    std::ofstream out(path, std::ios::binary);
    out.write("CTWWANT1",8);
    const uint16_t thresholds[8] = {0,40,150,500,1000,2000,4000,8000};
    for (uint16_t threshold : thresholds) {
        const uint16_t chances = 0;
        out.write((const char*)&threshold,2); out.write((const char*)&chances,2);
    }
    const uint16_t crimes[26]{};
    const int32_t carCrimes[13]{}, levels[7] = {0,1,1,1,1,1,1}, zero[7]{}, probation = 100;
    out.write((const char*)crimes,sizeof crimes); out.write((const char*)carCrimes,sizeof carCrimes);
    out.write((const char*)levels,sizeof levels);
    for (int i = 0; i < 11; ++i) out.write((const char*)zero,sizeof zero);
    out.write((const char*)&probation,4);
}
static void prepare(Game& g, const std::string& dir, int level, int health = 20) {
    check(g.wanted.init(dir), "synthetic wanted fixture loads");
    g.wanted.setLevel(level,false,true);
    g.player.pos[0] = g.player.pos[1] = g.player.pos[2] = 0;
    g.player.health = health;
    g.peds.peds.resize(1);
    auto& cop = g.peds.peds[0];
    cop.uid = 1; cop.type = 10;
    cop.body.pos[0] = 4096; cop.body.pos[1] = cop.body.pos[2] = 0;
    GameTestAccess::restarts(g);
}
int main() {
    SDL_Init(0);
    const auto dir = std::filesystem::temp_directory_path()/"ctw-police-synthetic";
    std::filesystem::create_directories(dir);
    wantedTable(dir/"wanted_tables.bin");
    {
        // Exercise normal approach, movement, dynamic body contacts and countdown together. Calling policeStep
        // on actors already overlapping misses a reach smaller than the collision solver permits them to reach.
        Game g; prepare(g,dir.string(),1);
        g.scriptedInput = true; g.clockRunning = false;
        g.peds.densityScale = g.traffic.densityScale = 0;
        g.frame = 100;
        auto& cop = g.peds.peds[0];
        cop.body.pos[0] = 10*4096;
        cop.body.setHeading(-0x4000);
        g.camera.reset(g.player.pos,0);
        bool approached = false, reachedContact = false;
        int64_t contactDistance = 0;
        for (int frame = 0; frame < 180 && !g.arrested(); ++frame) {
            const int32_t previous = cop.body.pos[0];
            g.tick();
            approached |= cop.body.pos[0] < previous;
            if (g.arrestGrabbed && !reachedContact) {
                const int64_t dx = (int64_t)cop.body.pos[0]-g.player.pos[0];
                const int64_t dy = (int64_t)cop.body.pos[1]-g.player.pos[1];
                const int64_t dz = (int64_t)cop.body.pos[2]-g.player.pos[2];
                contactDistance = dx*dx+dy*dy+dz*dz;
                reachedContact = true;
                check(!g.arrested() && cop.enforcer.arrestTimer == 19,
                      "a real cop approach begins the twenty-tick arrest countdown only on arrival");
            }
        }
        check(approached && reachedContact && contactDistance > (int64_t)(2*Player::kSphere)*(2*Player::kSphere) &&
              contactDistance <= (int64_t)(2*Player::kRadius)*(2*Player::kRadius),
              "a cop walking from ten units can arrive at the actual dynamic-contact boundary");
        check(g.arrested() && !g.player.dead && g.arrestingCop == cop.uid,
              "normal game ticks complete the approach-to-arrest sequence without teleporting either actor into overlap");
    }
    {
        Game g; prepare(g,dir.string(),1);
        auto& cop = g.peds.peds[0];
        cop.type = 1;
        check(!PedsTestAccess::police(g,0) && !g.arrestingCop, "a civilian never runs the enforcer task");
        cop.type = 10; cop.body.pos[0] = 20*4096;
        check(!PedsTestAccess::police(g,0), "one-star acquisition excludes the exact twenty-unit boundary");
        --cop.body.pos[0];
        check(PedsTestAccess::police(g,0) && cop.enforcer.pursuing && !g.arrestGrabbed,
              "one-star cop acquires just inside the boundary and approaches without remote arrest");
        g.wanted.setLevel(0,false,true);
        check(!PedsTestAccess::police(g,0) && !g.arrestingCop && !cop.enforcer.pursuing,
              "clearing wanted cancels pursuit and releases the arrest owner");
    }
    {
        Game g; prepare(g,dir.string(),1,21);
        PedsTestAccess::police(g,0);
        check(!g.arrestGrabbed && !g.arrestingCop, "a healthy one-star player is not arrested on contact");
        g.player.health = 20;
        for (int i = 0; i < 19; ++i) PedsTestAccess::police(g,0);
        check(g.arrestGrabbed && !g.arrested() && g.peds.peds[0].enforcer.arrestTimer == 1,
              "arrest waits for the native twenty-tick countdown");
        PedsTestAccess::police(g,0);
        check(g.arrested() && !g.player.dead, "the twentieth tick arrests without killing the player");
        check(g.stats.timesBusted() == 0 && g.stats.timesWasted() == 0, "the watcher owns the statistics event");
        GameTestAccess::death(g);
        check(g.stats.timesBusted() == 1 && g.stats.timesWasted() == 0,
              "the arrest watcher increments busted exactly once and never wasted");
        for (int i = 1; i < 72 && g.arrested(); ++i) GameTestAccess::death(g);
        check(!g.arrested() && !g.arrestGrabbed && !g.arrestingCop && !g.player.dead && g.player.health == 127,
              "police restart releases every arrest/control state and restores health");
        check(g.player.pos[0] == 3*4096 && g.player.pos[1] == 0 && std::abs(g.player.heading() + 0x4000) <= 2,
              "arrest uses the closest police station and its signed heading, never a hospital");
        check(g.wanted.level() == 0 && g.stats.timesBusted() == 1,
              "restart clears wanted without counting the arrest twice");
        check(g.world.timeCycle().time() == 18u*4096, "arrest advances the clock by six game hours");
    }
    {
        Game g; prepare(g,dir.string(),2);
        g.player.vel[0] = 4096;
        PedsTestAccess::police(g,0);
        check(!g.arrestGrabbed && !g.arrestingCop, "two-star foot arrest rejects a moving target");
        g.player.vel[0] = 0;
        PedsTestAccess::police(g,0);
        check(g.arrestGrabbed && g.arrestingCop == 1, "two-star stopped low-health player can be arrested");
        g.peds.peds.clear();
        g.tick();
        check(!g.arrestGrabbed && !g.arrestingCop, "removing an arresting officer cannot leave player input locked");
    }
    {
        Game g; prepare(g,dir.string(),3,1);
        for (int i = 0; i < 25; ++i) PedsTestAccess::police(g,0);
        check(!g.arrested() && !g.arrestGrabbed, "ordinary cops never force an arrest above two stars");
        g.wanted.setLevel(1,false,true);
        auto nearer = g.peds.peds[0]; nearer.uid = 2; nearer.body.pos[0] = 2048;
        g.peds.peds.push_back(nearer);
        PedsTestAccess::police(g,0); PedsTestAccess::police(g,1);
        check(g.arrestingCop == 2, "the nearer eligible officer takes ownership using a stable handle");
        PedsTestAccess::police(g,0);
        check(g.arrestingCop == 2, "a farther officer cannot steal an active arrest");
    }
    std::filesystem::remove_all(dir);
    SDL_Quit();
    return failures ? 1 : 0;
}
