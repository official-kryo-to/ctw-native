// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "sound.h"
#include "game.h"
#include "cargens.h"   // Rand32Critical
#include "audio/audio.h"
#include "os/gamefs.h"
#include "os/os.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>

namespace {
inline int32_t isqrt64(int64_t v) { return v <= 0 ? 0 : (int32_t)std::sqrt((double)v); }
std::string bankFile(int e) {   // eRomBanks -> file naming convention
    if (e == 0) return "resbnk";
    if (e == 19) return "carbnkpl";
    if (e == 20) return "carbnkgn";
    if (e < 1 || e > 24) return {};
    int number = e <= 4 ? 1 : e <= 18 ? e - 3 : e - 5;
    return "carbnk" + std::to_string(number);
}
}   // namespace

static Sound g_sound;
Sound& TheSound() { return g_sound; }

bool Sound::Bank::load(const std::string& dataDir, const char* name) {
    GameFs fs;
    data.clear();
    entries.clear();
    if (!fs.open(dataDir) || !fs.read((std::string(name) + ".bin").c_str(), data) || data.size() < 4) return false;
    uint32_t n;
    memcpy(&n, data.data(), 4);
    if (4 + (size_t)n * 12 > data.size()) return false;
    entries.resize(n);
    memcpy(entries.data(), &data[4], (size_t)n * 12);
    return true;
}

bool Sound::Bank::sample(int i, const uint8_t*& pcm, uint32_t& len, uint32_t& rate) const {
    if (i < 0 || i >= (int)entries.size()) return false;
    uint32_t off = entries[i].offset;
    if (off + 0x20 > data.size()) return false;
    memcpy(&len, &data[off + 4], 4);
    memcpy(&rate, &data[off + 0x14], 4);
    if (off + 0x20 + (size_t)len > data.size()) len = (uint32_t)(data.size() - off - 0x20);
    pcm = &data[off + 0x20];
    return len > 0;
}

bool Sound::init(const std::string& dataDir) {
    for (auto& item : ents_) stopSlots(item.second);
    stopSlots(ped_);
    lastWalkFrame_ = -1; firstFoot_ = true;
    ents_.clear();
    car_ = Bank{}; carBankEnum_ = -1; playerUid_ = 0;
    res_.data.clear(); res_.entries.clear();
    if (!tables_.load(dataDir + "/sound_tables.bin")) return false;
    if (!Audio_Init()) return false;
    return res_.load(dataDir, "resbnk");
}

void Sound::stopSlots(Entity& e, bool bankOnly) {
    for (Slot& s : e.s) {
        if (bankOnly && (s.event == 0x9C || tables_.events[s.event].bank == 0)) continue;
        if (s.voice) Audio_SfxStop(s.voice);
        s = Slot{};
    }
}

void Sound::stopLoops(Entity& e) {
    for (Slot& s : e.s) {
        if (s.event == 0x9C || tables_.events[s.event].mode != 1) continue;
        if (s.voice) Audio_SfxStop(s.voice);
        s = Slot{};
    }
}

int Sound::addEvent(Entity& e, int event, int volume, int radius, int sfx) {   // cAudioManager::AddSoundEvent
    if (event < 0 || event >= 156) return -1;
    const EventInfo& ei = tables_.events[event];
    if (ei.mode == 1)   // a looping event already playing just gets refreshed
        for (int i = 0; i < 5; ++i)
            if (e.s[i].event == event) {
                Slot& s = e.s[i];
                if (s.sfx != sfx) {
                    if (s.voice) Audio_SfxStop(s.voice);
                    s.voice = 0; s.freq = 0; s.sfx = sfx;
                }
                s.active = true; s.volume = volume; s.radius = radius;
                return i;
            }
    for (int i = 0; i < 5; ++i)
        if (e.s[i].event == 0x9C) {
            Slot& s = e.s[i];
            if (s.voice) Audio_SfxStop(s.voice);
            s = Slot{};
            s.event = event; s.active = true; s.volume = volume; s.radius = radius; s.sfx = sfx;
            return i;
        }
    return -1;
}

void Sound::processEntity(Game& g, Entity& e, bool persistent) {   // cAudioManager::ProcessStandardSfx
    int32_t ear[3];
    g.focus(ear);
    WorldCamera cam;
    g.viewCamera(cam);
    int64_t dx = e.pos[0] - ear[0], dy = e.pos[1] - ear[1], dz = e.pos[2] - ear[2];
    uint32_t d2 = (uint32_t)((dx * dx + dy * dy + dz * dz) >> 24);
    for (Slot& s : e.s) {
        if (s.event == 0x9C) continue;
        const EventInfo& ei = tables_.events[s.event];
        if (ei.mode == 1 && !s.active && !persistent) {
            if (s.voice) Audio_SfxStop(s.voice);
            s = Slot{};
            continue;
        }
        // cAudioManager::ComputeVolume
        int vol = 0;
        if (s.radius > 0 && d2 <= 100000) {
            uint32_t R = (uint32_t)s.radius;
            if (d2 <= R) {
                uint32_t near = R * 0x333 >> 12;
                if (d2 < near) vol = s.volume;
                else {
                    int k = (int)(R - near) ? (int)(((R - d2) * 0x1000) / (R - near)) : 0;
                    k = std::max(k, 0);
                    vol = ((k * k) >> 12) * s.volume >> 12;
                }
            }
        }
        if (vol < 1) {   // inaudible: one-shots are dropped, loops stop until they come back in range
            if (ei.mode == 2) s.event = 0x9C;
            if (s.voice) { Audio_SfxStop(s.voice); s.voice = 0; }
            if (!s.active && !persistent) s.event = 0x9C;
            s.active = false;
            continue;
        }
        // cAudioManager::ComputePan (camera space; 0 left .. 127 right)
        float px = (e.pos[0] / 4096.f - cam.eye[0]), py = (e.pos[1] / 4096.f - cam.eye[1]), pz = (e.pos[2] / 4096.f - cam.eye[2]);
        float lx = px * cam.right[0] + py * cam.right[1] + pz * cam.right[2];
        float lz = px * cam.fwd[0] + py * cam.fwd[1] + pz * cam.fwd[2];
        int32_t zq = (int32_t)(lz * 4096.f) >> 12;
        int pan;
        if (zq < 0x24) zq = 0x24;
        if (zq > 0x198) pan = 0x3F;
        else {
            int32_t k = (int32_t)(0x100000000000LL / (int64_t)zq >> 12) & ~0xFF;
            k = std::clamp(k, -0x1E000, 0x1E000);
            int32_t x = std::clamp((int32_t)(lx * 4096.f), -0x14000, 0x14000);
            pan = std::clamp((0x80 - ((int32_t)(((int64_t)k * x) >> 12) >> 13)) >> 1, 0, 0x7F);
        }
        if (ei.pan != -1) pan = ei.pan;
        float fpan = pan / 127.f * 2.f - 1.f;
        if (!s.voice) {   // cSoundEvents::StartSoundEvent
            int sfx = s.sfx != -1 ? s.sfx : ei.sfx;
            const Bank& b = ei.bank == 0 ? res_ : car_;
            const uint8_t* pcm;
            uint32_t len, rate;
            if (!b.sample(sfx, pcm, len, rate)) { s.event = 0x9C; continue; }
            s.rate = rate;
            s.voice = Audio_SfxPlay(pcm, len, (int)rate, vol / 127.f, fpan, ei.mode == 1);
            if (getenv("CTW_SNDDBG")) printf("sfx: event 0x%X sample %d (bank %d) vol %d pan %d loop %d -> voice %d\n", s.event, sfx, ei.bank, vol, pan, ei.mode == 1, s.voice);
            if (!s.voice) { s.event = 0x9C; continue; }
        } else if (!Audio_SfxPlaying(s.voice)) {   // a one-shot finished
            s.voice = 0;
            s.event = 0x9C;
            continue;
        }
        float pitch = 1.f;   // cAudioBaseOAL::SetVolumeAndPanAndFrequency
        if (s.freq != 0) pitch = std::max(0.f, ((float)s.rate - (float)s.freq) / (float)s.rate * 20.f + 1.f);
        Audio_SfxSet(s.voice, vol / 127.f, fpan, pitch);
        s.active = false;
    }
}

void Sound::doorEvent(const Vehicle& v, bool open) {
    if (!ok()) return;
    Entity& e = ents_[v.uid];
    for (int k = 0; k < 3; ++k) e.pos[k] = v.pos[k];
    addEvent(e, open ? 0x62 : 0x61, 0x78, 1000, -1);
}

void Sound::collision(const Vehicle& v, int strength) {   // cAudioManager::AddCollision
    if (!ok()) return;
    uint32_t now = (uint32_t)(OS_TimeAccurate() * 1000.0);
    if (now - lastCollision_ <= 200) return;
    lastCollision_ = now;
    const int32_t* tab = strength > 0x32 ? (strength > 0x45 ? tables_.collisionHigh : tables_.collisionMed) : tables_.collisionLow;
    int off = strength > 0x32 ? (strength > 0x45 ? 0x1E : 0) : -0x14;
    int vol = std::clamp(off + strength - (int)Rand32Critical(0x1E), 5, 0x7F);
    Entity& e = ents_[v.uid];
    for (int k = 0; k < 3; ++k) e.pos[k] = v.pos[k];
    addEvent(e, tab[Rand32Critical(3)], vol, 200, -1);
}

int Sound::explosion(Game& game, const int32_t pos[3]) {
    const uint8_t* pcm; uint32_t len, rate;
    if (!res_.sample(0x1D2, pcm, len, rate)) return 0;
    int32_t ear[3]; game.focus(ear);
    int64_t distance = 0;
    for (int k = 0; k < 3; ++k) { int64_t d = (int64_t)pos[k] - ear[k]; distance += d * d; }
    double d2 = distance / 16777216.0;
    if (d2 >= 1900) return 0;
    float volume = d2 <= 380 ? 1.f : (float)((1900 - d2) / 1520);
    volume *= volume;
    WorldCamera cam; game.viewCamera(cam);
    float right = 0, forward = 0;
    for (int k = 0; k < 3; ++k) {
        float d = pos[k] / 4096.f - cam.eye[k];
        right += d * cam.right[k]; forward += d * cam.fwd[k];
    }
    float pan = std::clamp(right / std::max(36.f, forward), -1.f, 1.f);
    int voice = Audio_SfxPlay(pcm, len, (int)rate, volume, pan, false);
    if (getenv("CTW_SNDDBG")) printf("explosion: resident sample 466 -> voice %d\n", voice);
    return voice;
}

void Sound::carEngine(Game& g, Vehicle& v, Entity& e) {   // cSoundEvents::ProcessEntityTypeCar
    if (!v.engineOn || v.dead()) return;
    const GearSound& gs = tables_.gears[std::min<int>(g.vehicleInfos[v.infoId].raw[0x8C], 19)];
    int32_t sp = v.speed();
    int32_t capped = std::min(sp, 0xF000);
    if (sp < 3000) addEvent(e, 0xD, 0x5A, 200, gs.idle);
    else {
        int i = addEvent(e, 0xC, 0x5A, 200, gs.drive);
        if (i >= 0) {
            uint32_t rate = res_.entries.size() > (size_t)gs.drive ? res_.entries[gs.drive].rate : 22050;
            e.s[i].freq = (int32_t)rate - (int32_t)(((uint32_t)capped / 0xF * 200) >> 12);
        }
    }
}

void Sound::playerCar(Game& g, Vehicle& v, Entity& e) {   // cSoundEvents::ProcessEntityTypePlayerCar
    const VehicleInfo& info = g.vehicleInfos[v.infoId];
    const GearSound& gs = tables_.gears[std::min<int>(info.raw[0x8C], 19)];
    uint16_t flags = (uint16_t)info.s16(0x8E);
    if (playerUid_ != v.uid) {
        // The closing door can be queued on the very frame the player takes this seat.
        stopLoops(e);
        state_ = revs_ = volA_ = volB_ = gear_ = lastRpm_ = 0;
        damageClank_ = 0; skidTimer_ = skidCount_ = 0; hornHeld_ = -1; lastMs_ = 0;
        playerUid_ = v.uid;
    }
    // the horn (event 0x47): full volume while held, one more at half volume when let go
    if (horn && !(flags & 1)) { addEvent(e, 0x47, gs.volume, 300, gs.horn); hornHeld_ = 1; }
    else if (hornHeld_ >= 0) { addEvent(e, 0x47, gs.volume >> 1, 300, gs.horn); hornHeld_ = -1; }
    // the car's own bank goes in RAM bank 1
    if (carBankEnum_ != gs.bank) {
        for (auto& item : ents_) stopSlots(item.second, true);
        carBankEnum_ = gs.bank;
        std::string file = bankFile(gs.bank);
        car_ = Bank{};
        if (!file.empty()) car_.load(g.dataDir, file.c_str());
        state_ = 0; revs_ = 0; volA_ = volB_ = 0; gear_ = 0;
    }
    if (!v.engineOn || v.dead()) return;
    Vehicle::SoundState ss = v.soundState();
    int32_t sp = v.speed();
    // tyres: screech (0x13) while a rear wheel spins, a rumble (0x91) on a burst tyre
    int32_t skid = 0;
    if (skidCount_ >= 3) skidTimer_ = 10;
    if (skidTimer_ > 0 || ss.rearSpin || ss.frontSpin) {
        addEvent(e, 0x13, 0x1E, 300, -1);
        skid = 0x1000;
    }
    if (ss.burst && (sp >> 10) > 0x1E) addEvent(e, 0x91, std::min(sp >> 10, 0x9D) - 0x1E, 300, -1);
    if (v.health() < 0x50 && Rand32Critical(100) < 2 && damageClank_ < 1) {   // a damaged engine knocks now and then
        damageClank_ = 0x1000;
        addEvent(e, 0x60, 0x46, 300, Rand32Critical(100) > 0x31 ? 0x13B : 0x13A);
    }
    bool airborne = !ss.rearGround || !ss.frontGround;   // IsCarTyresOffGround
    uint32_t now = (uint32_t)(OS_TimeAccurate() * 1000.0);
    uint32_t dtms = lastMs_ ? std::min(now - lastMs_, 100u) : 33;
    lastMs_ = now;
    int ia = addEvent(e, 0x31, volA_, 300, -1), ib = addEvent(e, 0x32, volB_, 300, -1);
    const uint8_t* pcm;
    uint32_t len, rateA = 22050, rateB = 22050;
    car_.sample(0, pcm, len, rateA);
    car_.sample(1, pcm, len, rateB);
    int step = gs.step[std::clamp(gear_, 0, 5)];
    if (lastRpm_ - ss.rpm > 0x64000 && (state_ | 2) == 3) {   // the revs dropped (a hit): down a gear
        revs_ -= 0x1E000;
        if (revs_ < 0) { if (gear_ > 0) --gear_; revs_ = 0; }
    }
    if (airborne && ss.gas) state_ = 6;
    int32_t accel = isqrt64((int64_t)ss.accel[0] * ss.accel[0] + (int64_t)ss.accel[1] * ss.accel[1] + (int64_t)ss.accel[2] * ss.accel[2]);
    uint32_t dt = (dtms * 0x1000) / 1000;   // seconds, Q12
    auto toIdle = [&]() { state_ = 0; };
    auto highRevs = [&](int32_t r) { volB_ = 10; revs_ = r; volA_ = 0x5A; };
    if (!ss.gas && sp < 3000) { state_ = 0; revs_ = 0; volA_ = volB_ = 0; }
    if (ss.reverseGear && ss.gas && accel > 0xFFF) {   // the original tests gear -1 before the reversing state
        if (state_ != 5 && (flags >> 6 & 1)) addEvent(e, 0x57, 0x32, 300, -1);
        state_ = 5;
        if (flags >> 6 & 1) addEvent(e, 0x56, 0x23, 400, -1);
        highRevs(0x96000);   // DAT_00579a28
    } else if (state_ < 4) {
        int32_t add = (int32_t)dt * step;
        if (state_ == 0) {
            if (!ss.gas) {
                if (volB_ > 0x19) --volB_;
                if (volA_ >= 0x1A) { --volA_; if (volA_ == 0x19 && volB_ < 0x1B) volA_ = volB_ = 0; }
                else if (volB_ < 0x1B) volA_ = volB_ = 0;
                addEvent(e, 0xD, 0x7F, 300, gs.idle);
            } else { gear_ = 0; revs_ = 0x3C000; state_ = 1; }   // DAT_0057e918: state 1
        } else if (state_ == 1) {
            if (!ss.gas) state_ = 4;
            else {
                revs_ += add + skid * (int)Rand32Critical(0x14);
                if (sp < 10000) {
                    gear_ = 0;
                    revs_ = std::min(revs_, 0x1E000);
                } else if (gs.thr[std::clamp(gear_, 0, 4)] < revs_) {
                    ++gear_;
                    if (gear_ < 4) { if (flags >> 7 & 1) addEvent(e, 0x6A, 0x23, 200, -1); state_ = 2; }
                    else state_ = 3;
                } else {   // cSoundEvents::AdjustVolume: cross-fade the two engine loops with the revs
                    int k = ((revs_ >> 12) * 0x505) >> 12;
                    int a = ((k + 10) & 0xFFFF) * 0x1451 >> 12 & 0xFFFF, b = ((0x5A - k) & 0xFFFF) * 0x1451 >> 12 & 0xFFFF;
                    int d = a - volB_;
                    volB_ = std::abs(d) > 0x19 ? (a < volB_ ? volB_ - 0x19 : volB_ + 0x19) : a;
                    d = b - volA_;
                    volA_ = std::abs(d) > 0x19 ? (b < volA_ ? volA_ - 0x19 : volA_ + 0x19) : b;
                }
            }
        } else if (state_ == 2) {   // changing gear
            volA_ = std::max(volA_ - 7, 10);
            volB_ = std::max(volB_ - 7, 10);
            revs_ += (int32_t)dt * 0x80 - (int32_t)dt * 8;
            if (gs.thr[std::clamp(gear_ - 1, 0, 4)] + 0x14000 < revs_) { state_ = 1; revs_ = tables_.revAfterShift[std::clamp(gear_, 0, 3)]; }
        } else {   // top gear
            if (!ss.gas) state_ = 4;
            else {
                if (revs_ < gs.thr[std::clamp(gear_ - 1, 0, 4)]) revs_ += add;
                if (ss.rpm < (int32_t)((int64_t)ss.maxRpm * 5 / 6)) { gear_ = 0; revs_ = 0; state_ = 1; }
            }
        }
    } else if (state_ == 4) {   // coasting
        if (!ss.gas) {
            volA_ = std::max(volA_ - 5, 0x1E);
            volB_ = std::max(volB_ - 5, 0x1E);
            revs_ -= (int32_t)dt * 0xFF;
            if (revs_ < 0) { revs_ = 0; if (sp == 0) toIdle(); }
        } else toIdle();   // (then state 1 next frame)
    } else if (state_ == 5) { if (ss.reverseGear && ss.gas && sp >= 3000) highRevs(0x96000); else toIdle(); }
    else if (state_ == 6) { if (airborne) highRevs(0xD2000); else toIdle(); }   // DAT_00578788
    lastRpm_ = ss.rpm;
    int va, vb;
    if (damageClank_ < 1) { va = volA_ ? std::clamp(volA_ + gs.vol, 0, 0x7F) : 0; vb = volB_ ? std::clamp(volB_ + gs.vol, 0, 0x7F) : 0; }
    else { va = vb = 0; damageClank_ = (int16_t)(damageClank_ - 600); }
    if (ia >= 0 && ib >= 0) {
        e.s[ia].volume = va;
        e.s[ib].volume = vb;
        int32_t k = gs.pitch + (revs_ >> 12);
        e.s[ia].freq = (int32_t)(rateA - k) & ~3;
        e.s[ib].freq = (int32_t)(rateB - k) & ~3;
    }
}

void Sound::playerPed(Game& g) {
    const Player& player = g.player;
    for (int k = 0; k < 3; ++k) ped_.pos[k] = player.pos[k];
    if (g.playerCar >= 0 || player.hidden || player.attached || player.dead || !player.onGround() || player.level() == 0) {
        lastWalkFrame_ = -1;
        firstFoot_ = true;
        // Let a footfall finish when stopping, but discard it when leaving the on-foot controller.
        if (g.playerCar >= 0 || player.hidden || player.attached || player.dead) stopSlots(ped_);
        return;
    }
    // Observed in Android 4.4.243 ProcessEntityTypePlayerPed: the unarmed cycle emits event 0x35
    // at frames 6 and 13, volume 0x23 and squared radius 200. The foot alternates once per half-cycle.
    int frame = player.walkFrame();
    if (frame == lastWalkFrame_) return;
    if ((firstFoot_ && frame >= 6 && frame < 13) || (!firstFoot_ && frame >= 13)) {
        addEvent(ped_, 0x35, 0x23, 200, -1);
        firstFoot_ = !firstFoot_;
    }
    lastWalkFrame_ = frame;
}

void Sound::update(Game& g) {
    if (!ok()) return;
    ++ticks_;
    const uint32_t uid = g.playerCar >= 0 && g.playerCar < (int)g.cars.size() ? g.cars[g.playerCar].uid : 0;
    if (playerUid_ && uid != playerUid_) {
        auto it = ents_.find(playerUid_);
        if (it != ents_.end()) stopLoops(it->second);
        playerUid_ = 0;
    }
    if (skidTimer_ > 0) --skidTimer_;   // cSoundEvents::Process
    if (skidCount_ > 0) --skidCount_;
    playerPed(g);
    processEntity(g, ped_, false);
    for (auto& [uid, e] : ents_) e.seen = false;
    for (int i = 0; i < (int)g.cars.size(); ++i) {
        Vehicle& v = g.cars[i];
        Entity& e = ents_[v.uid];
        e.seen = true;
        for (int k = 0; k < 3; ++k) e.pos[k] = v.pos[k];
        if (i == g.playerCar) playerCar(g, v, e);
        else carEngine(g, v, e);
        processEntity(g, e, false);
    }
    for (auto it = ents_.begin(); it != ents_.end();) {   // entities that went away
        if (it->second.seen) { ++it; continue; }
        for (Slot& s : it->second.s)
            if (s.voice) Audio_SfxStop(s.voice);
        it = ents_.erase(it);
    }
}
