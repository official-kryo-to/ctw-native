// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// Money, the HUD's cash meter, pickups and peds' dropped goodies, without game data.
#define SDL_MAIN_HANDLED
#include "game.h"
#include "random.h"
#include <SDL.h>
#include <cstdio>

static int failures;
static void check(bool value, const char* message) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); ++failures; }
}
struct GameTestAccess {
    static void restarts(Game& g) { g.restart_.hospitals = {{{200 * 4096, 200 * 4096, 0}, 0}}; }
    static void death(Game& g) { g.updateDeath(); }
    static int respawnFrame(const Game& g) { return g.respawnFrame(); }
};

int main() {
    SDL_Init(0);
    {   // cPlayer::GiveMoney / TakeMoney clamp to 0 .. 99,999,999
        Game& g = TheGame();
        g.money = 0;
        g.takeMoney(50);
        check(g.money == 0, "money never goes below zero");
        g.giveMoney(99999990);
        g.giveMoney(100);
        check(g.money == 99999999, "money stops at 99,999,999");
        g.money = 0;
    }
    {   // cHudApp::UpdateCash + cCashMeter::Process
        CashMeter meter;
        check(!meter.step(0) && meter.slide() == 0x2A, "with nothing to show the cash stays out of view");
        int frames = 0, sounds = 0;
        while (meter.slide() > 0 && frames < 100) { meter.step(100); ++frames; }
        check(frames == 21 && meter.shown() == 0, "the cash slides in two pixels a frame before counting starts");
        int count = 0;
        while (meter.shown() != 100 && count < 200) { sounds += meter.step(100); ++count; }
        check(meter.shown() == 100 && count > 10 && count < 60, "the shown amount counts up with a growing step");
        check(sounds > 0 && sounds <= count, "each changing count step plays the HUD tick");
        check(meter.text() < 100, "the $ text is made before the last count step, as in the original");
        int out = 0;
        while (meter.slide() < 0x2A && out < 100) { meter.step(100); ++out; }
        check(out == 21, "once the amount agrees the cash slides back out");
        check(CashMeter::widthTemplate(5) == 0 && CashMeter::widthTemplate(42) == 10 && CashMeter::widthTemplate(1234) == 1000 &&
              CashMeter::widthTemplate(12345678) == 1000000, "the meter is placed by an amount of the same digit count");
    }
    {   // a money pickup under the player is collected, fades over 15 frames and goes
        Game& g = TheGame();
        g.pickups.reset();
        g.money = 0;
        g.player.pos[0] = g.player.pos[1] = g.player.pos[2] = 0;
        const int32_t at[3] = {4096, 0, 0};
        Pickups::Pickup* p = g.pickups.add(Pickups::Money, at, 30, 0x3E8);
        check(p && p->texture == 0 && p->colour == 0x3E0, "a small amount of money uses the green dollar");
        const int32_t far[3] = {6 * 4096, 0, 0};
        Pickups::Pickup* big = g.pickups.add(Pickups::Money, far, 150, 0x3E8);
        check(big && big->texture == 0x22, "more than $99 uses the blue dollar");
        g.pickups.update(g);
        check(g.money == 30, "standing within two units collects the money");
        check(g.pickups.list().size() == 2 && g.pickups.list()[0].state == 4, "collected money goes, fading out");
        for (int i = 0; i < 15; ++i) g.pickups.update(g);
        check(g.pickups.list().size() == 1 && g.pickups.list()[0].amount == 150, "after the fade only the far pickup is left");
        check(g.stats.data().foundOnTheStreet == 30, "money found on the street is counted");
        for (int i = 16; i < 499; ++i) g.pickups.update(g);   // (16 frames have passed already)
        check(g.pickups.list().size() == 1 && g.pickups.list()[0].state == 0, "a dropped pickup lasts 1000 / 2 frames");
        g.pickups.update(g);
        check(g.pickups.list()[0].state == 4, "then it goes by itself");
    }
    {   // the pool: dropped pickups need five free slots, AddPickup makes room by force
        Game& g = TheGame();
        g.pickups.reset();
        g.player.pos[0] = 1000 * 4096;
        const int32_t at[3] = {0, 0, 0};
        for (int i = 0; i < 25; ++i) g.pickups.add(Pickups::Money, at, 10, (int16_t)(100 + i));
        check(g.pickups.addDroppedMoney(10, at) != nullptr, "dropped money appears while five pickup slots are free");
        check(!g.pickups.addDroppedMoney(10, at), "no dropped money while fewer than five pickup slots are free");
        check(g.pickups.add(Pickups::Health, at, 0, -1) != nullptr, "AddPickup still adds the pickup");
        int going = 0;
        for (const auto& p : g.pickups.list()) going += p.state == 4;
        check(going == 1 && g.pickups.list()[0].state == 4, "ForceFreePickup takes the dropped pickup with the least time left");
    }
    {   // health from the map: collected only when hurt, then remembered until it respawns
        Game& g = TheGame();
        g.pickups.reset();
        g.player.pos[0] = g.player.pos[1] = 0;
        g.player.health = 127;
        const int32_t at[3] = {0, 0, 0};
        g.pickups.add(Pickups::Health, at, 0, -1);
        g.pickups.update(g);
        check(g.pickups.list()[0].state == 0, "full health leaves the health pickup where it is");
        g.player.health = 40;
        g.pickups.update(g);
        check(g.player.health == 127 && g.pickups.list()[0].state == 1, "a hurt player is healed and the pickup is taken");
        g.pickups.update(g);
        check(g.pickups.list()[0].state == 4, "the collected list takes over and the pickup goes");
    }
    {   // cPed::AssociateWithPlayer and GetAssignedWeapon
        RandInit(0);
        int carrying[4] = {}, rich[4] = {};
        for (int i = 0; i < 2000; ++i) { ++carrying[Pedestrians::moneyTier(0, 0)]; ++rich[Pedestrians::moneyTier(2, 0)]; }
        check(carrying[2] == 0 && carrying[3] == 0 && carrying[1] > 400 && carrying[1] < 800,
              "in a poor zone three peds in ten carry $10");
        check(rich[1] == 0 && rich[0] > 850 && rich[0] < 1150 && rich[3] > 200, "in a rich zone half carry $30 or $60");
        check(Pedestrians::moneyTier(7, 2) == 2, "an unknown affluence keeps the money");
        Pedestrians::Record armed{};
        armed.weapons[0] = 8; armed.weapons[1] = 0; armed.chances[0] = 100; armed.chances[1] = 100;
        check(Pedestrians::assignedWeapon(armed) == 8, "a certain first weapon is always given");
        Pedestrians::Record unarmed{};
        check(Pedestrians::assignedWeapon(unarmed) == 0x1C, "a record without weapons gives none");
    }
    {   // cPed::DropGoodiesWhenDead: the weapon first, otherwise the money, once the body is still
        Game& g = TheGame();
        g.pickups.reset();
        g.player.pos[0] = g.player.pos[1] = 0;
        Pedestrians::Ped p;
        p.body.pos[0] = 20 * 4096;
        p.moneyTier = 2;
        p.body.vel[0] = 2 * 4096;
        g.peds.dropGoodies(g, p);
        check(g.pickups.list().empty(), "a body still sliding drops nothing yet");
        p.body.vel[0] = 0;
        g.peds.dropGoodies(g, p);
        check(g.pickups.list().size() == 1 && g.pickups.list()[0].type == Pickups::Money &&
              g.pickups.list()[0].amount == 30 && g.pickups.list()[0].pos[2] == 0x1800, "money tier two drops $30 1.5 units up");
        Pedestrians::Ped gang = p;
        gang.weapon = 8;
        g.peds.dropGoodies(g, gang);
        check(g.pickups.list().size() == 2 && g.pickups.list()[1].type == 10 && g.pickups.list()[1].pos[2] == 0x1000,
              "an armed ped drops its weapon instead");
        Pedestrians::Ped poor = p;
        poor.moneyTier = 0;
        g.peds.dropGoodies(g, poor);
        check(g.pickups.list().size() == 2, "a ped without money drops nothing");
        check(Pickups::typeFromWeapon(18) == 24 && Pickups::weaponFromType(24) == 18 && Pickups::typeFromWeapon(1) == 30,
              "weapons and pickup types map both ways");
    }
    {   // the hospital takes $200
        Game& g = TheGame();
        GameTestAccess::restarts(g);
        g.money = 150;
        g.player.dead = true;
        const uint32_t spent = g.stats.data().spentOnHealthCare;
        for (int i = 0; i < GameTestAccess::respawnFrame(g) + 1 && g.player.dead; ++i) GameTestAccess::death(g);
        check(!g.player.dead && g.money == 0, "being reborn at the hospital costs up to $200");
        check(g.stats.data().spentOnHealthCare == spent + 150, "the stats count what could be paid");
    }
    if (!failures) std::puts("PASS: money, cash meter, pickups and dropped goodies");
    return failures ? 1 : 0;
}
