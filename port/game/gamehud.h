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
private:
    void sprite(int index, float x, float y, float scale, float angle = 0.f) const;
    RadarMap map_;
    SpriteSet sprites_;
    unsigned texture_ = 0;
    int textureWidth_ = 0, textureHeight_ = 0;
    friend struct GameplayHudTestAccess;
};
