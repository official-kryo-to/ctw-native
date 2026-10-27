// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "pickups.h"
#include "game.h"
#include "sound.h"
#include "gfx/assets.h"
#include "world/worldrenderer.h"
#include <glad/gl.h>
#include <algorithm>

namespace {
// cPickupManager::AddPickup's switch: the sprite (ePickupTexture) and colour of each pickup type it creates
struct TypeInfo { int8_t texture; uint16_t colour; };
TypeInfo typeInfo(int type) {
    switch (type) {
    case Pickups::Health: return {1, 0x2C0};
    case Pickups::Armour: return {2, 0x7C00};
    case Pickups::Bribe: return {3, 0x3FF};             // cPickupBribe::cPickupBribe
    case 4: return {0x12, 0x7FFF};   case 5: return {9, 0x7FFF};     case 6: return {0xE, 0x7FFF};
    case 7: return {4, 0x7FFF};      case 8: return {0x19, 0x7FFF};  case 9: return {0x1A, 0x7FFF};
    case 10: return {5, 0x7FFF};     case 11: return {0x1B, 0x7FFF}; case 12: return {6, 0x7FFF};
    case 13: return {0x1C, 0x7FFF};  case 14: return {0x1D, 0x7FFF}; case 15: return {7, 0x7FFF};
    case 16: return {0x1E, 0x7FFF};  case 17: return {8, 0x7FFF};    case 18: return {0x13, 0x7FFF};
    case 19: return {0xA, 0x7FFF};   case 20: return {0xB, 0x7FFF};  case 21: return {0xC, 0x7FFF};
    case 22: return {0x1F, 0x7FFF};  case 23: return {0x20, 0x7FFF}; case 24: return {0x10, 0x7FFF};
    case Pickups::NiteStick: return {0x10, 0x7FFF};
    case Pickups::Tazer: return {0x21, 0x7FFF};
    default: return {-1, 0};
    }
}
// the weapon each weapon pickup gives (its cPickupX::Collect -> cBaseWeaponPickup::GiveStuff)
constexpr int8_t kPickupWeapon[32] = {0, 0, 0, 0, 2, 3, 4, 5, 7, 6, 8, 9, 10, 11, 12, 13,
                                      14, 15, 16, 17, 20, 21, 22, 23, 18, 0, 0, 0, 0, 0, 1, 19};
int cellKey(const int32_t pos[3]) {
    int cx, cy;
    Collision::cellOfPos(pos[0], pos[1], cx, cy);
    return cx * 100 + cy;
}
}

int Pickups::weaponFromType(int type) { return type >= 0 && type < 32 ? kPickupWeapon[type] : 0; }

int Pickups::typeFromWeapon(int weapon) {   // cPickupManager::AddDroppedWeapon's switch
    for (int t = 0; t < 32; ++t)
        if (weapon > 0 && kPickupWeapon[t] == weapon) return t;
    return -1;
}

void Pickups::reset() {   // cPickupManager::Reset (and the pickups of the old game going away)
    pickups_.clear();
    cells_.clear();
    for (Collected& c : collected_) c = Collected{};
    allowMelee = allowMoney = true;
}

Pickups::Pickup* Pickups::add(int type, const int32_t pos[3], int amount, int16_t lifetime) {
    if (freeSlots() <= 4) forceFree();   // (it goes once it has faded; the port never exceeds the pool)
    if (freeSlots() <= 0) return nullptr;
    // without melee pickups (AllowMeleePickups(false)) the second jump table leaves out the four melee weapons
    if (!allowMelee && (type == 4 || type == 5 || type == 6 || type == NiteStick)) return nullptr;
    Pickup p;
    if (type == Money) {   // cPickupMoney::cPickupMoney: the blue dollar for more than $99
        p.texture = amount > 99 ? 0x22 : 0;
        p.colour = 0x3E0;
    } else {
        const TypeInfo info = typeInfo(type);
        if (info.texture < 0) return nullptr;   // dumpsters, random stuff, luck lions and script pickups
        p.texture = info.texture;
        p.colour = info.colour;
        p.inVehicle = type == Bribe;
    }
    p.uid = nextUid_++;
    p.type = type;
    std::copy(pos, pos + 3, p.pos);
    p.amount = amount;
    p.timer = lifetime;
    p.cell = cellKey(pos);
    pickups_.push_back(p);
    return &pickups_.back();
}

Pickups::Pickup* Pickups::addDroppedMoney(int amount, const int32_t pos[3]) {   // cPickupManager::AddDroppedMoney
    if (freeSlots() < 5) return nullptr;
    const int32_t at[3] = {pos[0], pos[1], pos[2] + 0x1800};
    return add(Money, at, amount, 0x3E8);
}

Pickups::Pickup* Pickups::addDroppedWeapon(int weapon, const int32_t pos[3]) {   // cPickupManager::AddDroppedWeapon
    if (freeSlots() < 5) return nullptr;
    const int type = typeFromWeapon(weapon);
    if (type < 0 || (!allowMelee && weapon >= 1 && weapon <= 4)) return nullptr;
    const int32_t at[3] = {pos[0], pos[1], pos[2] + 0x1000};
    return add(type, at, std::max(Weapons_Ok() ? Weapons_Info(weapon).pickupAmmo : 0, 0), 0x3E8);
}

bool Pickups::forceFree() {   // cPickupManager::ForceFreePickup: the active dropped pickup with the least time left
    Pickup* best = nullptr;
    for (Pickup& p : pickups_)
        if (p.state == 0 && p.timer != -1 && (!best || p.timer < best->timer)) best = &p;
    if (!best) return false;
    setState(*best, 4);
    return true;
}

void Pickups::setState(Pickup& p, uint8_t state) {   // cBasePickup::SetState
    p.state = state;
    if ((state == 4 || state == 1) && !p.fade) p.fade = 1;
}

bool Pickups::addToCollected(const Pickup& p) {   // cCollectedAmbientPickupStore::AddToList
    Collected* slot = nullptr;
    for (Collected& c : collected_) if (!c.active) { slot = &c; break; }
    if (!slot) {   // full: the entry with the least time left makes way
        for (Collected& c : collected_) if (!slot || c.timer < slot->timer) slot = &c;
        if (!slot) return false;
    }
    *slot = {p.pos[0], p.pos[1], (uint8_t)p.type, true, p.timer};
    return true;
}

void Pickups::loadCell(Game& g, int cx, int cy) {   // cPickupManager::LoadPickupGridCell
    const auto* placements = g.collision.pickups(cx, cy);
    if (!placements) return;
    for (const Collision::PickupPlacement& d : *placements) {
        if (d.flags & 0x40) continue;   // needs the story flag (save game +0x64 & 0x3000), not set in a new game
        const int type = d.flags & 0x3F;
        if (d.flags & 0x80 || type == Dumpster) continue;   // dumpsters are not ported
        bool taken = false;
        for (const Collected& c : collected_)
            taken |= c.active && c.x == d.x && c.y == d.y && c.type == type;
        if (taken) continue;
        const int32_t at[3] = {d.x, d.y, d.z};
        if (Pickup* p = add(type, at, d.amount, -1)) p->ambient = true;
    }
}

int Pickups::touching(const Game& g, const Pickup& p) const {   // cBasePickup::IsTouchingPlayer
    if (g.player.dead || g.arrested()) return 2;
    const bool inVehicle = g.playerCar >= 0;
    if (inVehicle && !p.inVehicle) return 2;
    int32_t at[3];
    g.focus(at);
    // on foot: 2 units across and 4 up (xmmword_466F70); from a vehicle twice that (xmmword_468CA0)
    const int32_t reach = inVehicle ? 0x4000 : 0x2000, height = inVehicle ? 0x8001 : 0x4001;
    const int32_t dx = at[0] - p.pos[0], dy = at[1] - p.pos[1], dz = at[2] - p.pos[2];
    if ((uint32_t)(dx + reach) > (uint32_t)(2 * reach)) return 2;
    if (dy <= -reach - 1 || dy >= reach + 1 || dz <= -height || dz >= height) return 2;
    return 0;
}

bool Pickups::collect(Game& g, Pickup& p) {
    auto chime = [] { TheSound().playResident(0x26B, 0x7F); };
    switch (p.type) {
    case Money:   // cPickupMoney::Collect
        g.giveMoney((uint32_t)p.amount);
        setState(p, 4);
        chime();
        g.stats.addFoundOnTheStreet((uint32_t)p.amount);
        return false;
    case Health:   // cPickupHealth::Collect
    case Armour: {  // cPickupArmour::Collect
        const int had = p.type == Health ? g.player.health : g.player.armour;
        if (had > 126) return false;
        if (p.type == Health) g.player.health = 127;
        else g.player.giveArmour(127);
        setState(p, 1);
        p.timer = 0x4650;   // cPickupManager::GetPickupRespawnTime(1 / 2): five minutes
        chime();
        return true;
    }
    case Bribe: {   // cPickupBribe::Collect
        if (!g.wanted.level()) return false;
        g.wanted.forceDropWantedLevel(true);
        chime();
        setState(p, 1);
        p.timer = 0x4650;   // GetPickupRespawnTime(0)
        return true;
    }
    default: break;
    }
    // cBaseWeaponPickup::GiveStuff
    const int weapon = weaponFromType(p.type);
    if (!weapon) return false;
    auto taken = [&] {
        chime();
        if (p.timer == -1) {   // a map pickup comes back after GetPickupRespawnTime(3)
            p.timer = 0x4650;
            setState(p, 1);
            return true;
        }
        setState(p, 4);
        return false;
    };
    if (g.giveWeapon(weapon, p.amount, false)) return taken();
    const WeaponManager& w = g.playerWeapons;
    for (int s = 0; s < WeaponManager::kSlots; ++s)   // cWeaponManager::HasWeapon: nothing to take
        if (w.inSlot(s) && w.inSlot(s)->type == weapon) return false;
    // cWeaponManager::ShouldWeAutoPickupThisWeapon: a better weapon of the same kind (slots 2 to 5) replaces the one carried
    const int slot = Weapons_Info(weapon).slot;
    const Weapon* carried = w.inSlot(slot);
    if ((unsigned)(slot - 2) <= 3 && carried && carried->type < weapon) {
        g.giveWeapon(weapon, p.amount, true);
        return taken();
    }
    return false;   // (the phone's swap box)
}

void Pickups::process(Game& g, Pickup& p) {   // cBasePickup::Process
    switch (p.state) {
    case 0: {
        p.fade = 0;
        if (touching(g, p) == 2) {
            if (p.timer >= 1) {   // a dropped pickup's lifetime
                p.timer = (int16_t)(p.timer - 2);
                if (p.timer <= 0) { p.timer = -1; setState(p, 4); }
            }
        } else if (collect(g, p)) {
            ++p.hidden;
        }
        break;
    }
    case 1:   // taken from the map: the collected list remembers it until it respawns
        if (p.timer >= 1 && addToCollected(p)) {
            if (p.hidden) --p.hidden;
            setState(p, 4);
        }
        break;
    case 4:
        break;
    default:
        break;
    }
    if (p.fade >= 1) p.fade = (int8_t)(p.fade + 2 > 30 ? -1 : p.fade + 2);
}

void Pickups::update(Game& g) {
    // cWorld streaming: the sectors around the player; a sector coming in loads its pickups, one going away takes
    // its pickups with it (the entities of a sector are deleted with it)
    int32_t at[3];
    g.focus(at);
    int cx, cy;
    Collision::cellOfPos(at[0], at[1], cx, cy);
    std::set<int> now;
    for (int dx = -1; dx <= 1; ++dx)
        for (int dy = -1; dy <= 1; ++dy) now.insert((cx + dx) * 100 + (cy + dy));
    pickups_.erase(std::remove_if(pickups_.begin(), pickups_.end(),
                                  [&](const Pickup& p) { return !now.count(p.cell); }), pickups_.end());
    for (int k : now)
        if (!cells_.count(k)) loadCell(g, k / 100, k % 100);
    cells_ = std::move(now);
    // cPickupManager::Process: the collected list counts down
    for (Collected& c : collected_)
        if (c.active && (c.timer = (int16_t)(c.timer - 2)) < 0) c.active = false;
    for (size_t i = 0; i < pickups_.size(); ++i) process(g, pickups_[i]);   // (collecting can add none)
    pickups_.erase(std::remove_if(pickups_.begin(), pickups_.end(),
                                  [](const Pickup& p) { return p.state == 4 && p.fade == -1; }), pickups_.end());
}

void Pickups::render(const Game& g, const WorldCamera& cam) const {   // cBasePickup::ManagedRender
    int texture = 0;
    uint16_t rect[4];
    if (!Assets_PickupSprite(0, texture, rect)) return;
    const unsigned tex = Assets_Texture(texture);
    int tw = 256, th = 256;
    Assets_TextureSize(texture, &tw, &th);
    if (!tex || tw <= 0 || th <= 0) return;
    glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_CURRENT_BIT | GL_TEXTURE_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glBegin(GL_QUADS);
    for (const Pickup& p : pickups_) {
        if (p.state != 0 && p.fade < 1) continue;
        if (!Assets_PickupSprite(p.texture, texture, rect) || !rect[3]) continue;
        uint16_t colour;
        uint32_t alpha = 255;
        if (p.fade) {   // fading out: white, alpha (31 - fade) / 31 (0x8000000 at 31)
            colour = 0x7FFF;
            const uint32_t f = (uint32_t)(((uint64_t)((0x1F000 - (p.fade << 12)) & 0xFFFF000) * 0x10842109u) >> 33);
            alpha = p.fade == 0x1F ? 8 : ((f << 20) - (f << 12)) >> 24;
        } else {   // a grey that pulses up and down every 32 frames
            const uint32_t t = g.frame & 0x1F, v = g.frame & 0x10 ? 0x2E - t : t + 0xF;
            colour = (uint16_t)(v << 10 | v << 5 | v);
        }
        glColor4ub((GLubyte)((colour >> 10 & 0x1F) << 3), (GLubyte)((colour >> 5 & 0x1F) << 3),
                   (GLubyte)((colour & 0x1F) << 3), (GLubyte)alpha);
        // RenderWorldScaleCamAligned: centred one unit up, 1.5 units each way up (1 for the pistol) and as far across
        // as the sprite's aspect says
        const float h = p.type == Pistol ? 1.f : 1.5f, w = h * rect[2] / rect[3];
        const float c[3] = {p.pos[0] / 4096.f, p.pos[1] / 4096.f, p.pos[2] / 4096.f + 1.f};
        const float u0 = rect[0] / (float)tw, v0 = rect[1] / (float)th;
        const float u1 = (rect[0] + rect[2]) / (float)tw, v1 = (rect[1] + rect[3]) / (float)th;
        auto vertex = [&](float sx, float sy, float u, float v) {
            glTexCoord2f(u, v);
            glVertex3f(c[0] + sx * w * cam.right[0] + sy * h * cam.up[0], c[1] + sx * w * cam.right[1] + sy * h * cam.up[1],
                       c[2] + sx * w * cam.right[2] + sy * h * cam.up[2]);
        };
        vertex(-1, 1, u0, v0);
        vertex(-1, -1, u0, v1);
        vertex(1, -1, u1, v1);
        vertex(1, 1, u1, v0);
    }
    glEnd();
    glPopAttrib();
}
