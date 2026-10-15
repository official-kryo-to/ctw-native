// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// Stats: a port of cStatsManager (gStatsManager) and the StatsImpl forwarders.
//
// The counters live in the save game (cSaveGameMgr::GetStatsSectionOfSaveGame), laid out as StatsData below: plain
// 32-bit and 16-bit counters, then bit-packed counters in 64-bit words that wrap at their field width (12 bits for
// most, 8 bits for some). The manager itself holds the current playing time (+0), the stats section (+8), the
// highest wanted level this hour (+0x10) and an enabled flag (+0x14).
//
// The Calc* routines build the pause menu's stats pages; the ones that read other save-game sections (medals, unique
// jumps, cameras, safehouses, dealers, items, random encounters, race times, tattoos) take that section as SaveProgress.
// CalcCriminalRating and CalcMediaValue return e_main.gxt text ids.
//
// Where the port triggers them (the callers in brackets):
//   people killed / criminals killed / fishes fed (cPed::HandlePreDead), melee kills (cPed::Damage), weapon kills
//   (cSimpleMover::UpdateWeaponKills), bullets that hit (cPed::Damage, cVehicle::Damage), cars and bikes / boats
//   destroyed (cVehicle::HandlePreDeathEvent), explosions (Explosion::AddExplosion), playing time, hours and days
//   (cTimeCycle::Process), distances and the longest time in a vehicle (cPlayer::Process), times wasted
//   (cDeathArrestWatcher::PlayerWasted), weapons picked up (cPlayer::GiveWeapon).
// Not triggered yet (their systems are not ported): gang kills (cPedInfoManager's in-memory gang flag), the wanted
// level counters, trading, missions, pickups, radio time, stunt records, cheats, saves and the money counters.
#pragma once
#include <cstddef>
#include <cstdint>

class Game;

struct StatsData {                     // the save game's stats section (0xFC bytes used)
    int32_t criminalRating = 0;        // +0x00 (CalcCriminalRating)
    uint32_t totalPlayingTime = 0;     // +0x04 in game minutes
    uint32_t unknown08 = 0;            // +0x08
    uint32_t unknown0C = 0;            // +0x0C (the top criminal rating needs it set)
    uint32_t mediaValue = 0;           // +0x10 the sum of each hour's highest wanted level
    uint32_t distance[5] = {};         // +0x14 on foot, swum, by car, by bike, by boat (1/16 units)
    uint32_t spentOnWeapons = 0, spentOnPayNSpray = 0, spentOnProperty = 0, lostOnStreetRaces = 0;   // +0x28
    uint32_t spentOnBribingCops = 0, spentOnHealthCare = 0, madeFromActivities = 0, madeFromTaxiTips = 0;   // +0x38
    uint32_t foundOnTheStreet = 0, valueOfGoodsSold = 0, valueOfGoodsBought = 0;   // +0x48
    uint32_t bulletsFired = 0, bulletsThatHit = 0, longestTimeInVehicle = 0;       // +0x54 (the time in seconds)
    int32_t madeFromTrading = 0, spentOnScratchCards = 0, madeOnScratchCards = 0;  // +0x60
    uint16_t lionsCollected = 0, cockroachesSquashed = 0, peopleKilled = 0, peopleBurntOut = 0;   // +0x6C
    uint16_t nearMisses = 0, vehiclesStolen = 0, wantedStarsAttained = 0, wantedStarsEvaded = 0;   // +0x74
    uint16_t criminalsKilled = 0, longestPoliceChase = 0, longestChaseWith6Stars = 0;   // +0x7C
    uint16_t unitsOfGoodsSold = 0, unitsOfGoodsBought = 0, carsAndBikesDestroyed = 0, boatsDestroyed = 0;   // +0x82
    uint16_t meleeKills = 0, gangKills = 0;   // +0x8A
    uint16_t weaponKills[24] = {};            // +0x8E by eWeaponType
    uint16_t busted = 0;                      // +0xBE low 12 bits
    // +0xC0: wasted 0-11, unique commodities 12-19, missions failed 20-31, fishes fed 32-43, missions passed 44-51,
    // days passed 52-63
    uint64_t wordC0 = 0;
    // +0xC8: times cheated 0-11, shipments robbed 12-19, times saved 20-31, longest wheelie 32-43, warehouse raids
    // 44-51, longest stoppie 52-63
    uint64_t wordC8 = 0;
    // +0xD0: longest two-wheel distance 0-11, most flips 12-19, farthest jump 20-31, highest jump 32-43, most spin
    // 44-51, explosions 52-63
    uint64_t wordD0 = 0;
    uint8_t wordD8[3] = {};                   // +0xD8 (24 bits): weapons picked up 0-11, mission retries 12-23
    uint8_t padDB = 0;
    uint32_t tradesDone = 0;                  // +0xDC low 20 bits
    uint32_t drugsConfiscated = 0;            // +0xE0
    uint16_t radioTime[12] = {};              // +0xE4 by radio slot (0: off)
};
static_assert(offsetof(StatsData, peopleKilled) == 0x70 && offsetof(StatsData, weaponKills) == 0x8E &&
              offsetof(StatsData, wordC0) == 0xC0 && offsetof(StatsData, tradesDone) == 0xDC &&
              offsetof(StatsData, radioTime) == 0xE4, "the save game's layout");

// The save-game sections the Calc* routines read (SaveGameImpl and the script section)
struct SaveProgress {
    uint8_t ambientMedal[0x55] = {};   // SaveGameImpl::GetAmbientMissionMedal: 0 none, 1 bronze, 2 silver, 3 gold
    bool uniqueJump[30] = {};          // USJCompleted
    bool package[100] = {};            // PackageCollected (the security cameras)
    bool safehouse[21] = {};           // SafehouseOwned
    bool dealer[0x51] = {};            // DealerFound
    bool item[6] = {};                 // ItemOwned
    uint8_t encounter[8] = {};         // the script section +0x7A5: 0x80 = not found
    uint32_t raceBestTime[16] = {};    // OJRaceBestTime (race ids from 0x4C)
    uint32_t tattooBestScore = 0;      // TattooBestScore
};

class Stats {
public:
    Stats() { reset(); }
    void reset();                                       // cStatsManager::cStatsManager
    StatsData& data() { return data_; }                 // GetStatsData
    const StatsData& data() const { return data_; }

    void updateTime(int minutes);                       // UpdateTime (also cPlayer::UpdateChaseTime: no police yet)
    void addTotalPlayingTime(uint32_t minutes) { data_.totalPlayingTime += minutes; }
    void addCurrentPlayingTime(uint32_t minutes) { currentPlayingTime_ += minutes; }
    uint32_t currentPlayingTime() const { return currentPlayingTime_; }

    void addDistanceTravelledOnFoot(uint32_t d) { data_.distance[0] += d; }
    void addDistanceSwam(uint32_t d) { data_.distance[1] += d; }
    void addDistanceTravelledByCar(uint32_t d) { data_.distance[2] += d; }
    void addDistanceTravelledByBike(uint32_t d) { data_.distance[3] += d; }
    void addDistanceTravelledByBoat(uint32_t d) { data_.distance[4] += d; }
    void addSpentOnWeapons(uint32_t v) { data_.spentOnWeapons += v; }
    void addSpentOnPayNSpray(uint32_t v) { data_.spentOnPayNSpray += v; }
    void addSpentOnProperty(uint32_t v) { data_.spentOnProperty += v; }
    void addLostOnStreetRaces(uint32_t v) { data_.lostOnStreetRaces += v; }
    void addSpentOnBribingCops(uint32_t v) { data_.spentOnBribingCops += v; }
    void addSpentOnHealthCare(uint32_t v) { data_.spentOnHealthCare += v; }
    void addMadeFromActivities(uint32_t v) { data_.madeFromActivities += v; }
    void addMadeFromTaxiTips(uint32_t v) { data_.madeFromTaxiTips += v; }
    void addFoundOnTheStreet(uint32_t v) { data_.foundOnTheStreet += v; }
    void addValueOfGoodsSold(uint32_t v) { data_.valueOfGoodsSold += v; }
    void addValueOfGoodsBought(uint32_t v) { data_.valueOfGoodsBought += v; }
    void incBulletsFired() { ++data_.bulletsFired; }
    void incBulletsThatHit() { ++data_.bulletsThatHit; }
    void maxLongestTimeSpentInAVehicle(uint32_t seconds);
    void incPeopleKilled() { ++data_.peopleKilled; }
    void incPeopleBurntOut() { ++data_.peopleBurntOut; }
    void incNearMisses() { ++data_.nearMisses; }
    void incLionsCollected() { ++data_.lionsCollected; }
    void incCockroachesSquashed() { ++data_.cockroachesSquashed; }
    void incVehiclesStolen() { ++data_.vehiclesStolen; }
    void addNumberOfWantedStarsAttained(uint32_t n) { data_.wantedStarsAttained = (uint16_t)(data_.wantedStarsAttained + n); }
    void addNumberOfWantedStarsEvaded(uint32_t n) { data_.wantedStarsEvaded = (uint16_t)(data_.wantedStarsEvaded + n); }
    void incCriminalsKilled() { ++data_.criminalsKilled; }
    void maxLongestPoliceChase(uint32_t t);
    void maxLongestChaseWithA6StaRating(uint32_t t);
    void addUnitsOfGoodsSold(uint32_t n) { data_.unitsOfGoodsSold = (uint16_t)(data_.unitsOfGoodsSold + n); }
    void addUnitsOfGoodsBought(uint32_t n) { data_.unitsOfGoodsBought = (uint16_t)(data_.unitsOfGoodsBought + n); }
    void incNumberOfCarsAndBikesDestroyed() { ++data_.carsAndBikesDestroyed; }
    void incNumberOfBoatsDestroyed() { ++data_.boatsDestroyed; }
    void incMeleeKills() { ++data_.meleeKills; }
    void incGangKills() { ++data_.gangKills; }
    void incWeaponKills(int weaponType);
    void incNumberOfTimesBusted();
    void incNumberOfTimesWasted();
    void incUniqueCommoditiesOwned();
    void incMissionsFailed();
    void incFishesFed();
    void incMissionsPassed();
    void incDaysPassedInGame();
    void incTimesCheated();
    void incShipmentsRobbed();
    void incTradesDone();
    void addMadeFromTrading(int32_t v) { data_.madeFromTrading += v; }
    void addSpentOnScratchCards(int32_t v) { data_.spentOnScratchCards += v; }
    void addMadeOnScratchCards(int32_t v) { data_.madeOnScratchCards += v; }
    void maxLongestWheelieDistance(uint32_t d);
    void incWarehouseRaidsPulledOff();
    uint32_t warehouseRaidsPulledOff() const;            // GetWarehouseRaidsPulledOff
    void maxLongestStoppieDistance(uint32_t d);
    void maxLongest2WheelDistance(uint32_t d);
    void maxMostFlipsInOneJump(uint32_t n);
    void maxFarthestJumpDistance(uint32_t d);
    void maxHighestJumpReached(uint32_t h);
    void maxMostSpinInOneJump(uint32_t n);
    void incExplosions();
    void incWeaponsPickedUp();
    void incNumberOfTimesSaved();
    void incMissionRetries();
    void addRadioTime(unsigned station, uint32_t t);
    void forceRadioTime(unsigned station, uint16_t t) { data_.radioTime[station] = t; }
    // ProccessDistance: the player's velocity (units a second) this frame, by vehicle type (-1 on foot: 0 car,
    // 1 bike, 2 boat; other types count nothing) or swimming
    void processDistance(const int32_t vel[3], int vehicleType, bool swimming);
    // ProcessWantedChange (cMiniHud::UpdateWantedStars): the new and the previous wanted level
    void processWantedChange(uint32_t level, uint32_t previous);
    void processEndOfHour(uint32_t wantedLevel);         // ProcessEndOfHour (the player's current wanted level)

    int calcCriminalRating();                            // CalcCriminalRating: also stores the rating
    static int calcGoldMedals(const SaveProgress& s);    // CalcGoldMedals
    static int calcSilverMedals(const SaveProgress& s);  // CalcSilverMedals
    static int calcBronzeMedals(const SaveProgress& s);  // CalcBronzeMedals
    uint16_t calcTotalGangMembersWasted() const { return data_.gangKills; }
    int calcTotalDistanceTravelled() const;              // CalcTotalDistanceTravelled
    static int calcUSJsCompleted(const SaveProgress& s);
    static int calcCamsDestroyed(const SaveProgress& s);
    static int calcRandomEncountersFound(const SaveProgress& s);
    static int calcSafehousesOwned(const SaveProgress& s);
    int calcMostFavRadioStation() const;                 // CalcMostFavRadioStation: a station's text id
    int calcLeastFavRadioStation() const;                // CalcLeastFavRadioStation
    uint32_t calcAverageValueOfGoodsSold() const;
    uint32_t calcAverageValueOfGoodsBought() const;
    static int calculateNumDealersFound(const SaveProgress& s);
    static int calculateUniqueCommoditiesOwned(const SaveProgress& s);
    int32_t calcAccuracy() const;                        // CalcAccuracy: percent, Q12
    int calcFavouriteWeapon() const;                     // CalcFavororiteWeapon: the weapon type (its name: +0x28)
    // CalcMediaValue; actualLevel: the player's cWantedRating +0xC
    int calcMediaValue(int actualLevel) const;
    static void logRaceTime(SaveProgress& s, int race, uint32_t time);   // LogRaceTime
    static uint32_t bestRaceTime(const SaveProgress& s, int race);       // GetBestRaceTime
    static uint32_t tattoosDone(const SaveProgress& s) { return s.tattooBestScore; }   // GetTattoosDone
    static int medalAwarded(const SaveProgress& s, int mission) { return s.ambientMedal[mission]; }   // GetMedalAwarded
    uint32_t totalValueDrugsConfiscated() const { return data_.drugsConfiscated; }
    void addTotalValueDrugsConfiscated(uint32_t v) { data_.drugsConfiscated += v; }
    uint16_t lionsCollected() const { return data_.lionsCollected; }
    uint16_t cockroachesSquashed() const { return data_.cockroachesSquashed; }
    uint32_t daysPassed() const { return (uint32_t)(data_.wordC0 >> 52); }   // GetDaysPassed

    // the bit-packed counters
    uint32_t timesBusted() const { return data_.busted & 0xFFF; }
    uint32_t timesWasted() const { return (uint32_t)(data_.wordC0 & 0xFFF); }
    uint32_t missionsFailed() const { return (uint32_t)(data_.wordC0 >> 20 & 0xFFF); }
    uint32_t fishesFed() const { return (uint32_t)(data_.wordC0 >> 32 & 0xFFF); }
    uint32_t missionsPassed() const { return (uint32_t)(data_.wordC0 >> 44 & 0xFF); }
    uint32_t timesCheated() const { return (uint32_t)(data_.wordC8 & 0xFFF); }
    uint32_t timesSaved() const { return (uint32_t)(data_.wordC8 >> 20 & 0xFFF); }
    uint32_t explosions() const { return (uint32_t)(data_.wordD0 >> 52); }
    uint32_t weaponsPickedUp() const { return wordD8() & 0xFFF; }
    uint32_t missionRetries() const { return wordD8() >> 12; }
    uint32_t vehicleFrames = 0;   // cPlayer +0x1138: the frames the player has spent in the current vehicle

private:
    uint32_t wordD8() const { return data_.wordD8[0] | data_.wordD8[1] << 8 | (uint32_t)data_.wordD8[2] << 16; }
    void setWordD8(uint32_t v);
    uint32_t currentPlayingTime_ = 0;   // +0
    StatsData data_;                    // +8 (the save game's section)
    uint32_t hourWanted_ = 0;           // +0x10: the highest wanted level this hour
    bool enabled_ = true;               // +0x14
};

// cPlayer::Process's stats: ProccessDistance with the player's velocity (the vehicle's while driving, by its type)
// and MaxLongestTimeSpentInAVehicle with the frames spent in it (30 a second)
void Stats_PlayerProcess(Game& g);
// cPed::HandlePreDead for a ped killed by the player or the player's vehicle: people killed, and criminals killed for
// subtype 1
void Stats_PedKilledByPlayer(Game& g, int subtype);
// cPed::Damage's tail for damage the player did: melee kills (types 0, 11 and 12 that killed) and
// cSimpleMover::UpdateWeaponKills (a kill, or any taser hit (type 3), by the weapon in effect; not burning, 0x1D)
void Stats_PlayerDamage(Game& g, int healthAfter, int type, int effect);
