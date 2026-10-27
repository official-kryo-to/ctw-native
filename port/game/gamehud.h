// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#pragma once
#include "radar.h"
#include "gfx/spriteset.h"
#include <string>

class Game;

// The HUD's money: cHudApp::UpdateCash and its Gui::cCashMeter, stepped once per game frame. While the money
// (cPlayer +0x112C) differs from the amount shown (+0x1130), the green "$" amount (GlobalText 0x100, font 5, at
// x 0x118) slides down into view, two pixels a frame from 0x2A up; once it is in, the shown amount counts towards
// the money with a growing step (a 500th of the difference more each frame, a 100th when it falls or the
// difference is under 5), with HUD sfx 0x193 while it changes, and "+$" / "-$" (0xEF / 0xF0, font 3) shows what is
// left eight pixels to the right of it. When they agree it slides up again. As in the original the "$" text is made
// before each count step, so it can end one step short of the money.
class CashMeter {
public:
    bool step(int32_t money, bool titleShown = false);   // one frame; true when the counting sound plays
    int32_t shown() const { return shown_; }
    int32_t text() const { return text_; }       // the amount in the "$" text
    int slide() const { return slide_; }         // cHudApp +0x251: 0 in view .. 0x2A out of view
    bool delta() const { return delta_; }        // the meter's "+$" / "-$" shows money - shown()
    static int32_t widthTemplate(int32_t shown); // the amount whose width places the meter (same digit count)
private:
    int32_t shown_ = 0, text_ = 0;
    int slide_ = 0x2A;
    bool delta_ = false;
    int32_t target_ = 0, start_ = 0, progress_ = 0;   // cCashMeter +8, +0xC, +0x10
};

class GameplayHud {
public:
    GameplayHud() = default;
    GameplayHud(const GameplayHud&) = delete;
    GameplayHud& operator=(const GameplayHud&) = delete;
    bool init(const std::string& dataDir);
    void shutdown();
    void render(const Game& game, int width, int height) const;
    void setRotation(bool enabled) { rotation_ = enabled; }
    bool rotation() const { return rotation_; }
private:
    void sprite(int index, float x, float y, float scale, float angle = 0.f, float alpha = 1.f) const;
    // cMiniHud::UpdateWantedStars / ProcessWantedRating: six SS_AlwaysLoaded stars (124 lit, 125 on probation)
    void renderWantedStars(const Game& game, float k, int width, int height) const;
    void renderCash(const Game& game, float k) const;
    mutable CashMeter cash_;
    mutable uint32_t cashFrame_ = 0;                     // the game frame the money last stepped
    mutable float starScale_[6] = {1, 1, 1, 1, 1, 1};   // cSprite scale: new stars pop in from 2 and shrink to 1
    mutable int starLevel_ = 0;                          // cMiniHud +0x6C: the star display level last seen
    mutable uint32_t starFrame_ = 0;                     // the game frame the stars last stepped
    mutable int flashCount_ = 0;                         // cSprite +0x1B (flashing speed 5: 7 frames a phase)
    mutable bool flashOn_ = true, wasFlashing_ = false;
    mutable RadarFlash flash_;
    mutable uint32_t flashFrame_ = 0;                    // the game frame the radar's flashing last stepped
    RadarMap map_;
    SpriteSet sprites_;
    unsigned texture_ = 0;
    int textureWidth_ = 0, textureHeight_ = 0;
    bool rotation_ = false; // native saved preference defaults to north-up
    friend struct GameplayHudTestAccess;
};
