// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "weather.h"
#include "game.h"
#include "particles.h"
#include "random.h"
#include "sound.h"
#include <algorithm>

bool Weather::init(const std::string& dataDir) {
    ok_ = tables_.load(dataDir + "/weather_tables.bin");
    return ok_;
}

void Weather::reset(Game& g, int weather) {   // cTimeCycle::Reset
    TimeCycle& tc = g.world.timeCycle();
    forced = 8;
    state_ = 0; start_ = 0; lastEnd_ = 0; thunderDelay_ = 0;
    hourToggle_ = false; flashing_ = false;
    tc.setExtraColours(false);
    tc.setWeathers(weather, 0);
    tc.brightness = 4096;
    delay_ = Rand32Critical(0x2E4) + 10;
    tc.evaluate();
}

void Weather::force(Game& g, int weather) {   // ForceWeather(w, true); 8 releases it at the next hour
    forced = weather;
    if (weather >= 0 && weather < 8) {
        TimeCycle& tc = g.world.timeCycle();
        tc.setWeather(weather);
        tc.evaluate();
    }
}

int Weather::rollNext(int current) const {   // SetNewWeatherToTransitionTo
    if (forced != 8) return forced;
    const uint8_t* chance = tables_.chance[current & 7];
    int r = (int)Rand32Critical(100);
    for (int w = 0; w < 6; ++w) {
        r -= chance[w];
        if (r < 1) return w;
    }
    r -= chance[6];
    if (r < 1) return 6;
    return r > chance[7] ? 6 : 7;
}

void Weather::hourChanged(Game& g) {   // cTimeCycle::Process, the hour changed
    TimeCycle& tc = g.world.timeCycle();
    if (!tc.extraColours()) {
        const int current = tc.nextWeather();
        tc.setWeathers(current, ok_ ? rollNext(current) : current);
    }
    tc.evaluate();
}

void Weather::tick(Game& g) {
    TimeCycle& tc = g.world.timeCycle();
    if (rain) {   // UpdateRainDrops; the emitter is attached to the player
        int32_t at[3];
        g.focus(at);
        rain->setPos(at);
        rain->tint = tc.colour(13);
        const uint32_t value = (uint32_t)tc.value(28);
        if (value >= 0x800) rain->addDrops(std::min(value >> 8, 254u) >> 5, g.cameraYaw());
    }
    if (ok_) lightning(g);
}

void Weather::lightning(Game& g) {   // cTimeCycle::UpdateLightning
    TimeCycle& tc = g.world.timeCycle();
    const uint32_t frame = g.frame;
    flashing_ = false;
    if (state_ <= 1) {
        if (state_ == 1) { state_ = 2; return; }
        if (tc.weather() != 5 || frame <= lastEnd_ + delay_) return;
        start_ = frame - 30;
        state_ = 1;
        flashes_ = (uint8_t)(Rand16Critical(2) + 2);
        for (int i = 0; i < flashes_; ++i) {
            bool clear;
            do {   // at least 16 frames from every earlier flash
                flash_[i] = Rand32Critical(0x5A) + start_ + 40;
                clear = true;
                for (int j = i - 1; j >= 0 && clear; --j)
                    clear = flash_[i] < flash_[j] - 15 || flash_[i] > flash_[j] + 15;
            } while (!clear);
            bool distinct;
            do {   // a thunder sound of its own
                sound_[i] = (uint8_t)Rand16Critical(5);
                distinct = true;
                for (int j = i - 1; j >= 0 && distinct; --j) distinct = sound_[j] != sound_[i];
            } while (!distinct);
        }
        thunderDelay_ = Rand32Critical(8) + 2;
        return;
    }
    if (state_ == 3) {
        const int32_t next = tc.brightness + 13;
        const bool done = tc.brightness >= 4082;
        tc.brightness = next;
        if (done) { lastEnd_ = frame; tc.brightness = 4096; state_ = 0; }
        return;
    }
    if (state_ != 2) return;
    if (flashes_) {
        for (int i = 0; i < flashes_; ++i) {
            if (frame >= flash_[i] && frame <= flash_[i] + 3) flashing_ = true;   // (the PDA and fades are not ported)
            if (flashing_) tc.brightness = 3686;
            if (frame == thunderDelay_ + flash_[i]) {
                const int volume = std::max(50, 129 - (int)thunderDelay_);
                TheSound().playResident((int)tables_.thunder[sound_[i]], volume);
            }
        }
    }
    if (flashing_) {
        if (tc.weather() != 7) {   // the flash colours: weather 7 at 13:00 or 14:00
            savedWeather_ = tc.weather();
            savedNext_ = tc.nextWeather();
            tc.setExtraColours(true, hourToggle_ ? 0xD000u : 0xE000u);
            tc.setWeather(7);
            tc.evaluate();
            hourToggle_ = !hourToggle_;
        }
    } else if (tc.weather() == 7) {   // ClearExtraColours
        tc.setExtraColours(false);
        tc.setWeathers(savedWeather_, savedNext_);
        tc.evaluate();
    }
    if (frame >= start_ + 140) state_ = 3;
    if (tc.brightness <= 3685) tc.brightness += 13;
}
