// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#pragma once
#include "gfx/spriteset.h"
#include <string>

// Android's loading app artwork, used while the desktop host initializes. It owns
// its textures independently of the world/asset cache so it can be shown first.
class LoadingScreen {
public:
    LoadingScreen() = default;
    LoadingScreen(const LoadingScreen&) = delete;
    LoadingScreen& operator=(const LoadingScreen&) = delete;
    ~LoadingScreen() { shutdown(); }
    bool init(const std::string& dataDir);
    void shutdown();
    bool present();   // pumps window events, draws, swaps; false means close requested
    void render(int width, int height, double seconds) const;
    static int16_t rotation(unsigned frontendFrames);
private:
    unsigned background_ = 0, ring_ = 0;
    int backgroundWidth_ = 0, backgroundHeight_ = 0, ringWidth_ = 0, ringHeight_ = 0;
    SpriteSet sprites_;
    double start_ = 0;
    friend struct LoadingScreenTestAccess;
};
