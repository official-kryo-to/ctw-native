// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#pragma once
#include "gfx/spriteset.h"
#include <string>

class Game;

// The on-HUD pause screen; the PDA's System app is a separate screen in the original.
class PauseScreen {
public:
    bool init(const std::string& dataDir);
    void shutdown();
    void opened(double now) { touchReady_ = now + 20.0 / 30.0; }
    void render(Game& game, int width, int height);
    static void resumeCentre(int width, int height, float& x, float& y);
    bool resumeHit(int width, int height, float x, float y) const;
    static void quitCentre(int width, int height, float& x, float& y);
    bool quitHit(int width, int height, float x, float y) const;
private:
    SpriteSet sprites_;
    unsigned texture_ = 0;
    int textureWidth_ = 0, textureHeight_ = 0;
    double touchReady_ = 0;
    std::string quitLabel_;
    void sprite(int index, float x, float y, float scale) const;
    friend struct PauseScreenTestAccess;
};
