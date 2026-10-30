// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// F3: the developer menu (a PC addition, not in the original). Pages of actions for testing: the debug overlay,
// missions, cutscenes (any CSS_*.seq by name), minigames, the world (time, weather, speed) and the player.
//
// Controls: Up / Down pick a row, Left / Right change its value, Enter does it, Tab / Shift+Tab or a click on a tab
// changes the page, the mouse hovers, clicks and scrolls; F3 or Esc closes it. While it is open the game keeps
// running but does not read the keyboard, and a click on the menu is not gunfire (it claims its rectangle).
#pragma once
#include <functional>
#include <string>
#include <vector>

class Game;

class DevMenu {
public:
    bool open() const { return open_; }
    void toggle();
    bool key(Game& game, int scancode);      // true: used (all keys are while it is open)
    void render(Game& game, int width, int height);

private:
    struct Row {
        std::string label;
        std::function<std::string()> value;  // shown on the right (empty: an action)
        std::function<void(int)> change;     // Left / Right (-1 / +1)
        std::function<void()> run;           // Enter or a click
        bool field = false;                  // the text field (typed into while selected)
    };
    void build(Game& game);
    void move(int step);
    void activate(int row);
    std::vector<std::string> pages_ = {"Overlay", "Missions", "Cutscenes", "Minigames", "World", "Player"};
    std::vector<Row> rows_;
    int page_ = 0, selected_ = 0, scroll_ = 0;
    bool open_ = false;
    std::string sequence_ = "CSS_KEN_A01_SCENE_02.seq";   // the cutscene field
    bool timerRun_ = false;                               // hotwiring with the timer bar
    int carStyle_ = 0;                                    // eSinkingVehicleStyle for the minigame
    std::string status_;                                  // the last action's result
    int lastMouse_[2] = {-1, -1};
};
