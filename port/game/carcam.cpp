// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "carcam.h"
#include "vehicle.h"
#include "world/collision.h"
#include "world/worldrenderer.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

static int fastsin(int a) { return (int)(sinf((float)a * 9.587378e-05f) * 4096.f); }
static inline int32_t mulq(int64_t a, int64_t b) { return (int32_t)((a * b) >> 12); }
static int16_t atan2q(int32_t y, int32_t x) { return (int16_t)std::clamp((int)(atan2f((float)y, (float)x) * 10430.f), -0x8000, 0x7FFF); }
static const int32_t kCameraRadius = 0x14CD;
static const int32_t kHeight = 28 << 12;   // IsAPhone(): 28, else 35

// PutValueIntoRange: move an angle towards [lo, hi] by at most `step`
static void putIntoRange(uint16_t& v, int16_t lo, int16_t hi, int step) {
    int16_t cur = (int16_t)v;
    int16_t dLo = (int16_t)(lo - cur), dHi = (int16_t)(hi - cur);
    int16_t d = 0;
    if (dLo > 0) d = dLo;
    else if (dHi < 0) d = dHi;
    if (d > step) d = (int16_t)step;
    if (d < -step) d = (int16_t)-step;
    v = (uint16_t)(cur + d);
}

void FollowCarCam::setBehind(const Vehicle& car) {
    pitch_ = 0xD4E4;
    pos_[0] = car.pos[0] + car.fwd[0] * -10;
    pos_[1] = car.pos[1] + car.fwd[1] * -10;
    pos_[2] = car.pos[2] + kHeight + car.fwd[2] * -10;
    vel_[0] = vel_[1] = vel_[2] = 0;
    yaw_ = (uint16_t)-atan2q(pos_[0] - car.pos[0], car.pos[1] - pos_[1]);
    state_ = 0;
    reverseFrames_ = 0;
    obstruction_.reset();
}

void FollowCarCam::inherit(const int32_t pos[3], uint16_t yaw, uint16_t pitch, uint32_t blockedFrames) {
    // cBaseCam::TweenFrom(..., 1, false): retain the outgoing view, not SetCameraBehindTarget.
    for (int i = 0; i < 3; ++i) { pos_[i] = pos[i]; vel_[i] = 0; }
    yaw_ = yaw; pitch_ = pitch;
    state_ = 0; reverseFrames_ = 0;
    obstruction_.reset(blockedFrames);
}

void FollowCarCam::update(const Vehicle& car, Collision* col) {
    int32_t speed = car.speed();
    int32_t back, height;
    if (car.hy < 0x5666) {
        int32_t perFrame = (int32_t)(((int64_t)speed * 0x8800000) >> 32);
        int32_t sq = (int32_t)(((((int64_t)perFrame * perFrame * 0x100000) >> 32) * 0x7800) >> 12);
        int32_t u = std::clamp(speed - 0x14000, 0, 0x46000);
        __int128 m = (__int128)((int64_t)(uint32_t)u << 32) * 0xEA0EA0EA0EA1LL;
        int32_t lift = (int32_t)((((int64_t)(m >> 64) >> 22) * (car.pos[2] - 0x2000)) >> 12);
        lift = std::clamp(lift, 0, 0x2000);
        height = sq - lift * 12;
        back = (int32_t)(((int64_t)lift * -0x6000 + (int64_t)-0x1000 * (sq >> 1) + 0xA000000) >> 12);
        height = std::min(kHeight + height, 0x2A000);
    } else {
        back = 0xA000;
        height = 0x2A000;
    }
    int32_t dir[3] = {car.fwd[0], car.fwd[1], car.fwd[2]};
    int32_t off[3] = {mulq(dir[0], -back), mulq(dir[1], -back), mulq(dir[2], -back)};
    int32_t fwdSpeed = (int32_t)(((int64_t)car.vel[0] * car.fwd[0] + (int64_t)car.vel[1] * car.fwd[1]) >> 12);
    if (state_ == 0) {
        if (fwdSpeed < -0x8000) {
            if (++reverseFrames_ > 30) state_ = 1;
        } else reverseFrames_ = 0;
    } else if (fwdSpeed > 0x8000) state_ = 0;
    if (state_ == 1) { off[0] = -off[0]; off[1] = -off[1]; }   // looking back at the car from the front
    int32_t target[3];
    if (speed != 0) {
        target[0] = car.pos[0] + off[0];
        target[1] = car.pos[1] + off[1];
        target[2] = car.pos[2] + height + off[2];
    } else {   // stopped: stay on the side the camera is on
        int32_t d[3] = {pos_[0] - car.pos[0], pos_[1] - car.pos[1], 0};
        double l = std::sqrt((double)d[0] * d[0] + (double)d[1] * d[1]);
        if (l > 0) { d[0] = (int32_t)(d[0] * 4096.0 / l); d[1] = (int32_t)(d[1] * 4096.0 / l); }
        target[0] = car.pos[0] + mulq(d[0], back);
        target[1] = car.pos[1] + mulq(d[1], back);
        target[2] = height + car.pos[2];
    }
    // spring towards the target
    vel_[0] = mulq(target[0] - pos_[0], 400) + mulq(vel_[0], 500);
    vel_[1] = mulq(target[1] - pos_[1], 400) + mulq(vel_[1], 500);
    vel_[2] = mulq((int32_t)(((int64_t)(target[2] - pos_[2]) * 0x19900000) >> 32), 400) + mulq(vel_[2], 500);
    int32_t want[3] = {pos_[0] + vel_[0], pos_[1] + vel_[1], pos_[2] + vel_[2]};
    int32_t from[3] = {pos_[0], pos_[1], pos_[2]}, contact[3], n[3];
    if (col && col->ok() && col->sweptSphereHitsBoxes(from, want, kCameraRadius, contact, n)) {
        for (int it = 3;; --it) {
            int32_t mm[3] = {(int32_t)(n[0] * 0x10280 >> 16), (int32_t)(n[1] * 0x10280 >> 16), (int32_t)(n[2] * 0x10280 >> 16)};
            int32_t d = (int32_t)(((int64_t)(want[1] - contact[1]) * mm[1] + (int64_t)(want[0] - contact[0]) * mm[0] +
                                   (int64_t)(want[2] - contact[2]) * mm[2]) >> 12);
            for (int i = 0; i < 3; ++i) {
                want[i] = want[i] - mulq(d, mm[i]) + mulq(kCameraRadius, mm[i]);
                from[i] = contact[i] + mulq(kCameraRadius, mm[i]);
            }
            if (it == 0) { want[0] = from[0]; want[1] = from[1]; want[2] = from[2]; break; }
            if (!col->sweptSphereHitsBoxes(from, want, kCameraRadius, contact, n)) break;
        }
    }
    pos_[0] = want[0]; pos_[1] = want[1]; pos_[2] = want[2];
    // pitch and yaw
    int32_t k = speed < 0x6000 ? (int32_t)((((int64_t)(uint32_t)speed << 32) / 0x6000) >> 20) : 0x1000;
    int rate = (int16_t)((uint32_t)(k * 0x3C0) >> 12);
    putIntoRange(pitch_, -0x2B1C, -0x2B1C, rate + 0x40);
    int16_t targetYaw = (int16_t)-atan2q(pos_[0] - car.pos[0], car.pos[1] - pos_[1]);
    putIntoRange(yaw_, targetYaw, targetYaw, (int16_t)((uint32_t)(k * 0x6DC) >> 12) + 0x40);
    // cFollowCarCam2 calls the shared counter but does not invoke HandleStuckCam.
    obstruction_.count(car.pos, pos_, false, col);
}

void FollowCarCam::toWorldCamera(WorldCamera& cam) const {
    for (int i = 0; i < 3; ++i) cam.eye[i] = pos_[i] / 4096.f;
    cam.setYawPitch(-(int16_t)yaw_ * 360.f / 65536.f, (int16_t)pitch_ * 360.f / 65536.f);
    cam.fovY = 60.f;
    cam.zNear = 0x666 / 4096.f;
    cam.zFar = 150.f;
}
