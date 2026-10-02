// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// Render interpolation (a PC presentation feature, not part of the original): the game simulates at a fixed 30 Hz like
// OS_ApplicationTick, and a display frame between two ticks draws vehicles, the player, pedestrians and the game
// camera between their previous and current poses. The simulation itself never sees interpolated values: they are
// applied for the duration of Game::render and restored afterwards, unless something (a mod) moved the entity in the
// meantime, in which case that change is kept.
#include "game.h"
#include <cmath>

namespace {
constexpr int64_t kSnap = (int64_t)(10 * 4096) * (10 * 4096);   // farther than 10 units in one tick: a teleport

int32_t lerp(int32_t a, int32_t b, float t) { return a + (int32_t)std::lround((double)(b - a) * t); }
bool near(const int32_t a[3], const int32_t b[3]) {
    const int64_t dx = (int64_t)a[0] - b[0], dy = (int64_t)a[1] - b[1], dz = (int64_t)a[2] - b[2];
    return dx * dx + dy * dy + dz * dz <= kSnap;
}
void normalise(float v[3]) {
    const float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (l > 1e-6f) for (int i = 0; i < 3; ++i) v[i] /= l;
}
void cross(const float a[3], const float b[3], float out[3]) {
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}
// Blends two orthonormal bases given by forward and up vectors; right = forward x up, as in both the entity matrix
// and WorldCamera.
void blendBasis(const float f0[3], const float u0[3], const float f1[3], const float u1[3], float t,
                float right[3], float fwd[3], float up[3]) {
    for (int i = 0; i < 3; ++i) { fwd[i] = f0[i] + (f1[i] - f0[i]) * t; up[i] = u0[i] + (u1[i] - u0[i]) * t; }
    normalise(fwd);
    const float d = fwd[0] * up[0] + fwd[1] * up[1] + fwd[2] * up[2];
    for (int i = 0; i < 3; ++i) up[i] -= fwd[i] * d;
    normalise(up);
    cross(fwd, up, right);
}
}   // namespace

void Game::snapshotPoses() {
    Interp& s = interp_;
    s.cars.clear();
    for (const Vehicle& c : cars) {
        Pose& p = s.cars[c.uid];
        std::copy(c.pos, c.pos + 3, p.pos);
        std::copy(c.right, c.right + 3, p.right);
        std::copy(c.fwd, c.fwd + 3, p.fwd);
        std::copy(c.up, c.up + 3, p.up);
    }
    std::copy(player.pos, player.pos + 3, s.player);
    s.peds.clear();
    for (const Pedestrians::Ped& p : peds.peds) std::copy(p.body.pos, p.body.pos + 3, s.peds[p.uid].pos);
    s.cameraValid = !freeCam.on;
    if (s.cameraValid) viewCamera(s.camera);
    s.valid = true;
}

// The entity an interpolated pose belongs to, found again by id: callbacks during rendering may add or remove
// vehicles and pedestrians.
bool Game::poseTarget(const Applied& a, int32_t*& pos, int16_t*& r, int16_t*& f, int16_t*& u) {
    pos = nullptr; r = f = u = nullptr;
    if (a.kind == Applied::Player) { pos = player.pos; return true; }
    if (a.kind == Applied::Car) {
        for (Vehicle& c : cars) if (c.uid == a.uid) { pos = c.pos; r = c.right; f = c.fwd; u = c.up; return true; }
        return false;
    }
    for (Pedestrians::Ped& p : peds.peds) if (p.uid == a.uid) { pos = p.body.pos; return true; }
    return false;
}

void Game::applyInterpolation() {
    Interp& s = interp_;
    s.applied.clear();
    renderCamValid_ = false;
    const float t = renderAlpha;
    if (!interpolate || !s.valid || t >= 1.f) return;
    auto apply = [&](Applied a, const Pose& before, bool basis) {
        int32_t* pos; int16_t *r, *f, *u;
        if (!poseTarget(a, pos, r, f, u) || !near(before.pos, pos)) return;
        std::copy(pos, pos + 3, a.saved.pos);
        for (int i = 0; i < 3; ++i) pos[i] = lerp(before.pos[i], pos[i], t);
        std::copy(pos, pos + 3, a.set.pos);
        a.basis = basis && r;
        if (a.basis) {
            std::copy(r, r + 3, a.saved.right); std::copy(f, f + 3, a.saved.fwd); std::copy(u, u + 3, a.saved.up);
            float f0[3], u0[3], f1[3], u1[3], R[3], F[3], U[3];
            for (int i = 0; i < 3; ++i) {
                f0[i] = before.fwd[i] / 4096.f; u0[i] = before.up[i] / 4096.f;
                f1[i] = f[i] / 4096.f; u1[i] = u[i] / 4096.f;
            }
            blendBasis(f0, u0, f1, u1, t, R, F, U);
            for (int i = 0; i < 3; ++i) {
                r[i] = (int16_t)std::lround(R[i] * 4096.f);
                f[i] = (int16_t)std::lround(F[i] * 4096.f);
                u[i] = (int16_t)std::lround(U[i] * 4096.f);
            }
            std::copy(r, r + 3, a.set.right); std::copy(f, f + 3, a.set.fwd); std::copy(u, u + 3, a.set.up);
        }
        s.applied.push_back(a);
    };
    for (const Vehicle& c : cars) {
        auto it = s.cars.find(c.uid);
        if (it != s.cars.end()) apply({Applied::Car, c.uid}, it->second, true);
    }
    Pose player0{};
    std::copy(s.player, s.player + 3, player0.pos);
    apply({Applied::Player, 0}, player0, false);
    for (const Pedestrians::Ped& p : peds.peds) {
        auto it = s.peds.find(p.uid);
        if (it != s.peds.end()) apply({Applied::Ped, p.uid}, it->second, false);
    }
    // the game camera between its view at the start of the tick and its current view
    if (!s.cameraValid || freeCam.on) return;
    WorldCamera now;
    viewCamera(now);
    const int32_t a[3] = {(int32_t)(s.camera.eye[0] * 4096), (int32_t)(s.camera.eye[1] * 4096), (int32_t)(s.camera.eye[2] * 4096)};
    const int32_t b[3] = {(int32_t)(now.eye[0] * 4096), (int32_t)(now.eye[1] * 4096), (int32_t)(now.eye[2] * 4096)};
    if (!near(a, b)) return;
    renderCam_ = now;
    for (int i = 0; i < 3; ++i) renderCam_.eye[i] = s.camera.eye[i] + (now.eye[i] - s.camera.eye[i]) * t;
    blendBasis(s.camera.fwd, s.camera.up, now.fwd, now.up, t, renderCam_.right, renderCam_.fwd, renderCam_.up);
    renderCamValid_ = true;
}

void Game::restoreInterpolation() {
    for (const Applied& a : interp_.applied) {
        int32_t* pos; int16_t *r, *f, *u;
        if (!poseTarget(a, pos, r, f, u)) continue;
        if (std::equal(pos, pos + 3, a.set.pos)) std::copy(a.saved.pos, a.saved.pos + 3, pos);
        if (a.basis && r && std::equal(r, r + 3, a.set.right) && std::equal(f, f + 3, a.set.fwd) &&
            std::equal(u, u + 3, a.set.up)) {
            std::copy(a.saved.right, a.saved.right + 3, r);
            std::copy(a.saved.fwd, a.saved.fwd + 3, f);
            std::copy(a.saved.up, a.saved.up + 3, u);
        }
    }
    interp_.applied.clear();
    renderCamValid_ = false;
}
