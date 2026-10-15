// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// The sniper rifle (weapon 18): cPlayerOnFoot::HandlePlayerSniper -> cPlayerAsSniper, cSniperCam and
// cSniperRifle::CreateBullet.
//
// While the rifle is the player's weapon on foot he is in the scope: cSniperCam looks straight down (pitch 0xC000)
// from 30 units up over an aim point that starts 4 units in front of him, turned to his heading. The stick moves the
// aim point (cIPhonePad::ProcessSniper: a full stick is 1 unit a frame, 0.4 zoomed) relative to the screen; it stays
// within the rifle's range of him. He does not walk (ConstrainWalkSpeed(0, 0)) but turns to face the aim point once
// it is more than 0.1 units away (|dx| + |dy|). The zoom button toggles the field of view between 30 and 20 degrees,
// 5 degrees a frame. The scope model (gGlobalAssetMgr +0x246) hangs 2 units under the camera, scaled to the screen
// width (0.155 x width / height) and turned with it.
//
// A shot (FireSfx event 45, a camera shake of amplitude 2 decaying by 0.5) hits the first ped on foot or vehicle
// whose bound circle contains the aim point, within 10 units of it: a type-1 hit of the rifle's damage along the
// fire direction (effect 0x1D). The PC zoom key is Z.
//
// Not ported: the scope HUD (crosshair sprites, the top-screen blips) and the line-of-sight check in Render.
#pragma once
#include <cstdint>

class Game;
class Weapon;
struct WorldCamera;

struct SniperScope {
    bool active = false;
    int32_t pos[3] = {0, 0, 0x1E000};   // cSniperCam +0xB8: the aim point, the camera 30 units up
    uint16_t yaw = 0;                   // +0xE8: the player's heading
    int16_t fov = 0x1555;               // +0x3F8
    bool zoomed = false;                // cIPhonePad +0x670
    bool zoomPressed = false;           // the zoom button this frame (cControlWrapper::Pressed(0xD))

    void enter(const Game& g);   // cSniperCam::cSniperCam
    // cPlayerAsSniper::Process + cSniperCam::Update: the stick (x right, y up, -1..1) moves the aim point
    void update(Game& g, float stickX, float stickY);
    void toWorldCamera(const Game& g, WorldCamera& cam) const;
    void renderScope(int width, int height) const;   // cSniperCam::Set's scope model
};

void Sniper_CreateBullet(Game& g, Weapon& w);   // cSniperRifle::CreateBullet (after FireSfx)
