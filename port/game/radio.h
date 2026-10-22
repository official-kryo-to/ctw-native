// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#pragma once
#include "lookups.h"
#include "gfx/spriteset.h"
#include <functional>
#include <string>
#include <vector>

class Game;
class GameFs;
class Radio {
public:
    bool init(const std::string& dataDir, const std::function<bool()>& loadingFrame = {});
    void shutdown();
    void update(Game& game);
    bool key(int scancode, Game& game);
    int station() const { return selected_; }
    int stationCount() const { return (int)stations_.size(); }
    const char* stationName(int index) const;
    bool stationAvailable(int index) const;
    bool tune(int index, Game& game);
    int volumeLevel() const { return volume_; }
    bool setVolume(int level);
    // The radio app (Gui::cRadioApp): full screen, the game paused, Tab in a car opens and closes it.
    bool appOpen() const { return app_; }
    void openApp(bool open, Game& game);
    void renderApp(int width, int height, Game& game);
    bool appKey(int scancode, Game& game);
private:
    struct Station {
        int icon = 14, stream = -1;
        std::string name, label, path;
        double duration = 0, position = 0, listened = 0;
        bool available = false;
    };
    RadioTables tables_;
    SpriteSet sprites_;
    std::vector<Station> stations_;
    unsigned texture_ = 0;
    int textureW_ = 0, textureH_ = 0;
    // the PDA's own pieces: the heading bar (SS_PdaSprites 13) and the task bar's back button (SS_AlwaysLoaded 96)
    struct PdaSprite { unsigned texture = 0; SpriteDef def{}; float u0 = 0, v0 = 0, u1 = 0, v1 = 0; };
    PdaSprite headingBar_, backButton_;
    bool loadPdaSprite(GameFs& fs, const std::string& dir, const char* bin, const char* png, int index, PdaSprite& out);
    static void drawPdaSprite(const PdaSprite& s, float x, float y);
    int selected_ = 0, playing_ = -1, volume_ = 8;
    uint32_t carUid_ = 0;
    // cWavStream ambience: the city / rain / airport loop on the music stream when no station plays
    std::string dir_;
    int ambience_ = -1;          // stream table index playing, -1 none
    int ambienceLevel_ = 0;      // cSoundStream volume, 0..127, fading towards the target
    void ambience(Game& game);
    // radio app state (cRadioApp +0x183C scroll position, +0x1848 icon offsets, +0x1860 volume icon alpha)
    bool app_ = false;
    bool appTuning_ = false;
    int appScroll_ = 0;                   // +0x183C (pixels)
    int appDir_ = 1;                      // +0x185C: the way the carousel travels to the station
    float appFrames_ = 0.f;               // display time not yet spent on 30 Hz carousel steps
    double appLast_ = 0.0, volumeShownAt_ = -10.0;
    double listenClock_ = 0.0;
    bool pointerDown_ = false, pointerSwiped_ = false;
    float pointerX_ = 0.f, pointerY_ = 0.f;
    std::vector<int> appOrder_;           // available stations, left to right
    std::vector<int> appOffset_;          // +0x1848 (Init): x of each icon, width + 40 apart
    std::vector<int> appIconW_;           // +0x1850: the icons' widths
    int appWidth_ = 0;                    // +0x1844
    std::string appHeading_;              // cPdaApp::AddHeading(GlobalText 0x3F)
    void appStation(int step, Game& game);
    void advanceCarousel(Game& game);     // one ProcessStationIcons step
    void appPointer(float x, float y, bool down, bool pressed, Game& game);
    void appClick(float x, float y, Game& game);
    void countListening();
    int favoriteStars(int station) const;
    float volumeAlpha(double now) const;
    void select(int station, Game& game);
    void cycle(int step, Game& game);
    void sync(Game& game);
    void adoptCar(Game& game);
    void remember();
    void volume(int step);
    void sprite(int id, float x, float y, float size = 1, float alpha = 1) const;
    void save() const;
    void loadSave();
    friend struct RadioTestAccess;
};
