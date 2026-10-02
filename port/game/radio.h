// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#pragma once
#include "lookups.h"
#include "gfx/spriteset.h"
#include <string>
#include <vector>

class Game;
class Radio {
public:
    bool init(const std::string& dataDir);
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
    int selected_ = 0, playing_ = -1, volume_ = 8;
    uint32_t carUid_ = 0;
    // cWavStream ambience: the city / rain / airport loop on the music stream when no station plays
    std::string dir_;
    int ambience_ = -1;          // stream table index playing, -1 none
    int ambienceLevel_ = 0;      // cSoundStream volume, 0..127, fading towards the target
    void ambience(Game& game);
    // radio app state (cRadioApp +0x183C scroll position, +0x1848 icon offsets, +0x1860 volume icon alpha)
    bool app_ = false;
    float appScroll_ = 0.f;
    double appLast_ = 0.0, volumeShownAt_ = -10.0;
    std::vector<int> appOrder_;           // available stations, left to right
    std::vector<float> appOffset_;        // InitScrolling: x of each icon, width + 40 apart
    float appWidth_ = 0.f;
    void appStation(int step, Game& game);
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
