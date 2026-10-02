// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#pragma once
#include <cstdint>

class Collision;

// The shared cBaseCam blocked-view state. Recovery uses the previous frame's counter;
// cFollowPedCam calls HandleStuckCam before cBaseCam::Update counts the new view.
class CameraObstruction {
public:
    void reset(uint32_t frames = 0) { blockedFrames_ = frames; }
    uint32_t blockedFrames() const { return blockedFrames_; }
    void count(const int32_t target[3], const int32_t camera[3], bool onFoot, Collision* col);
    void recover(const int32_t target[3], int32_t camera[3], Collision* col) const;
private:
    static void aboveTarget(const int32_t target[3], int32_t camera[3], Collision& col);
    uint32_t blockedFrames_ = 0;
};
