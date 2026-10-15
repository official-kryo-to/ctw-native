// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "stats.h"
#include "game.h"
#include <cmath>

namespace {
// A field of a bit-packed word: the counters wrap at their width
uint64_t field(uint64_t word, int shift, int width) { return word >> shift & ((1ull << width) - 1); }
uint64_t withField(uint64_t word, int shift, int width, uint64_t value) {
    const uint64_t mask = ((1ull << width) - 1) << shift;
    return (word & ~mask) | (value << shift & mask);
}
void inc(uint64_t& word, int shift, int width) { word = withField(word, shift, width, field(word, shift, width) + 1); }

// e_main.gxt ids of the radio stations (cStatsManager's table at 0x486D50)
constexpr int kStationText[11] = {225, 226, 227, 228, 231, 232, 233, 1327, 1328, 1329, 229};
}   // namespace

// The one-line counters and getters are inline in stats.h, in the order of the original:
// cStatsManager::AddTotalPlayingTime, cStatsManager::AddCurrentPlayingTime,
// cStatsManager::AddDistanceTravelledOnFoot, cStatsManager::AddDistanceSwam,
// cStatsManager::AddDistanceTravelledByCar, cStatsManager::AddDistanceTravelledByBike,
// cStatsManager::AddDistanceTravelledByBoat, cStatsManager::AddSpentOnWeapons, cStatsManager::AddSpentOnPayNSpray,
// cStatsManager::AddSpentOnProperty, cStatsManager::AddLostOnStreetRaces, cStatsManager::AddSpentOnBribingCops,
// cStatsManager::AddSpentOnHealthCare, cStatsManager::AddMadeFromActivities, cStatsManager::AddMadeFromTaxiTips,
// cStatsManager::AddFoundOnTheStreet, cStatsManager::AddValueOfGoodsSold, cStatsManager::AddValueOfGoodsBought,
// cStatsManager::IncBulletsFired, cStatsManager::IncBulletsThatHit, cStatsManager::IncPeopleKilled,
// cStatsManager::IncPeopleBurntOut, cStatsManager::IncNearMisses, cStatsManager::IncLionsCollected,
// cStatsManager::IncCockroachesSquashed, cStatsManager::IncVehiclesStolen,
// cStatsManager::AddNumberOfWantedStarsAttained, cStatsManager::AddNumberOfWantedStarsEvaded,
// cStatsManager::IncCriminalsKilled, cStatsManager::AddUnitsOfGoodsSold, cStatsManager::AddUnitsOfGoodsBought,
// cStatsManager::IncNumberOfCarsAndBikesDestroyed, cStatsManager::IncNumberOfBoatsDestroyed,
// cStatsManager::IncMeleeKills, cStatsManager::IncGangKills, cStatsManager::AddMadeFromTrading,
// cStatsManager::AddSpentOnScratchCards, cStatsManager::AddMadeOnScratchCards, cStatsManager::ForceRadioTime,
// cStatsManager::CalcTotalGangMembersWasted, cStatsManager::GetTattoosDone,
// cStatsManager::TotalValueDrugsConfiscated, cStatsManager::AddTotalValueDrugsConfiscated,
// cStatsManager::GetLionsCollected, cStatsManager::GetCockroachesSquashed, cStatsManager::GetDaysPassed,
// cStatsManager::GetStatsData, cStatsManager::GetMedalAwarded

void Stats::reset() {   // cStatsManager::cStatsManager
    currentPlayingTime_ = 0;
    data_ = StatsData{};
    hourWanted_ = 0;
    enabled_ = true;
}

void Stats::updateTime(int minutes) {   // cStatsManager::UpdateTime
    data_.totalPlayingTime += (uint32_t)minutes;
    currentPlayingTime_ += (uint32_t)minutes;
    // then cPlayer::UpdateChaseTime for the local player (the police chase timers are not ported)
}

void Stats::maxLongestTimeSpentInAVehicle(uint32_t seconds) {   // cStatsManager::MaxLongestTimeSpentInAVehicle
    if (data_.longestTimeInVehicle < seconds) data_.longestTimeInVehicle = seconds;
}

void Stats::maxLongestPoliceChase(uint32_t t) {   // cStatsManager::MaxLongestPoliceChase
    if (data_.longestPoliceChase < t) data_.longestPoliceChase = (uint16_t)t;
}

void Stats::maxLongestChaseWithA6StaRating(uint32_t t) {   // cStatsManager::MaxLongestChaseWithA6StaRating
    if (data_.longestChaseWith6Stars < t) data_.longestChaseWith6Stars = (uint16_t)t;
}

void Stats::incWeaponKills(int weaponType) {   // cStatsManager::IncWeaponKills
    ++data_.weaponKills[weaponType];
}

void Stats::incNumberOfTimesBusted() {   // cStatsManager::IncNumberOfTimesBusted
    data_.busted = (uint16_t)((data_.busted & 0xF000) | ((data_.busted + 1) & 0xFFF));
}
void Stats::incNumberOfTimesWasted() { inc(data_.wordC0, 0, 12); }       // cStatsManager::IncNumberOfTimesWasted
void Stats::incUniqueCommoditiesOwned() { inc(data_.wordC0, 12, 8); }    // cStatsManager::IncUniqueCommoditiesOwned
void Stats::incMissionsFailed() { inc(data_.wordC0, 20, 12); }           // cStatsManager::IncMissionsFailed
void Stats::incFishesFed() { inc(data_.wordC0, 32, 12); }                // cStatsManager::IncFishesFed
void Stats::incMissionsPassed() { inc(data_.wordC0, 44, 8); }            // cStatsManager::IncMissionsPassed
void Stats::incDaysPassedInGame() { inc(data_.wordC0, 52, 12); }         // cStatsManager::IncDaysPassedInGame
void Stats::incTimesCheated() { inc(data_.wordC8, 0, 12); }              // cStatsManager::IncTimesCheated
void Stats::incShipmentsRobbed() { inc(data_.wordC8, 12, 8); }           // cStatsManager::IncShipmentsRobbed
void Stats::incNumberOfTimesSaved() { inc(data_.wordC8, 20, 12); }       // cStatsManager::IncNumberOfTimesSaved
void Stats::incWarehouseRaidsPulledOff() { inc(data_.wordC8, 44, 8); }   // cStatsManager::IncWarehouseRaidsPulledOff
uint32_t Stats::warehouseRaidsPulledOff() const {   // cStatsManager::GetWarehouseRaidsPulledOff
    return (uint32_t)field(data_.wordC8, 44, 8);
}
void Stats::incExplosions() { inc(data_.wordD0, 52, 12); }               // cStatsManager::IncExplosions
void Stats::incTradesDone() {   // cStatsManager::IncTradesDone
    data_.tradesDone = (data_.tradesDone & 0xFFF00000u) | ((data_.tradesDone + 1) & 0xFFFFF);
}

void Stats::setWordD8(uint32_t v) {
    data_.wordD8[0] = (uint8_t)v;
    data_.wordD8[1] = (uint8_t)(v >> 8);
    data_.wordD8[2] = (uint8_t)(v >> 16);
}
void Stats::incWeaponsPickedUp() {   // cStatsManager::IncWeaponsPickedUp
    const uint32_t w = wordD8();
    setWordD8((w & 0xFFF000) | ((w + 1) & 0xFFF));
}
void Stats::incMissionRetries() { setWordD8(wordD8() + 0x1000); }   // cStatsManager::IncMissionRetries (24-bit wrap)

// The stunt records: each keeps the larger value, stored at its field width
void Stats::maxLongestWheelieDistance(uint32_t d) {   // cStatsManager::MaxLongestWheelieDistance
    if (d > 0xFFE) d = 0xFFF;
    if (field(data_.wordC8, 32, 12) < d) data_.wordC8 = withField(data_.wordC8, 32, 12, d);
}
void Stats::maxLongestStoppieDistance(uint32_t d) {   // cStatsManager::MaxLongestStoppieDistance
    if (field(data_.wordC8, 52, 12) < d) data_.wordC8 = withField(data_.wordC8, 52, 12, d);
}
void Stats::maxLongest2WheelDistance(uint32_t d) {   // cStatsManager::MaxLongest2WheelDistance
    if (field(data_.wordD0, 0, 12) < d) data_.wordD0 = withField(data_.wordD0, 0, 12, d);
}
void Stats::maxMostFlipsInOneJump(uint32_t n) {   // cStatsManager::MaxMostFlipsInOneJump
    if (field(data_.wordD0, 12, 8) < n) data_.wordD0 = withField(data_.wordD0, 12, 8, n);
}
void Stats::maxFarthestJumpDistance(uint32_t d) {   // cStatsManager::MaxFarthestJumpDistance
    if (field(data_.wordD0, 20, 12) < d) data_.wordD0 = withField(data_.wordD0, 20, 12, d);
}
void Stats::maxHighestJumpReached(uint32_t h) {   // cStatsManager::MaxHighestJumpReached
    if (field(data_.wordD0, 32, 12) < h) data_.wordD0 = withField(data_.wordD0, 32, 12, h);
}
void Stats::maxMostSpinInOneJump(uint32_t n) {   // cStatsManager::MaxMostSpinInOneJump
    if (field(data_.wordD0, 44, 8) < n) data_.wordD0 = withField(data_.wordD0, 44, 8, n);
}

void Stats::addRadioTime(unsigned station, uint32_t t) {   // cStatsManager::AddRadioTime
    // all twelve stations' times halve when this one would overflow (a 32-bit carry)
    if ((uint64_t)data_.radioTime[station] + t > 0xFFFFFFFFull)
        for (uint16_t& r : data_.radioTime) r = (uint16_t)(r >> 1);
    data_.radioTime[station] = (uint16_t)(data_.radioTime[station] + t);
}

void Stats::processDistance(const int32_t vel[3], int vehicleType, bool swimming) {   // cStatsManager::ProccessDistance
    uint32_t* d;
    if (vehicleType < 0) d = swimming ? &data_.distance[1] : &data_.distance[0];
    else if (vehicleType <= 2) d = &data_.distance[2 + vehicleType];
    else return;
    const uint64_t s = (uint64_t)((int64_t)vel[0] * vel[0] + (int64_t)vel[1] * vel[1] + (int64_t)vel[2] * vel[2]);
    const uint32_t len = (uint32_t)std::sqrt((double)s);
    // |v| x 0x880 (17/512 of a second's move, x 16): the distance this frame in 1/16 units
    *d += (uint32_t)((int32_t)((uint64_t)((int64_t)(int32_t)len * 0x880) >> 12) >> 12);
}

void Stats::processWantedChange(uint32_t level, uint32_t previous) {   // cStatsManager::ProcessWantedChange
    if (!enabled_) return;
    // (cAudioManager::AddPoliceWantedChangeSfx: the police radio is not ported)
    if (level < previous) data_.wantedStarsEvaded = (uint16_t)(data_.wantedStarsEvaded + (previous - level));
    else if (level != previous) data_.wantedStarsAttained = (uint16_t)(data_.wantedStarsAttained + (level - previous));
    if (previous < level) hourWanted_ = level;
}

void Stats::processEndOfHour(uint32_t wantedLevel) {   // cStatsManager::ProcessEndOfHour
    if (data_.mediaValue >= 0xFA) return;
    data_.mediaValue += hourWanted_ > wantedLevel ? hourWanted_ : wantedLevel;
    hourWanted_ = 0;
}

int Stats::calcCriminalRating() {   // cStatsManager::CalcCriminalRating
    StatsData& s = data_;
    const uint32_t fired = s.bulletsFired;
    const int32_t accuracy = fired ? (int32_t)((s.bulletsThatHit / fired) << 12) : 0;
    s.criminalRating = s.peopleKilled;
    s.criminalRating += (s.boatsDestroyed + s.carsAndBikesDestroyed) * 30;
    s.criminalRating += s.madeFromTrading;
    if (fired > 100) s.criminalRating += (accuracy >> 12) * 500;
    s.criminalRating -= (int32_t)(timesWasted() * 3);
    s.criminalRating -= (int32_t)(timesBusted() * 3);
    s.criminalRating -= (int32_t)(timesCheated() * 1000);
    if (timesCheated() == 0) { if (s.criminalRating < 0) s.criminalRating = 0; }
    else if (s.criminalRating < -6500) s.criminalRating = -6500;
    if (s.criminalRating > 10000000) s.criminalRating = 10000000;

    const int32_t r = s.criminalRating;
    if (r < -6000) return 0x617;
    if (r < -4000) return 0x616;
    if (r < -2000) return 0x615;
    if (r < -500) return 0x614;
    if (r < 0) return 0x613;
    const uint32_t u = (uint32_t)r;
    if (u >= 1150 && u < 1300) return timesBusted() ? 0x5EA : 0x5E9;
    static constexpr struct { uint32_t below; int text; } kRanks[] = {
        {20, 0x5D2}, {50, 0x5D3}, {75, 0x5D4}, {100, 0x5D5}, {120, 0x5D6}, {150, 0x5D7}, {200, 0x5D8}, {240, 0x5D9},
        {270, 0x5DA}, {300, 0x5DB}, {335, 0x5DC}, {370, 0x5DD}, {400, 0x5DE}, {450, 0x5DF}, {500, 0x5E0},
        {550, 0x5E1}, {600, 0x5E2}, {610, 0x5E3}, {650, 0x5E4}, {700, 0x5E5}, {850, 0x5E6}, {1000, 0x5E7},
        {1005, 0x5E8}, {1150, 0x5E9}, {1300, 0}, {1500, 0x5EB}, {1700, 0x5EC}, {2000, 0x5ED}, {2100, 0x5EE},
        {2300, 0x5EF}, {2500, 0x5F0}, {2750, 0x5F1}, {3000, 0x5F2}, {3500, 0x5F3}, {4000, 0x5F4}, {5000, 0x5F5},
        {7500, 0x5F6}, {10000, 0x5F7}, {20000, 0x5F8}, {30000, 0x5F9}, {40000, 0x5FA}, {50000, 0x5FB},
        {65000, 0x5FC}, {80000, 0x5FD}, {100000, 0x5FE}, {150000, 0x5FF}, {200000, 0x600}, {300000, 0x601},
        {375000, 0x602}, {500000, 0x603}};
    for (const auto& rank : kRanks)
        if (u < rank.below) return rank.text;
    return s.unknown0C ? 0x605 : 0x604;
}

namespace {
template <size_t N, typename T> int count(const T (&a)[N]) {
    int n = 0;
    for (const T& v : a) n += v ? 1 : 0;
    return n;
}
int medals(const SaveProgress& s, int kind) {
    int n = 0;
    for (uint8_t m : s.ambientMedal) n += m == kind;
    return n;
}
}   // namespace

int Stats::calcGoldMedals(const SaveProgress& s) { return medals(s, 3); }     // cStatsManager::CalcGoldMedals
int Stats::calcSilverMedals(const SaveProgress& s) { return medals(s, 2); }   // cStatsManager::CalcSilverMedals
int Stats::calcBronzeMedals(const SaveProgress& s) { return medals(s, 1); }   // cStatsManager::CalcBronzeMedals
int Stats::calcUSJsCompleted(const SaveProgress& s) { return count(s.uniqueJump); }   // cStatsManager::CalcUSJsCompleted
int Stats::calcCamsDestroyed(const SaveProgress& s) { return count(s.package); }      // cStatsManager::CalcCamsDestroyed
int Stats::calcSafehousesOwned(const SaveProgress& s) { return count(s.safehouse); }  // cStatsManager::CalcSafehousesOwned
int Stats::calculateNumDealersFound(const SaveProgress& s) {   // cStatsManager::CalculateNumDealersFound
    return count(s.dealer);
}
int Stats::calculateUniqueCommoditiesOwned(const SaveProgress& s) {   // cStatsManager::CalculateUniqueCommoditiesOwned
    return count(s.item);
}
int Stats::calcRandomEncountersFound(const SaveProgress& s) {   // cStatsManager::CalcRandomEncountersFound
    int n = 0;   // the bits 01 02 04 .. 80 of the encounters that are not 0x80
    for (uint8_t e : s.encounter) n += e != 0x80;
    return n;
}

int Stats::calcTotalDistanceTravelled() const {   // cStatsManager::CalcTotalDistanceTravelled
    uint32_t total = 0;
    for (uint32_t d : data_.distance) total += d;
    return (int)total;
}

// The radio times are kept for twelve slots; slot 0 (no station) reads as text 0xE0, slot n as station n - 1.
int Stats::calcMostFavRadioStation() const {   // cStatsManager::CalcMostFavRadioStation
    unsigned best = 0;   // the first slot with the most time
    uint16_t most = data_.radioTime[0];
    for (unsigned i = 1; i < 12; ++i)
        if (data_.radioTime[i] > most) { most = data_.radioTime[i]; best = i; }
    return best ? kStationText[best - 1] : 0xE0;
}

int Stats::calcLeastFavRadioStation() const {   // cStatsManager::CalcLeastFavRadioStation
    unsigned best = 0;   // the first slot with the least time; slot 0 counts at most 0x100
    uint16_t least = data_.radioTime[0] > 0xFF ? 0x100 : data_.radioTime[0];
    for (unsigned i = 1; i < 12; ++i)
        if (data_.radioTime[i] < least) { least = data_.radioTime[i]; best = i; }
    return best ? kStationText[best - 1] : 0xE0;
}

namespace {
uint32_t average(uint32_t value, uint16_t units) {   // value / units in Q12, kept to 20 bits
    const uint32_t v = value * 0x1000;
    if ((int32_t)v < 1 || units == 0) return 0;
    return (uint32_t)((((uint64_t)v << 32) / ((uint64_t)units << 12)) >> 32 & 0xFFFFF);
}
}   // namespace

uint32_t Stats::calcAverageValueOfGoodsSold() const {   // cStatsManager::CalcAverageValueOfGoodsSold
    return average(data_.valueOfGoodsSold, data_.unitsOfGoodsSold);
}
uint32_t Stats::calcAverageValueOfGoodsBought() const {   // cStatsManager::CalcAverageValueOfGoodsBought
    return average(data_.valueOfGoodsBought, data_.unitsOfGoodsBought);
}

int32_t Stats::calcAccuracy() const {   // cStatsManager::CalcAccuracy
    const uint32_t fired = data_.bulletsFired;
    int32_t a = fired ? (int32_t)(((uint32_t)(data_.bulletsThatHit * 100) / fired) << 12) : 0;
    return a > 0x63FFF ? 0x64000 : a;
}

int Stats::calcFavouriteWeapon() const {   // cStatsManager::CalcFavororiteWeapon
    int best = 0;   // the first weapon with the most kills
    uint16_t most = 0;
    for (int i = 0; i < 24; ++i) {
        const uint16_t k = data_.weaponKills[i];
        if (k > most) best = i;
        if (k >= most) most = k;
    }
    return best;
}

int Stats::calcMediaValue(int actualLevel) const {   // cStatsManager::CalcMediaValue
    const uint32_t v = data_.mediaValue;
    static constexpr uint32_t kLow[] = {3, 20, 30, 40, 50, 60, 70, 80, 90, 100};
    for (int i = 0; i < 10; ++i)
        if (v < kLow[i]) return 0x553 + i;
    if (actualLevel < 5) return 0x55C;
    if (v < 110) return 0x55D;
    if (v < 120) return 0x55E;
    if (v < 130) return 0x55F;
    if (v < 140) return 0x560;
    if (v < 150) return 0x561;
    if (v < 160 || actualLevel == 5) return 0x562;
    if (v < 170) return 0x563;
    if (v < 180) return 0x564;
    if (v < 190) return 0x565;
    return v >= 200 ? 0x567 : 0x566;
}

void Stats::logRaceTime(SaveProgress& s, int race, uint32_t time) {   // cStatsManager::LogRaceTime
    const unsigned i = (unsigned)(race - 0x4C);
    if (i >= 16) return;
    if (s.raceBestTime[i] - 1 < time) return;   // a better time is kept (0: none yet)
    s.raceBestTime[i] = time;
}

uint32_t Stats::bestRaceTime(const SaveProgress& s, int race) {   // cStatsManager::GetBestRaceTime
    const unsigned i = (unsigned)(race - 0x4C);
    return i < 16 ? s.raceBestTime[i] : 0;
}

void Stats_PlayerProcess(Game& g) {   // cPlayer::Process
    Stats& s = g.stats;
    if (g.playerCar >= 0 && g.playerCar < (int)g.cars.size()) {
        const Vehicle& c = g.cars[g.playerCar];
        const int type = c.infoId >= 0 && c.infoId < (int)g.vehicleInfos.size() ? g.vehicleInfos[c.infoId].type() : 0;
        // (the original passes the ped's own velocity, cSimpleMover +0xB8; the port moves the hidden driver with the
        // vehicle without one, so the vehicle's is used)
        s.processDistance(c.vel, type, false);
        ++s.vehicleFrames;
        s.maxLongestTimeSpentInAVehicle(s.vehicleFrames / 30);   // 0x1E >> the frame-rate shift (30 Hz: 0)
    } else {
        s.processDistance(g.player.vel, -1, false);   // (swimming is not ported)
        s.vehicleFrames = 0;
    }
}

void Stats_PedKilledByPlayer(Game& g, int subtype) {   // cPed::HandlePreDead
    g.stats.incPeopleKilled();
    if (subtype == 1) g.stats.incCriminalsKilled();
    // IncGangKills for ped types whose cPedInfoManager info has the gang flag (+0x25): not mapped yet
}

void Stats_PlayerDamage(Game& g, int healthAfter, int type, int effect) {   // cPed::Damage
    if (healthAfter == 0 && (type == 0 || type == 11 || type == 12)) g.stats.incMeleeKills();
    // cSimpleMover::UpdateWeaponKills
    if ((healthAfter == 0 || type == 3) && effect != 0x1D && effect >= 0 && effect < 24) g.stats.incWeaponKills(effect);
}
