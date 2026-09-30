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
struct Slot { int v; bool angle; };
static const Slot kSlots[38] = {
    {0, false}, {1, false}, {2, false}, {3, true}, {4, true}, {5, false}, {6, false}, {7, false}, {8, true}, {9, true},
    {10, false}, {11, false}, {12, false}, {13, false}, {14, false}, {15, false}, {16, false}, {17, false}, {18, false},
    {25, false}, {19, false}, {20, false}, {21, false}, {26, false}, {27, false}, {28, false}, {22, false}, {23, false},
    {24, false}, {29, false}, {30, false}, {31, false}, {32, false}, {33, false}, {34, false}, {35, false}, {36, false},
    {37, false}};

bool TimeCycle::load(const std::vector<uint8_t>& dat) {
    if (dat.size() != 8 * 38 * 96) return false;
    for (int w = 0; w < 8; ++w)
        for (int t = 0; t < 38; ++t) {
            int32_t in[24];
            memcpy(in, &dat[((size_t)w * 38 + t) * 96], 96);
            // cTimeCycle::Init passes wrap=true for the four angle tables only
            initElement(tab_[w][t], in, kSlots[t].angle);
        }
    ok_ = true;
    evaluate();
    return true;
}

void TimeCycle::evaluate() {
    if (!ok_) return;
    uint32_t hour = time_ >> 12, frac = time_ & 0xFFF;
    uint32_t next = hour == 23 ? 0 : hour + 1;
    for (int t = 0; t < 38; ++t) {
        int cur = tab_[weather_][t][hour], nxt = tab_[weather_][t][next];
        int d = nxt - cur;
        if (kSlots[t].angle) {   // x0x100, wrapping at 256 (InitInterpolators)
            int dd = d * 0x100;
            if (d <= -0x80) dd += 0x10000;
            if (d >= 0x80) dd -= 0x10000;
            v_[kSlots[t].v] = (float)(((int)(dd * (int)frac + 0x800) >> 12) + cur * 0x100);
        } else {
            int dd = d * 0x800;
            v_[kSlots[t].v] = (float)(cur * 0x800 + ((int)(dd * (int)frac + 0x800) >> 12));
        }
    }
}

uint32_t TimeCycle::colour(int i) const {
    auto ch = [&](int k) { return std::min<uint32_t>((uint32_t)(int)v_[i + k] >> 8, 0xFE); };
    return 0xFF000000u | ch(2) << 16 | ch(1) << 8 | ch(0);
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
