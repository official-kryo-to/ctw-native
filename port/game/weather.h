// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// Weather: the parts of cTimeCycle::Process / Reset / SetNewWeatherToTransitionTo / UpdateLightning that change the
// weather, and cTimeCycle::UpdateRainDrops (the colours themselves are TimeCycle).
//
// Every hour the weather becomes the one it was changing to, and (unless forced) the next one is rolled from
// msWeatherChanceTable[current] (weather_tables.bin): Rand32Critical(100) minus each percentage in turn, the first
// that brings it below 1 wins, 6 when none does (7 after the seventh entry). Weathers 0..6; 5 is the storm, 7 the
// lightning flash colours.
//
// Rain: while the time cycle's rain value (slot 28) is at least 0x800, AddRaindrops(min(value >> 8, 254) >> 5) drops
// into the player's rain emitter every frame.
//
// Lightning (state at cTimeCycle +0x1DD0..): in the storm, once the frame counter passes the last strike plus the
// delay rolled at Reset (Rand32Critical(740) + 10), a strike starts 30 frames back: 2 or 3 flashes (Rand16Critical(2)
// + 2) at Rand32Critical(90) + start + 40, at least 16 frames apart, each with a different thunder sound
// (Rand16Critical(5)); the thunder follows each flash by Rand32Critical(8) + 2 frames, at volume max(50, 129 - delay).
// During a flash (4 frames) the brightness drops to 3686 (ColourLightning moves the sun light 10% towards white) and
// the colours switch to weather 7 at 13:00 or 14:00 (alternating). The strike ends 140 frames after it started; the
// brightness then recovers by 13 a frame.
#pragma once
#include "lookups.h"
#include <cstdint>
#include <string>

class Game;
class RainEmitter;

class Weather {
public:
    bool init(const std::string& dataDir);          // weather_tables.bin
    void reset(Game& g, int weather);               // cTimeCycle::Reset
    void hourChanged(Game& g);                      // cTimeCycle::Process at the end of an hour
    void tick(Game& g);                             // rain and lightning, once per game frame
    void force(Game& g, int weather);               // ForceWeather(w, true); 8 = let it change again
    int forced = 8;                                 // +0x1DD8
    bool ok() const { return ok_; }
    RainEmitter* rain = nullptr;
private:
    void lightning(Game& g);                        // cTimeCycle::UpdateLightning
    int rollNext(int current) const;                // SetNewWeatherToTransitionTo
    WeatherTables tables_;
    bool ok_ = false;
    // UpdateLightning state
    int state_ = 0;                                 // +0x1DE4: 0 idle, 1 starting, 2 striking, 3 fading
    uint32_t start_ = 0, lastEnd_ = 0, delay_ = 0;  // +0x1DE8, +0x1DF0, +0x1DEC
    uint32_t thunderDelay_ = 0;                     // +0x1DF4
    uint32_t flash_[3] = {};                        // +0x1DF8
    uint8_t sound_[3] = {}, flashes_ = 0;           // +0x1E04, +0x1E07
    bool hourToggle_ = false, flashing_ = false;    // +0x1E08, +0x1E09
    int savedWeather_ = 0, savedNext_ = 0;          // +0x1DD0
};
