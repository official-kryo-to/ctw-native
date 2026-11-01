// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#pragma once
#include <cstdint>
#include <string>

class Game;

// cSaveGameMgr and SaveGameImpl: the save game. Its data (cSaveGameMgr::mData, sSaveGameData) is 0xDB0 bytes kept
// as the original lays them out, and a slot is that block as it is, in the documents area as "savegame<slot>"
// (sysFOpen after ChDirDocuments). Its sections:
//   +0x000 the header (0x44): halfwords 0x1D and 0xDB0 (what GetSaveSlotHeader checks), the main and email text
//          counts 0x52E and 0x5CE, gVersionstr (0x20 bytes), the date and time saved (getDate: the year, month 1-12,
//          day, hour, minute, second) and +0x40 the Social Club stamp
//   +0x044 the code section (0x194, sCodeSaveData): the clock (+0x44, 20.12 hours) and its day (+0x48), the money
//          (+0x4C), the settings bits (+0x50), the 11 weapon slots' ammo (+0x62) and types (+0xF6, 0x1D none), health
//          (+0xF4) and armour (+0xF5), cHotWire's (+0x101, +0x154) and two 26-entry tables packed three to a word
//          (+0x178, +0x19C)
//   +0x1D8 the stats section (0x2A0, cStatsManager's StatsData)
//   +0x478 the script section (0x938): the missions' status, 2 bits each from +0; the help shown once a game, a bit
//          each from +0x2C; the safehouses owned, a bit each from +0x34; the current safehouse (+0x7A1) ...
// The game has two slots, 0 and 1 (cPauseLoadSaveDelApp), and loads the newer of the two when it starts
// (SaveGameImpl::TryAutoLoad from CScriptMain::Setup). Not ported: the replay's soft save (slot 4), the trade, email
// and garage data the port has no game for (they keep what the slot held), and sSaveGameData::FixUpTextIds (a save
// of this version has nothing to fix up).
class SaveGame {
public:
    static constexpr int kSize = 0xDB0, kCode = 0x44, kStats = 0x1D8, kScript = 0x478;
    static constexpr int kSlots = 2;
    // cActStructure's missions as InitaliseMissionStructure adds them: KEN_A01 is the 38th (id 0x3B1)
    static constexpr int kKenA01 = 37;
    enum MissionStatus { kPassed = 3 };   // eMissionStatus

    // cSaveGameMgr::Init: the tables from setup (save_tables.bin), and cleared data
    bool init(const std::string& dataDir);
    void clearCurrentSaveData(Game& game, bool restore);        // ClearCurrentSaveData
    bool slotHeader(int slot, uint8_t header[0x44]);             // GetSaveSlotHeader
    bool load(Game& game, int slot);                            // Load (-1: the newer of slots 0 and 1)
    bool saveCurrent(Game& game, int slot, bool inPda, bool skipCode);   // SaveCurrent
    bool deleteSlot(int slot);                                   // DeleteSaveInSlot
    void tryAutoLoad(Game& game);                               // SaveGameImpl::TryAutoLoad
    bool hasSaveGameLoaded() const { return loaded_; }          // SaveGameImpl::HasSaveGameLoaded
    void loadComplete() { loaded_ = false; }                    // SaveGameImpl::LoadComplete
    int slot() const { return slot_; }                          // +2: the slot last used
    bool autoSaveEnabled() const { return data_[kCode + 0xF] >> 2 & 1; }   // SaveGameImpl::AutoSaveEnabled

    // the script section (SaveGameImpl)
    bool helpDisplayed(int id) const { return id >= 0 && id < kHelpBits && (word(kScript + 0x2C, id) >> (id & 31) & 1); }
    void setHelpDisplayed(int id, bool shown) { if (id >= 0 && id < kHelpBits) setBit(kScript + 0x2C, id, shown); }
    bool safehouseOwned(int i) const { return i >= 0 && (word(kScript + 0x34, i) >> (i & 31) & 1); }
    void setSafehouseOwned(int i, bool owned) { if (i >= 0) setBit(kScript + 0x34, i, owned); }
    int currentSafehouse() const { return (int8_t)data_[kScript + 0x7A1]; }   // (-1: none)
    void setCurrentSafehouse(int i) { data_[kScript + 0x7A1] = (uint8_t)i; }
    void setMissionPassed(int mission, int status);              // SetMissionPassed
    bool isMissionPassed(int mission) const;                    // IsMissionPassed: status 3

    const uint8_t* data() const { return data_; }

private:
    static constexpr int kHelpBits = 64;   // +0x2C and +0x30: the safehouses' bits follow
    uint32_t word(int at, int bit) const;
    void setBit(int at, int bit, bool on);
    void writeHeader();                       // WriteHeaderPartOfSaveGameData
    void writeCode(Game& game);               // WriteCodePartOfSaveGameData
    void restore(Game& game);                 // RestoreGameFromCodeSaveData
    void defaults();                          // the cleared data (memset and the defaults)
    bool loadSlot(int slot);                  // LoadSlot
    static std::string fileName(int slot);    // "savegame%d"

    bool loaded_ = false;    // +0: a save has been loaded (until LoadComplete)
    bool autoLoad_ = true;   // +1: TryAutoLoad has not loaded yet
    int8_t slot_ = -1;       // +2
    bool inPda_ = false;     // +4
    uint8_t data_[kSize]{};
    uint32_t tableA_[26]{}, tableB_[26]{};
};
