// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "minigamecam.h"
#include "fixedmath.h"
#include "projectiles.h"
#include <cmath>

namespace {
int fastsin(int a) { return (int)(sinf((float)a * 9.587378e-05f) * 4096.f); }

// t / 25 (Q12) as the binary angle t / 25 x 90 degrees, by the original's steps: x 0x1.921 (pi / 2, Q32) to radians,
// x 0x28BE630 (65536 / 2 pi, Q24) to a binary angle
int32_t fraction(int32_t t) { return (int32_t)((((int64_t)t << 44) / 102400) >> 20); }
int angle(int32_t x) {
    const int32_t radians = (int32_t)(((int64_t)x * 0x192100000LL) >> 32);
    const uint32_t lo = (uint32_t)((uint64_t)((int64_t)radians * 0x28BE630) >> 8);
    return (int32_t)lo >> 16;
}
int32_t scaled(int32_t v, int32_t k) { return (int32_t)(uint32_t)((uint64_t)((int64_t)v * k) >> 12); }
}   // namespace

void MinigameCam::start(Collision& col, const int32_t eye[3], uint16_t yaw, uint16_t pitch, const int32_t dir[3],
                        int32_t scale, const int32_t follow[3], bool keepGoing, uint32_t blockedFrames) {
    obstruction_.reset(blockedFrames);   // (cCamera::SwitchToCam carries the count over)
    yaw_ = yaw;
    pitch_ = pitch;
    fov_ = 0x1555;
    frame_ = 0;
    for (int k = 0; k < 3; ++k) {
        eye_[k] = start_[k] = eye[k];
        target_[k] = eye[k] + scaled(dir[k], scale);
    }
    // the slide against the statics: on a hit, 1.3 units back from the contact towards the start
    int32_t contact[3], normal[3];
    if (Projectiles_SphereStatics(col, start_, target_, 0x14CC, contact, normal)) {
        int32_t back[3] = {start_[0] - contact[0], start_[1] - contact[1], start_[2] - contact[2]};
        Normalise3(back);
        for (int k = 0; k < 3; ++k) target_[k] = contact[k] + scaled(back[k], 0x14CC);
    }
    // eight steps towards it, each checked for a line to the followed entity (a 0.5-unit sphere to 5 units above it)
    int32_t step[3] = {target_[0] - start_[0], target_[1] - start_[1], target_[2] - start_[2]};
    const uint64_t l2 = (uint64_t)((int64_t)step[0] * step[0] + (int64_t)step[1] * step[1] + (int64_t)step[2] * step[2]);
    Normalise3(step);
    const int32_t length = (int32_t)(uint32_t)std::sqrt((double)l2), each = length / 8;
    for (int k = 0; k < 3; ++k) step[k] = scaled(step[k], each);
    const int32_t head[3] = {follow[0], follow[1], follow[2] + 0x5000};
    int32_t point[3] = {start_[0], start_[1], start_[2]}, end[3] = {point[0], point[1], point[2]};
    bool blocked = false;
    for (int i = 0; i < 8 && !blocked; ++i) {
        const int32_t previous[3] = {point[0], point[1], point[2]};
        for (int k = 0; k < 3; ++k) point[k] += step[k];
        if (Projectiles_SphereStatics(col, point, head, 0x800, contact, normal) && !keepGoing) {
            for (int k = 0; k < 3; ++k) end[k] = previous[k];
            blocked = true;
        }
    }
    for (int k = 0; k < 3; ++k) target_[k] = blocked ? end[k] : point[k];
    active_ = moving_ = true;
}

void MinigameCam::update(const int32_t follow[3], Collision* col) {
    if (!active_) return;
    if (moving_) slide();
    obstruction_.count(follow, eye_, false, col);
}

void MinigameCam::slide() {
    const int32_t x = fraction(frame_);
    const int32_t s = fastsin(angle(x));
    for (int k = 0; k < 3; ++k) eye_[k] = scaled(start_[k], 0x1000 - s) + scaled(target_[k], s);
    ++frame_;
    if (x > 0x1000) { moving_ = false; return; }
    // cBaseCam::SetFov: from 0x1555 to 0xE39 by the next frame's sine
    const int32_t n = fastsin(angle(fraction(frame_)));
    fov_ = (int16_t)(((int64_t)(0x1000 - n) * 0x1555000 + (int64_t)n * 0xE39000) >> 24);
}
