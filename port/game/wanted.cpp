// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "wanted.h"
#include "random.h"
#include <algorithm>
#include <cmath>

bool WantedManager::init(const std::string& dataDir) {
    loaded_ = tables_.load(dataDir + "/wanted_tables.bin");
    reset();
    return loaded_;
}

void WantedManager::reset() {   // cWantedManager::cWantedManager
    level_ = 0;
    rating_ = 0;
    decayMode_ = 1;
    state_ = 1;
    flash_ = 0;
    maxLevel_ = 6;
    minLevel_ = 0;
    copInViewFrame_ = pursuingFrame_ = 0;
    noCrimeTimer_ = 0;
    multiplier_ = 100;
    changed_ = false;
    takeDowns_ = 0;
    copSeenPos_[0] = copSeenPos_[1] = 0;
}

int WantedManager::starsTakenDown(int takeDowns) const {   // cWantedManager::StarsTakenDown
    if (!level_) return 0;
    int count = 0;
    for (int k = level_; k >= 1; --k) {   // from the top star down
        if (takeDowns < tables_.takeDownsPerCopCar[k]) return count;
        takeDowns -= tables_.takeDownsPerCopCar[k];
        ++count;
    }
    return level_;
}

int WantedManager::maxTakeDownsForLevel(int level) const {   // cWantedManager::MaxTakeDownsForLevel
    int total = 0;
    for (int k = level; k >= 1; --k) total += tables_.takeDownsPerCopCar[k];
    return total;
}

int WantedManager::levelRating(int level) const {
    if (level == 6) return 0x12C0;
    if (level == 1) return 0x50;   // MinWantedRatingForWanted1
    return (threshold(level) + threshold(level + 1)) >> 1;
}

void WantedManager::sync() {   // cWantedRating::Sync (and the same tail of Update, SetWantedLevel and the rest)
    int l = level_;
    if (l != 7) {
        for (;;) {   // up while the rating reaches the next threshold
            if (rating_ < (uint32_t)threshold(l + 1)) break;
            if (rating_ <= 0x4F && l == 0) rating_ = 0x50;
            level_ = ++l;
            flash_ = 90;
            if (l == 7) break;
        }
    }
    if (rating_ < (uint32_t)threshold(l)) {   // down below the level's own threshold
        flash_ = 90;
        do --l; while ((uint32_t)threshold(l) > rating_);
        level_ = l;
    }
}

void WantedManager::update(uint32_t frame, const int32_t pos[3], bool copInView, int framesSincePursuing) {
    // cWantedRating::Update. The player's process bucket is taken as every frame (shift 0, offset 0).
    flash_ = flash_ ? (uint8_t)(flash_ - 1) : 0;
    if (rating_ && !copInView && (frame & 7) == 7) {
        switch (decayMode_) {
        case 3:   // the longer since a pursuing cop saw the player, the faster (up to 5 every eighth frame)
            if (framesSincePursuing >= 1 && rating_ >= 0x51) {
                const uint32_t d = (uint32_t)framesSincePursuing > 0x4FF ? 5 : (uint32_t)framesSincePursuing >> 8;
                rating_ = rating_ - d > 0x50 ? rating_ - d : 0x50;
            }
            [[fallthrough]];
        case 2:   // DecayPoints: a unit per 200 from where a cop last saw him, up to 6
            if (rating_ >= 0x51) {
                const int64_t dx = (int64_t)pos[0] - copSeenPos_[0], dy = (int64_t)pos[1] - copSeenPos_[1];
                const uint32_t dist = (uint32_t)std::sqrt((double)(uint64_t)(dx * dx + dy * dy));
                const int64_t q = (int64_t)((uint64_t)dist << 32) / 204800;   // cFixed: dist / 50
                uint32_t amount = (uint32_t)((int32_t)(uint32_t)((uint64_t)q >> 20) >> 12) >> 2;
                amount = amount < 6 ? amount : 6;
                rating_ = rating_ - amount > 0x50 ? rating_ - amount : 0x50;
            }
            [[fallthrough]];
        case 1:
            rating_ = rating_ ? rating_ - 1 : 0;
            break;
        default:
            break;
        }
    }
    sync();
}

bool WantedManager::onLevelChange(int from, int to, bool oneStarDimmed, bool clearRating, bool keepRating) {
    // cWantedManager::OnLevelChange (the takedown HUD's reaction is not ported)
    takeDowns_ = 0;
    if (oneStarDimmed) {
        if (to > from) decayMode_ = 0;
    }
    const bool drop = (oneStarDimmed && to < from) || to == 0;
    if (!drop) {
        if (keepRating) return false;
        rating_ = levelRating(to);
        sync();
        return false;
    }
    if (clearRating) {
        rating_ = 0;
        sync();
        if (state_ != 1) { decayMode_ = 3; state_ = 1; }
        return true;
    }
    // GoOnNoCrime: the old level's rating back, dimmed for a 300-frame probation
    rating_ = levelRating(from);
    sync();
    if (state_ != 2) { decayMode_ = 0; state_ = 2; }
    takeDowns_ = 0;
    noCrimeTimer_ = 300;
    return true;
}

void WantedManager::failAnyNoCrimeState() {   // cWantedManager::FailAnyNoCrimeState
    if (state_ != 2) return;
    if (state_ != 1) { decayMode_ = 3; state_ = 1; }
    takeDowns_ = 0;
    rating_ = levelRating(level_);
    sync();
    changed_ = true;
}

void WantedManager::registerCrime(int type, uint32_t frame) {   // cWantedManager::RegisterCrime
    if (!loaded_ || type < 0 || type >= 26) return;
    if (state_ == 2 && ((type & ~1) != 12 || frame - copInViewFrame_ <= 4)) failAnyNoCrimeState();
    int l = level_;
    if (state_ != 0 && !(state_ != 2 && level_ >= 1 && type <= 24 && ((1u << type) & 0x1810000u))) {
        const uint32_t old = rating_;
        const uint16_t crime = tables_.crimes[type];
        uint32_t r = old + (uint32_t)(((uint64_t)((crime & 0x1FFFu) * multiplier_) * 0x28F5C29u) >> 32);
        rating_ = r;
        // with a pursuing cop seeing him (and for crimes 25 and 26) any crime is at least one star
        if (r <= 0x31 && ((unsigned)(type - 25) < 2 || (int32_t)(frame - pursuingFrame_) <= 4)) r = 0x50;
        switch (crime & 0xC000) {
        case 0: break;
        case 0xC000: if (r < 0x226) r = 0x244; break;   // three stars at once
        default:
            if (!((0x3FFFABFull >> type) & 1)) { if (r < 0xB4) r = 0xD2; }   // two stars
            else if (!((0x842FEBull >> type) & 1)) { if (r <= 0x31) r = 0x50; }   // one star
            break;
        }
        rating_ = r;
        if (maxLevel_ == 0) rating_ = r = 0;
        if (old != r) {
            if (l == 0) return;   // the level follows on the next Process
            if (l >= 2 && starsTakenDown(takeDowns_) == l && takeDowns_ >= 3) --takeDowns_;
        }
        l = level_;
    }
    if (l == 1) takeDowns_ = 0;
}

void WantedManager::setCopInView(uint32_t frame, const int32_t pos[3], bool pursuing) {
    copSeenPos_[0] = pos[0];   // cPlayer::SetCopInView: cPlayer +0x1114
    copSeenPos_[1] = pos[1];
    copInViewFrame_ = frame;   // cWantedManager::SetCopInView
    if (pursuing) pursuingFrame_ = frame;
}

void WantedManager::setLevel(int level, bool onlyRaise, bool clearRating) {   // cWantedManager::SetLevel
    if (level_ >= level && onlyRaise) return;
    if (level_ == level) return;
    changed_ = true;
    onLevelChange(level_, level, oneStarDimmed(), clearRating, false);
    takeDowns_ = 0;
}

void WantedManager::setMaxLevel(int level) {   // cWantedManager::SetMaxLevel
    maxLevel_ = level;
    if ((state_ == 2 ? 0 : level_) <= level || level_ == level) return;
    changed_ = true;
    onLevelChange(level_, level, oneStarDimmed(), false, false);
    takeDowns_ = 0;
}

void WantedManager::setMinLevel(int level) {   // cWantedManager::SetMinLevel
    minLevel_ = level;
    if (state_ == 2 ? level <= 0 : level_ >= level) return;
    if (level_ == level) return;
    changed_ = true;
    onLevelChange(level_, level, oneStarDimmed(), false, false);
    takeDowns_ = 0;
}

void WantedManager::forceDropWantedLevel(bool clearRating) {   // cWantedManager::ForceDropWantedLevel
    if (!level_) return;
    changed_ = true;
    onLevelChange(level_, level_ - 1, oneStarDimmed(), clearRating, false);
    takeDowns_ = 0;
}

void WantedManager::onTakeDown() {   // cWantedManager::OnTakeDown
    const int l = level_;
    if (l == 0 && l >= minLevel_) return;
    const int before = starsTakenDown(takeDowns_);
    if (l != 0 && maxTakeDownsForLevel(l) > takeDowns_) ++takeDowns_;
    const int after = starsTakenDown(takeDowns_);
    const int lit = l - after;
    if (l == 0 || lit < minLevel_) {   // never below the minimum: give the takedowns back
        while (l - starsTakenDown(takeDowns_) != minLevel_ && takeDowns_) --takeDowns_;
        return;
    }
    if (l >= 1 && starsTakenDown(takeDowns_) == l) {   // every star taken down: just over the level's threshold
        rating_ = threshold(l) + 1;
        if (state_ != 1) { decayMode_ = 3; state_ = 1; }
    }
    if (before == after) return;
    if (l >= 1 && starsTakenDown(takeDowns_) == l) return;
    if (lit == 1) rating_ = threshold(l) + 30;   // SetToMinimumTimeForThisLevel
}

bool WantedManager::process(uint32_t frame, const int32_t pos[3], bool inVehicle) {   // cWantedManager::Process
    if (!loaded_) return false;
    int prev = 0;
    if (state_ != 2) {
        prev = level_;
        const int lit = starsLit();
        decayMode_ = lit < 2 ? 3 : 0;   // two or more lit stars only go by taking down cops
        const bool dimmed = level_ > 0 && lit < 2;
        update(frame, pos, copInView(frame), (int32_t)(frame - pursuingFrame_));
        int l = level_;
        if (l > maxLevel_) {   // at the ceiling: just below the next threshold, and one more star lit
            rating_ = threshold(maxLevel_ + 1) - tables_.ratingToSubtractOnReachingMaxLevel[maxLevel_];
            sync();
            l = level_;
            int total = 0, lit2;
            if (l != 0) {
                lit2 = std::min(l - starsTakenDown(takeDowns_) + 1, maxLevel_);
                total = maxTakeDownsForLevel(l);
            } else lit2 = maxLevel_ < 1 ? maxLevel_ : 1;
            takeDowns_ = (uint8_t)(total - (lit2 > 0 ? maxTakeDownsForLevel(lit2) : 0));
            l = level_;
        }
        if (l < minLevel_) {
            rating_ = threshold(minLevel_);
            sync();
            l = level_;
        }
        if (prev != l) onLevelChange(prev, l, dimmed, false, true);
    }
    if (noCrimeTimer_) {   // the probation running out
        const uint16_t old = noCrimeTimer_;
        noCrimeTimer_ = (uint16_t)(old - 1);
        if (old <= 1 && state_ == 2 && minLevel_ == 0) {
            rating_ = 0;
            sync();
            if (state_ != 1) { decayMode_ = 3; state_ = 1; }
        }
    }
    // road blocks and stinger traps at three stars and more, every 128 frames while driving (not ported: the roll)
    if (inVehicle && state_ != 2 && level_ >= 3 && level_ <= 7 && (frame & 127) == 127) Rand32Critical(100);
    const bool result = changed_ || prev != (state_ == 2 ? 0 : level_);
    changed_ = false;
    return result;
}
