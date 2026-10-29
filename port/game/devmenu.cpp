// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "devmenu.h"
#include "game.h"
#include "hud.h"
#include "plugins.h"
#include "os/os.h"
#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {
constexpr uint32_t kPanel = 0x0B0D12E6u, kLine = 0xFFFFFF1Au, kText = 0xE6E8EEFFu, kDim = 0x9AA0ABFFu,
                   kAccent = 0xF2C230FFu, kHot = 0xF2C23038u, kField = 0xFFFFFF14u;

std::string onOff(bool v) { return v ? "on" : "off"; }

// the first mission's places, for jumping about while testing it
struct Place { const char* name; float x, y; };
constexpr Place kPlaces[] = {
    {"Jetty (Huang out of the water)", -307.74f, -40.05f},
    {"The car to steal", -281.78f, -64.5f},
    {"Kenny's restaurant", 325.86f, -57.59f},
    {"Huang's apartment", 364.08f, 25.02f},
    {"Airport", 1762.45f, -66.53f},
};

const char* const kSequences[] = {
    "CSS_KEN_A01_NewIntro1.seq", "CSS_KEN_A01_PlaneLand.seq", "CSS_KEN_A01_NewIntro2.seq", "CSS_Ken_A01_Splash.seq",
    "CSS_KEN_A01_LeaveWater.seq", "CSS_KEN_A01_SCENE_02.seq", "CSS_Ken_A01_Safehouse.seq",
};
}

void DevMenu::toggle() {
    open_ = !open_;
    Host_PopText();   // (whatever was typed before it opened is not the field's)
}

void DevMenu::build(Game& g) {
    rows_.clear();
    auto action = [this](std::string label, std::function<void()> run) {
        Row r;
        r.label = std::move(label);
        r.run = std::move(run);
        rows_.push_back(std::move(r));
    };
    auto toggleRow = [this](std::string label, bool* flag) {
        Row r;
        r.label = std::move(label);
        r.value = [flag] { return onOff(*flag); };
        r.change = [flag](int) { *flag = !*flag; };
        r.run = [flag] { *flag = !*flag; };
        rows_.push_back(std::move(r));
    };
    switch (page_) {
    case 0:   // Overlay
        toggleRow("Debug information", &g.showDebug);
        toggleRow("Collision and roads in the world", &g.showCollision);
        break;
    case 1: {   // Missions
        Row r;
        r.label = "Running";
        r.value = [&g] { return std::string(g.mission ? "a mission" : "none"); };
        rows_.push_back(std::move(r));
        action("Start: Arrival in Liberty (kena01)", [this, &g] {
            g.stopSequence();
            g.mission.reset();
            status_ = g.startMission("kena01") ? "Started kena01." : "kena01 could not start.";
        });
        action("Stop the mission", [this, &g] {
            g.mission.reset();
            g.stopSequence();
            g.cutscene = g.scriptCutscene = false;
            g.seqCamera.active = false;
            g.blips.erase(std::remove_if(g.blips.begin(), g.blips.end(), [](const RadarBlip& b) { return b.id < Game::kPluginBlips; }),
                          g.blips.end());
            g.objective.frames = 0;
            status_ = "Mission stopped.";
        });
        break;
    }
    case 2: {   // Cutscenes
        Row field;
        field.label = "Sequence";
        field.value = [this] { return sequence_; };
        field.field = true;
        field.run = [this, &g] {
            status_ = g.startSequence(sequence_) ? "Playing " + sequence_ : sequence_ + " was not found.";
        };
        rows_.push_back(std::move(field));
        for (const char* name : kSequences)
            action(std::string("Play ") + name, [this, &g, name] {
                sequence_ = name;
                status_ = g.startSequence(name) ? std::string("Playing ") + name : std::string(name) + " was not found.";
            });
        action("Stop the cutscene", [this, &g] { g.stopSequence(); status_ = "Stopped."; });
        break;
    }
    case 3: {   // Minigames
        Row style;
        style.label = "Sinking car: vehicle style";
        style.value = [this] {
            static const char* const names[4] = {"0 (rear window, seats)", "1 (rear window, headrests)", "2 (windscreen)",
                                                 "3 (small windscreen)"};
            return std::string(names[carStyle_]);
        };
        style.change = [this](int d) { carStyle_ = (carStyle_ + d + 4) % 4; };
        rows_.push_back(std::move(style));
        action("Start the sinking car", [this, &g] {
            open_ = false;
            status_ = g.startSinkingCar(carStyle_, [&g](bool) { g.screenFade.in(15); }) ? "Sinking car started."
                                                                                       : "The sinking car could not load.";
        });
        action("Stop the minigame", [this, &g] { g.sinkingCar.stop(); g.screenFade.in(1); status_ = "Stopped."; });
        break;
    }
    case 4: {   // World
        const CtwHostApi& api = Plugins_HostApi();
        Row t;
        t.label = "Time of day";
        t.value = [&api] {
            char b[16];
            const float h = api.get_time_of_day();
            std::snprintf(b, sizeof b, "%02d:%02d", (int)h, (int)((h - (int)h) * 60.f));
            return std::string(b);
        };
        t.change = [&api](int d) { api.set_time_of_day(std::fmod(api.get_time_of_day() + d + 24.f, 24.f)); };
        rows_.push_back(std::move(t));
        Row clock;
        clock.label = "Clock running";
        clock.value = [&api] { return onOff(api.get_clock_running() != 0); };
        clock.change = [&api](int) { api.set_clock_running(!api.get_clock_running()); };
        clock.run = [&api] { api.set_clock_running(!api.get_clock_running()); };
        rows_.push_back(std::move(clock));
        Row w;
        w.label = "Weather";
        w.value = [&api] { return std::to_string(api.get_weather()); };
        w.change = [&api](int d) { api.set_weather((api.get_weather() + d + 8) % 8); };
        rows_.push_back(std::move(w));
        Row sp;
        sp.label = "Game speed";
        sp.value = [&g] {
            char b[16];
            std::snprintf(b, sizeof b, "x%.2f", g.gameSpeed);
            return std::string(b);
        };
        sp.change = [&g](int d) {
            static const float steps[] = {0.1f, 0.25f, 0.5f, 1.f, 2.f, 4.f};
            int i = 0;
            while (i < 5 && steps[i] < g.gameSpeed - 0.01f) ++i;
            g.gameSpeed = steps[std::clamp(i + d, 0, 5)];
        };
        rows_.push_back(std::move(sp));
        break;
    }
    case 5: {   // Player
        Row wanted;
        wanted.label = "Wanted level";
        wanted.value = [&g] { return std::to_string(g.wanted.level()); };
        wanted.change = [&g](int d) { g.wanted.setLevel(std::clamp(g.wanted.level() + d, 0, 6), false, true); };
        rows_.push_back(std::move(wanted));
        action("Full health and armour", [this, &g] {
            g.player.health = 127;
            g.player.setArmour(127);
            status_ = "Healed.";
        });
        for (const Place& p : kPlaces)
            action(std::string("Go to ") + p.name, [this, &g, p] {
                if (g.playerCar >= 0) { status_ = "Get out of the car first."; return; }
                const float at[3] = {p.x, p.y, 0.f};
                Plugins_HostApi().set_player_position(at);
                g.player.placeOnGround(&g.collision);
                g.world.loadAllNow(p.x, p.y);
                status_ = std::string("Moved to ") + p.name + ".";
            });
        break;
    }
    }
    selected_ = std::clamp(selected_, 0, std::max(0, (int)rows_.size() - 1));
}

void DevMenu::move(int step) {
    if (rows_.empty()) return;
    selected_ = (selected_ + step + (int)rows_.size()) % (int)rows_.size();
}

void DevMenu::activate(int row) {
    if (row < 0 || row >= (int)rows_.size()) return;
    Row& r = rows_[row];
    if (r.run) r.run();
    else if (r.change) r.change(1);
}

bool DevMenu::key(Game& g, int k) {
    if (!open_) return false;
    build(g);
    const Uint8* ks = SDL_GetKeyboardState(nullptr);
    const bool shift = ks[SDL_SCANCODE_LSHIFT] || ks[SDL_SCANCODE_RSHIFT];
    const bool typing = !rows_.empty() && rows_[selected_].field;
    switch (k) {
    case SDL_SCANCODE_F3:
    case SDL_SCANCODE_ESCAPE: open_ = false; break;
    case SDL_SCANCODE_UP: move(-1); break;
    case SDL_SCANCODE_DOWN: move(1); break;
    case SDL_SCANCODE_TAB:
        page_ = (page_ + (shift ? -1 : 1) + (int)pages_.size()) % (int)pages_.size();
        selected_ = scroll_ = 0;
        break;
    case SDL_SCANCODE_LEFT:
    case SDL_SCANCODE_RIGHT:
        if (!typing && !rows_.empty() && rows_[selected_].change) rows_[selected_].change(k == SDL_SCANCODE_LEFT ? -1 : 1);
        break;
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_KP_ENTER: activate(selected_); break;
    default: break;
    }
    return true;   // nothing reaches the game while the menu is open
}

void DevMenu::render(Game& g, int W, int H) {
    if (!open_ || W <= 0 || H <= 0) return;
    build(g);
    const float s = std::clamp(H / 900.f, 0.8f, 1.4f), lh = Hud_LineHeight(s), rowH = lh + 10.f * s;
    const float w = std::min(W - 32.f, 620.f * s), x = W - w - 16.f, y = 16.f;
    const float tabsH = lh + 14.f * s, titleH = lh + 16.f * s;
    const int visible = std::max(3, (int)((H - 2 * y - titleH - tabsH - 2 * lh - 30.f * s) / rowH));
    const float h = titleH + tabsH + std::min<int>(visible, (int)rows_.size()) * rowH + 2 * lh + 30.f * s;
    Plugins_ClaimMouseRect(x, y, w, h);
    int mx = 0, my = 0;
    Host_GetMouse(&mx, &my);
    const CtwHostApi& api = Plugins_HostApi();
    const bool clicked = api.mouse_clicked(0) != 0;
    const bool inside = mx >= x && my >= y && mx < x + w && my < y + h;
    // the field takes what is typed while it is the selected row ('\b' is a backspace)
    if (!rows_.empty() && rows_[selected_].field) {
        for (const char* t = api.text_input(); t && *t; ++t) {
            if (*t == '\b') { if (!sequence_.empty()) sequence_.pop_back(); }
            else if ((unsigned char)*t >= 0x20 && *t != '\t' && sequence_.size() < 64) sequence_ += *t;
        }
    }
    if (inside) {   // the wheel scrolls the rows
        const float wheel = api.mouse_wheel();
        if (wheel != 0.f) { scroll_ -= (int)wheel; api.consume_mouse_wheel(); }
    }
    scroll_ = std::clamp(scroll_, 0, std::max(0, (int)rows_.size() - visible));
    if (selected_ < scroll_) scroll_ = selected_;
    if (selected_ >= scroll_ + visible) scroll_ = selected_ - visible + 1;

    Hud_Begin(W, H);
    Hud_Rect(x, y, w, h, kPanel);
    Hud_Rect(x, y, w, 2.f, kAccent);
    Hud_Text(x + 14.f * s, y + 8.f * s, s * 1.1f, kText, "Developer menu");
    const std::string hint = "F3 closes";
    Hud_Text(x + w - Hud_TextWidth(hint, s * 0.85f) - 14.f * s, y + 10.f * s, s * 0.85f, kDim, hint);
    // the tabs
    float tx = x + 10.f * s;
    const float ty = y + titleH;
    for (int i = 0; i < (int)pages_.size(); ++i) {
        const float tw = Hud_TextWidth(pages_[i], s * 0.9f) + 16.f * s;
        const bool hot = mx >= tx && my >= ty && mx < tx + tw && my < ty + tabsH - 4.f;
        if (i == page_) Hud_Rect(tx, ty, tw, tabsH - 4.f, kHot);
        if (i == page_) Hud_Rect(tx, ty + tabsH - 6.f, tw, 2.f, kAccent);
        Hud_Text(tx + 8.f * s, ty + 5.f * s, s * 0.9f, i == page_ || hot ? kText : kDim, pages_[i]);
        if (hot && clicked && i != page_) { page_ = i; selected_ = scroll_ = 0; }
        tx += tw + 4.f * s;
    }
    Hud_Rect(x + 10.f * s, ty + tabsH - 3.f, w - 20.f * s, 1.f, kLine);
    // the rows
    float ry = ty + tabsH + 4.f * s;
    for (int i = scroll_; i < (int)rows_.size() && i < scroll_ + visible; ++i, ry += rowH) {
        Row& r = rows_[i];
        const bool hot = mx >= x && mx < x + w && my >= ry && my < ry + rowH;
        if (hot && (mx != lastMouse_[0] || my != lastMouse_[1])) selected_ = i;   // hovering (once it moves)
        if (i == selected_) Hud_Rect(x + 6.f * s, ry, w - 12.f * s, rowH - 2.f, kHot);
        const bool action = !r.value;
        Hud_Text(x + 16.f * s, ry + 5.f * s, s, action ? kText : kDim, r.label);
        if (r.value) {
            std::string v = r.value();
            if (r.field) {
                const float fw = w * 0.55f;
                Hud_Rect(x + w - fw - 14.f * s, ry + 2.f * s, fw, rowH - 6.f * s, kField);
                if (i == selected_ && (g.frame / 15) % 2 == 0) v += "_";
                Hud_Text(x + w - fw - 8.f * s, ry + 5.f * s, s * 0.9f, kText, v);
            } else {
                if (r.change) v = "< " + v + " >";
                Hud_Text(x + w - Hud_TextWidth(v, s) - 16.f * s, ry + 5.f * s, s, kText, v);
            }
        }
        if (hot && clicked) {
            selected_ = i;
            if (r.field) { /* a click selects the field for typing */ }
            else if (r.change && !r.run) r.change(mx < x + w * 0.5f ? -1 : 1);
            else activate(i);
        }
    }
    lastMouse_[0] = mx;
    lastMouse_[1] = my;
    // the footer: the last result and the keys
    const float fy = y + h - 2 * lh - 14.f * s;
    Hud_Rect(x + 10.f * s, fy - 4.f * s, w - 20.f * s, 1.f, kLine);
    Hud_Text(x + 14.f * s, fy, s * 0.9f, kAccent, status_.empty() ? "Ready." : status_);
    Hud_Text(x + 14.f * s, fy + lh, s * 0.8f, kDim, "Up/Down: row   Left/Right: value   Enter: do   Tab: page   Esc: close");
    Hud_End();
}
