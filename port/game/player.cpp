// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "player.h"
#include "gfx/pedsprites.h"
#include "world/collision.h"
#include "vehicle.h"
#include <glad/gl.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

static int fastsin(int a) { return (int)(sinf((float)a * 9.587378e-05f) * 4096.f); }
static inline int32_t mulq(int64_t a, int64_t b) { return (int32_t)((a * b) >> 12); }

// ATan2(cFixed, cFixed): atan2f * 10430, clamped to a short
static int16_t atan2q(int32_t y, int32_t x) {
    int v = (int)(atan2f((float)y, (float)x) * 10430.f);
    return (int16_t)std::clamp(v, -0x8000, 0x7FFF);
}

static void normalise(int32_t v[3]) {   // Normalise(tv3d)
    double l2 = (double)v[0] * v[0] + (double)v[1] * v[1] + (double)v[2] * v[2];
    if (l2 <= 0) return;
    float inv = 1.f / std::sqrt((float)l2);
    for (int i = 0; i < 3; ++i) {
        float f = inv * (float)v[i];
        v[i] = (int32_t)((f >= 0 ? 0.5f : -0.5f) + f * 4096.f);
    }
}

// PED_BASE_SPEED, the speed per level (DAT_0115f750), the player's hold-type factor (DAT_0115f770[1]),
// PED_BASE_ACCELFRAME >> 12
static const int32_t kBaseSpeed = 819;
static const int32_t kLevelMul[4] = {0, 0x1000, 0x2000, 0x3000};
static const int32_t kHoldMul = 0x1000;
static const int kAccelFrames = 8;
static const int32_t kGravity = 0x1ED0;   // per frame, units per second

int16_t Player::heading() const { return (int16_t)-atan2q(-fwd[0], fwd[1]); }

int32_t Player::currentSpeed() const {
    if (level_ == 0) return 0;
    int32_t s = (int32_t)(((int64_t)kLevelMul[level_] * kBaseSpeed) >> 12);   // unarmed: weapon factor 1.0
    if (level_ != 3) s = (int32_t)(((int64_t)s * kHoldMul) >> 12);
    return s * 30;
}

void Player::placeOnGround(Collision* col) {
    if (!col || !col->ok()) return;
    Collision::Ground g = col->ground(pos[0] / 4096.f, pos[1] / 4096.f, pos[2] / 4096.f + 100.f);
    pos[2] = (int32_t)lroundf(g.z * 4096.f);
    vel[2] = 0;
    onGround_ = true;
}

// cPed::UpdateSpeed (the player: no aiming, unarmed)
void Player::updateSpeed(const Input& in) {
    uint32_t turnLimit = (!in.sprint && exhaustion_ < 0xB) ? 19000 : 32000;
    int32_t diff = (int16_t)(heading() - in.heading);
    uint32_t absDiff = (uint32_t)std::abs(diff);
    auto slowDown = [&] {   // LAB_0097fc8c
        if (level_ == 0) slowing_ = false;
        else if (!slowing_) { slowing_ = true; counter_ = 1; }
        else if (counter_ > 2) { counter_ = 0; --level_; }
        else ++counter_;
    };
    auto rampUp = [&] {     // LAB_0097fc40
        bool resetCounter = false;
        if (in.moving && turnLimit <= absDiff) { level_ = 0; resetCounter = true; }   // turning sharply: stop
        else if (slowing_) { slowing_ = false; resetCounter = true; }
        if (resetCounter) counter_ = 0;
        if (level_ == 0) {
            if (counter_ > 1) { ++level_; counter_ = 0; } else ++counter_;
        } else if (counter_ < kAccelFrames) ++counter_;
        else if (level_ < 3) { ++level_; counter_ = 0; }
        else counter_ = 0;
        if (level_ > in.maxLevel) level_ = in.maxLevel;   // yoke +0xAD / +0xAE
    };
    if (!in.moving && !in.sprint && exhaustion_ < 0xB) slowDown();
    else if (!slowing_ || level_ <= 1) rampUp();
    else slowDown();

    // cPlayer: sprint keeps going for 10 frames after the button is released
    if (exhaustion_ == 0) {
        if (in.sprintReleased) exhaustion_ = 10;
    } else {
        int e = exhaustion_ - 1;
        exhaustion_ = (uint8_t)(e == 10 ? 0 : e);
        if (in.sprintReleased) exhaustion_ = 20;
    }
    if (level_ != 3) return;
    bool canRun = !in.sprint && exhaustion_ < 10;
    if (canRun) level_ = 2;                        // no sprint button: top speed is run
    else if (exhaustion_ <= 9) exhaustion_ = 10;
}

// cEntity::TurnTo(short, short)
void Player::turnTo(int16_t target, int16_t maxStep) {
    int16_t cur = atan2q(-fwd[0], fwd[1]);   // = -heading
    int16_t d = (int16_t)(cur + target);
    int16_t step = maxStep;                  // << time slice (0 for the player)
    if (d > step) d = step;
    if (d < -step) d = (int16_t)-step;
    int16_t h = (int16_t)(d - cur);
    fwd[0] = (int16_t)fastsin(h);
    fwd[1] = (int16_t)fastsin(h + 0x4000);
    fwd[2] = 0;
}

void Player::update(const Input& in, Collision* col, const PedSprites* sprites) {
    act(in, sprites);
    advance(col);
}

void Player::act(const Input& in, const PedSprites* sprites) {
    if (reacting()) { actReaction(sprites); return; }
    // cPed::Act
    updateSpeed(in);
    animate(sprites);
    if (onGround_) {   // velocity from the facing, only while standing on something
        int32_t sp = (int32_t)(currentSpeed() * speedScale);
        int32_t v[3] = {mulq(sp, fwd[0]), mulq(sp, fwd[1]), vel[2] + mulq(sp, fwd[2])};
        if (groundNormal[2] != 0x1000) {   // along the slope
            int64_t dot = (int64_t)v[0] * groundNormal[0] + (int64_t)v[1] * groundNormal[1] + (int64_t)groundNormal[2] * v[2];
            int32_t k = (int32_t)(((dot >> 12) * 0xFD800000LL) >> 32);
            for (int i = 0; i < 3; ++i) v[i] -= mulq(k, groundNormal[i]);
            if ((int64_t)v[1] * v[1] + (int64_t)v[0] * v[0] + (int64_t)v[2] * v[2] > 0x640) {
                normalise(v);
                for (int i = 0; i < 3; ++i) v[i] = mulq(v[i], sp);
            }
        }
        vel[0] = v[0]; vel[1] = v[1]; vel[2] = v[2];
    }
    if (in.moving) turnTo(in.heading, 4000);   // cSimpleMover::UpdateForSimpleMovement

}

void Player::advance(Collision* col, bool avoidWater) {
    // cPed::Integrate (swimming is not ported: don't let walkers walk into water)
    int32_t before[3] = {pos[0], pos[1], pos[2]};
    integrate(col);
    blockedByWater = false;
    if (avoidWater && !reacting() && col && col->ok()) {
        Collision::Ground g = col->ground(pos[0] / 4096.f, pos[1] / 4096.f, pos[2] / 4096.f + 0.01f);
        if (g.surface == 2 && pos[2] - (int32_t)lroundf(g.z * 4096.f) < 0x999) {
            pos[0] = before[0]; pos[1] = before[1]; pos[2] = before[2];
            vel[0] = vel[1] = vel[2] = 0;
            onGround_ = true;
            blockedByWater = true;
        }
    }
}

void Player::dynamicContact(const int32_t normal[3], const int32_t otherVelocity[3], int32_t depth) {
    // cPedCollisionData::OnDynamicCollision: an impulse in relative velocity,
    // not a positional teleport. Separating contacts do not add energy.
    int64_t dot = 0;
    for (int i = 0; i < 3; ++i) dot += (int64_t)normal[i] * (vel[i] - otherVelocity[i]);
    const int32_t closing = (int32_t)(dot >> 12);
    if (closing >= 0) return;
    const int32_t impulse = (int32_t)(((int64_t)closing * 0x102800000LL) >> 32);
    for (int i = 0; i < 3; ++i) vel[i] += mulq(normal[i], depth) - mulq(normal[i], impulse);
    clearance_ = 0;
}

bool Player::vehicleImpact(const Vehicle& car, const int32_t normal[3], bool isPlayer) {
    if (dead || attached || hidden) return false;
    // cPed::ApplyCollisionEffect: only the vehicle's central width/front can
    // apply damage; the surrounding dynamic sphere is just contact.
    int32_t rel[3]; int64_t across = 0, along = 0, forwardVelocity = 0;
    for (int i = 0; i < 3; ++i) {
        rel[i] = pos[i] - car.pos[i];
        across += (int64_t)rel[i] * car.right[i];
        along += (int64_t)rel[i] * car.fwd[i];
        forwardVelocity += (int64_t)car.vel[i] * car.fwd[i];
    }
    across >>= 12; along >>= 12;
    if (std::abs(across) > car.hx ||
        (forwardVelocity < 0 ? -along : along) * 4096 < 0x4CC000LL - (int64_t)car.hy * 2048 ||
        (std::abs(along) < car.hy - 0x333 && std::abs(across) > (int64_t)car.hx * 3 / 4)) return false;
    int32_t velocity[3]; car.velocityAtWorldPos(pos, velocity);
    int64_t v2 = 0;
    for (int32_t v : velocity) v2 += (int64_t)v * v;
    const int64_t squared = v2 >> 12;
    if (squared <= 0x51000) return false; // 9 units/s, not gentle contact's 1.5 units/s
    const bool high = squared > 0x271000; // 25 units/s
    const int damage = high ? (isPlayer ? 15 : 255)
                            : std::clamp((int)std::sqrt((double)(v2 & ~0xFFFLL)) / 4030, 10, 50) / 2;
    health = std::max(0, health - damage);
    dead = health == 0;
    if (reacting()) return false; // ApplyDamageResponse does not replace an existing cAnimation task
    reactionAge_ = 0;
    if (dead) {
        // SetHealth(0) calls SetDead first. Its pending effect-4 animation is
        // visible to FindTask, so Damage cannot add a second impact task.
        // The default death direction is opposite the current facing.
        reactionForward_ = false;
        reactionPose(2);
        return true;
    }
    int32_t direction[3] = {normal[0], normal[1], 0}; normalise(direction);
    reactionForward_ = (int64_t)direction[0] * fwd[0] + (int64_t)direction[1] * fwd[1] > 0;
    // Damage's animation strength is 20, independently of the damage amount.
    // Physical effect 3 applies a 40-unit/s outward and 20-unit/s upward
    // impulse. In this Android build effect 2 computes a proposed impulse,
    // but returns after SetPos without SetVelocity: preserve contact velocity.
    if (high) {
        for (int i = 0; i < 2; ++i) vel[i] = direction[i] * 40;
        vel[2] = 20 * 4096;
    } else pos[2] += 2 * 4096;
    onGround_ = false; airFrames_ = 1;
    reactionPose(high ? 7 : 14);
    return true;
}

bool Player::takeDamage(int damage) {
    if (dead || attached || hidden) return false;
    health = std::max(0, health - damage);
    dead = health == 0;
    if (!dead || reacting()) return dead;
    reactionAge_ = 0;
    reactionForward_ = false;   // SetDead: ApplyDamageResponse(4) against the facing
    reactionPose(2);
    return true;
}

bool Player::explosionHit(int damage, const int32_t direction[3]) {
    if (dead || attached || hidden) return false;
    const bool hadReaction = reacting();
    takeDamage(damage);
    if (hadReaction) return false;
    if (dead) return true; // SetDead's effect-4 task takes priority over the blast response
    reactionAge_ = 0;
    reactionForward_ = (int64_t)direction[0]*fwd[0] + (int64_t)direction[1]*fwd[1] + (int64_t)direction[2]*fwd[2] > 0;
    // ApplyPhysicalEffect(3) uses the animation's heading, not the length or height of the damage vector.
    const int16_t angle = atan2q(direction[0], direction[1]);
    vel[0] = fastsin(angle) * 40;
    vel[1] = fastsin(angle + 0x4000) * 40;
    vel[2] = 20 * 4096;
    onGround_ = false; airFrames_ = 1;
    reactionPose(7);
    return true;
}

void Player::reactionPose(int state) {
    reaction_ = state; reactionTicks_ = 0;
    if (state == 5) recoveryAge_ = 0;
    // cAnimation::SetupForState: direction relative to the existing facing
    // selects front/back variants; the ped does not snap to face the car.
    int upper = 0x18;
    switch (state) {
    case 7: upper = reactionForward_ ? 0x2C : 0x2E; break;
    case 14: upper = reactionForward_ ? 0x32 : 0x30; break;
    case 2: upper = reactionForward_ ? 0x1A : 0x18; break;
    case 5: upper = reactionForward_ ? 0x1E : 0x1C; break;
    case 1: upper = 0x12; break;
    }
    playOneShot(upper, upper + 1, false);
}

void Player::actReaction(const PedSprites* sprites, bool vehicleAbove) {
    if (!reacting()) return;
    ++reactionTicks_;
    if (reactionAge_ < UINT32_MAX) ++reactionAge_;
    if ((reaction_ == 5 || reaction_ == 1) && recoveryAge_ < UINT32_MAX) ++recoveryAge_;
    const bool done = stepOneShot(sprites);
    if ((reaction_ == 7 || reaction_ == 14) && done) reactionPose(2);
    else if (reaction_ == 2 && reactionTicks_ >= 30 && !dead && onGround_ && vel[2] <= 0 && !vehicleAbove) reactionPose(5);
    else if (reaction_ == 5 && done) reactionPose(dead ? 2 : 1);
    else if (reaction_ == 1 && done) { if (dead) reactionPose(2); else { resetReaction(); return; } }
    // cAnimation::ApplyPhysics: low fallen sprites use flat drag. Airborne
    // impacts use quadratic drag (4 in the air, 40 on the ground).
    const int64_t v2 = (int64_t)vel[0]*vel[0] + (int64_t)vel[1]*vel[1];
    if (v2 < 0x400000) vel[0] = vel[1] = 0;
    else if (reaction_ == 2 || reaction_ == 5 || reaction_ == 1) {
        const int32_t speed = (int32_t)std::sqrt((double)v2), drag = std::min(speed, 0xAA0);
        for (int i = 0; i < 2; ++i) vel[i] -= mulq((int32_t)((int64_t)vel[i] * 4096 / speed), drag);
    } else {
        const int64_t s = v2 >> 12;
        const int64_t k = std::clamp((((s * 17) >> 9) * (onGround_ ? 40 : 4)) >> 12, (int64_t)0x28, (int64_t)0xFAE);
        for (int i = 0; i < 2; ++i) vel[i] = mulq(vel[i], 4096 - k);
    }
}

void Player::resetReaction() {
    reaction_ = -1; reactionTicks_ = 0; reactionAge_ = recoveryAge_ = 0; oneShotTicks_ = 0;
    animUpper_ = animLegs_ = -1; level_ = counter_ = 0;
}

void Player::integrate(Collision* col) {
    int32_t speed = (int32_t)std::sqrt((double)vel[0] * vel[0] + (double)vel[1] * vel[1] + (double)vel[2] * vel[2]);
    int32_t moveLen = (int32_t)(((int64_t)speed * 0x11) >> 9);
    int32_t vz0 = vel[2], z0 = pos[2];
    auto freeMove = [&] { for (int i = 0; i < 3; ++i) pos[i] += (int32_t)(((int64_t)vel[i] * 0x11) >> 9); };
    if (!col || !col->ok()) { freeMove(); if (vz0 != 0 || !onGround_) vel[2] -= kGravity; return; }

    bool full = vz0 != 0;
    if (!full) {   // still clear of everything seen last time: move without testing
        double dx = pos[0] - lastPos_[0], dy = pos[1] - lastPos_[1], dz = pos[2] - lastPos_[2];
        full = clearance_ <= (int32_t)std::sqrt(dx * dx + dy * dy + dz * dz) + moveLen;
    }
    if (!full) {
        freeMove();
        if (!onGround_) vel[2] -= kGravity;
    } else {
        bool walkable = false, hit = false;
        constrainByCollision(col, moveLen, walkable, hit);
        // cPed::Integrate's vertical/full path applies gravity after the
        // constrained move. Being within 0.6 units of ground does not cancel
        // it: that distance marks standing, it is not a vertical impulse.
        const bool appliedGravity = vz0 != 0;
        if (appliedGravity) vel[2] -= kGravity;
        if (vz0 < 0 && hit && std::abs(z0 - pos[2]) < 0x28) walkable = true;
        Collision::Ground g = col->ground(pos[0] / 4096.f, pos[1] / 4096.f, (pos[2] + 0x28) / 4096.f);
        int32_t gz = (int32_t)lroundf(g.z * 4096.f);
        for (int i = 0; i < 3; ++i) groundNormal[i] = (int16_t)lroundf(g.normal[i] * 4096.f);
        if (!walkable || g.surface == 2) {
            if (pos[2] - gz < 0x999) onGround_ = true;
            else { if (!appliedGravity) vel[2] -= kGravity; onGround_ = false; }
        } else onGround_ = true;
    }
    // cPed::Integrate, player part: one frame of grace when leaving the ground, and a ped hanging still in the
    // air for 10 frames counts as standing
    if (playerControlled && !reacting() && !onGround_) {
        if (airFrames_++ == 0) {
            int32_t v = vel[2] + kGravity;
            vel[2] = v != 0 ? v : vel[2] + 0x1EA8;
            onGround_ = true;
        }
    } else airFrames_ = 0;
    if (playerControlled && !reacting() && !onGround_ && std::abs(vel[2]) < 0x1000) {
        if (stuckFrames_ == 10) onGround_ = true;
        else ++stuckFrames_;
        return;
    }
    stuckFrames_ = 0;
}

// cPed::ConstrainByCollision (the static world and stationary vehicles).
void Player::constrainByCollision(Collision* col, int32_t moveLen, bool& walkable, bool& hit) {
    walkable = hit = false;
    const int32_t rS = kSphere;
    int32_t C[3] = {pos[0], pos[1], pos[2] + rS};   // sphere centre
    int32_t Rq = (int32_t)(((int64_t)kRadius * 0xB33 + (int64_t)moveLen * 0x4000) >> 12) + 0x1828;
    int32_t vz0 = vel[2];
    Collision::Candidates cand;
    col->candidates(C, Rq, vz0 != 0, cand);
    for (const Collision::Box& b : obstacles) {   // (the world entity iterator: at most 32, within reach)
        if (cand.boxes.size() >= 0x40) break;
        int64_t dx = b.cx - C[0], dy = b.cy - C[1], dz = b.cz - C[2];
        int64_t rr = (int64_t)Rq + (int32_t)std::sqrt((double)b.hx * b.hx + (double)b.hy * b.hy + (double)b.hz * b.hz);
        if (dx * dx + dy * dy + dz * dz <= rr * rr) cand.boxes.insert(cand.boxes.begin(), &b);
    }

    if (cand.cyls.empty() && cand.tris.empty() && vz0 == 0) {
        bool any = false;
        for (const Collision::Box* b : cand.boxes) {
            int32_t c[3], n[3], d;
            if (Collision::sphereVBox(C, Rq, *b, c, n, d)) { any = true; break; }
        }
        if (!any) {   // nothing near: move freely and remember how much room there is
            clearance_ = Rq - rS;
            lastPos_[0] = pos[0]; lastPos_[1] = pos[1]; lastPos_[2] = pos[2];
            for (int i = 0; i < 3; ++i) pos[i] += (int32_t)(((int64_t)vel[i] * 0x11) >> 9);
            return;
        }
    }
    lastPos_[0] = lastPos_[1] = lastPos_[2] = 0;

    int32_t v[3];   // this frame's move
    for (int i = 0; i < 3; ++i) v[i] = (int32_t)(((int64_t)vel[i] * 0x11) >> 9);
    struct Contact { int32_t p[3]; };
    std::vector<Contact> contacts;
    auto sweep = [&](const int32_t* from, const int32_t* to, int order) -> int32_t {
        // order 0: boxes, cylinders, triangles (the slide loop); 1: boxes, triangles, cylinders (the step down)
        contacts.clear();
        int32_t best = 0x7FFFFFFF;
        auto add = [&](const int32_t* p, int32_t t) {
            if (t > best) return;
            if (t < best) contacts.clear();
            contacts.push_back({{p[0], p[1], p[2]}});
            best = t;
        };
        int32_t p[3], n[3], t;
        auto boxes = [&] { for (const Collision::Box* b : cand.boxes) if (Collision::sweptSphereVBox(from, to, rS, *b, p, t)) add(p, t); };
        auto cyls = [&] { for (const Collision::Cyl* c : cand.cyls) if (Collision::sweptSphereVCylinder(from, to, rS, *c, p, n, t)) add(p, t); };
        auto tris = [&] { for (const Collision::TriRef& tr : cand.tris) if (Collision::sweptSphereVTri(from, to, rS, tr, p, n, t)) add(p, t); };
        boxes();
        if (order == 0) { cyls(); tris(); } else { tris(); cyls(); }
        return best;
    };

    const int iterations = playerControlled ? 3 : 2;   // cPlayer (2 for other peds)
    int32_t frac = 0x1000;
    for (int it = 0; it < iterations; ++it) {
        int32_t E[3];
        for (int i = 0; i < 3; ++i) E[i] = C[i] + mulq(v[i], frac);
        int32_t best = sweep(C, E, 0);
        if (contacts.empty()) { C[0] = E[0]; C[1] = E[1]; C[2] = E[2]; break; }
        int64_t tt = (best > 0x27 || it != iterations - 1) ? best : 0x28;
        int32_t N[3];
        for (int i = 0; i < 3; ++i) N[i] = C[i] + (int32_t)((tt * (E[i] - C[i])) >> 12);
        for (const Contact& ct : contacts) {   // slide: remove the velocity into each contact
            int32_t n[3] = {N[0] - ct.p[0], N[1] - ct.p[1], N[2] - ct.p[2]};
            normalise(n);
            if (std::abs(n[2]) < 0x73F) { n[2] = 0; normalise(n); }   // DAT_0115f788: steep contacts are walls
            hit = true;
            if (n[2] > 0xB33) walkable = true;
            int64_t dot = (int64_t)n[0] * v[0] + (int64_t)n[2] * v[2] + (int64_t)n[1] * v[1];
            for (int i = 0; i < 3; ++i) v[i] += (int32_t)(((int64_t)n[i] * 5) >> 9);
            if (dot < 0) {
                int32_t k = (int32_t)(((dot >> 12) * 0x102800000LL) >> 32);
                for (int i = 0; i < 3; ++i) v[i] -= mulq(k, n[i]);
            }
        }
        C[0] = N[0]; C[1] = N[1]; C[2] = N[2];
        frac -= (int32_t)((tt * frac) >> 12);
    }

    // cPlayer: follow the ground down (sweep 1.5 units down; if everything hit is walkable, go there next frame)
    if (playerControlled && vz0 != 0) {
        int32_t D[3] = {C[0], C[1], C[2] - 0x1800};
        int32_t best = sweep(C, D, 1);
        if (!contacts.empty()) {
            int32_t H[3];
            for (int i = 0; i < 3; ++i) H[i] = C[i] + (int32_t)(((int64_t)(D[i] - C[i]) * best) >> 12);
            bool allWalkable = true;
            for (const Contact& ct : contacts) {
                int32_t n[3] = {H[0] - ct.p[0], H[1] - ct.p[1], H[2] - ct.p[2]};
                normalise(n);
                if (n[2] < 0xB33) { allWalkable = false; break; }
            }
            if (allWalkable) {
                v[2] = (int32_t)(((int64_t)(H[2] - C[2]) * 0x88) >> 12);
                hit = walkable = true;
            }
        }
    }
    // cPlayer: push out of anything still overlapping (a fifth of the depth per frame)
    if (playerControlled) for (const Collision::Box* b : cand.boxes) {
        int32_t c[3], n[3], d;
        if (Collision::sphereVBox(C, rS, *b, c, n, d)) {
            int32_t k = (int32_t)(((int64_t)d * 0x33300000LL) >> 32);
            for (int i = 0; i < 3; ++i) v[i] += mulq(k, n[i]);
        }
    }
    if (playerControlled) for (const Collision::TriRef& tr : cand.tris) {
        int32_t c[3], n[3], d;
        if (Collision::sphereVTri(C, rS, tr, c, n, d)) {
            int32_t k = (int32_t)(((int64_t)d * 0x33300000LL) >> 32);
            for (int i = 0; i < 3; ++i) v[i] += mulq(k, n[i]);
        }
    }
    for (int i = 0; i < 3; ++i) vel[i] = v[i] * 0x1E;
    pos[0] = C[0]; pos[1] = C[1]; pos[2] = C[2] - rS;
}

// cPed::AnimateWalkRunCycle (unarmed, no reactions)
// cPed::AnimateWalkRunCycleWeapons (the player never has +0x210 set: his fire chance is 100): the upper-body
// animation for the held weapon. hold: show its first frame (it was fired this frame); sync: it steps with the
// walk speed like the legs instead of at the fixed rate.
static int weaponUpper(int base, int up, int type, int state, uint32_t flags, bool& hold, bool& sync, uint8_t& punch) {
    hold = sync = false;
    if (type == 0) {   // fists: a punch while the swing runs (cPlayer::GetMeleeAnim; the combo is not ported)
        bool swing = false, fresh = false;
        if (state) {
            const bool later = !(flags & 2) && state != 1;
            if (!(flags & 1)) { swing = true; fresh = !later; }
            else if (!later) { swing = true; fresh = true; }
        } else if (flags & 2) { swing = true; fresh = true; }
        if (!swing) return up;
        if (fresh) punch = ((punch + 1) & 0xFE) ? 0 : punch + 1;
        hold = fresh;
        return base + (punch == 1 ? 243 : 233);
    }
    const bool firing = state == 1 || state == 2 || (flags & 6);
    const bool reloading = state == 4 || (flags & 0x10);
    const bool fireHold = state == 1 || ((flags >> 1) & 1);
    switch (type) {
        case 1: case 2:   // nightstick, baseball bat
            if ((state && (state == 1 || !(flags & 1))) || (flags & 2)) { hold = fireHold; return base + 256; }
            sync = true;
            return base + 257;
        case 3:   // sword
            if ((state && (state == 1 || !(flags & 1))) || (flags & 2)) { hold = fireHold; return base + 232; }
            return base + 214;
        case 4:   // chainsaw
            if (!state || (flags & 1)) { sync = true; return base + 229; }
            return base + 230;
        case 5: case 7: case 19:   // pistol, revolver, taser
            if (reloading) up = base + 220;
            if (!firing) { hold = (flags >> 1) & 1; return up; }
            hold = fireHold;
            return base + 234;
        case 6:   // twin pistols
            if (firing) return base + 231;
            if (reloading) return base + 221;
            return up;
        case 8: case 9:   // micro SMG, SMG
            if (firing) return base + 239;
            if (reloading) return base + 222;
            sync = true;
            return up;
        case 10: case 11: case 12: {   // shotguns
            int a = base + 238;
            if (!firing) {
                a = base + 223;
                if (!reloading) { sync = true; a = base + 236; }
            }
            hold = fireHold;
            return a;
        }
        case 13: case 14:   // assault rifle, carbine rifle
            if (firing) return base + 237;
            if (!reloading) { sync = true; return base + 236; }
            return base + 222;
        case 15:   // minigun
            if (firing) return base + 242;
            if (!reloading) { sync = true; return base + 241; }
            return base + 223;
        case 16:   // flame thrower
            if (!reloading) { sync = true; return base + 228; }
            return base + 223;
        case 17:   // RPG
            if (!reloading) { sync = true; return base + 227; }
            return base + 224;
        case 18:   // sniper rifle
            if (reloading) return base + 235;
            sync = true;
            return base + 236;
        default:   // the thrown weapons
            return up;
    }
}

void Player::animate(const PedSprites* sprites) {
    oneShotTicks_ = 0;
    if (!sprites || !sprites->ok()) return;
    int base = bodySet * 0x113, up, legs;
    flip_ = false;
    switch (level_) {
        case 1: up = base + 2; legs = base + 3; break;
        case 2: up = base + 4; legs = base + 5; break;
        case 3: up = base + 0xA0; legs = base + 0xA1; break;   // the player's sprint (class 0x36)
        default: up = base; legs = base + 1; break;
    }
    bool hold = false, sync = false;
    uint8_t punchNext = punch;
    const int weaponUp = playerControlled ? weaponUpper(base, up, weaponType, weaponState, weaponFlags, hold, sync, punchNext) : up;
    punch = punchNext;
    if (weaponUp != up) {   // AnimateWalkRunCycle with a weapon pose: it restarts when it changes and steps at the
        // fixed rate (0x88 >> 4) unless synced with the legs; the legs keep their own cycle
        if (weaponUp != animUpper_) { animUpper_ = weaponUp; frameUpper_ = 0; }
        if (legs != animLegs_) { animLegs_ = legs; frameLegs_ = 0; }
        const int step = (int)(((int64_t)currentSpeed() * 0x88) >> 16);
        if (level_) frameLegs_ = sprites->advance(animLegs_, frameLegs_, step);
        else frameLegs_ = 0;
        if (!(sync && !level_)) frameUpper_ = sprites->advance(animUpper_, frameUpper_, sync ? step : 0x88 >> 4);
        if (hold) frameUpper_ = 0;
        return;
    }
    if (level_ == 0) {   // standing: both sprites hold their first frame
        if (up != animUpper_) { animUpper_ = up; frameUpper_ = 0; }
        if (legs != animLegs_) { animLegs_ = legs; frameLegs_ = 0; }
        frameLegs_ = 0;
        if (hold) frameUpper_ = 0;
        return;
    }
    if (up != animUpper_ || legs != animLegs_) {   // switching gait keeps the step phase
        int nOld = animUpper_ >= 0 ? sprites->numFrames(animUpper_) : 0, nNew = sprites->numFrames(up);
        int f = nOld ? (int)((int64_t)frameUpper_ * nNew * 16 / nOld) : 0;
        animUpper_ = up;
        frameUpper_ = f >> 4;
        int nLegs = sprites->numFrames(legs);
        if (nNew == nLegs) frameLegs_ = f >> 4;
        else {
            int nLo = animLegs_ >= 0 ? sprites->numFrames(animLegs_) : 0;
            frameLegs_ = nLo ? (int)((int64_t)frameLegs_ * nLegs * 16 / nLo) >> 4 : 0;
        }
        animLegs_ = legs;
    }
    int step = (int)(((int64_t)currentSpeed() * 0x88) >> 16);
    frameUpper_ = sprites->advance(animUpper_, frameUpper_, step);
    frameLegs_ = sprites->advance(animLegs_, frameLegs_, step);
}

void Player::ride(int upper, int legs, const PedSprites* sprites) {
    oneShotTicks_ = 0;
    upper += bodySet * 0x113;
    legs += bodySet * 0x113;
    flip_ = false;
    if (upper != animUpper_ || legs != animLegs_) {
        int nOld = animUpper_ >= 0 ? sprites->numFrames(animUpper_) : 0, nNew = sprites->numFrames(upper);
        int f = nOld ? (int)((int64_t)frameUpper_ * nNew * 16 / nOld) : 0;
        animUpper_ = upper;
        frameUpper_ = f >> 4;
        int nLegs = sprites->numFrames(legs);
        if (nNew == nLegs) frameLegs_ = f >> 4;
        else {
            int nLo = animLegs_ >= 0 ? sprites->numFrames(animLegs_) : 0;
            frameLegs_ = nLo ? (int)((int64_t)frameLegs_ * nLegs * 16 / nLo) >> 4 : 0;
        }
        animLegs_ = legs;
    }
    const int step = 0x88 >> 4;   // (0x88 << timeslice) >> 4
    frameUpper_ = sprites->advance(animUpper_, frameUpper_, step);
    frameLegs_ = sprites->advance(animLegs_, frameLegs_, step);
}

void Player::renderRiding(const PedSprites* sprites, const PedLight* light, const int32_t upper[3], const int32_t legs[3], int16_t heading) const {
    if (!sprites || !sprites->ok() || animUpper_ < 0) return;
    glEnable(GL_TEXTURE_2D);
    glDisable(GL_LIGHTING);
    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GREATER, 8.f / 256.f);
    float h = -heading * 3.14159265f / 32768.f;
    float pl[3] = {legs[0] / 4096.f, legs[1] / 4096.f, legs[2] / 4096.f}, pu[3] = {upper[0] / 4096.f, upper[1] / 4096.f, upper[2] / 4096.f};
    sprites->draw(pl, h, animLegs_, frameLegs_, palUpper, palLegs, 0.5f, 0.2998f, light, flip_);
    sprites->draw(pu, h, animUpper_, frameUpper_, palUpper, palUpper, 1.0f, 0.75f, light, flip_);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_TEXTURE_2D);
}

void Player::setHeading(int16_t h) {
    fwd[0] = (int16_t)(sinf(h * 9.587378e-05f) * 4096.f);
    fwd[1] = (int16_t)(cosf(h * 9.587378e-05f) * 4096.f);
    fwd[2] = 0;
}

void Player::playOneShot(int upper, int legs, bool flip) {
    animUpper_ = upper + bodySet * 0x113;
    animLegs_ = legs + bodySet * 0x113;
    frameUpper_ = frameLegs_ = 0;
    oneShotTicks_ = 0;
    doneUpper_ = doneLegs_ = false;
    flip_ = flip;
    level_ = 0;
}

bool Player::stepOneShot(const PedSprites* sprites) {   // cOneShotAnimationTask::Process
    // A one-frame sprite can finish immediately while a replacement mesh's
    // animation still needs to run. This clock follows simulation, even once
    // the sprite holds its final frame; it never advances from rendering.
    if (oneShotTicks_ < UINT32_MAX) ++oneShotTicks_;
    if (!sprites || !sprites->ok()) return true;
    if (doneUpper_ && doneLegs_) return true;
    const int step = 0x88 >> 4;   // (0x88 << timeslice) >> 4
    if (!doneUpper_) doneUpper_ = sprites->advanceOneShot(animUpper_, frameUpper_, step);
    if (!doneLegs_) doneLegs_ = sprites->advanceOneShot(animLegs_, frameLegs_, step);
    return false;
}

void Player::render(const PedSprites* sprites, const PedLight* light) const {
    if (!sprites || !sprites->ok() || animUpper_ < 0 || hidden) return;
    glEnable(GL_TEXTURE_2D);
    glDisable(GL_LIGHTING);
    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GREATER, 8.f / 256.f);
    float p[3];
    posf(p);
    float h = -heading() * 3.14159265f / 32768.f;   // PedSprites: radians, counter-clockwise
    // cAnimation selects sprite pose 3 while fallen (0x733 high), pose 0
    // for an impact's upper half, and restores the legs first in state 1.
    const int animation = animUpper_ - bodySet * 0x113;
    const bool fallen = reaction_ == 2 || reaction_ == 5 || (!reacting() && (animation == 0x18 || animation == 0x1a));
    const bool lowUpper = fallen || reaction_ == 1;
    const bool lowLegs = fallen || reaction_ == 7 || reaction_ == 14;
    const float low = 0x733 / 4096.f;
    sprites->draw(p, h, animLegs_, frameLegs_, palUpper, palLegs, lowLegs ? low : 1.0f, lowLegs ? low : 0.5f, light, flip_);
    const float hu = upperAim && !lowUpper ? atan2f((float)-fireDir[0], (float)fireDir[1]) : h;
    sprites->draw(p, hu, animUpper_, frameUpper_, palUpper, palUpper, lowUpper ? low : 2.0f, lowUpper ? low : 1.5f, light, flip_);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_TEXTURE_2D);
}

CtwCharacterPose Player::renderPose(const PedSprites* sprites, uint32_t id, int type, uint32_t flags, float alpha) const {
    CtwCharacterPose p{};p.size=sizeof p;p.id=id;p.flags=flags|(attached?CTW_CHARACTER_ATTACHED:0)|(dead?CTW_CHARACTER_DEAD:0)|(flip_?CTW_CHARACTER_FLIPPED:0);
    p.ped_type=type;p.body_set=bodySet;p.speed_level=level_;posf(p.position);p.heading=-heading()*360.f/65536.f;
    p.upper_animation=animUpper_-bodySet*0x113;p.legs_animation=animLegs_-bodySet*0x113;
    const int upper=sprites?sprites->numFrames(animUpper_):0,legs=sprites?sprites->numFrames(animLegs_):0;
    p.upper_phase=upper>1?std::clamp(frameUpper_/(float)((upper-1)*256),0.f,1.f):0;
    p.legs_phase=legs>1?std::clamp(frameLegs_/(float)((legs-1)*256),0.f,1.f):0;
    p.one_shot_seconds = oneShotTicks_ ? (oneShotTicks_ - 1 + std::clamp(alpha, 0.f, 1.f)) / 30.f : 0;
    p.reaction_state = reaction_;
    p.reaction_seconds = reactionAge_ ? (reactionAge_ - 1 + std::clamp(alpha, 0.f, 1.f)) / 30.f : 0;
    if (reacting()) p.flags |= CTW_CHARACTER_KNOCKED_DOWN;
    if (reacting() && reactionForward_) p.flags |= CTW_CHARACTER_FALL_FORWARD;
    p.recovery_seconds = recoveryAge_ ? (recoveryAge_ - 1 + std::clamp(alpha, 0.f, 1.f)) / 30.f : 0;
    return p;
}
