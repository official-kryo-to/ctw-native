// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#pragma once
#include "random.h"
#include <cstdint>

// cBaseCam::ApplyShake / cCamera::Shake. Values are Q12; shake uses the critical 32-bit stream.
class CameraShake {
public:
    void start(int32_t amplitude, int32_t decay) {
        amplitude_ = amplitude; decay_ = decay;
        if (!amplitude) clear();
    }
    void clear() { amplitude_ = 0; offset_[0] = offset_[1] = offset_[2] = 0; }
    void offset(int32_t out[3]) const { for (int k = 0; k < 3; ++k) out[k] = offset_[k]; }
    void apply(int32_t position[3]) {
        if (!amplitude_) return;
        for (int k=0;k<3;++k) position[k] -= offset_[k];
        offset_[0] = (int32_t)Rand32Critical(amplitude_*2)-amplitude_;
        offset_[1] = (int32_t)Rand32Critical(amplitude_*2)-amplitude_;
        offset_[2] = (int32_t)Rand32Critical(amplitude_);
        amplitude_ = (int32_t)((int64_t)amplitude_*decay_ >> 12);
        if (amplitude_ <= 0x19A) { clear(); return; }
        for (int k=0;k<3;++k) position[k] += offset_[k];
    }
    static bool nearExplosion(const int32_t player[3], const int32_t explosion[3]) {
        int64_t d2 = 0;
        for (int k=0;k<3;++k) {
            const int64_t d = (int64_t)player[k]-explosion[k];
            if (d <= -0x32000 || d >= 0x32000) return false;
            d2 += d*d;
        }
        return d2 < (int64_t)0x32000*0x32000; // ShakePlayersCameras: strictly inside 50 units
    }
private:
    int32_t amplitude_ = 0, decay_ = 0;
    int32_t offset_[3]{};
};
