// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "propdynamics.h"
#include "game.h"
#include "particles.h"
#include <algorithm>
#include <cmath>

namespace {
const float kDt = 1.f / 30.f;
const float kForcePerSpeed = 0.3f;   // f = speed (units/s) x 0.3 (see propdynamics.h)
const float kGravity = 30.f;         // units/s^2 for loose props
enum : uint16_t { kStanding = 0, kKnocked = 1 };
}

void PropDynamics::checkImpacts(Game& g) {
    for (Vehicle& car : g.cars) {
        const float speed = car.speed() / 4096.f;
        if (speed < 1.f) continue;
        const float f = speed * kForcePerSpeed;
        int32_t spheres[16][4];
        int n = 0;
        car.collisionSpheres(spheres, n);
        int ccx, ccy;
        Collision::cellOfPos(car.pos[0], car.pos[1], ccx, ccy);
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                const std::vector<Collision::Prop>* list = g.collision.props(ccx + dx, ccy + dy);
                if (!list) continue;
                for (int i = 0; i < (int)list->size(); ++i) {
                    const Collision::Prop& p = (*list)[i];
                    if (p.state != kStanding) continue;
                    const PropLibrary::Kind* kind = g.props.kind(p.kind);
                    if (!kind) continue;
                    const bool smash = kind->smashForce >= 0 && f >= kind->smashForce;
                    const bool uproot = kind->uprootForce >= 0 && f >= kind->uprootForce;
                    if (!smash && !uproot) continue;
                    float r, h;
                    g.props.footprint(p.prop, r, h);
                    if (r <= 0) continue;
                    const float px = p.x / 4096.f, py = p.y / 4096.f, pz = p.z / 4096.f;
                    bool hit = false;
                    for (int s = 0; s < n && !hit; ++s) {   // next frame's sphere against the prop's footprint
                        const float sx = (spheres[s][0] + car.vel[0] * kDt) / 4096.f - px;
                        const float sy = (spheres[s][1] + car.vel[1] * kDt) / 4096.f - py;
                        const float sz = spheres[s][2] / 4096.f, sr = spheres[s][3] / 4096.f;
                        hit = sx * sx + sy * sy < (sr + r + 0.2f) * (sr + r + 0.2f) && sz + sr > pz && sz - sr < pz + std::max(h, 0.5f);
                    }
                    if (hit) knock(g, ccx + dx, ccy + dy, i, car.vel, smash);
                }
            }
    }
}

void PropDynamics::knock(Game& g, int cx, int cy, int index, const int32_t v[3], bool smash) {
    const Collision::Prop p = (*g.collision.props(cx, cy))[index];
    g.collision.setPropState(cx, cy, index, kKnocked);
    g.collision.setPropSolid(cx, cy, index, false);   // cSimpleMover::ClearCollideAgainstFlags
    Loose l;
    l.cx = cx; l.cy = cy; l.index = index;
    l.prop = p;
    l.broken = smash;
    l.pos[0] = p.x / 4096.f; l.pos[1] = p.y / 4096.f; l.pos[2] = p.z / 4096.f;
    const float vx = v[0] / 4096.f, vy = v[1] / 4096.f, sp = std::sqrt(vx * vx + vy * vy);
    const float dx = sp > 0 ? vx / sp : 1.f, dy = sp > 0 ? vy / sp : 0.f;
    l.axis[0] = -dy; l.axis[1] = dx;   // tips over toward where the vehicle was going
    float r, h;
    g.props.footprint(p.prop, r, h);
    l.heightHalf = std::max(0.3f, h * 0.5f);
    if (smash) {   // falls over about its base
        l.vel[0] = vx * 0.1f; l.vel[1] = vy * 0.1f; l.vel[2] = 0;
        l.tiltVel = 0.8f + sp * 0.03f;
    } else {       // uprooted: thrown off the vehicle
        l.vel[0] = vx * 0.8f; l.vel[1] = vy * 0.8f; l.vel[2] = 2.f + sp * 0.1f;
        l.tiltVel = 1.5f + sp * 0.15f;
    }
    loose_.push_back(l);
    int32_t at[3] = {p.x, p.y, p.z + 0x800};   // a dust puff instead of the debris particles
    SmokeEmitter* dust = TheParticles().add<SmokeEmitter>(at, 8);
    for (int k = 0; k < 6; ++k) {
        const int32_t puff[3] = {(int32_t)(v[0] * 0.05f) + (k - 3) * 0x400, (int32_t)(v[1] * 0.05f), 0x800};
        dust->addParticle(puff, 0);
    }
    TheParticles().remove(dust);
}

void PropDynamics::update(Game& g) {
    int32_t f[3];
    g.focus(f);
    for (auto it = loose_.begin(); it != loose_.end();) {
        Loose& l = *it;
        const float fx = f[0] / 4096.f - l.pos[0], fy = f[1] / 4096.f - l.pos[1];
        if (fx * fx + fy * fy > 250.f * 250.f) {   // far away: back upright (the sector reloads in the game)
            g.collision.setPropState(l.cx, l.cy, l.index, kStanding);
            g.collision.setPropSolid(l.cx, l.cy, l.index, true);
            it = loose_.erase(it);
            continue;
        }
        if (!l.resting) {
            // tipping: gravity pulls a tilted prop further over (a rod about its end), stops lying down
            l.tiltVel += 1.5f * kGravity * std::sin(std::max(l.tilt, 0.05f)) / (2.f * l.heightHalf) * kDt;
            l.tilt += l.tiltVel * kDt;
            if (l.tilt >= l.maxTilt) { l.tilt = l.maxTilt; l.tiltVel = 0; }
            for (int k = 0; k < 3; ++k) l.pos[k] += l.vel[k] * kDt;
            l.vel[2] -= kGravity * kDt;
            const Collision::Ground gr = g.collision.ground(l.pos[0], l.pos[1], l.pos[2] + 1.f);
            if (l.pos[2] <= gr.z) {
                l.pos[2] = gr.z;
                l.vel[2] = 0;
                l.vel[0] *= 0.85f;
                l.vel[1] *= 0.85f;
            }
            if (l.tilt >= l.maxTilt && l.pos[2] <= gr.z + 0.01f && l.vel[0] * l.vel[0] + l.vel[1] * l.vel[1] < 0.05f)
                l.resting = true;
        }
        ++it;
    }
}

void PropDynamics::render(Game& g) const {
    for (const Loose& l : loose_) {
        const Model* m = l.broken ? g.props.brokenModel(l.prop.prop) : nullptr;
        if (!m) m = g.props.model(l.prop.prop);
        if (!m) continue;
        // rotation: tilt about the horizontal axis (Rodrigues) after the prop's heading
        const float h = l.prop.heading * 3.14159265f / 32768.f, ch = std::cos(h), sh = std::sin(h);
        const float c = std::cos(l.tilt), s = std::sin(l.tilt), t = 1 - c, x = l.axis[0], y = l.axis[1];
        const float T[3][3] = {{t * x * x + c, t * x * y, s * y}, {t * x * y, t * y * y + c, -s * x}, {-s * y, s * x, c}};
        const float H[3][3] = {{ch, -sh, 0}, {sh, ch, 0}, {0, 0, 1}};
        float R[3][3];
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) R[i][j] = T[i][0] * H[0][j] + T[i][1] * H[1][j] + T[i][2] * H[2][j];
        const float M[16] = {R[0][0], R[1][0], R[2][0], 0, R[0][1], R[1][1], R[2][1], 0, R[0][2], R[1][2], R[2][2], 0,
                             l.pos[0], l.pos[1], l.pos[2], 1};
        PropLibrary::drawModel(*m, M);
    }
}
