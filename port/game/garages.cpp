// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "garages.h"
#include "os/gamefs.h"
#include <cmath>
#include <cstring>

namespace {
int32_t fastsin(int a) { return (int32_t)std::lround(std::sin((int16_t)a * 3.14159265358979 / 32768.0) * 4096.0); }
}

bool Garages::load(const std::string& dataDir) {
    garages_.clear();
    GameFs fs;
    std::vector<uint8_t> d;
    if (!fs.open(dataDir) || !fs.read("garages.dat", d)) return false;
    for (size_t o = 0; o + 16 <= d.size(); o += 16) {   // cGarageManager::AddOne
        Garage g;
        std::memcpy(g.pos, &d[o], 12);
        std::memcpy(&g.heading, &d[o + 12], 2);
        std::memcpy(&g.doorProp, &d[o + 14], 2);
        g.baseZ = (int16_t)((uint32_t)g.pos[2] >> 12);
        // the front area: 9.6 x the heading's sine and cosine (x 0x9.999), halved, from the garage's position
        const int64_t s = ((int64_t)fastsin(g.heading) * 0x999900000LL) >> 32;
        const int64_t c = ((int64_t)fastsin(g.heading + 0x4000) * 0x999900000LL) >> 32;
        g.front[0] = g.pos[0] + (int32_t)(s >> 1);
        g.front[1] = g.pos[1] + (int32_t)(c >> 1);
        garages_.push_back(std::move(g));
    }
    return true;
}

int Garages::closest(const int32_t p[3]) const {
    int best = -1;
    uint32_t bestDist = 0x7A120000;
    for (int i = 0; i < count(); ++i) {   // from the front area's centre (0.5 + half its unit height up)
        const Garage& g = garages_[i];
        const int64_t dx = g.front[0] - p[0], dy = g.front[1] - p[1], dz = g.pos[2] + 0x1000 - p[2];
        const uint32_t d = (uint32_t)std::sqrt((double)(uint64_t)(dx * dx + dy * dy + dz * dz));
        if (d < bestDist) { best = i; bestDist = d; }
    }
    return best < 0 && count() ? 0 : best;
}

void Garages::open(int i) {
    if (i >= 0 && i < count()) garages_[i].state = kOpening;
}

void Garages::close(int i) {
    if (i >= 0 && i < count()) garages_[i].state = kClosing;
}

void Garages::whenOpen(int i, std::function<void()> fn) {
    if (i >= 0 && i < count()) garages_[i].whenOpen = std::move(fn);
}

void Garages::process() {
    for (Garage& g : garages_) {
        if (g.state == kOpening) {
            const int16_t h = g.height;
            if (h < 0x3000) {
                g.height = (int16_t)(h + 0x333);
                if (h < 0x2CCD) continue;
            }
            if (auto fn = std::move(g.whenOpen)) { g.whenOpen = nullptr; fn(); }
            g.state = kOpen;
            g.height = 0x3000;
        } else if (g.state == kClosing) {
            if (g.height >= 1 && (uint16_t)g.height > 0x333) {
                g.height = (int16_t)(g.height - 0x333);
                continue;
            }
            g.state = kShut;
            g.height = 0;
        }
    }
}

int Garages::doorOf(const Collision::Prop& p) const {
    if (p.kind != 1) return -1;
    for (int i = 0; i < count(); ++i) {
        const Garage& g = garages_[i];
        if (g.doorProp != p.prop) continue;
        if ((p.x + p.y) - (g.front[0] + g.front[1]) <= 0xFFF) return i;
    }
    return -1;
}

int32_t Garages::doorZ(int i) const {   // (sign-extended to 16 bits, as cGarage::Process stores it)
    const Garage& g = garages_[i];
    return (int16_t)(uint16_t)((uint16_t)g.height + ((uint16_t)g.baseZ << 12));
}

void Garages::position(int i, int32_t out[3]) const {
    for (int k = 0; k < 3; ++k) out[k] = garages_[i].pos[k];
}
