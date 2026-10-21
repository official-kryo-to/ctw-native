// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "fire.h"
#include "game.h"
#include "particles.h"
#include "projectiles.h"
#include "random.h"
#include "sound.h"
#include "gfx/assets.h"
#include "world/worldrenderer.h"
#include <glad/gl.h>
#include <algorithm>
#include <cmath>

namespace {
constexpr int kMaxFires = 30;
int fastsinq(int a) { return (int)(sinf((float)a * 9.587378e-05f) * 4096.f); }   // cFire::mTotalFiresRunning above 0x1E destroys the new fire at once

bool attachedPos(const Game& g, const Fires::Fire& f, int32_t out[3]) {
    switch (f.attach) {
        case Fires::None: std::copy(f.pos, f.pos + 3, out); return true;
        case Fires::Player: std::copy(g.player.pos, g.player.pos + 3, out); return true;
        case Fires::Ped:
            for (const Pedestrians::Ped& p : g.peds.peds)
                if (p.uid == f.uid) { std::copy(p.body.pos, p.body.pos + 3, out); return true; }
            return false;
        case Fires::Car:
            for (const Vehicle& c : g.cars)
                if (c.uid == f.uid) { c.localToWorld(f.offset, out); return true; }
            return false;
    }
    return false;
}
}   // namespace

Fires::Fire* Fires::start(Game& g, const int32_t pos[3], Attach attach, uint32_t uid, int16_t life, int style,
                          bool emitter, bool byPlayer, const int32_t offset[3], bool petrol) {
    if ((int)fires_.size() + 1 > kMaxFires) return nullptr;
    Fire f;
    std::copy(pos, pos + 3, f.pos);
    f.attach = attach;
    f.uid = uid;
    if (offset) std::copy(offset, offset + 3, f.offset);
    f.byPlayer = byPlayer;
    f.life = life;
    f.style = (uint8_t)(style & 0xF);
    switch (style) {   // the flames' half sizes (+0x140, +0x142)
        case 0: f.sx = 0x1333; f.sy = 0x1000; break;
        case 1: f.sx = 0x1000; f.sy = 0x1333; break;
        case 2: f.sx = 0x1000; f.sy = 0x1000; break;
        case 3: case 4: f.sx = 0x1800; f.sy = 0x1800; break;
        case 5: f.sx = 0x4CC; f.sy = 0x666; break;
        case 6: f.sx = 0x4CC; f.sy = 0x800; break;
        default: break;
    }
    f.emitterWanted = emitter;
    if (emitter) {   // cParticleEmitterFire::CreateFireEmitter and its first flame
        f.emitter = TheParticles().add<FireEmitter>(pos);
        f.emitter->addParticle();
    }
    f.petrol = petrol;
    if (petrol) f.petrolEmitter = TheParticles().add<PetrolFlameEmitter>(pos, f.sx);
    for (Flame& fl : f.flames) { fl = Flame{}; fl.rate = 0x1000; }
    f.id = ++nextId_;
    fires_.push_back(f);
    addSfx(g, fires_.back());
    return &fires_.back();
}

void Fires::addSfx(Game& g, const Fire& f) {   // cAudioManager::AddFireSfx
    for (FireSfx& s : sfx_) {
        if (s.fire) continue;
        // a free sound: PlayLoopingScriptSfx(0x1ED, volume 10, the fire, squared radius 500), fading in
        s.handle = TheSound().loopingScriptSfx(f.pos, 0x1ED, 10, 500);
        if (s.handle == -1) return;
        s.fire = f.id; s.state = 0; s.timer = 0x14; s.volume = 10;
        std::copy(f.pos, f.pos + 3, s.pos);
        return;
    }
    // all three in use: the farthest fire farther from the listener than this one, and not taken over within the
    // last 20 frames, hands its sound over at full volume
    int32_t ear[3];
    g.focus(ear);
    auto dist = [&](const int32_t p[3]) {
        const int64_t dx = p[0] - ear[0], dy = p[1] - ear[1], dz = p[2] - ear[2];
        return (uint32_t)((uint64_t)(dx * dx + dy * dy + dz * dz) >> 24);
    };
    const uint32_t mine = dist(f.pos);
    int best = -1;
    uint32_t farthest = 99999;
    for (int i = 0; i < 3; ++i) {
        const uint32_t d = dist(sfx_[i].pos);
        if (mine < d && d < farthest && sfx_[i].timer == 0) { best = i; farthest = d; }
    }
    if (best < 0) return;
    FireSfx& s = sfx_[best];
    s.fire = f.id; s.state = 1; s.timer = 0x14; s.volume = 100;
    std::copy(f.pos, f.pos + 3, s.pos);
}

void Fires::removeSfx(const Fire& f) {   // cAudioManager::RemoveFireSfx (~cFire): the sound fades out
    for (FireSfx& s : sfx_)
        if (s.fire == f.id && s.state != 2) { s.state = 2; return; }
}

void Fires::processSfx() {   // cAudioManager::ProcessFireSfx
    for (FireSfx& s : sfx_) {
        if (!s.fire) continue;
        if (s.timer) --s.timer;
        if (s.state == 2) {
            if (s.volume <= 10) {
                TheSound().stopScriptSfx(s.handle);
                s.fire = 0; s.timer = 0; s.handle = -1;
                continue;
            }
            s.volume = (uint8_t)(s.volume - 10);
        } else if (s.state == 0 && s.volume < 100) {
            const uint8_t v = s.volume;
            s.volume = (uint8_t)(v + 10);
            if (v > 0x59) s.state = 1;
        }
        for (const Fire& f : fires_)   // (a fire that went out keeps its last position)
            if (f.id == s.fire) { std::copy(f.pos, f.pos + 3, s.pos); break; }
        if (s.handle != -1) TheSound().scriptLoop(s.handle, s.pos, s.volume);
    }
}

void Fires::molotov(Game& g, const int32_t pos[3], bool byPlayer) {
    for (int i = 0; i < 3; ++i) {
        const unsigned a = (Rand16Critical(10) + i * 0x6486) & 0xFFFF;
        const int s = fastsinq((int)a), c = fastsinq((int)a + 0x4000);
        const int32_t at[3] = {pos[0] + (((s - c) * 0x30000) >> 17), pos[1] + (((c + s) * 0x30000) >> 17), pos[2]};
        const int16_t life = (int16_t)((Rand16Critical(0x96) & 0xFFFF) * 2 + 100);
        Fire* f = start(g, at, None, 0, life, 0, false, byPlayer, nullptr, true);
        if (!f) continue;
        f->vel[0] = (int16_t)((int16_t)(s - c) * 3);
        f->vel[1] = (int16_t)((int16_t)(c + s) * 3);
        f->vel[2] = 0;
        if (pos[2] > 0x5000) {   // thrown high: it burns briefly while it falls
            f->life = 0x28;      // Extinguish(0x28)
            f->falling = true;
        }
    }
    start(g, pos, None, 0, 400, 0, true, byPlayer, nullptr, true);
}

void Fires::ignite(Game& g, const Fire& f) {
    const Fire copy = f;   // starting fires may move the vector
    auto touches = [&](const int32_t p[3], int32_t radius) {
        const int64_t dx = p[0] - copy.pos[0], dy = p[1] - copy.pos[1], dz = p[2] - copy.pos[2];
        const int64_t r = 0x2000 + radius;   // SetCollisionPrimitiveSphere(2)
        return dx * dx + dy * dy + dz * dz < r * r;
    };
    if (g.playerCar < 0 && !g.player.dead && !g.player.hidden && !burning(Player, 0) &&
        touches(g.player.pos, Player::kRadius))
        start(g, g.player.pos, Player, 0, 0x1E0, 1, true, copy.byPlayer);   // cPed::SetOnFire: the player burns 8 s
    for (const Pedestrians::Ped& p : g.peds.peds)
        if (!p.vehicle && !p.body.dead && p.deadFrames < 0 && !burning(Ped, p.uid) &&
            touches(p.body.pos, Player::kRadius))
            start(g, p.body.pos, Ped, p.uid, -1, 1, true, copy.byPlayer);
    for (Vehicle& c : g.cars)
        if (!c.dead() && !burning(Car, c.uid) && touches(c.pos, c.boundRadius())) Fires_SetVehicleOnFire(g, c);
}

void Fires::processFire(Game& g, Fire& f) {   // cFire::Process
    if (f.attach != None) {
        if (!attachedPos(g, f, f.pos)) { f.out = true; return; }   // AttachLinkRemoved
        // cFire::Process: attached fires are extinguished when their owner is in water.
        if (g.collision.ground(f.pos[0] / 4096.f, f.pos[1] / 4096.f, f.pos[2] / 4096.f).surface == 2) {
            f.out = true;
            return;
        }
    }
    if (f.attach == None) {   // cObject::Process: the loose fire moves with its velocity
        int32_t step[3];
        for (int k = 0; k < 3; ++k) step[k] = (int32_t)((17LL * f.vel[k]) >> 9);
        // its 2-unit collision sphere collides against the statics (SetCollideAgainstFlags 0xF06): a wall stops
        // the slide. (The integrator's contact response is approximated: the sphere is swept 2.25 units up so the
        // ground does not count, and a wall hit zeroes the horizontal velocity.)
        if (step[0] || step[1]) {
            const int32_t a[3] = {f.pos[0], f.pos[1], f.pos[2] + 0x2400};
            const int32_t b[3] = {a[0] + step[0], a[1] + step[1], a[2]};
            int32_t contact[3], n[3];
            if (Projectiles_SphereStatics(g.collision, a, b, 0x2000, contact, n) && std::abs(n[2]) < 0x800) {
                step[0] = step[1] = 0;
                f.vel[0] = f.vel[1] = 0;
            }
        }
        for (int k = 0; k < 3; ++k) f.pos[k] += step[k];
    }
    if (!f.groundChecked) {   // the first Process: a loose fire over water goes out
        f.groundChecked = true;
        if (f.attach == None &&
            g.collision.ground(f.pos[0] / 4096.f, f.pos[1] / 4096.f, f.pos[2] / 4096.f).surface == 2) {
            f.out = true;
            return;
        }
    }
    for (Flame& fl : f.flames) {
        int16_t rate;
        if (fl.age < 1) {   // a new flame somewhere within the fire's size
            const int16_t ox = (int16_t)((int16_t)Rand32NonCritical((uint32_t)(f.sx - -f.sx)) - f.sx);
            const int16_t oy = (int16_t)((int16_t)Rand32NonCritical((uint32_t)(f.sy - -f.sy)) - f.sy);
            fl.off[0] = ox; fl.off[1] = oy; fl.off[2] = 0x800;
            const int32_t a = (int32_t)Rand32NonCritical(0x2000), b = (int32_t)Rand32NonCritical(0x2000);
            fl.w = (int16_t)((f.sx * (a + 0x2000)) >> 12);
            fl.h = (int16_t)((f.sy * (b + 0x2000)) >> 12);
            rate = (int16_t)((int16_t)Rand32NonCritical(0x667) + 0x333);
            fl.abgr = 0xFF40E0F8u;
            fl.rate = rate;
        } else if ((uint16_t)fl.age < 0x1000) {
            rate = fl.rate;
        } else {   // full grown: shrink again
            rate = (int16_t)(-0x333 - (int16_t)Rand32NonCritical(0x667));
            fl.rate = rate;
        }
        const int age = std::clamp((int)(int16_t)(fl.age + rate), 0, 0x1000);
        fl.off[2] = (int16_t)(fl.off[2] + 0x199);
        fl.age = (int16_t)age;
        uint8_t* c = reinterpret_cast<uint8_t*>(&fl.abgr);   // little-endian bytes: R, G, B, A
        if (c[1] > 0x17) c[1] = (uint8_t)(c[1] - 0x18);
        if (c[2] > 0xF) c[2] = (uint8_t)(c[2] - 0x10);
        const unsigned u = (unsigned)(0x1000 - age) & 0xFFFF;
        const unsigned grown = (uint16_t)(0x1000 - ((u * u) >> 12));
        c[3] = (uint8_t)(c[3] & (grown * 0xFF >> 12));
    }
    // the lifetime: below 3 the fire goes out; a negative lifetime burns until put out
    if (f.life < 0) {
        if (f.fade < 0x1000) f.fade = (int16_t)(f.fade + 0x199);
    } else if (f.life < 3) {
        f.out = true;
    } else if (f.style == 5 || f.life < 0x1F) {
        if (f.fade >= 1) f.fade = (int16_t)(f.fade - 0xCC);
    } else if (f.fade <= 0xFFF) {
        f.fade = (int16_t)(f.fade + 0x199);
    }
    f.life = (int16_t)(f.life - 2);
    if (f.attach == None && f.falling) {   // gravity
        const int32_t vz = f.vel[2];
        f.vel[2] = std::abs(vz) > 0xFFF ? vz - 0x14E8 : -0x34E8;
        const float ground = g.collision.ground(f.pos[0] / 4096.f, f.pos[1] / 4096.f, f.pos[2] / 4096.f + 1.f).z;
        if (f.pos[2] < (int32_t)(ground * 4096.f)) {   // (the landing: cObject's collision, approximated)
            f.pos[2] = (int32_t)(ground * 4096.f);
            f.vel[2] = 0;
        }
    }
    if ((int64_t)f.vel[0] * f.vel[0] + (int64_t)f.vel[1] * f.vel[1] + (int64_t)f.vel[2] * f.vel[2] > 0x28000)
        for (int k = 0; k < 3; ++k) f.vel[k] = (int32_t)(((int64_t)f.vel[k] * 0xE66) >> 12);
    if (f.emitter && !TheParticles().contains(f.emitter)) f.emitter = nullptr;
    if (f.petrolEmitter && !TheParticles().contains(f.petrolEmitter)) f.petrolEmitter = nullptr;
    if (f.out) {
        if (f.emitter) f.emitter->dying = true;
        if (f.petrolEmitter) f.petrolEmitter->dying = true;
        return;
    }
    if (f.emitter && f.emitterWanted) {
        if (Rand16NonCritical(3) == 0) f.emitter->addParticle();
        f.emitter->setPos(f.pos);
    }
    if (f.petrol && f.petrolEmitter) {
        f.petrolEmitter->setPos(f.pos);
        if (Rand16NonCritical(3) == 0) f.petrolEmitter->addParticle();
    }
    if (f.attach == None) ignite(g, f);
}

void Fires::process(Game& g) {
    for (size_t i = 0; i < fires_.size(); ++i)   // (ignite can start fires: index, not references)
        if (!fires_[i].out) processFire(g, fires_[i]);
    // cPed::Process: a burning ped takes a point of type-5 damage (effect 0x1D) every second frame, the player
    // every fourth; in water the fire goes out
    for (Fire& f : fires_) {
        if (f.out) continue;
        if (f.attach == Player) {
            if (g.player.dead) { f.out = true; continue; }   // cPed::SetDead -> Extinguish
            if ((g.frame & 1) || (g.frame & 3)) continue;
            if (g.player.takeDamage(1)) f.out = true;
        } else if (f.attach == Ped) {
            for (Pedestrians::Ped& p : g.peds.peds) {
                if (p.uid != f.uid) continue;
                if (p.body.dead || p.deadFrames >= 0) { f.out = true; break; }
                if (g.frame & 1) break;
                const int32_t from[3] = {f.pos[0], f.pos[1], f.pos[2]};
                const int subtype = p.subtype;
                g.peds.fireHit(g, p, 1, f.byPlayer ? g.player.pos : from);
                if (p.body.dead || p.deadFrames >= 0) {
                    f.out = true;
                    if (f.byPlayer) Stats_PedKilledByPlayer(g, subtype);   // cPed::HandlePreDead: the fire's owner
                }
                break;
            }
        }
    }
    for (Fire& f : fires_) {
        if (!f.out) continue;
        if (f.emitter && TheParticles().contains(f.emitter)) f.emitter->dying = true;
        if (f.petrolEmitter && TheParticles().contains(f.petrolEmitter)) f.petrolEmitter->dying = true;
        removeSfx(f);
    }
    fires_.erase(std::remove_if(fires_.begin(), fires_.end(), [](const Fire& f) { return f.out; }), fires_.end());
    processSfx();
}

void Fires::render(const WorldCamera& cam) const {   // cFire::ManagedRender, in the additive pass
    if (fires_.empty()) return;
    glEnable(GL_DEPTH_TEST);
    for (const Fire& f : fires_) {
        if (f.style == 7) continue;
        const float base[3] = {f.pos[0] / 4096.f, f.pos[1] / 4096.f,
                               (f.pos[2] + (int16_t)((int16_t)(f.sy >> 2) * 3)) / 4096.f};
        for (const Flame& fl : f.flames) {
            const float at[3] = {base[0] + fl.off[0] / 4096.f, base[1] + fl.off[1] / 4096.f, base[2] + fl.off[2] / 4096.f};
            // RenderWorldScaleCamAligned(14, colour, camera, pos, w, -h): flipped vertically
            DrawSheetSprite(0xE, fl.abgr, at, cam.right, cam.up, fl.w / 4096.f, -fl.h / 4096.f, true);
        }
    }
}

bool Fires::burning(Attach attach, uint32_t uid) const {
    for (const Fire& f : fires_) if (!f.out && f.attach == attach && f.uid == uid) return true;
    return false;
}

void Fires::extinguish(Attach attach, uint32_t uid) {
    for (Fire& f : fires_) if (f.attach == attach && f.uid == uid) f.out = true;
}

void Fires::clear() {
    for (Fire& f : fires_) {
        if (f.emitter && TheParticles().contains(f.emitter)) f.emitter->dying = true;
        if (f.petrolEmitter && TheParticles().contains(f.petrolEmitter)) f.petrolEmitter->dying = true;
    }
    fires_.clear();
    for (FireSfx& s : sfx_) {   // cAudioManager::StopAllFireSfx
        if (s.fire) TheSound().stopScriptSfx(s.handle);
        s = FireSfx{};
    }
}

bool Fires_PedOnFire(const Game& g, uint32_t uid) { return g.fires.burning(Fires::Ped, uid); }
bool Fires_VehicleOnFire(const Game& g, uint32_t uid) { return g.fires.burning(Fires::Car, uid); }

void Fires_SetPedOnFire(Game& g, uint32_t uid) {   // cPed::SetOnFire(the player)
    for (Pedestrians::Ped& p : g.peds.peds) {
        if (p.uid != uid || p.vehicle || g.fires.burning(Fires::Ped, uid)) continue;
        // a ped set on fire by someone burns until it dies (lifetime -1); a style-1 fire with an emitter
        g.fires.start(g, p.body.pos, Fires::Ped, uid, -1, 1, true, true);
        return;
    }
}

void Fires_SetVehicleOnFire(Game& g, Vehicle& car) {   // cVehicle::SetOnFire -> AttachFire(true, 300)
    if (g.fires.burning(Fires::Car, car.uid)) return;
    const int type = car.infoId >= 0 && car.infoId < (int)g.vehicleInfos.size() ? g.vehicleInfos[car.infoId].type() : 0;
    const int style = (type & 0xFFFE) == 4 ? 4 : 2;
    // 0.45 x the half length towards the engine (cVehicle +0x94C bit 4: at the back) and 0.25 up
    const int32_t y = (int32_t)(((int64_t)car.hy * 0x733) >> 12);
    const int32_t offset[3] = {0, car.rearEngine() ? -y : y, 0x400};
    int32_t at[3];
    car.localToWorld(offset, at);
    g.fires.start(g, at, Fires::Car, car.uid, 300, style, true, true, offset);
}
