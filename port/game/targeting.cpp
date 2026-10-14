// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "targeting.h"
#include "fixedmath.h"
#include "game.h"
#include "gfx/assets.h"
#include "os/pak.h"
#include <glad/gl.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace {
constexpr int16_t kFovCone = 2048;          // PLAYER_FOV_CONE (cSensorCone +8)
constexpr int16_t kHalfFireConeDp = 2896;   // cSensorCone::HalfFireConeDp
constexpr int32_t kPullback = 10240;        // AVERANGE_PULLBACK_VALUE
constexpr int32_t kRange = 163840;          // WeaponRangeForTargetDetection: cPed +0x367 = 0 (cPed::Reset)

int fastsin(int a) { return (int)(sinf((float)a * 9.587378e-05f) * 4096.f); }
int16_t ATan2(int32_t x, int32_t y) {   // ATan2(cFixed, cFixed): atan2f x 10430, clamped to a short
    const int v = (int)(atan2f((float)x, (float)y) * 10430.f);
    return (int16_t)std::clamp(v, -32768, 32767);
}

struct Ent {
    int kind = 0;
    uint32_t uid = 0;
    const int32_t* pos = nullptr;
    const int16_t* fwd = nullptr;
    bool active = false;      // +0x77 bit 2 (and visible, bit 3)
    bool inVehicle = false;   // a ped in a vehicle
    uint32_t vehicle = 0;
    int health = 0;           // the ped's +0x35F / the vehicle's +0x99D
    int pedType = 0;
    int32_t mass = 0;
};

bool resolve(const Game& g, const PlayerTargeting::Ref& r, Ent& e) {
    if (r.kind == 1) {
        for (const Pedestrians::Ped& p : g.peds.peds) {
            if (p.uid != r.uid) continue;
            e.kind = 1; e.uid = p.uid; e.pos = p.body.pos; e.fwd = p.body.fwd;
            e.active = !p.body.dead && !p.body.hidden;   // +0x77 bits 2 and 3: alive and visible (knocked down is fine)
            e.inVehicle = p.vehicle != 0; e.vehicle = p.vehicle;
            e.health = p.body.health; e.pedType = p.type; e.mass = 409;
            return true;
        }
    } else if (r.kind == 2) {
        for (size_t i = 0; i < g.cars.size(); ++i) {
            const Vehicle& c = g.cars[i];
            if (c.uid != r.uid) continue;
            e.kind = 2; e.uid = c.uid; e.pos = c.pos; e.fwd = c.fwd;
            e.active = !c.dead();
            e.health = c.health(); e.mass = c.mass();
            return true;
        }
    }
    return false;
}

struct Cone {   // cSensorCone::sConeInfo
    int32_t pos[3], dir[3];
    int32_t reach() const { return kRange + kPullback; }
    int64_t reach2() const { return (int64_t)reach() * reach(); }
    int64_t reach2Masked() const { return ((reach2() << 20) >> 20) & ~0xFFFLL; }
    void fromApex(const int32_t p[3], int32_t d[3]) const {   // the entity relative to the pulled-back apex
        for (int k = 0; k < 3; ++k) d[k] = p[k] - pos[k] + (int32_t)(((int64_t)dir[k] * kPullback) >> 12);
    }
};

int64_t len2(const int32_t d[3]) { return (int64_t)d[0] * d[0] + (int64_t)d[1] * d[1] + (int64_t)d[2] * d[2]; }

// cSensorCone::Validate for an entity that is not a player (PreValidate leaves both flags clear): 0 or 1
int validate(const Cone& c, const Ent& e, int64_t maxDist2) {
    if (!e.active) return 0;
    int32_t d[3];
    c.fromApex(e.pos, d);
    const int64_t l = len2(d);
    if (!l || maxDist2 < l) return 0;
    Normalise3(d);
    const int32_t dot = (int32_t)(((int64_t)d[0] * c.dir[0] + (int64_t)d[1] * c.dir[1] + (int64_t)d[2] * c.dir[2]) >> 12);
    return dot >= kFovCone ? 1 : 0;
}

// cSensorCone::IsTargetInFireCone (sensorFov: the narrower test with the sensor cone's own angle)
bool inFireCone(const Cone& c, const Ent& e, bool sensorFov) {
    int32_t d[3];
    c.fromApex(e.pos, d);
    const int64_t l = len2(d);
    if (!l || c.reach2Masked() < l) return false;
    if (l < 419430401LL) return true;
    Normalise3(d);
    const int64_t dot = (int64_t)d[0] * c.dir[0] + (int64_t)d[1] * c.dir[1] + (int64_t)d[2] * c.dir[2];
    const int16_t threshold = sensorFov ? kFovCone : std::max(kHalfFireConeDp, kFovCone);   // FireConeFov
    return threshold < (int16_t)((uint32_t)dot >> 12);
}

int band(int32_t d, int32_t r) {   // cSensorCone::GetSensorBand (squared distances >> 12)
    const int32_t t = r / 3;
    if (t >= d) return 0;
    return d <= 2 * t ? 1 : 2;
}

int32_t dist2q(const int32_t a[3], const int32_t b[3]) {
    const int32_t d[3] = {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
    return (int32_t)((uint64_t)len2(d) >> 12);
}

// cSensorCone::GetSensorPriority (lower is more important)
int priority(const Game& g, const Ent* e) {
    if (!e) return 100;
    const bool wanted = false;   // (the wanted level is not ported)
    if (e->kind == 1) return e->pedType == 10 && !wanted ? 7 : 5;
    // vehicles: the driver's ped type, or abandoned
    int driverType = -1;
    bool anyone = g.playerCar >= 0 && g.cars[g.playerCar].uid == e->uid;
    for (const Pedestrians::Ped& p : g.peds.peds) {
        if (p.vehicle != e->uid) continue;
        anyone = true;
        if (p.seat == 0) driverType = p.type;
    }
    if (driverType == 10 && !wanted) return 8;
    if (!anyone) return 9;
    return 6;
}

struct Candidate { PlayerTargeting::Ref ref; Ent ent; };

void candidates(const Game& g, int network, std::vector<Candidate>& out) {   // cWorldEntityIterator, by network
    out.clear();
    if (network == 1) {
        for (const Pedestrians::Ped& p : g.peds.peds) {
            Candidate c{{1, p.uid}, {}};
            if (resolve(g, c.ref, c.ent)) out.push_back(c);
        }
    } else {
        for (size_t i = 0; i < g.cars.size(); ++i) {
            if ((int)i == g.playerCar) continue;
            Candidate c{{2, g.cars[i].uid}, {}};
            if (resolve(g, c.ref, c.ent)) out.push_back(c);
        }
    }
}

uint32_t playerVehicleUid(const Game& g) { return g.playerCar >= 0 ? g.cars[g.playerCar].uid : 0; }
}   // namespace

void PlayerTargeting::reset() { target = targeted = {}; turning = false; locked = false; }

bool PlayerTargeting::targetInfo(const Game& g, int32_t pos[3], int& health) const {
    Ent e;
    if (!target || !resolve(g, target, e)) return false;
    std::copy(e.pos, e.pos + 3, pos);
    health = e.health;
    return true;
}

bool PlayerTargeting::handleAiming(Game& g, bool fireHeld) {
    Player& pl = g.player;
    Cone cone;
    for (int k = 0; k < 3; ++k) { cone.pos[k] = pl.pos[k]; cone.dir[k] = pl.fwd[k]; }
    const int curSlot = g.playerWeapons.cur;
    const int weapon = g.playerWeapons.current() ? g.playerWeapons.current()->type : 0;
    const bool noAuto = !enabled || weapon == 17;
    // cPlayerOnFoot::HandleStrafe (touch controls, no lock-on button): holding fire with a sensor target locks it
    // (state 0 -> 1: SetTargetedEntity), the lock follows the sensor's target while fire stays down, and letting go,
    // or the target going away, releases it (SetTargetedEntity(nullptr)).
    if (!fireHeld || noAuto || !target) targeted = {};
    else targeted = target;

    // cPlayerSensorCone::CheckTarget (sensor fov)
    auto checkTarget = [&]() {
        Ent e;
        if (!target) return;
        if (!resolve(g, target, e)) { target = {}; return; }
        const int32_t d[3] = {e.pos[0] - cone.pos[0], e.pos[1] - cone.pos[1], e.pos[2] - cone.pos[2]};
        const int64_t l = len2(d);
        if (cone.reach2Masked() < l || !e.active) { target = {}; return; }
        if (l >= 419430401LL && (((int64_t)cone.dir[0] * d[0] + (int64_t)cone.dir[1] * d[1] + (int64_t)cone.dir[2] * d[2]) &
                                 0x80000000000LL)) { target = {}; return; }   // behind the player
        if (!inFireCone(cone, e, true)) { target = {}; return; }
        if (e.kind == 1 && e.inVehicle) target = {2, e.vehicle};   // a ped in a vehicle: the vehicle
        if (target.kind == 2 && target.uid == playerVehicleUid(g)) target = {};
    };
    // cPlayerSensorCone::HandleTarget
    auto handleTarget = [&](const Candidate& c, int validity) {
        checkTarget();
        const int pri = priority(g, &c.ent);
        if (validity == 2 && pri > 2) return;
        bool better = true;
        Ent cur;
        if (target && resolve(g, target, cur)) {
            const int32_t r = (int32_t)((uint64_t)((int64_t)cone.reach() * cone.reach()) >> 12);
            const int bandNew = band(dist2q(c.ent.pos, pl.pos), r), bandCur = band(dist2q(cur.pos, pl.pos), r);
            const int curPri = priority(g, &cur);
            better = pri < curPri || (pri == curPri && bandNew < bandCur);
        }
        if (targeted) { if (targeted == c.ref && better) target = c.ref; }
        else if (better) target = c.ref;
    };
    // cPlayerSensorCone::FillUp
    std::vector<Candidate> list;
    auto fillUp = [&](int network) {
        const int32_t half = cone.reach() >> 1;
        int32_t centre[3];
        for (int k = 0; k < 3; ++k)
            centre[k] = cone.pos[k] - (int32_t)(((int64_t)cone.dir[k] * kPullback) >> 12) +
                        (int32_t)(((int64_t)cone.dir[k] * half) >> 12);
        candidates(g, network, list);
        for (const Candidate& c : list) {
            if (c.ref == target) continue;
            if (c.ent.kind == 1 && c.ent.inVehicle) continue;   // (closed vehicles)
            if (c.ent.kind == 2 && c.ent.uid == playerVehicleUid(g)) continue;
            const int32_t d[3] = {c.ent.pos[0] - centre[0], c.ent.pos[1] - centre[1], c.ent.pos[2] - centre[2]};
            if (len2(d) > (int64_t)half * half) continue;        // cWorldIterator::SetRadius
            const int v = validate(cone, c.ent, cone.reach2());
            if (v) handleTarget(c, v);
        }
    };
    // cPlayerSensorCone::Update
    checkTarget();
    fillUp(1);
    if (curSlot >= 2) fillUp(0);

    // cPlayerSensorCone::GetBestTarget while fire is held (not for the RPG: the yoke's +0x94)
    if (!noAuto && fireHeld) {
        Ent cur;
        const bool haveCur = target && resolve(g, target, cur);
        Ref best = haveCur ? target : Ref{};
        Ent bestEnt = haveCur ? cur : Ent{};
        const int32_t r = (int32_t)((uint64_t)((int64_t)cone.reach() * cone.reach()) >> 12);
        bool found = false;
        for (int b = 0; b <= 2 && !found; ++b) {
            for (int network = 1; network >= 0; --network) {
                if (found) continue;
                candidates(g, network, list);
                for (const Candidate& c : list) {
                    if (found) break;
                    if (c.ref == target) continue;
                    if (c.ent.kind == 1 && c.ent.inVehicle) continue;
                    if (c.ent.kind == 2 && (c.ent.uid == playerVehicleUid(g) || curSlot < 2)) continue;
                    const int v = validate(cone, c.ent, cone.reach2());
                    if (!v) continue;
                    if (band(dist2q(c.ent.pos, pl.pos), r) != b) continue;
                    const int pri = priority(g, &c.ent);
                    if (v == 2 && pri > 2) continue;
                    if (priority(g, best ? &bestEnt : nullptr) > pri) { best = c.ref; bestEnt = c.ent; found = pri < 3; }
                }
            }
        }
        if (best) { target = best; targeted = best; }
    }

    // the fire direction: towards a target in the fire cone, else the facing
    Ent t;
    const bool inCone = !noAuto && target && resolve(g, target, t) && inFireCone(cone, t, false);
    int32_t x, y, z;
    bool valid;
    if (inCone) {   // cSensorCone::DirectionToTarget
        int32_t d[3] = {t.pos[0] - pl.pos[0], t.pos[1] - pl.pos[1], t.pos[2] - pl.pos[2]};
        if (len2(d) >= 167773) Normalise3(d);
        else for (int k = 0; k < 3; ++k) d[k] = t.fwd[k];
        x = d[0]; y = d[1]; z = d[2];
        valid = true;
        turning = true;
    } else {
        x = pl.fwd[0]; y = pl.fwd[1]; z = pl.fwd[2];
        valid = false;
    }
    if (turning) {   // a quarter of the remaining angle a frame
        const int16_t from = ATan2(-pl.fireDir[0], pl.fireDir[1]);
        const int16_t diff = (int16_t)(from - ATan2(-x, y));
        if (!((uint16_t)(diff - 364) < 0xFD29u || valid)) turning = false;
        const int16_t a = (int16_t)((((int)diff + (((uint32_t)(int)diff >> 29) & 3)) >> 2) - from);
        int32_t v[2] = {fastsin(a), fastsin(a + 0x4000)};
        Normalise2(v);
        x = v[0]; y = v[1];
    }
    pl.fireDir[0] = (int16_t)x; pl.fireDir[1] = (int16_t)y; pl.fireDir[2] = (int16_t)z;
    locked = inCone;
    return inCone;
}

void PlayerTargeting::render(Game& g) const {   // cPlayer::RenderTarget: around cPlayer +0x1120, the locked target
    if (!targeted || g.playerCar >= 0) return;
    const Weapon* w = g.playerWeapons.current();
    if (!w || w->type == 18) return;   // not with the sniper rifle
    Ent e;
    if (!resolve(g, targeted, e)) return;
    static unsigned tex = 0;
    static bool loaded = false;
    if (!loaded) {   // gGlobalAssetMgr +0x24A: resource gGameDir[13] +2
        loaded = true;
        std::vector<uint8_t> dir, d;
        uint16_t gd[28];
        if (Assets_Pak().read(0, dir) && dir.size() >= sizeof gd) {
            memcpy(gd, dir.data(), sizeof gd);
            if (Assets_Pak().read(gd[13], d) && d.size() >= 4) tex = Assets_Texture(d[2] | d[3] << 8);
        }
    }
    const int h = std::clamp(e.health, 0, 255);
    int v15 = (int)((4063232u * (uint32_t)h) >> 24);
    if (v15 >= 31) v15 = 31;
    const uint16_t colour = (uint16_t)((33 * v15) ^ 0x1F);
    const float a = (e.mass <= 6144 ? 9830 : 12288) / 4096.f;
    const float z = (e.pos[2] <= 40960 ? 2048 : 4096) / 4096.f;
    const float yaw = g.cameraYaw() * 3.14159265f / 32768.f, cs = std::cos(yaw), sn = std::sin(yaw);
    const float o[3] = {e.pos[0] / 4096.f, e.pos[1] / 4096.f, e.pos[2] / 4096.f + z};
    const float uv = 1920.f / 2048.f;
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_LIGHTING);
    glDisable(GL_CULL_FACE);
    if (tex) { glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, tex); } else glDisable(GL_TEXTURE_2D);
    glColor4f((colour & 31) / 31.f, ((colour >> 5) & 31) / 31.f, ((colour >> 10) & 31) / 31.f, 30.f / 31.f);
    // four quads from the centre to each corner: (x1, y1) u = 1, (corner) u = v = 1, (x2, y2) v = 1
    const float q[4][6] = {{a, 0, a, a, 0, a}, {0, -a, a, -a, a, 0}, {-a, 0, -a, -a, 0, -a}, {0, a, -a, a, -a, 0}};
    auto put = [&](float lx, float ly, float u, float v) {
        glTexCoord2f(u, v);
        glVertex3f(o[0] + lx * cs - ly * sn, o[1] + lx * sn + ly * cs, o[2]);
    };
    glBegin(GL_QUADS);
    for (const auto& s : q) {
        put(0, 0, 0, 0);
        put(s[0], s[1], uv, 0);
        put(s[2], s[3], uv, uv);
        put(s[4], s[5], 0, uv);
    }
    glEnd();
    glColor4f(1, 1, 1, 1);
    glEnable(GL_CULL_FACE);
}
