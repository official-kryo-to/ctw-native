// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "cameraobstruction.h"
#include "world/collision.h"
#include <algorithm>
#include <cmath>

void CameraObstruction::count(const int32_t target[3], const int32_t camera[3], bool onFoot, Collision* col) {
    int32_t from[3] = {target[0], target[1], target[2] + (onFoot ? 0x2000 : 0)};
    if (col && col->ok() && col->staticLine(from, camera, 0x80000A00)) {
        if (blockedFrames_ != UINT32_MAX) ++blockedFrames_;
    } else blockedFrames_ = 0;
}

void CameraObstruction::aboveTarget(const int32_t t[3], int32_t camera[3], Collision& col) {
    // SetCameraAboveTarget checks both the space above the view and the target's vertical sight line.
    int32_t high[3] = {t[0], t[1], camera[2] + 0x1E000}, low[3] = {t[0], t[1], camera[2]};
    if (col.staticLine(high, low, 0x80000200)) return;
    high[2] = t[2] + 0x1E000;
    if (col.staticLine(t, high, 0x80000200)) return;
    camera[0] = t[0]; camera[1] = t[1];
}

void CameraObstruction::recover(const int32_t t[3], int32_t camera[3], Collision* col) const {
    if (blockedFrames_ < 6 || !col || !col->ok()) return;
    const int32_t margin = 0x14CD + 40; // CameraRadius plus the original fixed-point clearance
    int32_t from[3] = {t[0], t[1], t[2] + 40}, to[3] = {t[0], t[1], camera[2] + 40};
    const double dx = (double)t[0] - camera[0], dy = (double)t[1] - camera[1];
    const int32_t distance = (int32_t)std::sqrt(dx*dx + dy*dy);
    Collision::LineHit hit;
    if (col->staticLine(from, to, 0x200, &hit)) {
        if (distance < 0x12000 && (hit.box.flags & 2)) {
            int32_t view[3] = {camera[0], camera[1], camera[2] + 40};
            if (col->staticLine(from, view, 0x80002200)) aboveTarget(t, camera, *col);
            return;
        }
        // HandleStuckCam chooses the nearest box face in world XY, even for a rotated box.
        const int32_t offset[2] = {t[0] - hit.box.cx, t[1] - hit.box.cy};
        const int32_t half[2] = {hit.box.hx, hit.box.hy};
        const int32_t gap[2] = {std::abs(std::abs(offset[0]) - half[0]), std::abs(std::abs(offset[1]) - half[1])};
        const int axis = gap[0] < gap[1] ? 0 : 1;
        const int sign = offset[axis] < 0 ? -1 : 1;
        to[axis] += sign * (gap[axis] + margin);
        if (col->staticLine(from, to, 0x80000200)) to[axis] -= sign * 2 * (half[axis] + margin);
        if (col->staticLine(from, to, 0x80000200)) return;
        int32_t sky[3] = {to[0], to[1], to[2] + 0x64000};
        if (col->staticLine(to, sky, 0x80000200)) return;
        // The reference retains the old camera height, not the line endpoint's +40 offset.
        camera[0] = to[0]; camera[1] = to[1];
    } else if (col->staticLine(from, to, 0x2200)) {
        aboveTarget(t, camera, *col);
    } else if (!col->staticLine(from, to, 0x200) && distance > 0x12000) {
        aboveTarget(t, camera, *col);
    }
}
