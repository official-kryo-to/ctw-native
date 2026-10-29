// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// Gui::cHotWiringApp, easy mode (hotwire class 1): the PDA app that starts a parked car the player has got into.
// cPlayer::ApplyDefaultOrders gives the player a cPlayerInVehicleHotwire task when the car needs hotwiring
// (cVehicle::NeedsHotwiring: a hotwire class, from the vehicle info +0x8E bits 3-4 or Vehicle::SetHotWireMode, and
// parked by a car generator, +0x944 bit 11).
//
// The app (ortho mode 2: a 1024 x 768 space aligned to the right, the extra width on the left) puts the dashboard
// (gGameDir[26] 0x4E at (609, 398), its frame line 0x4F at (606, 390)) in the bottom right. Easy mode
// (Process_EasyMode):
//   0  drag the screwdriver (SS_HotWire_Easy sprite 5) along the line from (624, 720) to the ignition at (752, 647)
//      (a cFatLineStrokeRegion 120 wide, 120 behind and 40 past, with a momentum variable, K 0.8); its glow (sprite 8)
//      fades in while it is still. Within 3 of the ignition: it goes in (sfx 0x2EB), the inserted screwdriver
//      (sprites 6 / 7) and the lock (sprite 10) show.
//   1  30 frames of the zoom towards g_EasyMode_ZoomedIn (the app keeps the rectangle; easy mode does not draw
//      zoomed, its layers are not in cPda's zoom mask)
//   2  turn it a full turn anticlockwise (a cWindingStrokeRegion about (840, 590), radius 0..100, a one-way
//      variable): the arrow (sprite 3) turns with it; a ratchet click (sfx 0x22D) every quarter turn
//   3  turn it back (sprite 2)
//   4  and round again; then the starter (sfx 0x191), the rev counter lights up at night, done.
// The exit button (SS_AlwaysLoaded 93, at (704, 488)) gives up. A timer bar (MiniGameTimerBar) runs once the player
// has hotwired a car of the class before; when it runs out the alarm goes and the attempt fails.
//
// Not ported yet: medium and hard mode (classes 2 and 3), the timer bar, cMinigameCam, the help text (HUDText's
// queue) and the app's opening transition.
#pragma once
#include "stroke.h"
#include "gfx/spriteset.h"
#include <cstdint>
#include <functional>
#include <string>

class HotWiring {
public:
    // cHotWiringApp::Init for a hotwire class (only 1 is ported: false for the others). done(success, quit).
    bool start(const std::string& dataDir, int hotwireClass, bool showTimer, std::function<void(bool success, bool quit)> done);
    void stop();
    bool active() const { return active_; }
    void process(uint32_t frame, float timeOfDay);   // cHotWiringApp::Process, once per 30 Hz frame
    void render(int W, int H) const;
    // the mouse as cTouchScreen's touch: held, released, at window pixels
    void touch(bool down, int x, int y, int W, int H);
    void click(int x, int y, int W, int H);           // a tap (the exit button)
    void quit() { if (active_ && !finished_) finish(false, true); }   // OnExitButtonPress
    int state() const { return state_; }
    std::function<void(int sample, int volume)> playSfx;

private:
    struct Sprite { int index = -1; float x = 0, y = 0; bool shown = true; float alpha = 1; uint16_t rotation = 0; int priority = 0; };
    void toApp(int x, int y, int W, int H, float& ax, float& ay) const;
    void drawSprite(const SpriteSet& set, unsigned tex, int tw, int th, const Sprite& s, int W, int H) const;
    void drawBackground(int texture, float x, float y, int W, int H) const;
    void winding(int sprite);                 // states 2..4: the turn and its arrow
    void ratchet();
    void finish(bool success, bool quit);

    bool active_ = false;
    std::function<void(bool, bool)> done_;
    int state_ = 0;                            // +0x190
    bool touchDown_ = false;
    float touchX_ = 0, touchY_ = 0;
    // assets
    SpriteSet sprites_, always_;
    unsigned sheet_ = 0, alwaysSheet_ = 0;
    int sheetW_ = 1, sheetH_ = 1, alwaysW_ = 1, alwaysH_ = 1;
    int dashboard_ = 0, frameLine_ = 0;
    // sprites
    Sprite screwdriver_, glow_;                // +0x198, +0x1A0
    Sprite inserted_[2];                       // +0x298, +0x299
    Sprite arrows_[2];                         // +0x29A (sprite 3), +0x29B (sprite 2)
    Sprite lock_, revCounter_;                 // +0x29C, +0x29D
    Sprite exit_;                              // cPdaApp +0x124
    float exitAlpha_ = 0;
    // the screwdriver's drag
    StrokeFatLine line_;                       // +0x1A8
    StrokeValue linePos_;                      // +0x270
    StrokeMomentum momentum_{&linePos_};       // +0x250
    float glowMin_ = 0, glowMax_ = 1, glowStep_ = 1.f / 3.f, glowFade_ = 1;   // +0x280 .. +0x294
    // the turn
    StrokeWinding wind_;                       // +0x768
    StrokeValue turn_;                         // +0x810
    StrokeOneWay oneWay_{&turn_};              // +0x7E0
    float arrowMin_ = 0, arrowMax_ = 1, arrowStep_ = 0, arrowFade_ = 1;   // +0x7F0 .. +0x804
    bool arrowIn_ = true;                      // +0x7FC
    int ratchetStep_ = 0;                      // RatchetSfx +0x830
    // the zoom (ZOOM a, b, scale), kept as the original keeps it
    float zoomA_[2] = {374, 218}, zoomB_[2] = {374, 218}, zoomScale_ = 1;
    float zoomTo_[5] = {0, 0, 0, 0, 0}, zoomT_ = 0, zoomStep_ = 0;
    bool finished_ = false;
};
