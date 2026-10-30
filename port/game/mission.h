// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// The mission script runtime: a mission is a chain of callbacks (cScriptProcessBase::Call / SetState) run from
// TimerImpl::Wait, from the WhenEnters / WhenEntersVehicle style conditions and from its sequences' callbacks.
// The game side provides what the script asks of CameraImpl (FadeToBlack, FadeIn), HUDImpl (DisplayObjective,
// AddBlip) and InitMissionText.
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class Game;

class Mission {
public:
    virtual ~Mission() = default;
    virtual void start(Game& game) = 0;
    void tick(Game& game);                 // the waits and conditions that are due, once per 30 Hz frame
    bool finished() const { return finished_; }
    // InitMissionText("KEN_A01"): e_ken_a01.gxt; DisplayObjective ids from 0x52F on are its strings
    bool loadText(const std::string& dataDir, const std::string& name);
    std::u16string text(int id) const;

protected:
    virtual void onFrame(Game&) {}                          // the mission's own processes, before its waits
    void wait(int frames, std::function<void()> fn);        // TimerImpl::Wait
    void when(std::function<bool()> test, std::function<void()> fn);   // polled every frame until true
    void cancelAll();                                       // cScriptProcessBase::Stop
    void finish() { finished_ = true; cancelAll(); }

private:
    struct Wait { int frames; std::function<void()> fn; };
    struct When { std::function<bool()> test; std::function<void()> fn; };
    std::vector<Wait> waits_;
    std::vector<When> whens_;
    std::vector<std::u16string> text_;
    bool finished_ = false;
    unsigned generation_ = 0;   // bumps when everything is cancelled, so a running callback's queue is dropped
};

// cScreenFade (CameraImpl::FadeToBlack / FadeIn): black over the picture, with a callback when it is done. The
// level is 0 (clear) .. 0x10000 (black). FadeOut steps (0x10000 - level) / frames a frame, FadeIn -level / frames
// (all of it when that rounds to 0); Process adds the step and stops at the end, or a step early when the next
// would pass it (a fade-in once the level is below 0x1000).
struct ScreenFade {
    float alpha = 0.f;   // level / 0x10000, for drawing
    void toBlack(int frames, std::function<void()> done = nullptr);   // cScreenFade::FadeOut
    void in(int frames, std::function<void()> done = nullptr);        // cScreenFade::FadeIn
    void tick();                                                      // cScreenFade::Process
    bool fading() const { return step_ != 0; }
    int32_t level() const { return level_; }
private:
    int32_t level_ = 0, step_ = 0, target_ = 0;   // +8, +0xC, +0x16 (16 or 0)
    std::function<void()> done_;
};

// HUDImpl::AddBlip for the mission: a radar marker (the original's eMarkerRepresentation sprites are drawn as a dot
// in the blip's colour, clamped to the radar's edge when out of range)
struct RadarBlip {
    int32_t pos[3];
    uint32_t colour;   // 0xRRGGBBAA
    int id;
    char label[3]{};   // optional short plugin marker, e.g. A or AA; empty keeps the normal dot
};

// gp_MissionText objectives (HUDImpl::DisplayObjective): a line at the bottom of the screen for a while
struct Objective {
    std::u16string text;
    int frames = 0;
};

// the missions the port can start (kena01.cpp, ...)
std::unique_ptr<Mission> Mission_Create(const std::string& name);
