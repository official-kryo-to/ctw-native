// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// What spinning, burst and burning-out tyres leave behind: skid marks, tyre smoke, rim sparks and burnout flames
// (cWheeledVehicle::AddSkidmarks, cBike::AddSkidmarks), once a frame after the vehicle's ProcessAlways.
//
// A car marks both wheels of an axle, 0.4 x its width either side of the axle's centre; a bike marks one track. The
// back wheels mark while they spin (a burnout holds each point four frames instead of one), scrape sparks off a
// burst rim, and throw smoke: during a burnout a dark puff on three frames in five per wheel, drifting at random,
// otherwise a light one straight up on one frame in three. A launched burnout (state 3) drops a short style-5 fire
// at each back wheel every frame. Front wheels only mark (along the car's right axis) while they spin or scrape.
// Cars nobody holds (traffic) mark only on every other frame, offset by their entity byte. The script switch
// Vehicle::SetNoSkid (+0x945 bit 1) is never set here.
#include "game.h"
#include "fixedmath.h"
#include "skidmarks.h"

namespace {
void axlePoints(const Vehicle& car, int32_t axleY, const int32_t lateral[3], int32_t right[3], int32_t left[3]) {
    const int32_t a = axleY << 4;
    for (int k = 0; k < 3; ++k) {
        const int32_t centre = car.pos[k] + ((a * car.fwd[k]) >> 16);
        right[k] = centre + lateral[k];
        left[k] = centre - lateral[k];
    }
}

void rimSpark(Game& g, const int32_t at[3]) {   // AddSpark(pos, dir, 16, false, 0, true, 0x11F, 0x2E0, 1.0)
    int16_t dir[3];
    dir[0] = (int16_t)((int)Rand32NonCritical(0x198) - 0xCC);
    dir[1] = (int16_t)((int)Rand32NonCritical(0x198) - 0xCC);
    dir[2] = (int16_t)(Rand32NonCritical(0xA4) + 0x28);
    g.addSpark(at, dir, 0x10, false, 0, true, 0x11F, 0x2E0, 0x1000);
}

void burnoutFlame(Game& g, const Vehicle& car, const int32_t at[3]) {   // cFire(pos, driver, 20..39, style 5)
    const int16_t life = (int16_t)(Rand32Critical(0x14) + 0x14);
    g.fires.start(g, at, Fires::None, 0, life, 5, false, car.playerDriving());
}
}   // namespace

void Vehicles_AddSkidmarks(Game& g, const Vehicle& car) {
    const Vehicle::Tyre& front = car.tyre_[0];
    const Vehicle::Tyre& rear = car.tyre_[1];
    const uint32_t rearId = car.uid * 4, frontId = car.uid * 4 + 2;
    const int16_t up[3] = {0, 0, 0x199}, upFront[3] = {0, 0, 0x19A};
    const int32_t right[3] = {car.right[0], car.right[1], car.right[2]};

    if (car.bike_) {   // cBike::AddSkidmarks: one track, no frame skipping
        int32_t at[3];
        if (rear.spinning || rear.rimSparks || car.burnout_ == 3) {
            const int32_t a = rear.axleY << 4;
            for (int k = 0; k < 3; ++k) at[k] = car.pos[k] + ((a * car.fwd[k]) >> 16);
            TheSkidmarks().addPoint(rearId, at, right, car.burnout_ == 1 ? 4 : 1);
            if (car.burnout_ == 3) {
                const int32_t flame[3] = {at[0], at[1], at[2] + 0x400};
                burnoutFlame(g, car, flame);
            }
            if (rear.spinning) {
                if (car.burnout_ == 1) {
                    if (Rand32Critical(5) >= 3) {   // a dark puff blown back off the wheel
                        int16_t v[3];
                        v[0] = (int16_t)(((uint32_t)(car.fwd[0] * 0x0FFFFF34u) >> 12) + Rand32NonCritical(0xF4) - 0x7A);
                        v[1] = (int16_t)(((uint32_t)(car.fwd[1] * 0x0FFFFF34u) >> 12) + Rand32NonCritical(0xF4) - 0x7A);
                        v[2] = (int16_t)(((uint32_t)(car.fwd[2] * 0x0FFFFF34u) >> 12) + 0x199);
                        g.addSkidSmoke(at, v, true);
                    }
                } else if (Rand32Critical(3) == 1) g.addSkidSmoke(at, upFront, false);
            }
            if (rear.rimSparks) rimSpark(g, at);
        }
        if (front.spinning || front.rimSparks) {
            const int32_t a = front.axleY << 4;
            for (int k = 0; k < 3; ++k) at[k] = car.pos[k] + ((a * car.fwd[k]) >> 16);
            TheSkidmarks().addPoint(frontId, at, right, 1);
            if (front.spinning && Rand32Critical(3) == 1) g.addSkidSmoke(at, upFront, false);
            if (front.rimSparks) rimSpark(g, at);
        }
        return;
    }

    // cWheeledVehicle::AddSkidmarks: vehicles nobody holds skip every other frame
    if (!car.playerDriving() && !((g.frame + (car.uid & 0xFF)) & 1)) return;
    int32_t lateral[3] = {0, 0, 0}, r[3], l[3];
    auto computeLateral = [&] {   // 0.4 x the width (hx x 2 x 0x666 / 0x1000), along the right axis
        const int32_t across = (int32_t)(((int64_t)(car.hx << 1) * 0x66600000LL) >> 32);
        for (int k = 0; k < 3; ++k) lateral[k] = (int32_t)(((int64_t)across * car.right[k]) >> 12);
    };
    bool computed = false;
    if (rear.spinning || rear.rimSparks || car.burnout_ == 3 || (car.b64_ & 0x100)) {
        computeLateral();
        computed = true;
        axlePoints(car, rear.axleY, lateral, r, l);
        const int hold = car.burnout_ == 1 ? 4 : 1;
        if (rear.onGround) {
            int32_t d[3] = {r[0] - l[0], r[1] - l[1], r[2] - l[2]};
            Normalise3(d);
            TheSkidmarks().addPoint(rearId, r, d, hold);
            TheSkidmarks().addPoint(rearId + 1, l, d, hold);
        }
        if (car.burnout_ == 3 || (car.b64_ & 0x100)) {
            burnoutFlame(g, car, l);
            burnoutFlame(g, car, r);
        }
        if (rear.spinning) {
            if (car.burnout_ == 1) {
                for (const int32_t* at : {r, l}) {
                    if (Rand32Critical(5) < 3) continue;
                    int16_t v[3];
                    v[0] = (int16_t)((int)Rand32NonCritical(0x198) - 0xCC);
                    v[1] = (int16_t)((int)Rand32NonCritical(0x198) - 0xCC);
                    v[2] = 0x199;
                    g.addSkidSmoke(at, v, true);
                }
            } else {
                if (Rand32Critical(3) == 1) g.addSkidSmoke(r, up, false);
                if (Rand32Critical(3) == 1) g.addSkidSmoke(l, up, false);
            }
        }
        if (rear.rimSparks) { rimSpark(g, r); rimSpark(g, l); }
    }
    if (front.spinning || front.rimSparks) {
        if (!computed) computeLateral();
        axlePoints(car, front.axleY, lateral, r, l);
        TheSkidmarks().addPoint(frontId, r, right, 1);
        TheSkidmarks().addPoint(frontId + 1, l, right, 1);
        if (front.spinning) {
            if (Rand32Critical(3) == 1) g.addSkidSmoke(r, upFront, false);
            if (Rand32Critical(3) == 1) g.addSkidSmoke(l, upFront, false);
        }
        if (front.rimSparks) { rimSpark(g, r); rimSpark(g, l); }
    }
}
