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
