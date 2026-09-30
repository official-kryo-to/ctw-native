// Time of day / weather colours (cTimeCycle, timecycle.dat in ROM.WAD).
//
// timecycle.dat: 8 weathers x 38 tables x 24 hourly i32 values (20.12 fixed point, -1.0 = "fill in"): 29184 bytes.
// cCycleElement::Init stores each hour as a byte and linearly fills the missing hours around the clock (angle
// tables wrap at 256). cTimeCycle::InitInterpolators then blends hour h (current weather) into hour h+1 (next
// weather) by the minute fraction. Value v[i] is what the game keeps at cTimeCycle + i*8:
//   v0-2   sun light colour (ColourLightning(0))      v3,v4  sun angles (1/256 turn, x256)
//   v13-15 ambient (Colour(0xD) -> MaterialAmbient)  v22-24 water colour (Colour(0x16))
//   v25    shadow alpha                              v29-31 clear/sky colour (/65535)
// Colours are 0..31 per channel in the file, x0x800 here, and Colour() returns min(v >> 8, 0xFE) per channel.
#pragma once
#include <cstdint>
#include <vector>

class TimeCycle {
public:
    bool load(const std::vector<uint8_t>& dat);
    void setWeather(int w) { weather_ = w & 7; }
    void setTime(uint32_t hours20_12) { time_ = hours20_12 % (24u << 12); }
    uint32_t time() const { return time_; }
    void advanceFrames(int frames) { setTime(time_ + 2u * (uint32_t)frames); }   // cTimeCycle::Process: +2 per frame
    void evaluate();                                  // recompute v[] for the current time
    uint32_t colour(int i) const;                     // cTimeCycle::Colour: 0xAABBGGRR, alpha 0xFF
    void sunDirection(float out[3]) const;            // vecMainLightDirection (points towards the sun)
    void skyColour(float out[3]) const;               // glClearColor from cRenderer
    float value(int i) const { return v_[i]; }
    bool ok() const { return ok_; }
private:
    uint8_t tab_[8][38][24] = {};
    float v_[38] = {};
    int weather_ = 0;
    uint32_t time_ = 12u << 12;
    bool ok_ = false;
};
