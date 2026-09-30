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
    void render(int width, int height, Game& game);
    bool open() const { return open_; }
    int station() const { return selected_; }
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
    bool open_ = false, paused_ = false;
    float carousel_ = 0;
    uint32_t carUid_ = 0;
    void select(int station, Game& game);
    void cycle(int step, Game& game);
    void sync(Game& game);
    void remember();
    void volume(int step);
    void sprite(int id, float x, float y, float size = 1, float alpha = 1) const;
    void save() const;
    void loadSave();
    friend struct RadioTestAccess;
};
