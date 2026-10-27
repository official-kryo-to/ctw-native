// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "radar.h"
#include <algorithm>
#include <cmath>
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

RadarProjection::RadarProjection(uint16_t cameraYaw) {
    // cHudApp::Process calls cRadar::Rotate with the negative active camera yaw.
    const int angle = (int16_t)(uint16_t)(0u - cameraYaw);
    sine_ = (int32_t)(std::sin(angle * 9.587378e-05f) * 4096.f);
    cosine_ = (int32_t)(std::sin((angle + 0x4000) * 9.587378e-05f) * 4096.f);
}

void RadarProjection::mapOffset(float dx, float dy, float& x, float& y) const {
    x = (dx * cosine_ - dy * sine_) / 4096.f;
    y = (dx * sine_ + dy * cosine_) / 4096.f;
}

void RadarProjection::worldOffset(const int32_t pos[3], const int32_t focus[3],
                                  float& x, float& y, float radius) const {
    // GetRadarSpaceFromWorldSpace retains Q12 fractions for markers, unlike the tile origin.
    const int64_t dx = (((int64_t)pos[0] - focus[0]) * 0x666) >> 12;
    const int64_t dy = -((((int64_t)pos[1] - focus[1]) * 0x666) >> 12);
    x = (float)((dx * cosine_ - dy * sine_) >> 12) / 4096.f;
    y = (float)((dx * sine_ + dy * cosine_) >> 12) / 4096.f;
    const float length = std::hypot(x, y);
    if (radius > 0.f && length > radius) {
        x *= radius / length;
        y *= radius / length;
    }
}

void RadarFlash::setStyle(int style, int period) {
    requested_ = (uint8_t)style;
    requestedPeriod_ = (uint8_t)period;
    if (style == 6) colour_ = 0;
}

void RadarFlash::process() {
    // (the original also reloads the blips' palette whenever the colour changes, and starting style 1 sends the
    // player's script listener an event)
    auto change = [&](uint8_t style) { style_ = style; period_ = requestedPeriod_; countdown_ = (int8_t)requestedPeriod_; };
    switch (style_) {
    case 0:
        if (colour_) colour_ = 0, countdown_ = 0;
        if (requested_) change(requested_);
        return;
    case 1:
        if (colour_ != 1) colour_ = 1, countdown_ = 0;
        if (requested_ != 1) change(requested_);
        return;
    case 2:   // grey while it counts down, then back to normal
        if (countdown_ > 0) { --countdown_; colour_ = 3; return; }
        if (requested_ == 2) { requested_ = 0; change(0); }
        else change(requested_);
        return;
    case 3: case 4: case 5: case 6:
        if (countdown_ > 0) { --countdown_; return; }
        if (requested_ != style_) { change(requested_); return; }
        if (style_ == 6) {   // four quick grey flashes, then a two second pause
            const uint32_t next = pulses_ + 1;
            colour_ = colour_ != 3 ? 3 : 0;
            pulses_ = next < 4 ? next : 0;
            countdown_ = next < 4 ? 5 : 0x3C;
            return;
        }
        countdown_ = (int8_t)period_;
        if (style_ == 3) colour_ = colour_ != 1 ? 1 : 0;
        else if (style_ == 4) colour_ = colour_ != 3 ? 3 : 0;
        else colour_ = colour_ == 1 ? 2 : 1;
        return;
    default:
        return;
    }
}

void RadarFlash::stepTint() {
    static const int targets[4][3] = {{255, 255, 255}, {0, 0, 0xC0}, {255, 0, 0}, {0x80, 0x80, 0x80}};
    const int* target = targets[colour_ <= 3 ? colour_ : 0];
    for (int k = 0; k < 3; ++k)
        rgb_[k] = rgb_[k] < target[k] ? std::min(rgb_[k] + 0x3F, target[k]) : std::max(rgb_[k] - 0x3F, target[k]);
}
