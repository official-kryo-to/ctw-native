// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "watercannon.h"
#include "game.h"
#include "plugins.h"
#include "sound.h"
#include "cargens.h"   // Rand32Critical
#include "gfx/assets.h"
#include "world/worldrenderer.h"
#include <glad/gl.h>
#include <algorithm>
#include <cmath>

void WaterCannon::input(const int32_t p[3], const int32_t v[3], uint32_t frame) {   // CWaterCannon::Update_NewInput
    frame_=frame;
    if(!audioId_) { static uint32_t next=1; audioId_=next++; }
    for (int k = 0; k < 3; ++k) { pos_[head_][k] = p[k]; vel_[head_][k] = v[k]; }
    age_[head_] = 1;
    if (flags & 1)   // a little extra lift that changes with the frame counter
        vel_[head_][2] += (int32_t)(((frame & 15) << 7) + Rand32Critical(0x51)) >> 1;
    if((flags&8) && maxPoints>=16) {
        if(!splash_) { splash_.emplace(p,v,colour); splash_->addParticle(); }
        else {
            splash_->setPos(p); splash_->direction(v);
            if((flags&1) ? Rand16NonCritical(3)==0 : !(frame&1)) splash_->addParticle();
        }
    }
}

void WaterCannon::step() {   // CWaterCannon::Update_OncePerFrame
    if(splash_) splash_->tick(frame_);
    if((flags&0x80) && audioId_) {
        const int volume=(renderedVertices_<32 ? renderedVertices_*2 : 60) << ((flags&0x100) ? 0 : 1);
        TheSound().waterStream(audioId_,pos_[head_],volume);
    }
    head_ = (int16_t)((head_ + 1) & 15);
    age_[head_] = 0;
    for (int k = 0; k < 16; ++k) {
        uint8_t& age = age_[k];
        int32_t* v = vel_[k];
        int32_t* p = pos_[k];
        if ((flags & 2) && age == 1) {
            const int64_t l2 = (int64_t)v[0] * v[0] + (int64_t)v[1] * v[1] + (int64_t)v[2] * v[2];
            if (l2 >= 0x418A) {   // Normalise: + one unit along the jet
                const double l = std::sqrt((double)l2);
                for (int i = 0; i < 3; ++i) v[i] += (int32_t)std::lround(v[i] * 4096.0 / l);
            }
        } else if (age >= 2) {
            for (int i = 0; i < 3; ++i) v[i] = (int32_t)(((int64_t)v[i] * 0xE66) >> 12);
            if (flags & 4) v[2] -= 0x10B;
            for (int i = 0; i < 3; ++i) p[i] += v[i];
            if (p[2] <= -0xA001) age = 0;
        }
        if (age >= 1 && age <= 24) ++age;
    }
}

void WaterCannon::render(const WorldCamera& cam, uint32_t ambient) const {   // CWaterCannon::Render
    int texture;
    uint16_t r[4];
    if (!Assets_EffectSprite(20, texture, r)) return;
    int tw = 256, th = 256;
    Assets_TextureSize(texture, &tw, &th);
    const float u0 = r[0] / (float)tw, u1 = (r[0] + r[2]) / (float)tw, v0 = r[1] / (float)th, v1 = (r[1] + r[3]) / (float)th;
    // colour: RGB555 -> 8 bit, then 0x99 / 0x100 of the way to the ambient colour
    auto mix = [&](uint16_t c555, float out[3]) {
        const int c[3] = {(c555 & 0x1F) << 3, (c555 >> 5 & 0x1F) << 3, (c555 >> 10 & 0x1F) << 3};
        for (int i = 0; i < 3; ++i) {
            const int a = (int)(ambient >> (8 * i) & 0xFF);
            out[i] = (c[i] + (a - c[i]) * 0x99 / 0x100) / 255.f;
        }
    };
    float base[3], light[3];
    mix(colour, base);
    mix(0x7FBD, light);
    const float alpha = (flags & 0x20) ? 0x7B / 255.f : 1.f;
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, Assets_Texture(texture));
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glDisable(GL_LIGHTING);
    glBegin(GL_TRIANGLE_STRIP);
    float side[3] = {cam.right[0], cam.right[1], cam.right[2]};
    int drawn = 0;
    for (int n = 1; n <= 16 && drawn < maxPoints; ++n) {   // newest first
        const int k = (head_ - n) & 15;
        if (!age_[k]) break;
        const float p[3] = {pos_[k][0] / 4096.f, pos_[k][1] / 4096.f, pos_[k][2] / 4096.f};
        const float v[3] = {vel_[k][0] / 4096.f, vel_[k][1] / 4096.f, vel_[k][2] / 4096.f};
        // across the stream: perpendicular to the view and to the flow
        const float c[3] = {cam.fwd[1] * v[2] - cam.fwd[2] * v[1], cam.fwd[2] * v[0] - cam.fwd[0] * v[2], cam.fwd[0] * v[1] - cam.fwd[1] * v[0]};
        const float l = std::sqrt(c[0] * c[0] + c[1] * c[1] + c[2] * c[2]);
        if (l > 0.05f) for (int i = 0; i < 3; ++i) side[i] = c[i] / l;
        const float w = age_[k] / 16.f;
        const float* col = ((flags & 0x10) && (k & 3) == 0) ? light : base;
        glColor4f(col[0], col[1], col[2], alpha);
        const float tv = v0 + (v1 - v0) * (drawn & 1);
        glTexCoord2f(u0, tv); glVertex3f(p[0] + side[0] * w, p[1] + side[1] * w, p[2] + side[2] * w);
        glTexCoord2f(u1, tv); glVertex3f(p[0] - side[0] * w, p[1] - side[1] * w, p[2] - side[2] * w);
        ++drawn;
    }
    glEnd();
    renderedVertices_=(uint8_t)(drawn*2);
    if(splash_) splash_->render(cam);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glDisable(GL_TEXTURE_2D);
}

Fountain::Fountain(const int32_t p[3], const int16_t v[3]) {
    for (int k = 0; k < 3; ++k) { pos_[k] = p[k]; vel_[k] = v[k]; }
    jet_.flags = 0xDE | 1 | 0x100;   // cFountainStream(..., flags 1, keep the default flags, colour 0x7F39)
    jet_.colour = 0x7F39;
}

void WaterCannon::putOutFires(Game& g, bool strong) {   // CWaterCannon::PutOutFires
    const int32_t* head = pos_[head_];
    maxPoints = 0x11;
    const uint8_t amount = strong ? 2 : 1;
    const auto& fires = g.fires.list();
    for (size_t i = 0; i < fires.size(); ++i) {
        const Fires::Fire& f = fires[i];
        // cWorldIterator::SetRadius(head, 30): the fire's position within 30 units of the newest point
        int64_t d2 = 0;
        for (int k = 0; k < 3; ++k) d2 += ((int64_t)f.pos[k] - head[k]) * ((int64_t)f.pos[k] - head[k]);
        if (f.out || d2 > (int64_t)0x1E000 * 0x1E000) continue;
        // from the newest point back over all sixteen: below 1 the index wraps to 15, so slot 0 is only ever
        // seen as the head itself
        int k = head_;
        for (int n = 0; n < 16; ++n) {
            if (age_[k]) {
                const int32_t reach = std::min((int32_t)((age_[k] << 11) & 0x7F000), 0x5000);
                const int64_t dx = (int64_t)pos_[k][0] - f.pos[0], dy = (int64_t)pos_[k][1] - f.pos[1];
                if (dx * dx + dy * dy <= (int64_t)reach * reach) {
                    const int32_t at[3] = {f.pos[0], f.pos[1], f.pos[2]};
                    g.fires.hitByWater(g, i, amount);
                    if (!(g.frame & 0xF)) g.addSteamCloud(at, 0x7FFF, 0x1000);
                    maxPoints = (uint8_t)(n + 2);   // the stream ends at the fire
                    break;
                }
            }
            k = k - 1 < 1 ? 15 : k - 1;
        }
    }
}

namespace {
// the wobble cFountainStream::Process and its constructor give the jet: + (frame * 0xF4000 & 0xFC000) *
// 0x100805 >> 32 (up to about 6 %)
void wobbled(const int16_t vel[3], uint32_t frame, int32_t out[3]) {
    const uint32_t k = (uint32_t)(((uint64_t)((frame * 0xF4000u) & 0xFC000u) * 0x100805u) >> 32);
    for (int i = 0; i < 3; ++i) out[i] = (int16_t)(vel[i] + (int16_t)((uint32_t)(k * vel[i]) >> 12));
}
}   // namespace

void Fountain::tick(uint32_t frame) {
    int32_t v[3];
    wobbled(vel_, frame, v);
    jet_.input(pos_, v, frame);
    jet_.step();
}

bool Fountain::touches(const int32_t centre[3], int32_t radius) const {
    int64_t d2 = 0;
    for (int k = 0; k < 3; ++k) {
        const int64_t d = std::max<int64_t>(std::abs((int64_t)centre[k] - pos_[k]) - kBoxHalf[k], 0);
        d2 += d * d;
    }
    return d2 <= (int64_t)radius * radius;
}

void Fountain::jet(int32_t out[3], int scale) const {
    for (int k = 0; k < 3; ++k) out[k] = vel_[k] * scale;
}

void Fountain::process(Game& g) {
    // cFountainStream::Process: the cannon's new point, then PutOutFires(true)
    int32_t v[3];
    wobbled(vel_, g.frame, v);
    jet_.input(pos_, v, g.frame);
    jet_.putOutFires(g, true);

    // cFountainStream::OnCollision
    std::vector<uint32_t> knockedDown;
    auto push = [&](Player& body, Pedestrians::Ped* ped) {   // cPed / cPlayer (ClassId 0x35, 0x36)
        const int32_t centre[3] = {body.pos[0], body.pos[1], body.pos[2] + Player::kSphere};
        if (body.hidden || body.attached || !touches(centre, Player::kRadius)) return;
        int32_t water[3];
        jet(water, 4);
        int32_t velocity[3];
        if (std::abs(water[0]) >= 0x32000 || std::abs(water[1]) >= 0x7000) std::copy(water, water + 3, velocity);
        else if ((int64_t)body.vel[0] * body.vel[0] + (int64_t)body.vel[1] * body.vel[1] +
                 (int64_t)body.vel[2] * body.vel[2] >= 0x32000001LL) std::copy(body.vel, body.vel + 3, velocity);
        else { velocity[0] = velocity[1] = 0x7000; velocity[2] = water[2]; }   // the reference's fixed diagonal
        // cPed::Damage(sDamageInfo{this, 0, velocity, type 13, eEffect 3}): no health is lost
        const bool knocked = body.waterHit(velocity);
        if (ped && knocked) {
            ped->deadFrames = 0;
            ped->rollFrames = -1;
        }
        if (ped && !body.dead) g.peds.react(g, *ped, 0, pos_, 0);   // ReactToDamage: GetAction(13) is action 0
        if (ped && knocked) knockedDown.push_back(ped->uid);
        std::copy(velocity, velocity + 3, body.vel);   // SetVelocity
    };
    if (g.playerCar < 0) push(g.player, nullptr);
    for (Pedestrians::Ped& p : g.peds.peds)
        if (!p.vehicle) push(p.body, &p);
    for (Vehicle& car : g.cars) {   // vehicles (ClassId 0x28..0x34): ApplyWorldForce(base, 8 x jet, type 1)
        int32_t spheres[16][4];
        int n = 0;
        car.collisionSpheres(spheres, n);
        bool hit = false;
        for (int i = 0; i < n && !hit; ++i) hit = touches(spheres[i], spheres[i][3]);
        if (!hit) continue;
        int32_t force[3];
        jet(force, 8);
        car.applyWorldForce(pos_, force, 1);
    }
    g.propDynamics.waterJet(g, *this);   // dynamic props (ClassId 0x23..0x27): 4 x jet

    jet_.step();   // CWaterCannons::Process -> Update_OncePerFrame
    // mod callbacks may add or remove pedestrians: no body references are held here
    for (uint32_t uid : knockedDown) Plugins_EmitEvent(CTW_EVENT_PED_KNOCKED_DOWN, 0, -1, (int)uid);
}
