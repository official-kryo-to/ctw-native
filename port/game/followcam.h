// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// The on-foot game camera (cFollowPedCam + cBaseCam), in the game's own fixed point (20.12, angles 0x10000 = 360°).
//   - Sits `height` above the target (0x23000 = 35 units, times GetCamHeightScale: 0.72 on phones, 1.0 otherwise)
//     plus 2 units, following it with a damped spring (gain 0x333, damping 0x38D) - ProcessFacingWander; it sits
//     24 x |cos(pitch)| units behind the target along its yaw.
//   - Pitch: CanSeeTargetFromDefaultPos (a line from the target to the default position, against boxes) picks
//     55000 (tilted ~32° from straight down); if the view is blocked it goes straight down (0xC000), or stays
//     tilted when there is something overhead. Spring: vel = diff * 399 + vel * 0x7CE.
//   - Yaw: TryToFaceAngle swings it towards the ped's heading while the ped moves - at most 33 per frame (a third
//     of 100), half the remaining angle; almost opposite (> 0x7C72) it creeps (1 per frame).
//   - Collision: the move is swept as a 1.3-unit sphere against boxes (GetSphereCollision); on a hit the camera is
//     slid along the surface, up to 4 times.
//   - Projection: vertical half-angle 0x1555 (30°), near 0.4, far 150 (cBaseCam::cBaseCam).
#pragma once
#include <cstdint>
#include "cameraobstruction.h"

class Collision;
struct WorldCamera;

class FollowPedCam {
public:
    void reset(const int32_t target[3], int16_t heading);
    // one 30 fps game frame; pedVel is the ped's velocity (units per second, 20.12)
    void update(const int32_t target[3], int16_t pedHeading, const int32_t pedVel[3], Collision* col);
    void toWorldCamera(WorldCamera& out) const;

    int32_t height = 0;          // 20.12, set from heightScale in reset()
    float heightScale = 0.72f;   // GetCamHeightScale(): 0.72 on phone-form devices
    uint16_t yaw = 0;            // cBaseCam +0xE8
    uint16_t pitch() const { return pitch_; }

    void position(float out[3]) const;
    void position(int32_t out[3]) const { for (int i = 0; i < 3; ++i) out[i] = pos_[i]; }
    uint32_t blockedFrames() const { return obstruction_.blockedFrames(); }
    void inherit(const int32_t pos[3], uint16_t angle, uint16_t pitch, uint32_t blockedFrames = 0) {
        for (int i = 0; i < 3; ++i) { pos_[i] = pos[i]; vel_[i] = 0; }
        yaw = angle; pitch_ = pitch; pitchVel_ = 0;
        obstruction_.reset(blockedFrames);
    }
private:
    bool canSeeTarget(Collision* col) const;
    void tryToFaceAngle(int16_t pedHeading, const int32_t pedVel[3], int div, int maxStep);

    int32_t pos_[3] = {0, 0, 0}, vel_[3] = {0, 0, 0};   // +0xB8, +0x134
    int32_t target_[3] = {0, 0, 0};                     // +0x128
    uint16_t pitch_ = 55000;                            // +0xE4
    int32_t pitchVel_ = 0;                              // +0x140
    CameraObstruction obstruction_;
};
