// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// Weapons: ports of cWeaponManager, cWeapon / cMeleeBasedWeapon / cBulletBasedWeapon, the guns' CreateBullet,
// cBulletBasedWeapon::HelperMakeTraceAndFlash, sGetClosestHitThing and cBulletTraces.
//
// Weapon types (eWeaponType, weapon_tables.bin = cWeaponInfo::msWeaponInfos, names from e_main.gxt):
//   0 fists, 1 nightstick, 2 baseball bat, 3 sword, 4 chainsaw (melee: cWeapon::Ammo is -1)
//   5 pistol, 6 twin pistols, 7 revolver, 8 micro SMG, 9 SMG, 10 shotgun, 11 stubby shotgun,
//   12 double barreled shotgun, 13 assault rifle, 14 carbine rifle, 18 sniper rifle (cBulletBasedWeapon)
//   15 minigun, 17 RPG, 20 grenades, 21 molotovs, 22 flashbangs, 23 mines (ported: projectiles.cpp)
//   16 flame thrower, 19 taser, 24 thrown objects (own Fire / CreateBullet: not ported yet - they keep ammo
//   but do not fire)
//
// cWeaponManager: 11 slots (cWeaponInfo +0x2C), slot 0 always holds the fists. +0 is the current primary slot
// (0..7), +1 the current secondary (8..10). GiveWeapon / CreateNewWeapon / SetUseWeapon / CycleToNextWeapon /
// CycleToPrevWeapon / UpdateAlways / IsReady / IsReloading / AccuracyModifyer follow the decompiled code.
//
// cWeapon state (+0xC) and timer (+8, counted down by 2 a frame): 0 ready, 1 fired (-> 2 with the fire delay),
// 2 fire delay (then 0, or 3 when the clip is empty), 3 four ticks before the reload sound (-> 4), 4 reloading
// (cWeaponInfo +0x18, then cBulletBasedWeapon::Reload refills the clip), 5 dry fire (50). Bullet weapons hold
// the total ammo (+0x14, -1 = infinite) and the clip (+0x16).
//
// Firing (cPed::HandleWeaponsAlways -> FireIfPossible -> cPlayer::FirePrimary -> cWeaponManager::Fire ->
// cBulletBasedWeapon::Fire -> CreateBullet -> HelperMakeTraceAndFlash): the fire direction (cPed +0x32A) is the
// ped's facing while nothing is targeted. CalcAccuracyDir turns it by Rand16Critical(2 x spread) - spread
// (cWeaponInfo +0x20 + the move-rate term), the line from the ped (+2 units up) along it for the weapon's range is
// cut at the first static hit (CCollision::GetLineCollision 0x40001E00), sGetClosestHitThing finds the nearest ped
// (a vertical cylinder of its radius) or vehicle (CCollision::SweptVertVBox in its frame) on it, which takes the
// weapon's damage (sDamageInfo type 1, the minigun 2, the taser 3), and a bullet trace is drawn from the muzzle
// (the player's: +2 up, 1.5 units out) to the hit.
//
// Auto-aim is targeting.cpp, the RPG and the thrown weapons projectiles.cpp, the wheel and button weaponhud.cpp.
// Not ported yet: the flame thrower, sniper rifle, taser and sword, muzzle flashes and ricochet sounds, blood, the
// damage reactions' animations, drive-by shooting, AI peds with weapons and weapon pickups.
#pragma once
#include "lookups.h"
#include <cstdint>
#include <memory>
#include <string>

class Game;

class Weapon {   // cWeapon
public:
    explicit Weapon(int type) : type(type) {}
    int type;                 // vfunc +0x38 Type
    int32_t timer = 0;        // +8
    int state = 0;            // +0xC
    uint32_t flags = 0;       // +0x10: bit n set when state n is entered
    int16_t ammo = 0;         // +0x14 (cBulletBasedWeapon)
    uint8_t clip = 0;         // +0x16
    uint8_t shots = 0;        // +0x17 (cDualPistol: alternating hands)
    int8_t spin = 0;          // +0x18 (cMinigun: the barrel spin, fires above 8)
    bool melee() const { return type <= 4; }
    int ammoCount() const { return melee() ? -1 : ammo; }        // vfunc +0x48 Ammo
    int ammoInClip() const { return melee() ? 1 : clip; }         // vfunc +0x50 AmmoLeftInClip
    void setState(int s, int t = 0);                              // cWeapon::SetState
    void addAmmo(int n);                                          // vfunc +0x40 AddAmmo
    void reload();                                                // vfunc +0x20 Reload
    int reduceAmmo(int n);                                        // cBulletBasedWeapon::ReduceAmmo
    void unload();                                                // vfunc +0x28 Unload
};

class WeaponManager {   // cWeaponManager (cPed +0x160)
public:
    static constexpr int kSlots = 11;
    WeaponManager();
    void reset();                                                 // cWeaponManager::Reset
    bool give(int type, int ammo, bool replace);                  // GiveWeapon (replace: its bool)
    void setUse(int type, bool reload);                           // SetUseWeapon
    void cycleNext(bool primary);                                 // CycleToNextWeapon
    void cyclePrev(bool primary);                                 // CycleToPrevWeapon
    void cycleToBest(bool primary, bool inVehicle);               // CycleToBestWeapon
    bool ready(bool primary) const;                               // IsReady
    bool reloading(bool primary) const;                           // IsReloading
    int accuracyModifier(int moveRate) const;                     // AccuracyModifyer
    void clearAll();                                              // the mod API's "take all weapons"
    Weapon* current() const { return slots[cur].get(); }
    Weapon* inSlot(int slot) const { return slot >= 0 && slot < kSlots ? slots[slot].get() : nullptr; }
    uint8_t cur = 0, secondary = 0;                               // +0, +1
    int mode = 0;                                                 // +0x78: 1 = in a vehicle (drive-by)
    int16_t accuracyExtra = 0;                                    // +0x7C
    uint8_t savedSlot = 0;                                        // +0x7E
    bool changed = true;                                          // +0x82
    bool locked = false;                                          // +0x81
    bool noSwitchPrimary = false, noSwitchSecondary = false;      // +0x83 / +0x84 (DontSwitchToWeaponOnPickup)
    bool explosiveRounds = false;                                 // +0x85
    std::unique_ptr<Weapon> slots[kSlots];                        // +0x20
private:
    void create(int slot, int type, int ammo);                    // CreateNewWeapon
    void switchedTo(Weapon* w);                                   // the "clip not full: reload" tail of the cycles
};

// The weapon data (weapon_tables.bin) and names (e_main.gxt), loaded once.
bool Weapons_Init(const std::string& dataDir);
bool Weapons_Ok();
const WeaponInfo& Weapons_Info(int type);
const WeaponTables& Weapons_Tables();
std::string Weapons_Name(int type);                               // e_main.gxt via cWeaponInfo +0x28
int Weapons_DefaultAmmo(int type);                                // cWeaponManager::GetDefaultWeaponAmmo
bool Weapons_Fires(int type);                                     // the bullet weapons this port can shoot

// cBulletTraces: 16 lines that shrink from the muzzle towards the hit over their lifetime.
class BulletTraces {
public:
    void add(const int32_t from[3], const int32_t vec[3], int kind);   // AddTrace
    void update();                                                      // cBulletTraces::Update
    void render() const;                                                // cBulletTraces::Render
    void clear();
private:
    struct Trace { int32_t pos[3], vec[3], step[3]; int8_t life = 0; bool active = false; };
    Trace traces_[16];
};

// The player's weapon handling for one frame (cPed::HandleWeapons / HandleWeaponsAlways with the fire button,
// cPlayer::FirePrimary). fireHeld: the button state; cycle: -1 previous, +1 next weapon.
// throwHeld: the secondary (thrown weapon) button.
void Weapons_PlayerFrame(Game& g, bool fireHeld, int cycle, bool throwHeld = false);
int Weapons_ThrowPower();   // the power being built up while the secondary button is held (0..128), else -1
// cIPhonePad::RenderSecondWeaponPowerArc -> cWeaponManager::RenderSecondaryPower: the throw's arc on the ground
void Weapons_RenderThrowArc(const Game& g);
