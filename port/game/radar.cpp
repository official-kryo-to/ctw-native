// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "radar.h"
#include <cstring>

bool RadarMap::load(const std::vector<uint8_t>& data) {
    tiles_.clear();
    originX_ = originY_ = columns_ = rows_ = 0;
    defaultTexture_ = -1;
    if (data.size() < 14) return false;
    uint16_t header[7];
    std::memcpy(header, data.data(), sizeof header);
    const size_t count = (size_t)header[2] * header[3];
    if (!count || count > (data.size() - 14) / sizeof(uint16_t)) return false;
    originX_ = header[0]; originY_ = header[1];
    columns_ = header[2]; rows_ = header[3];
    defaultTexture_ = header[6] == 0xffff ? -1 : header[6];
    tiles_.resize(count);
    std::memcpy(tiles_.data(), data.data() + 14, count * sizeof(uint16_t));
    return true;
}

int RadarMap::texture(int column, int row) const {
    if (column < 0 || row < 0 || column >= columns_ || row >= rows_) return -1;
    const uint16_t id = tiles_[(size_t)row * columns_ + column];
    return id == 0xffff ? -1 : id;
}

void RadarMap::worldToMap(const int32_t pos[3], float& x, float& y) {
    // cRadar::RenderRadar / WorldPosToRadarOffset: the map's 0x666 Q12 scale and world origin.
    x = (float)((((int64_t)pos[0] + 3540LL * 4096) * 0x666) >> 24);
    y = (float)(((2520LL * 4096 - (int64_t)pos[1]) * 0x666) >> 24);
}
