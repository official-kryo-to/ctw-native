// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// PC extra: a game controller (Host_Pad) on top of the keyboard controls. The keyboard wins where both give a value.
//   On foot: the left stick moves (a light push, under half way, walks), A held sprints, RT fires, LT throws.
//   Driving: RT accelerates, LT brakes and reverses, the left stick steers (proportionally), A is the handbrake and
//   X the horn.
// The other buttons arrive as keys (os.h).
#pragma once
#include "vehicle.h"
#include "os/os.h"
#include <cmath>
#include <cstdint>

inline void PadInput_OnFoot(const HostPad& pad, float& mx, float& my, bool& walk, bool& sprint) {
    if (!pad.connected) return;
    if (mx == 0 && my == 0 && (pad.lx != 0 || pad.ly != 0)) {
        mx = pad.lx;
        my = pad.ly;
        walk = walk || std::sqrt(pad.lx * pad.lx + pad.ly * pad.ly) < 0.5f;
    }
    sprint = sprint || pad.a;
}

inline void PadInput_Weapons(const HostPad& pad, bool& fire, bool& throwing) {
    if (!pad.connected) return;
    fire = fire || pad.rt > 0.5f;
    throwing = throwing || pad.lt > 0.5f;
}

inline void PadInput_Driving(const HostPad& pad, Vehicle::Controls& dc, bool& horn) {
    if (!pad.connected) return;
    if (dc.throttle == 0 && (pad.rt > 0 || pad.lt > 0))
        dc.throttle = pad.rt >= pad.lt ? (int32_t)(pad.rt * 0x1000) : -(int32_t)(pad.lt * 0x1000);
    if (dc.steer == 0 && pad.lx != 0) dc.steer = (int32_t)(pad.lx * 0x1000);
    dc.handbrake = dc.handbrake || pad.a;
    horn = horn || pad.x;
}
