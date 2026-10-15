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
