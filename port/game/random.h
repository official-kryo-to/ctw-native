// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#pragma once
#include <cstdint>

// The Android RandInit / Rand16 / Rand32 routines have four independent streams.
// A zero bound returns the unscaled state; positive bounds return values in [0,bound).
void RandInit(uint32_t seed);
uint32_t Rand32Critical(uint32_t bound);
uint32_t Rand32NonCritical(uint32_t bound);
uint32_t Rand16Critical(uint32_t bound);
uint32_t Rand16NonCritical(uint32_t bound);
