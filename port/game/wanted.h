// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// The player's wanted level: ports of cWantedRating and cWantedManager (cPlayer +0x400).
//
// Crimes add rating points (wanted_tables.bin: the crime's points x the multiplier / 100; some crimes jump straight
// to one, two or three stars, and with cops pursuing in view any crime is at least one star). The level is the
// highest threshold the rating reaches (0..6). Stars can be "taken down" by wrecking cop cars (takeDownsPerCopCar per
// star, counted from the top): the lit stars are the level minus those taken down. With fewer than two lit stars the
// rating decays every eighth frame while no cop is in view (faster the longer since a pursuing cop saw the player and
// the further he is from where a cop last saw him); with two or more lit stars it does not decay at all. When the
// level falls the stars stay dimmed for a probation of 300 frames: a crime in that time brings the old level back,
// otherwise it clears. Reaching the maximum level puts the rating just below the next threshold.
// Not ported yet: the road blocks and stinger traps (their Rand32Critical(100) roll is made), the takedown HUD
// (cMiniHud::OnTakedownMechanicStart) and the script listeners.
#pragma once
#include "lookups.h"
#include <cstdint>
#include <string>

class WantedManager {
public:
    // eCrimeType (the original's names are not exported; these describe the crimes that use them)
    enum Crime {
        HitPed = 1, HitCop = 2, ShootPed = 3, ShootCop = 4, KillPed = 5, KillCop = 6, HitPedWith11 = 7,
        HitCopWith11 = 8, ChainsawPed = 9, ChainsawCop = 10, Explosion = 11, HotCarSeenByCops = 12, Jack = 13,
        JackSeenByCops = 14, JackCopCar = 15, DamageCopCar = 16, DamageCopCarHard = 17, TaserPed = 18,
        TaserCop = 19, BurnPed = 20, BurnCop = 21, BurnVehicle = 22, RunOverPed = 23, RunOverCop = 24
    };

    bool init(const std::string& dataDir);   // wanted_tables.bin
    bool ok() const { return loaded_; }
    void reset();                            // cWantedManager::cWantedManager
    // cWantedManager::Process, from the player's update: true when the level (or the probation) changed.
    // inVehicle: the player drives (road blocks only come then).
    bool process(uint32_t frame, const int32_t playerPos[3], bool inVehicle);
    void registerCrime(int type, uint32_t frame);                 // cWantedManager::RegisterCrime
    // cPlayer::SetCopInView: a cop sees the player at playerPos (pursuing: a cop that is chasing him)
    void setCopInView(uint32_t frame, const int32_t playerPos[3], bool pursuing);
    void setLevel(int level, bool onlyRaise, bool clearRating);   // cWantedManager::SetLevel
    void setMaxLevel(int level);                                  // SetMaxLevel
    void setMinLevel(int level);                                  // SetMinLevel
    void forceDropWantedLevel(bool clearRating);                  // ForceDropWantedLevel
    void onTakeDown();                                            // OnTakeDown: a cop car was taken down

    int level() const { return state_ == 2 ? 0 : level_; }       // cWantedRating::WantedLevel
    int actualLevel() const { return level_; }                    // ActualWantedLevel / GetStarDisplayLevel
    int starsLit() const { return level_ - starsTakenDown(takeDowns_); }   // WantedStarsLit
    bool starsFlashing() const { return state_ == 2 || flash_ != 0; }       // IsStarsFlashing
    bool onProbation() const { return state_ == 2; }
    bool copInView(uint32_t frame) const { return frame - copInViewFrame_ < 5; }   // IsCopInView
    bool pursuingCopInView(uint32_t frame) const { return (int32_t)(frame - pursuingFrame_) < 5; }
    uint32_t rating() const { return rating_; }
    int takeDowns() const { return takeDowns_; }
    const WantedTables& tables() const { return tables_; }

private:
    int threshold(int level) const { return tables_.levels[level].threshold; }
    int starsTakenDown(int takeDowns) const;           // StarsTakenDown
    int maxTakeDownsForLevel(int level) const;         // MaxTakeDownsForLevel
    int levelRating(int level) const;                  // the rating OnLevelChange / GoOnNoCrime give a level
    void sync();                                       // the level from the rating (cWantedRating's common tail)
    void update(uint32_t frame, const int32_t playerPos[3], bool copInView, int framesSincePursuing);   // cWantedRating::Update
    bool onLevelChange(int from, int to, bool oneStarDimmed, bool clearRating, bool keepRating);       // OnLevelChange
    void failAnyNoCrimeState();                        // FailAnyNoCrimeState
    bool oneStarDimmed() const { const int lit = starsLit(); return level_ > 0 && lit < 2; }

    WantedTables tables_;
    bool loaded_ = false;
    int level_ = 0;                  // +0x00
    uint32_t rating_ = 0;            // +0x04
    uint8_t decayMode_ = 1;          // +0x08: 0 none, 1 plain, 2 + distance, 3 + time since a pursuing cop
    uint8_t state_ = 1;              // +0x09: 0 off, 1 normal, 2 probation (no-crime period)
    uint8_t flash_ = 0;              // +0x0B: frames the stars flash after a change
    int maxLevel_ = 6, minLevel_ = 0;            // +0x0C, +0x10
    uint32_t copInViewFrame_ = 0, pursuingFrame_ = 0;   // +0x14, +0x18
    uint16_t noCrimeTimer_ = 0;      // +0x1C
    uint8_t multiplier_ = 100;       // +0x1E: crime points percentage
    bool changed_ = false;           // +0x1F
    uint8_t takeDowns_ = 0;          // +0x21
    int32_t copSeenPos_[2] = {0, 0}; // cPlayer +0x1114: where a cop last saw the player
};
