// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#pragma once
#include <cstdint>
#include <vector>

// cRadar's resource 2760: atlas origin, tile grid, three overview textures, then the tile IDs.
// The resource remains in the user's game.pak; no map artwork or extracted lookup data is built in.
class RadarMap {
public:
    bool load(const std::vector<uint8_t>& data);
    int texture(int column, int row) const;
    int columns() const { return columns_; }
    int rows() const { return rows_; }
    int originX() const { return originX_; }
    int originY() const { return originY_; }
    int defaultTexture() const { return defaultTexture_; }
    static void worldToMap(const int32_t pos[3], float& x, float& y);
private:
    int originX_ = 0, originY_ = 0, columns_ = 0, rows_ = 0;
    int defaultTexture_ = -1;
    std::vector<uint16_t> tiles_;
};

// Normal radar projection: camera yaw is clockwise from world north, as in cBaseCam.
// Map tiles and world-space markers must use the same rotation.
class RadarProjection {
public:
    explicit RadarProjection(uint16_t cameraYaw = 0);
    void mapOffset(float dx, float dy, float& x, float& y) const;
    // A nonzero radius clamps the marker's centre to the circular radar edge.
    void worldOffset(const int32_t pos[3], const int32_t focus[3], float& x, float& y,
                     float radius = 0.f) const;
private:
    int32_t cosine_ = 4096, sine_ = 0;
};

// cRadar's flashing (+0xC48..+0xC50) and the map tint RenderRadar moves towards it (+0xC3C..+0xC44).
// cHudApp::Process asks for style 1 (steady blue) or 5 (blue and red) while wanted; 3, 4 and 6 are the PDA
// tutorials' grey flashes. Like the stars, it steps once per game frame.
class RadarFlash {
public:
    void setStyle(int style, int period);   // cRadar::SetRadarFlashStyle
    void process();                          // cRadar::ProcessRadarFlashing
    void stepTint();                         // RenderRadar: each channel moves 0x3F a frame towards the colour
    int colour() const { return colour_; }   // 0 normal, 1 blue, 2 red, 3 grey
    int style() const { return style_; }
    void tint(float rgb[3]) const { for (int k = 0; k < 3; ++k) rgb[k] = rgb_[k] / 255.f; }
private:
    uint8_t colour_ = 0;          // +0xC48
    int8_t countdown_ = 0;        // +0xC49
    uint8_t period_ = 0;          // +0xC4A
    uint8_t style_ = 0;           // +0xC4B
    uint8_t requested_ = 0;       // +0xC4C
    uint8_t requestedPeriod_ = 0; // +0xC4D
    uint32_t pulses_ = 0;         // +0xC50: style 6's flashes
    // red, green, blue (+0xC3C, +0xC40, +0xC44). The original's radar starts at zero in .bss and fades in over its
    // first five frames; the port starts at the normal colour.
    int rgb_[3] = {255, 255, 255};
};
