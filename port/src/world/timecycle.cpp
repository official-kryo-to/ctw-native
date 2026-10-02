// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "timecycle.h"
#include <algorithm>
#include <cmath>
#include <cstring>

// cCycleElement::Init: byte per hour, missing hours (-1.0) filled linearly around the clock.
static void initElement(uint8_t out[24], const int32_t* in, bool wrapAngles) {
    const int32_t kMissing = -0x1000;
    for (int i = 0; i < 24; ++i) out[i] = (uint8_t)((uint32_t)in[i] >> 12);
    int first = -1;
    for (int i = 0; i < 24; ++i)
        if (in[i] != kMissing) { first = i; break; }
    if (first < 0) { memset(out, 0, 24); return; }
    int i = first + 1 == 24 ? 0 : first + 1;
    while (i != first) {
        if (in[i] != kMissing) { i = i == 23 ? 0 : i + 1; continue; }
        int prev = i == 0 ? 23 : i - 1;
        int next = i;
        while (in[next] == kMissing) next = next == 23 ? 0 : next + 1;
        int n = next - prev;
        if (n <= 0) n += 24;
        uint32_t val = (uint32_t)in[prev];
        int diff = in[next] - (int)val;
        if (wrapAngles) {
            if (diff > 0x80000) diff -= 0x100000;
            if (diff < -0x80000) diff += 0x100000;
        }
        int step = diff / n;
        int h = prev;
        for (int k = 1; k < n; ++k) {
            val += (uint32_t)step;
            h = h == 23 ? 0 : h + 1;
            out[h] = (uint8_t)(val >> 12);
        }
        i = next;
    }
}

// Table order in the file (per weather) -> value slot, and whether the table is an angle (x0x100, wraps).
static bool angleSlot(int table) { return table == 3 || table == 4 || table == 8 || table == 9; }
static int valueSlot(int table) {
    if (table == 19) return 25;
    if (table >= 20 && table <= 22) return table - 1;
    if (table >= 23 && table <= 25) return table + 3;
    if (table >= 26 && table <= 28) return table - 4;
    return table;
}

bool TimeCycle::load(const std::vector<uint8_t>& dat) {
    if (dat.size() != 8 * 38 * 96) return false;
    for (int w = 0; w < 8; ++w)
        for (int t = 0; t < 38; ++t) {
            int32_t in[24];
            memcpy(in, &dat[((size_t)w * 38 + t) * 96], 96);
            // cTimeCycle::Init passes wrap=true for the four angle tables only
            initElement(tab_[w][t], in, angleSlot(t));
        }
    ok_ = true;
    evaluate();
    return true;
}

void TimeCycle::evaluate() {
    if (!ok_) return;
    const uint32_t time = extra_ ? extraTime_ : time_;
    uint32_t hour = time >> 12, frac = time & 0xFFF;
    uint32_t next = extra_ ? hour : hour == 23 ? 0 : hour + 1;   // (hour - extra flag) + 1
    for (int t = 0; t < 38; ++t) {
        int cur = tab_[weather_][t][hour], nxt = tab_[next_][t][next];
        int d = nxt - cur;
        if (angleSlot(t)) {   // x0x100, wrapping at 256 (InitInterpolators)
            int dd = d * 0x100;
            if (d <= -0x80) dd += 0x10000;
            if (d >= 0x80) dd -= 0x10000;
            v_[valueSlot(t)] = (float)(((int)(dd * (int)frac + 0x800) >> 12) + cur * 0x100);
        } else {
            int dd = d * 0x800;
            v_[valueSlot(t)] = (float)(cur * 0x800 + ((int)(dd * (int)frac + 0x800) >> 12));
        }
    }
}

uint32_t TimeCycle::colour(int i) const {
    auto ch = [&](int k) { return std::min<uint32_t>((uint32_t)(int)v_[i + k] >> 8, 0xFE); };
    return 0xFF000000u | ch(2) << 16 | ch(1) << 8 | ch(0);
}

uint32_t TimeCycle::colourLightning(int i) const {
    auto ch = [&](int k) { return std::min<uint32_t>((uint32_t)(int)v_[i + k] >> 8, 0xFE); };
    uint32_t c[3] = {ch(0), ch(1), ch(2)};
    if (brightness != 4096)
        for (uint32_t& v : c) {
            if (brightness <= 4096) v += (uint32_t)((((uint64_t)((v << 12) ^ 0xFF000)) * (uint32_t)(4096 - brightness)) >> 24);
            else v -= (uint32_t)(((uint64_t)(v << 12) * (uint32_t)(brightness - 4096)) >> 24);
        }
    return 0xFF000000u | c[2] << 16 | c[1] << 8 | c[0];
}

static int fastsin(int a) { return (int)(sinf((float)a * 9.587378e-05f) * 4096.f); }

void TimeCycle::sunDirection(float out[3]) const {
    // cRenderer: angles from v3/v4, direction = -normalise(sin4*sin3, sin4*cos3, -cos4)
    int a3 = (int16_t)(int)v_[3], a4 = (int16_t)(int)v_[4];
    int s3 = fastsin(a3), c3 = fastsin(a3 + 0x4000);
    int s4 = fastsin((int16_t)(a4 - 0x4000) + 0x4000), m = fastsin((int16_t)(a4 - 0x4000));
    float x = (float)((int64_t)s4 * s3 >> 12), y = (float)((int64_t)s4 * c3 >> 12), z = (float)m;
    float len = sqrtf(x * x + y * y + z * z);
    if (len <= 0) { out[0] = 0; out[1] = 0; out[2] = 1; return; }
    out[0] = -x / len; out[1] = -y / len; out[2] = -z / len;
}

void TimeCycle::skyColour(float out[3]) const {
    for (int i = 0; i < 3; ++i) out[i] = std::max(-1.f, std::min(1.f, v_[29 + i] / 65535.f));
}
