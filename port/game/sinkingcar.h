// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// Gui::cSinkingCarApp and Gui::cSinkingCar, the PDA minigame of a car sinking with the player inside: tap the window
// until it breaks before the air runs out.
//
// The screen: cPdaApp ortho mode 4 (+0x128) is a plain 1024 x 768 ortho stretched over the display (y down for
// windows and sprites). cPda::Render draws layers 6 down to 0, each its backgrounds and then its sprites:
//   layer 5  glass shards (sprite 0 at 0.3 scale)            layer 2  the interior (gGameDir[26] 0xAB..0xAE by style),
//   layer 4  the cracks (sprites 1 and 2)                              a 1024 x 512 texture with its top left at the
//   layer 3  bubbles (sprite 0)                                        car's offset, then the window's outline
//   layer 1  the 0xAF background at (0, 426)                  layer 0  (sprites 3..6, flashing until the first tap)
// so cracks, bubbles and shards are only seen through the interior's transparent window. The Android build draws no
// sea there: Seabg_tiles.bin is a DS tile background, which has no texture (cBg::Render skips texture 0xFFFF); the 3D
// world shows through instead, from the cMinigameCam the app switches to.
//
// The car's offset starts at (-40, 388), drifts about it, and shakes for 3 frames after a hit. A hit inside the window
// (two rectangles a style, from the offset) makes a crack where it landed; 4 to 7 are needed. The air: +0x190 rises by
// 9 a frame; at 0xF0B the warning (sfx 0x22B), past 0x1000 the player drowns (a 20-frame fade, then dead). The heart
// beats and the breathing quickens as it rises (timed in milliseconds of real time, as OS_TimeAccurate). A smashed
// window fades out in two frames, a splash (explosion 8) goes off at the car, and 20 frames later the player is out.
//
// Not ported yet: cMinigameCam (the game's camera stays), the help text (HUDImpl::DisplayHelp(0x19C) goes through
// HUDText's cHudTextQueue and cDynamicHudLayout, which the port does not have) and the app's opening transition.
#pragma once
#include "gfx/spriteset.h"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class SinkingCar {
public:
    // cSinkingCarApp::Init for an eSinkingVehicleStyle (0..3: the player's vehicle info +0x8E >> 13, 0 on foot; 5, the
    // tank, is not ported). done(true) when the player is out (SetFinished after a smashed window), done(false) when
    // they drowned.
    bool start(const std::string& dataDir, int style, std::function<void(bool escaped)> done);
    void stop();
    bool active() const { return active_; }
    void process(uint32_t frame);                 // cSinkingCarApp::Process, once per 30 Hz frame
    void press(float x, float y, int W, int H);   // OnPress: a tap at window pixels
    void shoulder(int which);                     // ProcessController: L (0) and R (1) pressed in turn
    void render(int W, int H) const;
    int state() const { return state_; }          // +0x19C: 2 playing, 3/4 escaping, 5 drowning
    int cracks() const { return hits_; }
    int needed() const { return needed_; }
    std::function<void(int sample, int volume)> playSfx;   // the game's HUD sound channel
    std::function<void(bool toBlack, int frames)> fade;    // the player's cScreenFade
    std::function<bool()> fading;
    std::function<void()> splash;                          // Explosion::AddExplosion(type 8) at the player's car

private:
    struct Bubble {   // Gui::cSinkingCarBubble, in 20.12 like the original
        bool on = false;
        int32_t base[2] = {0, 0}, off[2] = {0, 0}, vel[2] = {0, 0};
        int16_t scale = 0x1000;                    // +0x18
        int16_t drawScale = 0x1000;                // its sprite's scale (set before the shrink)
    };
    struct BubbleArea {   // Gui::cBubbleArea
        int32_t pos[2] = {0, 0}, size[2] = {0, 0};
        int interval = 8, counter = 0;
        std::vector<Bubble> bubbles;
        void init(int x, int y, int w, int h, int count, int every);
    };
    struct Shard { bool on = false; int pos[2] = {0, 0}; int32_t vel[2] = {0, 0}; int life = 0; };
    void hitScreen(int x, int y);
    void processBubbles(BubbleArea& a);
    void launch(Bubble& b, int x, int y);
    void sounds();
    void sprite(int index, float x, float y, float scale, uint16_t rotation, float alpha, int W, int H) const;
    void background(int texture, float x, float y, int W, int H) const;

    bool active_ = false;
    std::function<void(bool)> done_;
    int style_ = 0, state_ = 0, counter_ = 0;
    int air_ = 0;                                  // +0x190
    bool warned_ = false;                          // +0x1E4
    // cSinkingCar
    int32_t off_[2] = {0, 0}, drift_[2] = {0, 0};  // +0xF8, +0xF0 (20.12)
    int32_t shake_[2] = {0, 0};                    // +0x100
    int shakeLeft_ = 0;                            // +0x108
    int hits_ = 0, needed_ = 4, tapsLeft_ = 1, bigCracks_ = 0;   // +0x109, +0x10A, +0x10B, +0x10C
    bool outline_ = true;                          // the outline sprite shown (+0xE4 >= 1)
    float flashPhase_ = 1.f, flashAlpha_ = 1.f;    // the outline cSprite's +0x6C and +0x64 (RefreshSprite)
    int lastShoulder_ = -1;                        // +0x12C
    struct Crack { int sprite; int pos[2]; uint16_t rotation; };   // +0x10.., +0x18..: relative to the offset
    std::vector<Crack> crackList_;
    Shard shards_[8];                              // +0x90..
    BubbleArea leaks_[2], ambient_[3];             // +0x50 / +0x70; the app's +0x1A8..+0x1B8
    uint32_t nextBeat_ = 0, nextBreath_ = 0;       // +0x110, +0x114 (ms)
    bool exhale_ = false;                          // +0x118
    bool breathing_ = true;                        // +0x128
    // assets
    SpriteSet sprites_;
    unsigned sheet_ = 0;
    int sheetW_ = 1, sheetH_ = 1;
    int interior_ = 0, frameLine_ = 0;             // texture ids (0xAB..0xAE, 0xAF)
};
