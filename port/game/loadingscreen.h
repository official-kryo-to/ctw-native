// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#pragma once
#include "gfx/spriteset.h"
#include <string>

// The Android start-up: the legal splash, then the loading app (Gui::cLoadingAppRing) while the desktop host
// initializes. It owns its textures independently of the world/asset cache so it can be shown first.
//
// GameActivity::onActivityReady shows LegalScreens_<language>.png in an ImageView (fit-centred, nothing behind it)
// and hides it 5000 ms later; until then OS_ApplicationTick neither updates nor renders the game. The loading app
// then starts its own frame count: the ring makes a half-turn over eight frames and holds it, and the app is done
// once more than eight frames have passed (cLoadingAppRing::Process).
class LoadingScreen {
public:
    LoadingScreen() = default;
    LoadingScreen(const LoadingScreen&) = delete;
    LoadingScreen& operator=(const LoadingScreen&) = delete;
    ~LoadingScreen() { shutdown(); }
    bool init(const std::string& dataDir);
    void shutdown();
    bool present();   // pumps window events, draws, swaps; false means close requested
    bool finish();    // presents until the splash and the loading app are done; false means close requested
    // the loading app, `seconds` after it started
    void render(int width, int height, double seconds) const;
    void renderLegal(int width, int height) const;   // the legal splash
    static int16_t rotation(unsigned frontendFrames);
    static constexpr double kLegalSeconds = 5.0;      // displaySplashScreen(..., 5000L)
private:
    void draw(int width, int height, bool legal, double seconds) const;
    unsigned background_ = 0, ring_ = 0, legal_ = 0;
    int backgroundWidth_ = 0, backgroundHeight_ = 0, ringWidth_ = 0, ringHeight_ = 0;
    int legalWidth_ = 0, legalHeight_ = 0;
    SpriteSet sprites_;
    double start_ = 0;
    double ringStart() const { return start_ + (legal_ ? kLegalSeconds : 0); }
    friend struct LoadingScreenTestAccess;
};
