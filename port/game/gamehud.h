// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#pragma once
#include "radar.h"
#include "gfx/spriteset.h"
#include <string>

class Game;
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
    mutable float starScale_[6] = {1, 1, 1, 1, 1, 1};   // cSprite scale: new stars pop in from 2 and shrink to 1
    mutable int starLevel_ = 0;                          // cMiniHud +0x6C: the star display level last seen
    mutable uint32_t starFrame_ = 0;                     // the game frame the stars last stepped
    mutable int flashCount_ = 0;                         // cSprite +0x1B (flashing speed 5: 7 frames a phase)
    mutable bool flashOn_ = true, wasFlashing_ = false;
    RadarMap map_;
    SpriteSet sprites_;
    unsigned texture_ = 0;
    int textureWidth_ = 0, textureHeight_ = 0;
    bool rotation_ = false; // native saved preference defaults to north-up
    friend struct GameplayHudTestAccess;
};
