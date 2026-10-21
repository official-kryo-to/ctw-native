// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "sniper.h"
#include "fixedmath.h"
#include "game.h"
#include "sound.h"
#include "weapons.h"
#include "gfx/model.h"
#include "os/pak.h"
#include "gfx/assets.h"
#include "world/props.h"
#include "world/worldrenderer.h"
#include <glad/gl.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>

namespace {
int fastsin(int a) { return (int)(sinf((float)a * 9.587378e-05f) * 4096.f); }
int16_t padAngle(float x, float y) { return (int16_t)(atan2f(x, y) * 10430.f); }

// gGlobalAssetMgr +0x246: the fifth model of resource gGameDir[11] (after the grenade, molotov and rocket ones)
const Model* scopeModel() {
    static bool loaded = false;
    static std::unique_ptr<Model> model;
    if (loaded) return model.get();
    loaded = true;
    std::vector<uint8_t> dir, d, raw;
    uint16_t gd[28];
    if (!Assets_Pak().read(0, dir) || dir.size() < sizeof gd) return nullptr;
    std::memcpy(gd, dir.data(), sizeof gd);
    if (!Assets_Pak().read(gd[11], d) || d.size() < 12) return nullptr;
    const uint16_t id = (uint16_t)(d[10] | d[11] << 8);
    if (id == 0xFFFF) return nullptr;
    model = std::make_unique<Model>();
    if (!Assets_Pak().read(id, raw) || !model->parse(raw)) model.reset();
    return model.get();
}
}   // namespace

void SniperScope::enter(const Game& g) {   // cSniperCam::cSniperCam: 4 units ahead, 30 up, looking down, 30 degrees
    active = true;
    pos[0] = g.player.pos[0] + g.player.fwd[0] * 4;
    pos[1] = g.player.pos[1] + g.player.fwd[1] * 4;
    pos[2] = 0x1E000;
    yaw = (uint16_t)g.player.heading();
    fov = 0x1555;
}

void SniperScope::update(Game& g, float stickX, float stickY) {
    // cIPhonePad::ProcessSniper: the zoom button toggles; the stick's length is the speed (a full stick 1 unit a frame)
    if (zoomPressed) zoomed = !zoomed;
    zoomPressed = false;
    const float len = std::min(1.f, std::sqrt(stickX * stickX + stickY * stickY));
    const int32_t speed = (int32_t)(len * 4096.f);
    // cSniperCam::Update
    pos[2] = 0x1E000;
    if (len > 0.f) {   // the walk angle is relative to the player's heading, which the view is turned to
        const int a = (int16_t)(padAngle(stickX, stickY) + g.player.heading());
        int32_t dx = (int32_t)(((int64_t)speed * fastsin(a)) >> 12);
        int32_t dy = (int32_t)(((int64_t)speed * fastsin(a + 0x4000)) >> 12);
        if (zoomed) {
            dx = (int32_t)(((int64_t)dx * 0x666) >> 12);
            dy = (int32_t)(((int64_t)dy * 0x666) >> 12);
        }
        pos[0] += dx;
        pos[1] += dy;
    }
    yaw = (uint16_t)g.player.heading();
    {   // the aim point stays within the rifle's range of the player
        int32_t d[3] = {pos[0] - g.player.pos[0], pos[1] - g.player.pos[1], 0};
        const int32_t range = Weapons_Info(18).range;
        const int64_t d2 = (((int64_t)d[0] * d[0] + (int64_t)d[1] * d[1]) << 20 >> 20) & ~0xFFFLL;
        if ((int64_t)range * range < d2) {
            const int32_t excess = (int32_t)std::sqrt((double)d2) - range;
            Normalise3(d);
            pos[0] -= (int32_t)(((int64_t)d[0] * excess) >> 12);
            pos[1] -= (int32_t)(((int64_t)d[1] * excess) >> 12);
        }
    }
    if (!zoomed) fov = (int16_t)std::min(fov + 0x38E, 0x1555);   // 5 degrees a frame towards 30 or 20
    else fov = (int16_t)std::max(fov - 0x38E, 0xE39);
}

void SniperScope::toWorldCamera(const Game& g, WorldCamera& cam) const {
    int32_t shake[3] = {0, 0, 0};
    g.camera.shakeOffset(shake);
    for (int k = 0; k < 3; ++k) cam.eye[k] = (pos[k] + shake[k]) / 4096.f;
    cam.setYawPitch(-(int16_t)yaw * 360.f / 65536.f, (int16_t)0xC000 * 360.f / 65536.f);
    cam.fovY = fov * 360.f / 65536.f;
    cam.zNear = 0x666 / 4096.f;
    cam.zFar = 150.f;
}

void SniperScope::renderScope(int width, int height) const {   // cSniperCam::Set: scale, RotZ(heading), 2 under
    const Model* m = scopeModel();
    if (!m || height <= 0) return;
    const float s = (float)(0.155 * width / height);
    const float a = -(int16_t)yaw * 3.14159265f / 32768.f;
    const float c = std::cos(a) * s, sn = std::sin(a) * s;
    const float M[16] = {c, sn, 0, 0, -sn, c, 0, 0, 0, 0, s, 0,
                         pos[0] / 4096.f, pos[1] / 4096.f, (pos[2] - 0x2000) / 4096.f, 1};
    glDisable(GL_DEPTH_TEST);
    PropLibrary::drawModel(*m, M);
    glEnable(GL_DEPTH_TEST);
}

void Sniper_CreateBullet(Game& g, Weapon& w) {
    (void)w;
    g.camera.shake(0x2000, 0x800);   // cCamera::Shake
    const SniperScope& s = g.scope;
    const WeaponInfo& info = Weapons_Info(18);
    auto under = [&](const int32_t p[3], int32_t radius) {   // within 10 units, and the aim point inside its circle
        const int64_t dx = p[0] - s.pos[0], dy = p[1] - s.pos[1];
        if (std::abs(dx) > 0xA000 || std::abs(dy) > 0xA000) return false;
        return (int32_t)std::sqrt((double)(dx * dx + dy * dy)) < radius;
    };
    const int32_t dir[3] = {g.player.fireDir[0], g.player.fireDir[1], g.player.fireDir[2]};
    for (Pedestrians::Ped& p : g.peds.peds) {
        if (p.vehicle || p.deadFrames >= 0 || p.body.dead || !under(p.body.pos, Player::kRadius)) continue;
        g.peds.bulletHit(g, p, info.damage, dir, info.effect, g.player.pos, 1);
        return;
    }
    for (size_t i = 0; i < g.cars.size(); ++i) {
        Vehicle& c = g.cars[i];
        if ((int)i == g.playerCar || c.dead() || !under(c.pos, c.boundRadius())) continue;
        c.damage(info.damage >> 2);   // cVehicle::Damage, type 1: a quarter
        g.vehicleReaction(c, 1, info.damage, 0, g.player.pos);
        return;
    }
}
