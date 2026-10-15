// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// cStatsManager arithmetic: the bit-packed counters, the criminal rating and media ladders and the Calc* helpers.
#include "stats.h"
#include <cstdio>
#include <cstdlib>

static int failures;
static void check(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); ++failures; }
}

int main() {
    Stats s;
    s.incMissionsFailed();
    for (int i = 0; i < 4097; ++i) s.incNumberOfTimesWasted();
    check(s.timesWasted() == 1 && s.missionsFailed() == 1, "wasted wraps at 12 bits without touching its neighbours");
    for (int i = 0; i < 256; ++i) s.incMissionsPassed();
    s.incDaysPassedInGame();
    check(s.missionsPassed() == 0 && s.daysPassed() == 1, "missions passed wraps at 8 bits");
    s.incWeaponsPickedUp();
    for (int i = 0; i < 4096; ++i) s.incMissionRetries();
    check(s.missionRetries() == 0 && s.weaponsPickedUp() == 1, "the 24-bit word keeps its two counters apart");
    s.incExplosions();
    check(s.explosions() == 1, "explosions count in the top bits of +0xD0");

    s.maxLongestWheelieDistance(5000);
    check((s.data().wordC8 >> 32 & 0xFFF) == 0xFFF, "a wheelie record saturates at 0xFFF");
    s.maxLongestStoppieDistance(0x1234);
    check((s.data().wordC8 >> 52) == 0x234, "a stoppie record keeps only its 12 bits");
    s.maxMostFlipsInOneJump(3);
    s.maxMostFlipsInOneJump(2);
    check((s.data().wordD0 >> 12 & 0xFF) == 3, "records keep the larger value");

    {   // CalcCriminalRating
        Stats r;
        r.data().peopleKilled = 10;
        r.data().carsAndBikesDestroyed = 2;
        check(r.calcCriminalRating() == 0x5D4 && r.data().criminalRating == 70, "10 kills and 2 cars rate 70");
        r.data().peopleKilled = 1200;
        r.data().carsAndBikesDestroyed = 0;
        check(r.calcCriminalRating() == 0x5E9, "1200 without arrests");
        r.incNumberOfTimesBusted();
        check(r.calcCriminalRating() == 0x5EA && r.data().criminalRating == 1197, "an arrest costs 3 and changes the rank");
        Stats a;
        a.data().bulletsFired = 101;
        a.data().bulletsThatHit = 101;
        check(a.calcCriminalRating() == 0x5E0 + 1 && a.data().criminalRating == 500, "perfect aim over 100 shots adds 500");
        Stats c;
        c.incTimesCheated();
        check(c.calcCriminalRating() == 0x614 && c.data().criminalRating == -1000, "a cheat costs 1000");
        Stats n;
        n.incNumberOfTimesWasted();
        check(n.calcCriminalRating() == 0x5D2 && n.data().criminalRating == 0, "without cheats the rating stays at 0");
        Stats top;
        top.data().peopleKilled = 0xFFFF;
        top.data().madeFromTrading = 600000;
        check(top.calcCriminalRating() == 0x604, "the top rank");
        top.data().unknown0C = 1;
        check(top.calcCriminalRating() == 0x605, "the top rank with +0xC set");
    }

    {   // CalcMediaValue
        Stats m;
        check(m.calcMediaValue(0) == 0x553, "no wanted hours");
        m.data().mediaValue = 105;
        check(m.calcMediaValue(4) == 0x55C && m.calcMediaValue(5) == 0x55D, "past 100 the wanted level gates the rank");
        m.data().mediaValue = 165;
        check(m.calcMediaValue(5) == 0x562 && m.calcMediaValue(6) == 0x563, "level 5 stops at 0x562");
        m.data().mediaValue = 200;
        check(m.calcMediaValue(6) == 0x567, "the top media rank");
    }

    {   // the radio slots: 0 is no station
        Stats r;
        check(r.calcMostFavRadioStation() == 0xE0, "no radio time: slot 0");
        r.data().radioTime[3] = 5;
        check(r.calcMostFavRadioStation() == 227, "slot 3 is the third station");
        for (int i = 1; i < 12; ++i) r.data().radioTime[i] = 10;
        r.data().radioTime[0] = 300;
        r.data().radioTime[5] = 2;
        check(r.calcLeastFavRadioStation() == 231, "the least played slot");
        r.addRadioTime(2, 7);
        check(r.data().radioTime[2] == 17, "radio time adds up");
    }

    {   // distances, the wanted level, time
        Stats d;
        const int32_t vel[3] = {0x3000, 0x4000, 0};
        d.processDistance(vel, 0, false);
        d.processDistance(vel, 3, false);
        d.processDistance(vel, -1, true);
        check(d.data().distance[2] == 2 && d.data().distance[1] == 2 && d.calcTotalDistanceTravelled() == 4,
              "5 units a second add 2 sixteenths by car and swimming; other vehicle types count nothing");
        d.processWantedChange(2, 0);
        d.processWantedChange(0, 2);
        check(d.data().wantedStarsAttained == 2 && d.data().wantedStarsEvaded == 2, "stars attained and evaded");
        d.processEndOfHour(1);
        d.processEndOfHour(1);
        check(d.data().mediaValue == 3, "each hour adds its highest wanted level");
        d.updateTime(5);
        check(d.data().totalPlayingTime == 5 && d.currentPlayingTime() == 5, "playing time");
    }

    {   // the averages, accuracy, favourite weapon, race times
        Stats t;
        t.addValueOfGoodsSold(1000);
        t.addUnitsOfGoodsSold(3);
        check(t.calcAverageValueOfGoodsSold() == 333 && t.calcAverageValueOfGoodsBought() == 0, "average sale value");
        t.data().bulletsFired = 4;
        t.data().bulletsThatHit = 3;
        check(t.calcAccuracy() == 75 << 12, "accuracy in percent, Q12");
        t.incWeaponKills(9);
        for (int i = 0; i < 3; ++i) { t.incWeaponKills(5); t.incWeaponKills(9); }
        t.data().weaponKills[9] = 3;
        check(t.calcFavouriteWeapon() == 5, "ties keep the first weapon");
        SaveProgress p;
        Stats::logRaceTime(p, 0x4C, 100);
        Stats::logRaceTime(p, 0x4C, 120);
        check(Stats::bestRaceTime(p, 0x4C) == 100, "a slower race time is not logged");
        Stats::logRaceTime(p, 0x4C, 90);
        check(Stats::bestRaceTime(p, 0x4C) == 90, "a faster one is");
        p.ambientMedal[0] = 3; p.ambientMedal[1] = 3; p.ambientMedal[2] = 1;
        p.encounter[0] = 0x80; p.encounter[1] = 0x80;
        check(Stats::calcGoldMedals(p) == 2 && Stats::calcBronzeMedals(p) == 1 && Stats::calcSilverMedals(p) == 0,
              "medals by kind");
        check(Stats::calcRandomEncountersFound(p) == 6, "encounters not 0x80 count");
    }
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
