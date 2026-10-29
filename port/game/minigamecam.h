// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// cMinigameCam: the camera a minigame (hotwiring, the bug planting, the dumpsters ...) switches to. It keeps the
// outgoing camera's angles (cBaseCam::TweenFrom with one frame) and slides its eye from where that camera was to a
// point along a direction (the minigames pass the view's right vector, +0x94, times 10 or -10 units):
//   - the slide is cut short by the statics (a 1.3-unit sphere, GetSphereCollision 0xF00): the eye stops 1.3 units
//     back from the contact, towards the start;
//   - then it is walked in eight equal steps; at the first step from which a 0.5-unit sphere cannot reach a point
//     five units above the followed entity, the eye stops at the step before (unless told to go on regardless);
//   - Update moves the eye by sin(t / 25 x 90 degrees) of the way, frame t = 0, 1, ...; after frame 26 it stops
//     (at sin(26 / 25 x 90), just short of the end). The field of view goes from 0x1555 (30 degrees half-angle) to
//     0xE39 (20) by the same curve, one frame ahead.
#pragma once
#include <cstdint>
#include "cameraobstruction.h"

class Collision;

class MinigameCam {
public:
    // cMinigameCam::cMinigameCam(follow, eye, dir, scale, keepGoing). follow: the followed entity's position.
    void start(Collision& col, const int32_t eye[3], uint16_t yaw, uint16_t pitch, const int32_t dir[3], int32_t scale,
               const int32_t follow[3], bool keepGoing, uint32_t blockedFrames);
    // cMinigameCam::Update, then cBaseCam::Update's count of frames the followed entity is out of sight
    void update(const int32_t follow[3], Collision* col);
    void stop() { active_ = false; }
    bool active() const { return active_; }
    void position(int32_t out[3]) const { out[0] = eye_[0]; out[1] = eye_[1]; out[2] = eye_[2]; }
    uint16_t yaw() const { return yaw_; }
    uint16_t pitch() const { return pitch_; }
    int16_t fov() const { return fov_; }   // cBaseCam::SetFov: the vertical half-angle
    const int32_t* target() const { return target_; }
    uint32_t blockedFrames() const { return obstruction_.blockedFrames(); }

private:
    void slide();
    bool active_ = false, moving_ = false;   // +0x12C
    int32_t eye_[3] = {}, start_[3] = {}, target_[3] = {};   // +0xB8, +0x13C, +0x148
    uint16_t yaw_ = 0, pitch_ = 0;           // +0xE8, +0xE4 (TweenFrom)
    int16_t fov_ = 0x1555;
    int32_t frame_ = 0;                      // +0x128
    CameraObstruction obstruction_;          // +0x104
};
