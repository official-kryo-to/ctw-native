// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "safehouse.h"
#include "game.h"
#include <cmath>
#include <cstdlib>

namespace {
// cSafehouseManager::Init's first safehouse: where its marker goes (+0x3A0) and its heading (+0x3AC, degrees)
constexpr int32_t kMarker[3] = {0x179A14, 0x16DC2, 0};
constexpr int kHeading = -90;
// its door (qword_4ACB50 / dword_4ACC4C, entry +0x34 = 0): (380.89, 22.90), facing -90 degrees
constexpr int32_t kDoor[3] = {0x17BE3D, 0x16E66, 0};
constexpr int kDoorHeading = -90;
constexpr int kBlip = 0x7000;   // (the marker's id among the radar blips)
constexpr int kSafehouse = 0;

int16_t angle16(int degrees) { return (int16_t)(degrees * 0xB6); }   // SimpleMover::SetHeading

// SimpleMover::GetHeading: degrees, as Math::Get2DAngleFromTo turns ATan2's angle into them
int headingDegrees(int16_t a) {
    const int32_t v = (int32_t)a * 0x2D03;   // (the player's heading() is the negated ATan2 already)
    return (int32_t)((v >> 21) + ((uint32_t)v >> 31));
}

// SetupDoorMarker's angle: Math::Get2DAngleFromTo(marker, marker + the offset (0, 10) turned by heading - 180)
int doorMarkerDegrees() {
    const int16_t a = (int16_t)((((uint32_t)(kHeading - 180) << 12) >> 12) * 0xB6);   // GetLocalOffsetForHeading
    const float r = a * 9.587378e-05f;
    const int32_t dx = (int32_t)(-std::sin(r) * 0xA000), dy = (int32_t)(std::cos(r) * 0xA000);   // MakeRotationZ
    const float t = std::atan2((float)-dx, (float)dy) * 10430.f;   // ATan2(from.x - to.x, to.y - from.y)
    const int32_t v = (int32_t)(int16_t)-(int32_t)t * 0x2D03;
    return (int32_t)((v >> 21) + ((uint32_t)v >> 31));
}
}

void Safehouses::reset() {
    state_ = kIdle;
    forced_ = false;
    waitFrames_ = 0;
}

void Safehouses::setupDoorMarker(Game& game) {
    // (addChevron takes CreateChevron's degrees, from which it is SetAngle(degrees - 180))
    game.addChevron(kMarker, 0x40C040FF, kBlip, doorMarkerDegrees() + 180, 0, 11, 2, 0x3000);
}

// Save's checks: on foot in the rectangle 1.7 either side and 2 up and down about the marker, standing, in control
// and facing the door (within 60 degrees of the safehouse's heading turned round)
bool Safehouses::inArea(const Game& game) const {
    const int32_t* p = game.player.pos;
    return std::abs(p[0] - kMarker[0]) <= 0x1B33 && std::abs(p[1] - kMarker[1]) <= 0x1B33 &&
           std::abs(p[2] - kMarker[2]) <= 0x2000;
}

bool Safehouses::saveConditions(Game& game) const {
    if (game.playerCar >= 0 || game.player.dead || game.cutscene || game.mission) return false;
    if (!inArea(game)) return false;
    int want = kHeading - 180;
    if (want < 0) want = kHeading + 180;
    int h = headingDegrees(game.player.heading());
    if (h < 0) h += 360;
    const unsigned diff = (unsigned)std::abs(h - want);
    return diff < 60 || diff - 301 < 60;
}

void Safehouses::forcePlayerStart(Game& game) {
    forced_ = true;
    save(game, true);
}

void Safehouses::save(Game& game, bool forced) {
    game.cutscene = game.scriptCutscene = true;   // StartCutscene(true)
    game.widescreen.set(true);
    if (forced) {
        // the player at the marker facing out, then PutPlayerAtSafehouseDoor: 7 units behind it, on the ground
        std::copy(kMarker, kMarker + 3, game.player.pos);
        game.player.setHeading(angle16(kHeading));
        const float r = angle16(kHeading) * 9.587378e-05f;
        game.player.pos[0] = kMarker[0] - (int32_t)(std::sin(r) * 0x7000);
        game.player.pos[1] = kMarker[1] - (int32_t)(std::cos(r) * 0x7000);
        game.player.placeOnGround(&game.collision);
        game.scriptWalk.on = false;   // Ped::ClearAllOrders
        doSave(game, true);
        return;
    }
    game.addChevron(kMarker, 0x40C040FF, kBlip, doorMarkerDegrees() + 180, 0, 11, 3, 0x3000);   // style 3
    game.screenFade.toBlack(10);   // CameraImpl::FadeToBlack(10, true, false)
    state_ = kEntering;
    door_.start(game, kDoor, kDoorHeading, DoorHandler::kEnter, [this, &game] { doSave(game, false); }, true);
}

void Safehouses::doSave(Game& game, bool forced) {
    state_ = kSaving;
    game.removeBlip(kBlip);
    game.saveGame.setCurrentSafehouse(kSafehouse);   // SaveGameImpl::CurrentSafehouse
    if (!forced) {   // the app's save (SaveSlot), in a safehouse
        const int slot = game.saveGame.slot() >= 0 && game.saveGame.slot() < SaveGame::kSlots ? game.saveGame.slot() : 0;
        game.saveGame.saveCurrent(game, slot, false, false);
        game.player.health = 0x7F;                   // cPlayer::SetHealth(0x7F)
        game.wanted.setLevel(0, false, false);       // cWantedManager::SetLevel(0)
        TimeCycle& tc = game.world.timeCycle();      // cTimeCycle::AdvanceTime(6 hours)
        const uint32_t t = tc.time() + (6u << 12);
        if (t >= (24u << 12)) tc.day = tc.day + 1 == 7 ? 0 : tc.day + 1;
        tc.setTime(t);
    }
    walkOut(game);
}

void Safehouses::walkOut(Game& game) {
    state_ = kLeaving;
    waitFrames_ = 0x15;   // DelayFadeOnWalkout
    door_.start(game, kDoor, kDoorHeading, DoorHandler::kLeave, [this, &game] {
        // SaveFinished: on the ground, the screen in over 30 frames, then the cutscene ends (WaitForFade)
        game.player.placeOnGround(&game.collision);
        if (game.screenFade.level() != 0) game.screenFade.in(30);
        game.cutscene = game.scriptCutscene = false;
        game.widescreen.set(false);
        forced_ = false;
        state_ = kIdle;
    }, true);
}

void Safehouses::tick(Game& game) {
    door_.tick(game);
    if (state_ == kLeaving && waitFrames_ > 0 && --waitFrames_ == 0 && game.screenFade.level() != 0)
        game.screenFade.in(30);   // DelayFadeOnWalkout: FadeIn(30) while it is black
    const bool owned = game.saveGame.safehouseOwned(kSafehouse);
    if (state_ == kIdle) {
        if (!owned || game.mission) return;
        setupDoorMarker(game);   // Setup -> DefaultCallbacks
        state_ = kWatching;
    }
    if (state_ != kWatching) return;
    if (!owned || game.mission) {   // (cSafehouseManager::SetOnMission: the marker goes)
        game.removeBlip(kBlip);
        state_ = kIdle;
        return;
    }
    // DefaultCallbacks: the player entering the marker's location -> Save; while he is in it without the rest
    // (SaveCheck), Save again every 10 frames
    if (checkWait_ > 0 && --checkWait_ > 0) return;
    if (saveConditions(game)) save(game, false);
    else if (inArea(game)) checkWait_ = 10;
}
