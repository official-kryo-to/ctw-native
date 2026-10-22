// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "weapons.h"
#include "fixedmath.h"
#include "game.h"
#include "particles.h"
#include "sniper.h"
#include "random.h"
#include "sound.h"
#include "text/gxt.h"
#include <glad/gl.h>
#include <algorithm>
#include <cmath>

struct PlayerYoke { bool released = false, pressed = false, held = false; };   // sWeaponYoke +0x14..+0x16
namespace {
PlayerYoke gYoke;
WeaponTables gTables;
bool gOk = false;
GxtFile gNames;
const WeaponInfo kNone{};

int fastsin(int a) { return (int)(sinf((float)a * 9.587378e-05f) * 4096.f); }
int16_t atan2q(int32_t x, int32_t y) {   // ATan2: atan2f(x, y) x 10430.38, clamped to int16
    const int v = (int)(atan2f((float)x, (float)y) * 10430.378f);
    return (int16_t)std::clamp(v, -32768, 32767);
}
}   // namespace

bool Weapons_Init(const std::string& dataDir) {
    gOk = gTables.load(dataDir + "/weapon_tables.bin");
    gNames.load(dataDir + "/e_main.gxt");
    return gOk;
}
bool Weapons_Ok() { return gOk; }
const WeaponTables& Weapons_Tables() { return gTables; }
const WeaponInfo& Weapons_Info(int type) { return type >= 0 && type < 28 ? gTables.info[type] : kNone; }
int Weapons_DefaultAmmo(int type) { return type >= 0 && type < 28 ? gTables.defaultAmmo[type] : 0; }
std::string Weapons_Name(int type) {
    const int id = Weapons_Info(type).textId;
    std::string s;
    if (id < 0 || (size_t)id >= gNames.count()) return "Weapon " + std::to_string(type);
    for (char16_t c : gNames.get(id)) s += c < 0x80 ? (char)c : '?';
    return s;
}
bool Weapons_Fires(int type) {   // the weapons whose CreateBullet is ported
    return type >= 5 && type <= 23;
}

// ---- cWeapon ----------------------------------------------------------------------------------------------------

void Weapon::setState(int s, int t) {   // cWeapon::SetState
    const WeaponInfo& info = Weapons_Info(type);
    if (s == 4) timer = info.reloadTime;
    else if (s == 5) timer = 50;
    else if (s == 1 || s == 2) timer = info.fireDelay;
    else if (s == 3) timer = 4;
    if (t) timer = t;
    flags |= 1u << s;
    state = s;
}

void Weapon::reload() {   // cBulletBasedWeapon::Reload (cWeapon::Reload is empty)
    if (melee()) return;
    const int c = Weapons_Info(type).clip;
    int v = ammo;
    if (v >= (uint8_t)c || v == -1) v = c;
    clip = (uint8_t)v;
}

void Weapon::addAmmo(int n) {   // cBulletBasedWeapon::AddAmmo (cWeapon::AddAmmo is empty)
    if (melee()) return;
    ammo = (int16_t)(n < 0 ? -1 : ammo + n);
    const int32_t maxAmmo = Weapons_Info(type).maxAmmo;
    int16_t v;
    if (maxAmmo >= ammo) v = ammo;
    else { ammo = (int16_t)maxAmmo; v = (int16_t)maxAmmo; }
    if (v <= -2) ammo = -1;
    if (!clip && (uint8_t)Weapons_Info(type).clip) reload();
}

int Weapon::reduceAmmo(int n) {   // cBulletBasedWeapon::ReduceAmmo
    if ((uint8_t)Weapons_Info(type).clip) {
        const int c = clip;
        if (c < n) {
            const int16_t a = ammo;
            clip = 0;
            if (a < 1) return c;
            ammo = 0;
            return c;
        }
        clip = (uint8_t)(c - n);
        if (ammo >= 1) ammo = (int16_t)(ammo - n);
        return n;
    }
    const int16_t a = ammo;
    if (a >= 1) {
        if (a >= n) { ammo = (int16_t)(a - n); return n; }
        ammo = 0;
        return a;
    }
    return n;
}

void Weapon::unload() {   // cBulletBasedWeapon::Unload (cWeapon::Unload is empty)
    if (melee()) return;
    if (ammo != -1) {
        const int32_t maxAmmo = Weapons_Info(type).maxAmmo;
        if (maxAmmo < ammo) ammo = (int16_t)maxAmmo;
    }
    setState(3, 0);
    clip = 0;
}

// ---- cWeaponManager ---------------------------------------------------------------------------------------------

WeaponManager::WeaponManager() { reset(); }

void WeaponManager::reset() {   // cWeaponManager::Reset
    mode = 0;
    for (int i = 1; i < kSlots; ++i) slots[i].reset();
    cur = secondary = 0;
    if (!slots[0]) slots[0] = std::make_unique<Weapon>(0);   // the cFistWeapon at +8
    changed = true;
    accuracyExtra = 0;
    savedSlot = 0;
    if (!locked) {
        setUse(0, false);
        cyclePrev(true);
        if (current()->type == 17) cyclePrev(true);
    }
    explosiveRounds = false;
}

void WeaponManager::clearAll() {
    const bool wasLocked = locked;
    locked = false;
    reset();
    locked = wasLocked;
}

void WeaponManager::switchedTo(Weapon* w) {   // a weapon whose clip is not full reloads when chosen
    if (w && w->ammoInClip() != (uint8_t)Weapons_Info(w->type).clip) w->unload();
}

void WeaponManager::create(int slot, int type, int ammo) {   // cWeaponManager::CreateNewWeapon
    if (slot <= 7) changed = true;
    if (type < 0 || type > 24) return;
    if (type != 0) slots[slot] = std::make_unique<Weapon>(type);
    if (slots[slot]) slots[slot]->addAmmo(ammo);
}

bool WeaponManager::give(int type, int ammo, bool replace) {   // cWeaponManager::GiveWeapon
    if (type < 0 || type >= 28) return false;
    if (type == 24) return true;   // GiveObjThrowerWeapon: thrown objects are not ported
    const int slot = Weapons_Info(type).slot;
    int carried = 0;
    Weapon* w = slots[slot].get();
    if (w && replace && w->type != type) {
        carried = (uint8_t)(slot - 2) > 3 ? 0 : w->ammoCount();
        slots[slot].reset();
        if ((slot > 7 ? secondary : cur) == slot) changed = true;
        w = nullptr;
    }
    bool switchOk = false, keepSaved = false;
    if ((slot <= 7 && noSwitchPrimary) || (slot >= 8 && noSwitchSecondary)) {
        switchOk = true;
        keepSaved = true;
    } else {
        const bool fistOrFour = type == 0 || slot == 4;
        const bool driving = !fistOrFour && mode == 1;
        switchOk = driving && slot != 2 && slot < 8;
    }
    if (mode == 1 && (slot == 4 || slot == 2) && cur != 2 && cur != 4) switchOk = keepSaved = false;
    auto after = [&](void) {
        if (type == 17) return;
        const Weapon* now = slots[slot <= 7 ? cur : secondary].get();
        if (now && Weapons_Info(type).slot <= Weapons_Info(now->type).slot) return;
        if (!switchOk) setUse(type, false);
        else if (!keepSaved) savedSlot = (uint8_t)slot;
    };
    if (w) {
        if (w->type == type) {
            if (w->ammoCount() != Weapons_Info(w->type).maxAmmo) {
                w->addAmmo(ammo);
                after();
                return true;
            }
        } else if ((unsigned)(Weapons_Info(type).slot - 2) <= 3) {
            Weapon* inSlot = slots[Weapons_Info(type).slot].get();
            if (inSlot && inSlot->ammoCount() && inSlot->type >= type) {
                inSlot->addAmmo(ammo);
                return true;
            }
        }
        if (w->ammoCount()) return false;
        slots[slot].reset();
        if ((slot > 7 ? secondary : cur) == slot) changed = true;
    } else {
        ammo += std::max(carried, 0);
    }
    create(slot, type, ammo);
    after();
    return true;
}

void WeaponManager::setUse(int type, bool) {   // cWeaponManager::SetUseWeapon
    if (locked || type < 0 || type >= 28) return;
    const int slot = Weapons_Info(type).slot;
    Weapon* w = slots[slot].get();
    if (w && w->type == type) {
        if (slot > 7) secondary = (uint8_t)slot;
        else if (cur != slot) {   // (re-selecting the current one only reloads when the yoke asks: not ported)
            cur = (uint8_t)slot;
            if (mode == 1) savedSlot = (uint8_t)slot;
            switchedTo(w);
        }
    }
}

void WeaponManager::cycleNext(bool primary) {   // cWeaponManager::CycleToNextWeapon
    if (locked) return;
    const int from = primary ? cur : secondary;
    changed = true;
    if (primary) {
        int s = ((from + 1) & 0xF8) ? 0 : from + 1;
        for (int n = 0; n < 7; ++n) {
            Weapon* w = slots[s].get();
            if (w && w->ammoCount()) {
                cur = (uint8_t)s;
                switchedTo(w);
                return;
            }
            s = ((s + 1) & 0xF8) ? 0 : s + 1;
        }
        return;
    }
    int s = from + 1;
    if (!from) s = 8;
    if (s > 9) s = 0;
    if (slots[s] && slots[s]->ammoCount()) secondary = (uint8_t)s;
}

void WeaponManager::cyclePrev(bool primary) {   // cWeaponManager::CycleToPrevWeapon
    if (locked) return;
    changed = true;
    int s = primary ? cur : secondary;
    for (int n = primary ? 8 : 2; n > 0; --n) {
        if (primary) s = (uint8_t)(s - 1) >= 7 ? 7 : s - 1;
        else s = ((uint8_t)(s - 1) & 0xF8) ? std::min((uint8_t)(s - 1), (uint8_t)9) : 0;
        Weapon* w = slots[s].get();
        if (w && w->ammoCount()) {
            if (primary) { cur = (uint8_t)s; switchedTo(w); }
            else secondary = (uint8_t)s;
            return;
        }
    }
}

void WeaponManager::cycleToBest(bool primary, bool inVehicle) {   // cWeaponManager::CycleToBestWeapon
    if (locked) return;
    changed = true;
    if (primary) {
        setUse(0, false);
        cyclePrev(true);
        if (current()->type == 17) cyclePrev(true);
        if (inVehicle)
            for (int n = 0; n < 8 && cur != 2 && cur != 4 && cur; ++n) cyclePrev(true);
        return;
    }
    int s = (uint8_t)(secondary - 1);
    s = (s & 0xF8) ? std::min(s, 9) : 0;
    if (slots[s] && slots[s]->ammoCount()) { secondary = (uint8_t)s; return; }
    int t = (uint8_t)(s - 1);
    s = (t & 0xF8) ? std::min(t, 9) : 0;
    if (slots[s] && slots[s]->ammoCount()) secondary = (uint8_t)s;
}

bool WeaponManager::ready(bool primary) const {   // cWeaponManager::IsReady
    const Weapon* w = slots[primary ? cur : secondary].get();
    return w && (w->state == 0 || (w->flags & 1));
}

bool WeaponManager::reloading(bool primary) const {   // cWeaponManager::IsReloading
    const Weapon* w = slots[primary ? cur : secondary].get();
    return w && (w->state == 3 || w->state == 4 || (w->flags & 0x18));
}

int WeaponManager::accuracyModifier(int moveRate) const {   // cWeaponManager::AccuracyModifyer
    int v = (uint16_t)accuracyExtra;
    const WeaponInfo& info = Weapons_Info(current()->type);
    if (moveRate >= 1 && moveRate <= 3) v += (uint16_t)info.spread[moveRate];
    return v;
}

// ---- cBulletTraces ----------------------------------------------------------------------------------------------

void BulletTraces::clear() { for (Trace& t : traces_) t.active = false; }

void BulletTraces::add(const int32_t from[3], const int32_t vec[3], int kind) {   // AddTrace -> AddTraceRaw
    if (kind < 0 || kind > 2) return;
    const int life = Weapons_Tables().traceLife[kind];
    for (Trace& t : traces_) {
        if (t.active) continue;
        for (int k = 0; k < 3; ++k) { t.pos[k] = from[k]; t.vec[k] = vec[k]; t.step[k] = vec[k] / life; }
        t.life = (int8_t)life;
        t.active = true;
        return;
    }
}

void BulletTraces::update() {   // cBulletTraces::Update: the start moves towards the end
    for (Trace& t : traces_) {
        if (!t.active) continue;
        if (--t.life > 0)
            for (int k = 0; k < 3; ++k) { t.pos[k] += t.step[k]; t.vec[k] -= t.step[k]; }
        else t.active = false;
    }
}

void BulletTraces::render() const {   // cBulletTraces::Render: untextured lines, cGlInterface::Colour(0x2EBF, 0x1F)
    bool any = false;
    for (const Trace& t : traces_) any = any || t.active;
    if (!any) return;
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_LIGHTING);
    glEnable(GL_DEPTH_TEST);
    glColor4f((0x2EBF & 31) / 31.f, ((0x2EBF >> 5) & 31) / 31.f, ((0x2EBF >> 10) & 31) / 31.f, 0x1F / 31.f);
    glBegin(GL_LINES);
    for (const Trace& t : traces_) {
        if (!t.active) continue;
        const float a[3] = {t.pos[0] / 4096.f, t.pos[1] / 4096.f, t.pos[2] / 4096.f};
        glVertex3f(a[0], a[1], a[2]);
        glVertex3f(a[0] + t.vec[0] / 4096.f, a[1] + t.vec[1] / 4096.f, a[2] + t.vec[2] / 4096.f);
    }
    glEnd();
    glColor4f(1.f, 1.f, 1.f, 1.f);
}

// ---- cMuzzleFlashes ---------------------------------------------------------------------------------------------

void MuzzleFlashes::add(const int32_t pos[3], int16_t angle, int type, int16_t scale) {
    if (type < 0 || used_ >= 8) return;   // Add: type -1 is "no flash"; the ninth and later are dropped
    Flash& f = flashes_[used_++];
    std::copy(pos, pos + 3, f.pos);
    f.angle = angle;
    f.type = (int8_t)type;
    f.scale = scale;
    f.rolled = false;
}

bool MuzzleFlashes::quad(int i, Quad& out) {
    // Render: per type the half-width base, the length and a +-0.1 jitter on both (Q12)
    static const int32_t kSize[5][2] = {{0x1400, 0x3000}, {0x400, 0x2800}, {0xCCD, 0x2000}, {0x1000, 0x2333},
                                        {0x1000, 0x5000}};
    if (i < 0 || i >= used_) return false;
    Flash& f = flashes_[i];
    if (f.type > 4) return false;
    if (!f.rolled) {   // RenderFlash: a + Rand32NonCritical(2r) - r, then times the flash's scale; same for b
        const int32_t r = 0x19A;
        int32_t a = kSize[f.type][0] + ((int32_t)Rand32NonCritical((uint32_t)(r << 1)) - r);
        f.width = (int32_t)((uint64_t)((int64_t)f.scale * a) >> 12);
        int32_t b = kSize[f.type][1] + ((int32_t)Rand32NonCritical((uint32_t)(r << 1)) - r);
        f.length = (int32_t)((uint64_t)((int64_t)f.scale * b) >> 12);
        f.rolled = true;
    }
    // the matrix rows: right (cos, -sin, 0) and forward (sin, cos, 0), each through Normalise
    const int c = fastsin(f.angle + 0x4000), s = fastsin(f.angle);
    int32_t right[3] = {c, -s, 0}, fwd[3] = {s, c, 0};
    Normalise3(right);
    Normalise3(fwd);
    // the quad is centred one length ahead of the muzzle, spanning +-2a across and +-b along the fire direction
    for (int k = 0; k < 3; ++k) {
        const int32_t centre = f.pos[k] + (int32_t)((uint64_t)((int64_t)fwd[k] * f.length) >> 12);
        out.centre[k] = centre / 4096.f;
        out.right[k] = right[k] / 4096.f;
        out.forward[k] = fwd[k] / 4096.f;
    }
    out.halfWidth = (f.width << 1) / 4096.f;
    out.halfLength = f.length / 4096.f;
    return true;
}

void MuzzleFlashes::render() {
    if (!used_) return;
    // cEffectManager::Render: BlendFunc(GL_SRC_ALPHA, GL_ONE) just before cMuzzleFlashes::Render, no depth writes
    glEnable(GL_DEPTH_TEST);
    for (int i = 0; i < used_; ++i) {
        Quad q;
        if (!quad(i, q)) continue;
        // RenderWorldScaleCamAligned puts the sprite rect's left edge on +right; DrawSheetSprite puts it on -ax
        const float left[3] = {-q.right[0], -q.right[1], -q.right[2]};
        DrawSheetSprite(3, 0xFFFFFFFFu, q.centre, left, q.forward, q.halfWidth, q.halfLength, true);
    }
}

// ---- firing -----------------------------------------------------------------------------------------------------

namespace {
// what a bullet can hit (sGetClosestHitThing): peds and the player as vertical cylinders of their bound radius,
// vehicles as their collision box (100 units high) in their own frame
struct Hit { int kind = 0; int index = -1; int64_t dist = 0; };   // kind 1 ped, 2 vehicle

void weaponSound(Player* shooter, uint32_t uid, int event, int volume, int radius) {
    if (shooter) TheSound().pedEvent(uid, shooter->pos, event, volume, radius);
    else TheSound().playerEvent(event, volume, radius);
}

// the sGetClosestHitThing::operator() cylinder test; returns the distance^2 >> 12, or -1
int64_t cylinderDistance(const int32_t pos[3], int32_t radius, const int32_t start[3], const int16_t dir[2],
                         int32_t reach2) {
    const int32_t dx = pos[0] - start[0], dy = pos[1] - start[1];
    if (((int64_t)dir[0] * dx + (int64_t)dir[1] * dy) & 0x80000000000LL) return -1;
    int64_t perp = (int64_t)dir[0] * dy - (int64_t)dir[1] * dx;
    if (perp < 0) perp = -perp;
    if ((((int64_t)(uint32_t)radius << 12) - perp) & 0x80000000000LL) return -1;
    const int32_t dz = pos[2] - start[2];
    const int32_t lim = (int32_t)(uint32_t)std::sqrt((double)(uint64_t)((int64_t)reach2 << 12));
    if (std::abs(dx) > lim || std::abs(dy) > lim || std::abs(dz) > lim) return -1;
    const int64_t d = ((int64_t)dx * dx + (int64_t)dy * dy + (int64_t)dz * dz) >> 12;
    if (reach2 < (int32_t)d || (d & 0x80000000)) return -1;
    return d;
}

// HelperMakeTraceAndFlash for an on-foot shooter (the player); returns true if something was hit
void traceAndFlash(Game& g, Weapon& w, const int32_t origin[3], int32_t dir[3], int32_t range, int traceKind,
                   int damage, bool accuracy, int flash, Player* shooter = nullptr, uint32_t uid = 0) {
    const Player& body = shooter ? *shooter : g.player;
    if (range < 40) return;
    const WeaponInfo& info = Weapons_Info(w.type);
    if (accuracy) {   // CalcAccuracyDir
        const int level = body.level();
        int spread = info.spread[0] + (shooter ? (level >= 1 && level <= 3 ? info.spread[level] : 0) :
                                     (int16_t)g.playerWeapons.accuracyModifier(level));
        if (spread) {
            if ((unsigned)spread >= 0x7FFF) spread = 0x7FFF;
            const int16_t a = (int16_t)(Rand16Critical((uint32_t)(2 * spread)) - spread);
            const int c = fastsin(a + 0x4000), s = fastsin(a);
            const int32_t x = dir[0], y = dir[1];
            dir[0] = (int32_t)((uint64_t)((int64_t)x * c + (int64_t)y * -s) >> 12);
            dir[1] = (int32_t)((uint64_t)((int64_t)x * s + (int64_t)y * c) >> 12);
        }
    }
    // the trace starts at the muzzle: the player's +2 units up and 1.5 units out (TweakBulletOrigin)
    int32_t muzzle[3] = {origin[0], origin[1], origin[2] + 0x2000};
    {
        int32_t n[3] = {dir[0], dir[1], dir[2]};
        Normalise3(n);
        for (int k = 0; k < 3; ++k) muzzle[k] += (int32_t)((uint64_t)(3LL * n[k]) >> 1);
    }
    int32_t vec[3] = {(int32_t)((uint64_t)((int64_t)range * dir[0]) >> 12),
                      (int32_t)((uint64_t)((int64_t)range * dir[1]) >> 12),
                      (int32_t)((uint64_t)((int64_t)range * dir[2]) >> 12)};
    int32_t start[3] = {origin[0], origin[1], origin[2] + 0x2000};
    const int32_t end[3] = {start[0] + vec[0], start[1] + vec[1], start[2] + vec[2]};
    Collision::LineHit wall;
    const bool walled = g.collision.staticLine(start, end, 0x40001E00, &wall);
    auto beforeWall = [&](const int32_t point[3]) {
        if (!shooter || !walled) return true;
        int64_t along = 0, wallAlong = 0;
        for (int k = 0; k < 3; ++k) {
            const int32_t begin = origin[k] + (k == 2 ? 0x2000 : 0);
            along += (int64_t)dir[k] * (point[k] - begin);
            wallAlong += (int64_t)dir[k] * (wall.point[k] - begin);
        }
        return along < wallAlong;
    };
    if (walled) {   // the rest is measured from the muzzle
        std::copy(muzzle, muzzle + 3, start);
        for (int k = 0; k < 3; ++k) vec[k] = wall.point[k] - muzzle[k];
        range = (int32_t)(uint32_t)std::sqrt((double)(uint64_t)((int64_t)vec[0] * vec[0] + (int64_t)vec[1] * vec[1] +
                                                               (int64_t)vec[2] * vec[2]));
    }
    // sGetClosestHitThing (reach = range + 2 units)
    const int32_t reach = range + 0x2000;
    const int32_t reach2 = (int32_t)((uint64_t)((int64_t)reach * reach) >> 12);
    Hit best;
    int64_t closest = reach2;
    if (reach >= 409) {
        int32_t d2[2] = {vec[0], vec[1]};
        if (!d2[0] && !d2[1]) { d2[0] = (98304 * body.fwd[0]) >> 16; d2[1] = (98304 * body.fwd[1]) >> 16; }
        Normalise2(d2);
        const int16_t dir2[2] = {(int16_t)d2[0], (int16_t)d2[1]};
        for (size_t i = 0; i < g.peds.peds.size(); ++i) {
            const Pedestrians::Ped& p = g.peds.peds[i];
            // cPed::CanBeHit: a ped in a closed vehicle cannot be hit (the bullet goes on to the vehicle). Riders of
            // open-top vehicles can, but their damage on a vehicle is not ported, so they are skipped as well.
            if (&p.body == shooter || (shooter && p.uid == uid) || p.vehicle || p.deadFrames >= 0 || p.body.dead ||
                !WorldSectors::inBox(body.pos, reach, p.body.pos[0], p.body.pos[1]) ||
                !g.sectors.loadedAt(p.body.pos[0], p.body.pos[1]) || !beforeWall(p.body.pos)) continue;
            const int64_t d = cylinderDistance(p.body.pos, Player::kRadius, start, dir2, reach2);
            if (d >= 0 && closest > d) { closest = d; best = {1, (int)i, d}; }
        }
        if (shooter && g.playerCar < 0 && !g.player.dead && !g.player.hidden && !g.arrested() && beforeWall(g.player.pos)) {
            const int64_t d = cylinderDistance(g.player.pos, Player::kRadius, start, dir2, reach2);
            if (d >= 0 && closest > d) { closest = d; best = {3, -1, d}; }
        }
        for (size_t i = 0; i < g.cars.size(); ++i) {
            const Vehicle& c = g.cars[i];
            if ((!shooter && (int)i == g.playerCar) || !WorldSectors::inBox(body.pos, reach, c.pos[0], c.pos[1]) ||
                !g.sectors.loadedAt(c.pos[0], c.pos[1])) continue;
            const int64_t d = cylinderDistance(c.pos, c.boundRadius(), start, dir2, reach2);
            if (d < 0 || closest <= d) continue;
            // CCollision::SweptVertVBox from the start to the reach along the trace, in the vehicle's frame
            const int32_t r = (int32_t)(uint32_t)std::sqrt((double)(uint64_t)((int64_t)reach2 << 12));
            int32_t a[3] = {start[0], start[1], start[2]};
            int32_t b[3] = {start[0] + (int32_t)(((int64_t)dir2[0] * r) >> 12),
                            start[1] + (int32_t)(((int64_t)dir2[1] * r) >> 12), start[2]};
            auto toLocal = [&](const int32_t p[3], int32_t out[3]) {
                const int64_t q[3] = {(int64_t)p[0] - c.pos[0], (int64_t)p[1] - c.pos[1], (int64_t)p[2] - c.pos[2]};
                out[0] = (int32_t)((q[0] * c.right[0] + q[1] * c.right[1] + q[2] * c.right[2]) >> 12);
                out[1] = (int32_t)((q[0] * c.fwd[0] + q[1] * c.fwd[1] + q[2] * c.fwd[2]) >> 12);
                out[2] = (int32_t)((q[0] * c.up[0] + q[1] * c.up[1] + q[2] * c.up[2]) >> 12);
            };
            int32_t la[3], lb[3], hit[3], n[3], t;
            toLocal(a, la);
            toLocal(b, lb);
            Collision::Box box{};
            box.hx = c.hx; box.hy = c.hy; box.hz = 409600;
            if (!Collision::sweptVertVBox(la, lb, box, hit, n, t)) continue;
            int32_t hitWorld[3];
            c.localToWorld(hit, hitWorld);
            if (!beforeWall(hitWorld)) continue;
            closest = d;
            best = {2, (int)i, d};
        }
    }
    int32_t traceVec[3] = {vec[0], vec[1], vec[2]};
    if (best.kind) {
        const int32_t len = (int32_t)(uint32_t)std::sqrt((double)(uint64_t)(best.dist << 12));
        for (int k = 0; k < 3; ++k) traceVec[k] = (int32_t)((uint64_t)((int64_t)dir[k] * len) >> 12);
        if (best.kind == 1) {
            g.peds.bulletHit(g, g.peds.peds[best.index], damage, dir, info.effect, body.pos, w.type == 15 ? 2 : 1, !shooter);
            if (!shooter) g.pedCrime(g.peds.peds[best.index], w.type == 15 ? 2 : 1);
        } else if (best.kind == 3) {
            g.player.takeDamage(damage);
            g.addBlood(g.player.pos, damage >= 5 ? 15 : 0);
        } else {
            Vehicle& c = g.cars[best.index];
            // cVehicle::Damage, sDamageInfo type 1: a quarter of the damage (doors, sparks and the takedown logic
            // are not ported)
            c.damage(damage >> 2);
            if (!shooter) g.vehicleReaction(c, 1, damage, 0, body.pos);
            else c.lastDamagedBy = 0;   // NPC damage must not credit a previous player hit or register a player crime
        }
    }
    g.traces.add(muzzle, traceVec, traceKind);
    // then cMuzzleFlashes::Add at the trace's start, facing -ATan2(-dir.x, dir.y), at unit scale
    g.muzzleFlashes.add(muzzle, (int16_t)-atan2q(-dir[0], dir[1]), flash);
    // a trace that stopped at a wall without hitting anyone: cAudioManager::AddRicochet at its end. Only the
    // pistols, SMGs and rifles (weapon-type mask 0x4E3E0) ricochet, 15% of the time, with one of three samples.
    if (walled && !best.kind && w.type < 19 && (0x4E3E0u >> w.type & 1) && Rand32NonCritical(100) < 15) {
        TheSound().scriptSfx(wall.point, 723 + (int)Rand32NonCritical(3), 105, 1500);   // muzzle + vec
    }
}

// the gun's CreateBullet: FireSfx, then one trace (three for the shotguns)
void createBullet(Game& g, Weapon& w, Player* shooter = nullptr, uint32_t uid = 0) {
    const Player& body = shooter ? *shooter : g.player;
    static const int kFireSfx[19][3] = {   // cAudioManager::AddSoundEvent(event, volume, radius) per FireSfx
        {}, {}, {}, {}, {}, {0, 100, 1000}, {0, 100, 1000}, {34, 100, 1000}, {39, 100, 1000}, {39, 100, 1000},
        {1, 40, 200}, {1, 40, 200}, {1, 100, 1000}, {3, 100, 1000}, {3, 100, 1000}, {38, 70, 1000}, {}, {}, {45, 100, 1000}};
    if (w.type == 17) { g.projectiles.fireRocket(g); return; }   // cRocketLauncher (FireSfx: event 44)
    if (w.type == 23) { g.projectiles.dropMine(g); return; }     // cMineLauncher
    if (w.type >= 20 && w.type <= 22) { g.projectiles.throwGrenade(g, w.type, g.player.throwPower); return; }
    if (w.type < 19 && kFireSfx[w.type][2]) weaponSound(shooter, uid, kFireSfx[w.type][0], kFireSfx[w.type][1], kFireSfx[w.type][2]);
    if (w.type == 18) { Sniper_CreateBullet(g, w); return; }   // cSniperRifle: the scope's aim point, no trace
    const WeaponInfo& info = Weapons_Info(w.type);
    const int32_t range = info.range;   // cPlayer::PrimaryWeaponRange on foot
    const int32_t origin[3] = {body.pos[0], body.pos[1], body.pos[2]};   // GetBulletOrigin (on foot)
    // cPed::FireDirection (+0x32A): the facing while nothing is targeted (or a mod's aim point)
    const int32_t f[3] = {body.fireDir[0], body.fireDir[1], body.fireDir[2]};
    if (w.type >= 10 && w.type <= 12) {   // cShotgun / cSawnOffShotgun / cDoubleBarreledShotgun: +-5 degrees
        const int32_t x = f[0], y = f[1];
        const int32_t a = (int32_t)((uint64_t)(357LL * y) >> 12), b = (int32_t)((uint64_t)(4080LL * x) >> 12);
        const int32_t c = (int32_t)((uint64_t)(357LL * x) >> 12), d = (int32_t)((uint64_t)(4080LL * y) >> 12);
        int32_t d0[3] = {x, y, f[2]}, d1[3] = {b - a, d + c, f[2]}, d2[3] = {a + b, d - c, f[2]};
        traceAndFlash(g, w, origin, d0, range, 1, info.damage, true, 0);   // only the centre pellet flashes
        traceAndFlash(g, w, origin, d1, range, 1, info.damage, true, -1);
        traceAndFlash(g, w, origin, d2, range, 1, info.damage, true, -1);
        return;
    }
    int32_t d[3] = {f[0], f[1], f[2]};
    const int kind = w.type == 15 ? 2 : (w.type == 13 || w.type == 14) ? 1 : 0;   // cMinigun: trace 2, flash 4
    const int flash = w.type == 15 ? 4 : (w.type == 13 || w.type == 14) ? 0 : 2;   // cColt / cAK47: flash 0
    traceAndFlash(g, w, origin, d, range, kind, info.damage, true, flash, shooter, uid);
    if (w.type == 6) ++w.shots;
}

// cMeleeBasedWeapon::HitStuffHelper for the player: every ped within the weapon's range (and in sight) inside
// +-angle of the fire direction, measured to a point 2 units in front of it, takes pedDamage; a vehicle whose box
// holds the point 2 units in front of the player takes vehicleDamage. (Props are not ported here yet.)
void hitStuff(Game& g, Weapon& w, int16_t angle, int pedDamage, int vehicleDamage, int type,
              Player* shooter = nullptr, uint32_t uid = 0) {
    const int32_t range = Weapons_Info(w.type).range;   // cPed::PrimaryWeaponRange
    const int64_t range2 = (((int64_t)range * range) << 20 >> 20) & ~0xFFFLL;
    const Player& body = shooter ? *shooter : g.player;
    const int32_t* a = body.pos;
    const int16_t* f = body.fireDir;
    const int16_t facing = atan2q(-f[0], f[1]);
    bool hitPed = false, hitCar = false;
    auto inRange = [&](const int32_t t[3]) {
        const int64_t dx = t[0] - a[0], dy = t[1] - a[1], dz = t[2] - a[2];
        if (range2 < dx * dx + dy * dy + dz * dz) return false;
        const int32_t s[3] = {a[0], a[1], a[2] + 0x2000}, e[3] = {t[0], t[1], t[2] + 0x2000};
        if (g.collision.staticLine(s, e, 0x80000200)) return false;   // line of sight (boxes)
        const int16_t to = atan2q(a[0] - (t[0] + 2 * f[0]), (t[1] + 2 * f[1]) - a[1]);
        const int16_t diff = (int16_t)(facing - to);
        return diff < angle && diff > -angle;
    };
    for (Pedestrians::Ped& p : g.peds.peds) {
        if (&p.body == shooter || (shooter && p.uid == uid) || p.vehicle || p.deadFrames >= 0 || p.body.dead ||
            !inRange(p.body.pos)) continue;
        g.peds.meleeHit(g, p, pedDamage, type, a, Weapons_Info(w.type).effect, !shooter);
        if (!shooter) g.pedCrime(p, type);
        hitPed = true;
    }
    if (shooter && g.playerCar < 0 && !g.player.dead && !g.player.hidden && !g.arrested() && inRange(g.player.pos)) {
        g.player.takeDamage(pedDamage);
        g.addBlood(g.player.pos, Weapons_Info(w.type).effect == 0x1D ? 0 : 4);
        hitPed = true;
    }
    for (Vehicle& c : g.cars) {
        if (c.dead()) continue;
        const int64_t q[3] = {(int64_t)a[0] + 2 * f[0] - c.pos[0], (int64_t)a[1] + 2 * f[1] - c.pos[1],
                              (int64_t)a[2] + 2 * f[2] - c.pos[2]};
        const int32_t lx = (int32_t)((q[0] * c.right[0] + q[1] * c.right[1] + q[2] * c.right[2]) >> 12);
        const int32_t ly = (int32_t)((q[0] * c.fwd[0] + q[1] * c.fwd[1] + q[2] * c.fwd[2]) >> 12);
        if (std::abs(lx) >= c.hx || std::abs(ly) >= c.hy) continue;
        c.damage(vehicleDamage);
        if (shooter) c.lastDamagedBy = 0;
        hitCar = true;
    }
    // HitSfx (a ped: only the fists make a sound; the chainsaw marks it for DoSound) and HitVehicleSfx
    if (hitPed) {
        if (w.type == 0) weaponSound(shooter, uid, 35, 50, 200);   // cFistWeapon::HitSfx
        else if (w.type == 4) w.sawHit = true;                  // cChainsaw::HitSfx
    }
    if (hitCar) {
        static const int kHitVehicleSfx[5][2] = {{85, 127}, {83, 127}, {81, 127}, {84, 127}, {82, 30}};
        if (w.type >= 0 && w.type <= 4) weaponSound(shooter, uid, kHitVehicleSfx[w.type][0], kHitVehicleSfx[w.type][1], 200);
    }
}

void fireWeapon(Game& g, Weapon& w, Player* shooter = nullptr, uint32_t uid = 0) {   // cBulletBasedWeapon::Fire / cMeleeBasedWeapon::Fire
    if (w.type == 16) { FlameThrower_Fire(g, w); return; }   // cFlameThrower::Fire
    if (w.type == 19) { Tazer_Fire(g, w, gYoke.held); return; }   // cTazer::Fire
    if (w.state != 0) return;
    if (w.melee()) {   // the weapon's HitStuff, then the swing's timer
        const int dmg = Weapons_Info(w.type).damage;
        if (w.type == 0) hitStuff(g, w, 0x2000, dmg, 1, 0, shooter, uid);           // cFistWeapon
        else if (w.type == 1) hitStuff(g, w, 0x2000, dmg, 3, 0, shooter, uid);      // cNiteStick (props 3)
        else if (w.type == 2) hitStuff(g, w, 0x2000, dmg, 3, 0);      // cBaseballBat (props 12)
        else if (w.type == 3) w.delayedHit = 7;                     // cSword::HitStuff
        else if (w.type == 4) hitStuff(g, w, 0x2000, 200, dmg, 12);   // cChainsaw (props 35)
        w.flags = 0;
        w.setState(1, 0);
        return;
    }
    if (!Weapons_Fires(w.type)) return;
    if (w.type == 15) {   // cMinigun::Fire: the barrel spins up (+2 a press) before it fires
        if (w.spin <= 8) { w.spin = (int8_t)(w.spin + 2); return; }
        w.spin = 9;
        if (w.ammoCount() != -1 && w.ammoCount() < 1) {
            TheSound().playerEvent(99, 35, 1000);   // FireEmptySfx
            w.setState(5, 0);
            return;
        }
        w.reduceAmmo(1);
        createBullet(g, w);
        w.setState(1, 0);
        return;
    }
    if (w.ammoCount() != -1 && w.ammoCount() < 1) {
        weaponSound(shooter, uid, 99, 35, 1000);   // cWeapon::FireEmptySfx
        w.setState(5, 0);
        return;
    }
    if ((uint8_t)Weapons_Info(w.type).clip) {
        if (!w.clip) {
            const int16_t a = w.ammo;
            w.clip = 0;
            if (a >= 1) w.ammo = 0;
        } else {
            const int16_t a = w.ammo;
            --w.clip;
            if (a >= 1) w.ammo = (int16_t)(a - 1);
        }
    } else if (w.ammo >= 1) {
        w.ammo = (int16_t)(w.ammo - 1);
    }
    createBullet(g, w, shooter, uid);
    w.setState(1, 0);
}

// cChainsaw::DoSound (from cChainsaw::Update): the idle engine while it is held, a rev-up when it starts to cut, the
// cutting loop (quieter while the rev-up is still held), and a rev-down when it stops
static void chainsawSound(Weapon& w) {
    w.sawTimer = w.sawTimer > 0 ? (int16_t)std::max(w.sawTimer - 0x20, 0) : (int16_t)0;
    if (w.sawPrevState == 0) {
        if (w.state == 1 && w.sawTimer == 0) {
            TheSound().playerEvent(0x1F, 0x7F, 0x12C);
            w.sawTimer = 0x12C;
        }
    } else if (w.sawPrevState == 1) {
        if (w.state == 0 && w.sawTimer <= 0x95) TheSound().playerEvent(0x20, 0x7F, 0x12C);
    }
    // (the original also retimes the looping slot by the sample's length, plus Rand32NonCritical(100) + 150 after a
    // hit; the port's loops repeat by themselves)
    if (w.state == 1) {
        if (w.sawTimer <= 0x7E) TheSound().playerEvent(0x1E, 0x7F - w.sawTimer, 0x12C);
    } else {
        TheSound().playerEvent(0x21, 100, 0xC8);
    }
    w.sawHit = false;
    w.sawPrevState = (uint8_t)w.state;
}

void updateWeapon(Game& g, Weapon& w, Player* shooter = nullptr, uint32_t uid = 0) {   // cWeapon::Update
    if (w.type == 4) chainsawSound(w);              // cChainsaw::Update, then cWeapon::Update
    if (w.type == 16) FlameThrower_Update(g, w);   // cFlameThrower::Update, then cWeapon::Update
    if (w.type == 19) Tazer_Update(g, w, gYoke.held && &w == g.playerWeapons.current());   // cTazer::Update
    if (w.type == 15 && w.spin - 1 >= 0) {   // cMinigun::Update: the spin winds down, its sound follows it
        --w.spin;
        TheSound().playerEvent(79, std::min(16 * w.spin, 127), 200);
    }
    switch (w.state) {
        case 3:
            w.timer -= 2;
            if (w.timer > 0) return;
            // the gun's ReloadSfx: AddSoundEvent(event, volume, radius)
            {
                static const int kReloadSfx[19][3] = {
                    {}, {}, {}, {}, {},
                    {73, 40, 200},    // cPistol
                    {73, 40, 200},    // cDualPistol
                    {73, 40, 200},    // cDesertEagle
                    {77, 40, 200},    // cUzi
                    {77, 40, 200},    // cMp5
                    {78, 40, 200},    // cShotgun
                    {43, 40, 200},    // cSawnOffShotgun
                    {43, 100, 1000},  // cDoubleBarreledShotgun
                    {74, 40, 200},    // cAK47
                    {74, 40, 200},    // cColt
                    {72, 100, 200},   // cMinigun
                    {},
                    {75, 60, 200},    // cRocketLauncher
                    {76, 40, 200}};   // cSniperRifle
                if (w.type < 19 && kReloadSfx[w.type][2])
                    weaponSound(shooter, uid, kReloadSfx[w.type][0], kReloadSfx[w.type][1], kReloadSfx[w.type][2]);
            }
            w.timer = Weapons_Info(w.type).reloadTime;
            w.state = 4;
            w.flags |= 0x10;
            return;
        case 4:
            w.timer -= 2;
            if (w.timer > 0) return;
            if (w.type == 23 && g.projectiles.mineWaiting()) { w.setState(4, 2); return; }   // cMineLauncher::Reload
            w.state = 0; w.flags |= 1; w.reload();
            return;
        case 5:
            w.timer -= 2;
            if (w.timer <= 0) { w.state = 0; w.flags |= 1; }
            return;
        case 1:
            w.timer = Weapons_Info(w.type).fireDelay;
            w.flags |= 4;
            w.state = 2;
            [[fallthrough]];
        case 2:
            w.timer -= 2;
            if (w.timer > 0) return;
            if (w.ammoInClip()) { w.state = 0; w.flags |= 1; }
            else { w.flags |= 8; w.timer = 4; w.state = 3; }
            return;
        default:
            return;
    }
}

// cIPhonePad's secondary (thrown) weapon button in the controller mode (CheckSecondaryPressed, byte_E0E611): while
// it is held the power (+0x638) grows by the frames it has been held (dword_105C7E4), up to 128; letting go throws
// (cPlayer::ThrowSecondary -> cPed::FireSecondary -> cWeaponManager::Fire(secondary)).
struct ThrowButton { bool held = false; int frames = 0, power = 0; };
ThrowButton gThrow;
uint8_t gBurst = 0;   // cPed +0x364 (+0x363, the burst length, is 1 for every ped: cPed::Reset)
}   // namespace

int Weapons_ThrowPower() { return gThrow.held ? gThrow.power : -1; }

void Weapons_NpcFrame(Game& g, Player& shooter, Weapon& weapon, bool fireHeld, uint32_t uid) {
    // Share the native cWeapon state machine and the on-foot damage geometry.
    // Task selection supplies the NPC's fire direction; it never borrows the
    // player's weapon manager, yoke, targeting or ammunition.
    if (!gOk || &shooter == &g.player || !uid ||
        (weapon.type != 0 && weapon.type != 1 && weapon.type != 5)) {
        shooter.weaponType = shooter.weaponState = 0;
        shooter.weaponFlags = 0; shooter.upperAim = false; shooter.weaponMovementScale = 4096;
        return;
    }
    const bool fire = fireHeld && !shooter.dead && !shooter.attached && !shooter.reacting() &&
                      !g.player.dead && !g.arrested();
    updateWeapon(g, weapon, &shooter, uid);
    if (fire) fireWeapon(g, weapon, &shooter, uid);
    if (weapon.type == 0 && (weapon.state == 1 || (weapon.flags & 2)))
        weaponSound(&shooter, uid, 9, 50, 200);
    if (weapon.type == 1 && ((weapon.state && (weapon.state == 1 || !(weapon.flags & 1))) ||
                            (weapon.flags & 2)) && shooter.enteredUpperFrame(4))
        weaponSound(&shooter, uid, 7, 127, 200);
    shooter.upperAim = fire && weapon.type == 5;
    shooter.weaponType = weapon.type;
    shooter.weaponState = weapon.state;
    shooter.weaponFlags = weapon.flags;
    shooter.weaponMovementScale = Weapons_Info(weapon.type).scale[fire ? 1 : 0];
    weapon.flags = 0;
}

void Weapons_RenderThrowArc(const Game& g) {
    // RenderSecondaryPower(origin = the ped + 2 up, dir, power = +0x638 << 12 >> 7, ready, ..., a8 = 1): a tristrip
    // of up to 12 vertex pairs in immsprite3d space (scale 24576 / 2^24 per unit). Pair i is (power x dir >> 14) x i
    // along the throw, i / 12 x 0.1 x power to each side, at the height of a parabola that starts at power / 2 and
    // loses 334 a step; it stops when the height goes below 0 after the fourth pair. Colour: RGB555
    // (31 - k) | k << 5 with k = (60 x power - 122880) >> 12 above half power, grey 0x4210 while the weapon is busy.
    if (!gThrow.held || g.playerCar >= 0) return;
    const WeaponManager& m = g.playerWeapons;
    const Weapon* s = m.secondary >= 8 ? m.inSlot(m.secondary) : nullptr;
    if (!s || s->type == 23) return;   // (the mine is dropped, not thrown)
    const int32_t power = gThrow.power << 12 >> 7;
    int32_t d[2] = {g.player.fireDir[0], g.player.fireDir[1]};
    if (!d[0] && !d[1]) return;
    Normalise2(d);
    const bool ready = !s->state || (s->flags & 1);
    int k = power <= 2048 ? 0 : (60 * power - 122880) >> 12;
    uint16_t c = (uint16_t)((31 - k) | (32 * k));
    if (!ready) c = 16912;
    const int64_t stepX = ((int64_t)power * d[0]) >> 14, stepY = ((int64_t)power * d[1]) >> 14;
    const int64_t wx = (1717567488LL * d[1]) >> 34, wy = (1717567488LL * -d[0]) >> 34;
    float verts[24][3];
    int n = 0;
    int32_t along = 0, across = 0, height = 0, rise = power >> 1;
    for (int i = 0; i < 12; ++i) {
        const int64_t f = ((int64_t)i << 44) / 0xC00000000LL;
        const int32_t ox = (int32_t)((f * wx) >> 12), oy = (int32_t)((f * wy) >> 12);
        verts[n][0] = (float)(int16_t)(along - ox); verts[n][1] = (float)(int16_t)(across - oy); verts[n][2] = (float)height; ++n;
        verts[n][0] = (float)(int16_t)(along + ox); verts[n][1] = (float)(int16_t)(across + oy); verts[n][2] = (float)height; ++n;
        along += (int32_t)stepX;
        across += (int32_t)stepY;
        rise -= 334;
        height += rise;
        if (height < 0) {
            if (i > 3) break;
            height = 0;
        }
    }
    const float scale = 24576.f / 16777216.f;
    const float o[3] = {g.player.pos[0] / 4096.f, g.player.pos[1] / 4096.f, (g.player.pos[2] + 0x2000) / 4096.f};
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_LIGHTING);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f((c & 31) / 31.f, ((c >> 5) & 31) / 31.f, ((c >> 10) & 31) / 31.f, 1.f);
    glBegin(GL_TRIANGLE_STRIP);
    for (int i = 0; i < n; ++i) glVertex3f(o[0] + verts[i][0] * scale, o[1] + verts[i][1] * scale, o[2] + verts[i][2] * scale);
    glEnd();
    glColor4f(1.f, 1.f, 1.f, 1.f);
    glEnable(GL_CULL_FACE);
}

void Weapons_PlayerFrame(Game& g, bool fireHeld, int cycle, bool throwHeld) {
    WeaponManager& m = g.playerWeapons;
    if (!gOk) return;
    // cPed::HandleWeapons: the weapon cycle buttons (on foot), then sWeaponYoke::SetFire
    if (g.playerCar < 0) {
        if (cycle > 0) m.cycleNext(true);
        else if (cycle < 0) m.cyclePrev(true);
    }
    const bool fire = fireHeld && g.playerCar < 0 && !g.player.dead && !g.player.attached;
    // cPlayer::HandleAiming without a target: the fire direction is the facing. A mod's aim point replaces it (PC).
    g.player.upperAim = false;
    if (!g.aimOverride.on && g.playerCar < 0 && !g.player.dead) {   // the original's auto-aim (targeting.cpp)
        if (g.targeting.handleAiming(g, fire)) g.player.upperAim = true;
    } else {
        g.targeting.reset();
        for (int k = 0; k < 3; ++k) g.player.fireDir[k] = g.player.fwd[k];
    }
    if (g.aimOverride.on && g.playerCar < 0) {
        int32_t d[2] = {g.aimOverride.target[0] - g.player.pos[0], g.aimOverride.target[1] - g.player.pos[1]};
        if (std::abs(d[0]) + std::abs(d[1]) > 0x100) {
            Normalise2(d);
            g.player.fireDir[0] = (int16_t)d[0];
            g.player.fireDir[1] = (int16_t)d[1];
            g.player.fireDir[2] = 0;
            g.player.upperAim = true;
        }
    }
    if (gYoke.held != fire) {
        if (fire) gYoke.pressed = true;
        else gYoke.released = true;
        gYoke.held = fire;
    }
    // cPed::HandleWeaponsAlways: cWeaponManager::UpdateAlways, FireIfPossible, sWeaponYoke::Update
    updateWeapon(g, *m.current());
    if (m.secondary != m.cur) {   // UpdateAlways: the secondary weapon too, and the next one when it runs out
        if (Weapon* s = m.inSlot(m.secondary)) {
            updateWeapon(g, *s);
            if (!s->ammoCount() && !m.locked) m.cycleToBest(false, g.playerCar >= 0);
        }
    }
    if (!gYoke.held && !m.current()->ammoCount()) m.cycleToBest(true, g.playerCar >= 0);
    {   // the secondary button: hold to build up the power, release to throw (on foot)
        Weapon* s = m.secondary >= 8 ? m.inSlot(m.secondary) : nullptr;
        const bool can = s && s->ammoCount() && g.playerCar < 0 && !g.player.dead && !g.player.attached;
        if (throwHeld && can) {
            if (!gThrow.held) {   // SecondaryWeapon_OnPress: the button click (PlayHudSfx 325) and a power of 0
                gThrow.held = true; gThrow.frames = 0; gThrow.power = 0;
                TheSound().playResident(325, 127);
            }
            gThrow.power = std::min(gThrow.power + gThrow.frames, 128);
            ++gThrow.frames;
        } else if (gThrow.held) {
            gThrow.held = false;
            int power = gThrow.power;
            if (g.aimOverride.on) {   // PC aiming mod: the throw lands on the aim point (at most the 14-unit range)
                const int64_t dx = g.aimOverride.target[0] - g.player.pos[0], dy = g.aimOverride.target[1] - g.player.pos[1];
                const double d = std::sqrt((double)(dx * dx + dy * dy));
                power = (int)std::clamp(d * 128.0 / 0xE000, 0.0, 128.0);
            }
            if (can && s->state == 0) {   // cWeaponManager::Fire(secondary) -> cBulletBasedWeapon::Fire
                g.player.throwPower = power;
                if (s->ammoCount() != -1 && s->ammoCount() < 1) {
                    TheSound().playerEvent(99, 35, 1000);
                    s->setState(5, 0);
                } else {
                    if (s->ammo >= 1) s->ammo = (int16_t)(s->ammo - 1);
                    createBullet(g, *s);
                    s->setState(1, 0);
                }
            }
        }
    }
    Weapon& w = *m.current();
    if (gBurst && (!m.reloading(true) || !(uint8_t)Weapons_Info(w.type).clip)) --gBurst;
    if (gYoke.held) {
        bool fireNow = gBurst != 0;
        if (!gBurst && m.ready(true)) {
            gBurst = 1;
            Rand32Critical(100);   // the fire chance roll (cPed +0x356 = 100 for the player)
            fireNow = true;
        }
        if (fireNow) fireWeapon(g, *m.current());   // cPlayer::FirePrimary -> cWeaponManager::Fire
    } else if (gBurst) {
        --gBurst;
    }
    gYoke.pressed = gYoke.released = false;
    // what the walk cycle reads this frame (AnimateWalkRunCycleWeapons), then cWeaponManager::ClearStateHistory
    const Weapon& c = *m.current();
    // cPed::AnimateWalkRunCycleWeapons calls cSword::ProcessDelayedHit once per sword-swing tick, including
    // the firing tick. Keep that simulation work here, before ClearStateHistory; Player::animate only selects
    // the sprites. Damage uses the current position/facing at contact, so moving away can evade the swing.
    const bool onFootAnimation=g.playerCar<0 && !g.player.dead && !g.player.attached && !g.player.reacting();
    if (c.type==0 && onFootAnimation && (c.state==1 || (c.flags&2)))
        TheSound().playerEvent(9,50,200); // native unarmed cue 0x902218, animation call 0x87e228
    // the nightstick and the bat (cases 1 and 2): their FireSfx when the swing steps from frame 3 to 4 (0x87e180)
    if ((c.type == 1 || c.type == 2) && onFootAnimation &&
        ((c.state && (c.state == 1 || !(c.flags & 1))) || (c.flags & 2)) && g.player.enteredUpperFrame(4))
        TheSound().playerEvent(7, 127, 200);   // cNiteStick / cBaseballBat::FireSfx
    if (c.type == 3 && onFootAnimation &&
        ((c.state && (c.state == 1 || !(c.flags & 1))) || (c.flags & 2))) {
        Weapon& sword = *m.current();
        // Native animation branch at 0x87e258 checks the previous sprite update, before advancing it again.
        if (g.player.enteredUpperFrame(8) || g.player.enteredUpperFrame(12))
            TheSound().playerEvent(152,127,200); // native sword cue at 0x906058
        if (sword.delayedHit && --sword.delayedHit == 0)
            hitStuff(g, sword, 0x2000, Weapons_Info(3).damage, 3, 11);
    }
    g.player.weaponType = c.type;
    g.player.weaponMovementScale=Weapons_Info(c.type).scale[fire ? 1 : 0];
    g.player.weaponState = c.state;
    g.player.weaponFlags = c.flags;
    // cPed::Render -> cFlameThrower::Render -> CreateGasJet; built once per tick (its Rand32Critical)
    if (m.current()->type == 16) FlameThrower_Jet(g, *m.current());
    if (m.current()->type == 19) Tazer_Wire(g, *m.current());   // cTazer::Render
    m.current()->flags = 0;
    if (Weapon* s = m.inSlot(m.secondary)) s->flags = 0;
}
