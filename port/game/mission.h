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

// cScriptProcessBase for a mission's helper scripts (cInWaterHelpText, ...): their own waits and watches, which
// Stop drops
class ScriptProcess {
public:
    void wait(int frames, std::function<void()> fn);                    // TimerImpl::Wait
    void when(std::function<bool()> test, std::function<void()> fn);    // polled every frame until true
    void stop();                                                        // cScriptProcessBase::Stop
    void tick();                                                        // once per 30 Hz frame
private:
    struct Wait { int frames; std::function<void()> fn; };
    struct When { std::function<bool()> test; std::function<void()> fn; };
    std::vector<Wait> waits_;
    std::vector<When> whens_;
    unsigned generation_ = 0;
};

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
    bool white = false;  // the flash bang's daze fades white (cPlayer::Daze's FadeIn, b=0); every other fade is black
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
// in the blip's colour, clamped to the radar's edge when out of range) and, for some representations, a marker in
// the world (markers.h)
struct RadarBlip {
    int32_t pos[3];
    uint32_t colour;   // 0xRRGGBBAA
    int id;
    char label[3]{};   // optional short plugin marker, e.g. A or AA; empty keeps the normal dot
    int8_t marker = -1;          // eMarkerRepresentation: 1 sMarker3D, 5 sMarkerChevron; -1 (a plugin's), 4 radar only
    uint8_t markerColour = 2;    // Location::SetColour (HUDImpl::AddBlip sets 2)
    int16_t markerAngle = 0;     // sMarkerChevron::SetAngle (Marker::SetAngle: degrees x 0xB6)
    uint8_t chevronType = 0;     // Marker::SetChevronType
    int32_t markerSize = 0;      // sPosPrimitive +0x70 (Marker::SetRadius); 0: the representation's own
    bool onRadar = true;         // Marker::SetBlip
    // the radar sprite (iPosition): -1 a plugin's dot; HUDImpl::AddBlip's sprite 106 in palette 7, long range
    int16_t sprite = -1;         // +0x46 (HUDImpl::SetBlipSprite)
    uint8_t palette = 7;         // +0x48 (Marker::SetSpritePalette)
    bool longRange = true;       // +0x4A bit 1 (eBlipRange 1): kept at the radar's edge when far
    int16_t appear = 0x1000;     // +0x40: the pop-in (DrawBlip / AnimScaleBlip), at the scale once it has settled
    int16_t spriteScale = 0x1000;   // the sprite's current scale
};


// the missions the port can start (kena01.cpp, ...)
std::unique_ptr<Mission> Mission_Create(const std::string& name);
