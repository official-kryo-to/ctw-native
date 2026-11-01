// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "savegame.h"
#include "game.h"
#include "os/datafile.h"
#include "os/os.h"
#include <cstdio>
#include <cstring>
#include <ctime>

namespace {
template <class T> T get(const uint8_t* d, int at) { T v; std::memcpy(&v, d + at, sizeof v); return v; }
template <class T> void put(uint8_t* d, int at, T v) { std::memcpy(d + at, &v, sizeof v); }
// gVersionstr, copied into the header (__strcpy_chk into 0x20 bytes)
constexpr char kVersion[] = " 3.0 (11:11:11 02/10/15)";
static_assert(sizeof(StatsData) <= 0x2A0, "the stats section");
}

std::string SaveGame::fileName(int slot) {
    char name[32];
    std::snprintf(name, sizeof name, "savegame%d", slot);
    return name;
}

uint32_t SaveGame::word(int at, int bit) const { return get<uint32_t>(data_, at + (bit >> 5) * 4); }

void SaveGame::setBit(int at, int bit, bool on) {
    const int w = at + (bit >> 5) * 4;
    const uint32_t m = 1u << (bit & 31);
    put<uint32_t>(data_, w, (get<uint32_t>(data_, w) & ~m) | (on ? m : 0));
}

bool SaveGame::init(const std::string& dataDir) {
    std::vector<uint8_t> t;
    const bool ok = Data_ReadAll(dataDir + "/save_tables.bin", t) && t.size() == 8 + 2 * sizeof tableA_ &&
                    !std::memcmp(t.data(), "CTWSAVE1", 8);
    if (ok) {
        std::memcpy(tableA_, t.data() + 8, sizeof tableA_);
        std::memcpy(tableB_, t.data() + 8 + sizeof tableA_, sizeof tableB_);
    }
    loaded_ = false;
    autoLoad_ = true;
    slot_ = -1;
    defaults();
    return ok;
}

// the memset and the values ClearCurrentSaveData (and Init, and Load without a save) put in
void SaveGame::defaults() {
    std::memset(data_, 0, sizeof data_);
    std::memset(data_ + 0xC1D, 0x80, 8);
    put<uint16_t>(data_, 0xB82, 0x3B1);                    // (script +0x70A: KEN_A01's id)
    data_[0xF4] = 0x7F;                                    // health
    put<uint64_t>(data_, 0x50, 0x00000020C80A0552ULL);     // the settings
    put<uint64_t>(data_, 0x58, 0x0022001A00000005ULL);
    put<uint16_t>(data_, 0x60, 0x1D);
    for (int i = 0; i < 26; ++i) {   // ten bits each, three to a word
        const int at = (i / 3) * 4, shift = (i % 3) * 10;
        const uint32_t m = 0x3FFu << shift;
        put<uint32_t>(data_, 0x178 + at, (get<uint32_t>(data_, 0x178 + at) & ~m) | (tableA_[i] & 0x3FF) << shift);
        put<uint32_t>(data_, 0x19C + at, (get<uint32_t>(data_, 0x19C + at) & ~m) | (tableB_[i] & 0x3FF) << shift);
    }
}

void SaveGame::clearCurrentSaveData(Game& game, bool restoreIt) {
    loaded_ = false;
    defaults();
    std::memset(&game.stats.data(), 0, sizeof(StatsData));   // (the stats are the data's)
    if (restoreIt) restore(game);
}

bool SaveGame::slotHeader(int slot, uint8_t header[0x44]) {
    slot_ = (int8_t)slot;
    void* f = nullptr;
    if (OS_FileOpen(OS_AREA_DOCUMENTS, &f, fileName(slot).c_str(), OS_FILE_READ) != OS_OK) return false;
    const bool ok = OS_FileSize(f) >= 0x44 && OS_FileRead(f, header, 0x44) == OS_OK &&
                    get<uint16_t>(header, 0) == 0x1D && get<uint16_t>(header, 2) == 0xDB0;
    OS_FileClose(f);
    return ok;
}

bool SaveGame::loadSlot(int slot) {
    slot_ = (int8_t)slot;
    void* f = nullptr;
    if (OS_FileOpen(OS_AREA_DOCUMENTS, &f, fileName(slot).c_str(), OS_FILE_READ) != OS_OK) return false;
    OS_FileRead(f, data_, kSize);   // (sysFRead: what it reads is taken as it is)
    OS_FileClose(f);
    return true;
}

bool SaveGame::load(Game& game, int slot) {
    autoLoad_ = false;
    if (slot == -1) {
        uint8_t a[0x44]{}, b[0x44]{};
        const bool hasA = slotHeader(0, a), hasB = slotHeader(1, b);
        if (!hasA && !hasB) {   // "NO SAVE TO AUTOLOAD"
            loaded_ = false;
            defaults();
            std::memset(&game.stats.data(), 0, sizeof(StatsData));
            restore(game);
            return false;
        }
        slot = 0;
        if (!hasA) slot = 1;
        else if (hasB) {   // the newer: the year, month, day, hour, minute, then the second (later wins)
            for (int at = 0x28; at < 0x40; at += 4) {
                const int32_t x = get<int32_t>(a, at), y = get<int32_t>(b, at);
                if (x != y || at == 0x3C) { slot = at == 0x3C ? (x < y ? 1 : 0) : (x <= y ? 1 : 0); break; }
            }
        }
    }
    slot_ = (int8_t)slot;
    if (loadSlot(slot)) {
        loaded_ = true;
        std::memcpy(&game.stats.data(), data_ + kStats, sizeof(StatsData));
        restore(game);
        return true;
    }
    loaded_ = false;
    defaults();
    std::memset(&game.stats.data(), 0, sizeof(StatsData));
    restore(game);
    return false;
}

void SaveGame::tryAutoLoad(Game& game) {
    if (loaded_) return;
    if (autoLoad_) load(game, -1);
    else {   // a new game keeps the autosave's setting and slot (bits 26 and 25 of the settings)
        const uint64_t kept = get<uint64_t>(data_, 0x50) & (3ULL << 25);
        clearCurrentSaveData(game, true);
        put<uint64_t>(data_, 0x50, (get<uint64_t>(data_, 0x50) & ~(3ULL << 25)) | kept);
    }
    autoLoad_ = true;
}

void SaveGame::writeHeader() {
    put<uint16_t>(data_, 0, 0x1D);
    put<uint16_t>(data_, 2, 0xDB0);
    put<uint16_t>(data_, 4, 0x52E);
    put<uint16_t>(data_, 6, 0x5CE);
    std::memset(data_ + 8, 0, 0x20);
    std::memcpy(data_ + 8, kVersion, sizeof kVersion);
    const std::time_t now = std::time(nullptr);
    const std::tm* t = std::localtime(&now);   // getDate
    put<int32_t>(data_, 0x28, t->tm_year + 1900);
    put<int32_t>(data_, 0x2C, t->tm_mon + 1);
    put<int32_t>(data_, 0x30, t->tm_mday);
    put<int32_t>(data_, 0x34, t->tm_hour);
    put<int32_t>(data_, 0x38, t->tm_min);
    put<int32_t>(data_, 0x3C, t->tm_sec);
}

void SaveGame::writeCode(Game& game) {
    data_[0xF4] = 0x7F;   // full health, whatever the player has
    put<int32_t>(data_, 0x4C, game.money);
    data_[0xF5] = (uint8_t)game.player.armour;
    for (int i = 0; i < WeaponManager::kSlots; ++i) {   // a slot's type and ammo, but not 0x12, 0x18 or 0x19
        data_[0xF6 + i] = 0x1D;
        put<int16_t>(data_, 0x62 + 2 * i, 0);
        const Weapon* w = game.playerWeapons.inSlot(i);
        if (!w || !w->ammoCount() || w->type == 0x18 || w->type == 0x19 || w->type == 0x12) continue;
        data_[0xF6 + i] = (uint8_t)w->type;
        put<int16_t>(data_, 0x62 + 2 * i, (int16_t)w->ammoCount());
    }
    // (cHotWire::Save: the port has no cHotWire state, so +0x101 and +0x154 keep what they hold)
    // the clock six hours on, into the next day past midnight
    const TimeCycle& tc = game.world.timeCycle();
    int32_t time = (int32_t)tc.time() + (6 << 12);
    int32_t day = tc.day;
    if (time >= (24 << 12)) {
        time -= 24 << 12;
        day = day + 1 > 6 ? 0 : day + 1;
    }
    put<int32_t>(data_, 0x44, time);
    put<int32_t>(data_, 0x48, day);
}

void SaveGame::restore(Game& game) {
    game.giveMoney((uint32_t)get<int32_t>(data_, 0x4C));
    game.player.health = data_[0xF4];   // cPlayer::SetHealth (its +0x1264 is never set here)
    game.player.setArmour(data_[0xF5]);
    for (int i = 0; i < WeaponManager::kSlots; ++i) {
        const int type = (int8_t)data_[0xF6 + i];
        if ((unsigned)type <= 0x1D && (0x23040000u >> type & 1)) continue;   // not 0x12, 0x18, 0x19 or 0x1D
        const int16_t ammo = get<int16_t>(data_, 0x62 + 2 * i);
        if (ammo) game.giveWeapon(type, ammo, false);
    }
    TimeCycle& tc = game.world.timeCycle();
    tc.setTime((uint32_t)get<int32_t>(data_, 0x44));   // cTimeCycle::SetTime(t, false)
    tc.day = get<int32_t>(data_, 0x48);                // SetDay
    // (the settings' auto drive-by, control mode and weapon switching have no counterpart in the port)
}

bool SaveGame::saveCurrent(Game& game, int slot, bool inPda, bool skipCode) {
    const uint64_t settings = get<uint64_t>(data_, 0x50);
    put<uint64_t>(data_, 0x50, settings & ~(1ULL << 28));
    if (slot == -1) {
        if (settings >> 26 & 1) {   // the autosave's own slot
            put<uint64_t>(data_, 0x50, get<uint64_t>(data_, 0x50) | 1ULL << 28);
            slot = (int)(settings >> 25 & 1);
        } else slot = slot_;
    }
    uint8_t header[0x44]{};
    if (!slotHeader(slot, header) || get<int32_t>(header, 0x40) != get<int32_t>(data_, 0x40))
        put<int32_t>(data_, 0x40, 0);
    writeHeader();
    if (!skipCode) writeCode(game);
    if (slot == 4) return true;
    slot_ = (int8_t)slot;
    game.stats.incNumberOfTimesSaved();
    game.stats.calcCriminalRating();
    if (inPda) inPda_ = true;
    std::memcpy(data_ + kStats, &game.stats.data(), sizeof(StatsData));
    void* f = nullptr;
    bool ok = OS_FileOpen(OS_AREA_DOCUMENTS, &f, fileName(slot).c_str(), OS_FILE_WRITE) == OS_OK;
    if (ok) {
        ok = OS_FileWrite(f, data_, kSize) == OS_OK;
        OS_FileClose(f);
    }
    put<uint64_t>(data_, 0x50, get<uint64_t>(data_, 0x50) & ~(1ULL << 28));
    return ok;
}

bool SaveGame::deleteSlot(int slot) { return OS_FileDelete(OS_AREA_DOCUMENTS, fileName(slot).c_str()) == OS_OK; }

void SaveGame::setMissionPassed(int mission, int status) {
    const int at = kScript + (mission >> 4) * 4, shift = (mission & 15) * 2;
    put<uint32_t>(data_, at, (get<uint32_t>(data_, at) & ~(3u << shift)) | (uint32_t)(status & 3) << shift);
}

bool SaveGame::isMissionPassed(int mission) const {
    return (get<uint32_t>(data_, kScript + (mission >> 4) * 4) >> ((mission & 15) * 2) & 3) == 3;
}
