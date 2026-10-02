// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// The original's vector normalisation, bit for bit (Q12 unit vectors).
#pragma once
#include <cmath>
#include <cstdint>

// Normalise(tv2d<cFixed<20,12>>&, const tv2d&): integer square root of 4 x |v|^2, a 2^56 reciprocal, rounded >> 45.
// The original divides by |v|^2 without a check; callers keep zero vectors out.
inline void Normalise2(int32_t v[2]) {
    const int64_t l2 = (int64_t)v[0] * v[0] + (int64_t)v[1] * v[1];
    if (l2 <= 0) return;
    const uint32_t root = (uint32_t)std::sqrt((double)(uint64_t)(4 * l2));
    const int64_t k = (0x100000000000000LL / l2) * (int64_t)root;
    v[0] = (int32_t)((k * v[0] + 0x100000000000LL) >> 45);
    v[1] = (int32_t)((k * v[1] + 0x100000000000LL) >> 45);
}

// Normalise(tv3d<cFixed<20,12>>&, const tv3d&): float reciprocal square root, each component rounded half away
// from zero.
inline void Normalise3(int32_t v[3]) {
    const int64_t l2 = (int64_t)v[0] * v[0] + (int64_t)v[1] * v[1] + (int64_t)v[2] * v[2];
    if (l2 <= 0) return;
    const float inv = 1.f / std::sqrt((float)l2);
    for (int k = 0; k < 3; ++k) {
        const float f = inv * (float)v[k];
        v[k] = (int32_t)((f >= 0.f ? 0.5f : -0.5f) + f * 4096.f);
    }
}
