// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "random.h"

namespace {
uint64_t critical32 = 0, noncritical32 = 0;
uint32_t critical16 = 0, noncritical16 = 0;

uint32_t next32(uint64_t& state, uint32_t bound) {
    state = state*0x5D588B656C078965ULL + 0x269EC3;
    const uint32_t value = (uint32_t)state;
    return bound ? (uint32_t)(((uint64_t)value*bound) >> 32) : value;
}
uint32_t next16(uint32_t& state, uint32_t bound) {
    state = state*0x5D588B65u + 0x269EC3;
    // The reference multiplies and truncates to 32 bits before shifting, even for large bounds.
    return bound ? ((state & 0xFFFFu)*bound) >> 16 : state;
}
}

void RandInit(uint32_t seed) {
    critical32 = noncritical32 = seed;
    critical16 = noncritical16 = seed;
}
uint32_t Rand32Critical(uint32_t bound) { return next32(critical32,bound); }
uint32_t Rand32NonCritical(uint32_t bound) { return next32(noncritical32,bound); }
uint32_t Rand16Critical(uint32_t bound) { return next16(critical16,bound); }
uint32_t Rand16NonCritical(uint32_t bound) { return next16(noncritical16,bound); }
