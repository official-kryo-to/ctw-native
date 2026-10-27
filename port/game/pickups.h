// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// Pickups: a port of cPickupManager, cBasePickup and the pickups the free-roaming game uses (cPickupMoney,
// cPickupHealth, cPickupArmour, cPickupBribe and the weapons' cBaseWeaponPickup::GiveStuff).
//
// The map places pickups in every 50-unit sector (world.bin section +0x110, see world/collision.h). When a sector
// streams in, LoadPickupGridCell creates its pickups unless the cCollectedAmbientPickupStore remembers taking one of
// that type at that spot (25 entries, a respawn time counted down by two a frame). Dead peds drop their weapon, or
// otherwise their money (cPed::DropGoodiesWhenDead -> AddDroppedWeapon / AddDroppedMoney), as a pickup with a 1000
// lifetime (500 frames) that only appears while at least five of the 30 pickup slots are free.
//
// cBasePickup::Process: an active pickup is collected by the player standing within 2 units across and 4 up (twice
// that from a vehicle, for the pickups that allow it). Collecting money or a weapon makes it disappear; health,
// armour and bribes from the map hide and respawn after five minutes. A pickup that goes fades over 15 frames.
// ManagedRender draws it as a camera-facing sprite of the pickup sheet (gGameDir[12]) one unit above its position,
// pulsing between dark and light grey, white while it fades.
//
// Not ported: dumpsters (sSectorPickupData flag 0x80 and type 33), cPickupRandomStuff, cPickupLuckLion,
// cPickupGeneric and the script's pickups; a weapon that would replace a different one in its slot waits for the
// phone's "touch to swap" box, which the port does not have, so it stays where it is.
#pragma once
#include <cstdint>
#include <set>
#include <vector>

class Game;
struct WorldCamera;

class Pickups {
public:
    // cPickupManager::AddPickup's types
    enum Type { Money = 0, Health = 1, Armour = 2, Bribe = 3, Bat = 4, Pistol = 7, NiteStick = 30, Tazer = 31,
                Dumpster = 33 };
    struct Pickup {
        uint32_t uid = 0;
        int type = 0;                 // +0xCB
        int texture = 0;              // +0xC8 (ePickupTexture)
        uint16_t colour = 0x7FFF;     // +0xC6
        int32_t pos[3]{};
        int16_t timer = -1;           // +0xC2: lifetime, or the respawn countdown; -1 = none
        int amount = 0;               // +0xD2: ammo or money
        uint8_t state = 0;            // +0xC0: 0 active, 1 collected (respawning), 4 gone
        int8_t fade = 0;              // +0xCF: 1..31 while fading out, -1 = done
        uint8_t hidden = 0;           // +0x38
        bool inVehicle = false;       // +0xCD: can be collected from a vehicle
        bool ambient = false;         // placed by the map (LoadPickupGridCell)
        int cell = -1;                // the 50-unit sector it belongs to (x * 100 + y)
    };

    void reset();                                   // cPickupManager::Reset
    void update(Game& g);                           // streaming, cPickupManager::Process and each pickup's Process
    void render(const Game& g, const WorldCamera& cam) const;   // cBasePickup::ManagedRender
    // cPickupManager::AddPickup (lifetime -1: none). nullptr when there is no such pickup or it is not ported.
    Pickup* add(int type, const int32_t pos[3], int amount, int16_t lifetime);
    Pickup* addDroppedMoney(int amount, const int32_t pos[3]);       // AddDroppedMoney
    Pickup* addDroppedWeapon(int weaponType, const int32_t pos[3]);  // AddDroppedWeapon
    // cPickupManager::GetWeaponFromPickUpType: the weapon a pickup type gives (0: none)
    static int weaponFromType(int type);
    static int typeFromWeapon(int weapon);           // AddDroppedWeapon's switch (-1: none)
    const std::vector<Pickup>& list() const { return pickups_; }
    bool allowMelee = true;                          // AllowMeleePickups (+0x191)
    bool allowMoney = true;                          // WorldImpl::AllowMoneyPickups (cPlayer +0x3F9): dead peds' money
    static constexpr int kPoolSize = 30;             // cGamePoolManager's "Pickup" pool

private:
    std::vector<Pickup> pickups_;
    uint32_t nextUid_ = 1;
    struct Collected { int32_t x = 0, y = 0; uint8_t type = 0; bool active = false; int16_t timer = 0; };
    Collected collected_[25];                        // cCollectedAmbientPickupStore
    std::set<int> cells_;                            // the streamed-in sectors (x * 100 + y)
    void loadCell(Game& g, int cx, int cy);          // LoadPickupGridCell
    bool addToCollected(const Pickup& p);            // AddToAmbientCollectedList
    bool forceFree();                                // ForceFreePickup
    int touching(const Game& g, const Pickup& p) const;   // IsTouchingPlayer: 0 the player, 2 nobody
    bool collect(Game& g, Pickup& p);                // the pickup's Collect: true to keep counting it as taken
    void setState(Pickup& p, uint8_t state);         // cBasePickup::SetState
    void process(Game& g, Pickup& p);                // cBasePickup::Process
    int freeSlots() const { return kPoolSize - (int)pickups_.size(); }
};
