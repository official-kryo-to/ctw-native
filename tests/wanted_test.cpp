// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// cWantedRating / cWantedManager with a synthetic wanted_tables.bin (made-up values, not the game's).
#include "wanted.h"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

static int failures;
static void check(bool ok, const char* message) { if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); ++failures; } }

static void writeTables(const std::filesystem::path& file) {
    std::ofstream out(file, std::ios::binary);
    out.write("CTWWANT1", 8);
    const uint16_t thresholds[8] = {0, 40, 150, 500, 1000, 2000, 4000, 8000};
    for (uint16_t t : thresholds) { const uint8_t chances[2] = {0, 0}; out.write((const char*)&t, 2); out.write((const char*)chances, 2); }
    uint16_t crimes[26]{};
    crimes[1] = 30;              // a plain crime
    crimes[5] = 10 | 0xC000;     // straight to three stars
    crimes[6] = 5 | 0x4000;      // class 1: bit 6 is clear in 0x3FFFABF, so two stars
    crimes[11] = 1;              // a tiny crime
    out.write((const char*)crimes, sizeof crimes);
    int32_t carCrimes[13]{};
    out.write((const char*)carCrimes, sizeof carCrimes);
    const int32_t takeDowns[7] = {0, 1, 1, 2, 2, 3, 3}, subtract[7] = {0, 10, 20, 30, 40, 50, 60}, zero[7]{};
    out.write((const char*)takeDowns, sizeof takeDowns);
    out.write((const char*)subtract, sizeof subtract);
    for (int i = 0; i < 10; ++i) out.write((const char*)zero, sizeof zero);
    const int32_t failed = 100;
    out.write((const char*)&failed, 4);
}

int main() {
    const auto dir = std::filesystem::temp_directory_path() / "ctw-wanted-synthetic";
    std::filesystem::create_directories(dir);
    writeTables(dir / "wanted_tables.bin");
    WantedManager w;
    check(w.init(dir.string()), "the synthetic wanted table loads");
    const int32_t here[3] = {0, 0, 0};
    uint32_t frame = 100;   // past the first frames, when no cop has been in view yet

    w.registerCrime(1, frame);
    w.process(frame++, here, false);
    check(w.rating() == 30 && w.level() == 0, "points below the first threshold give no star");
    w.registerCrime(1, frame);
    w.process(frame++, here, false);
    check(w.level() == 1 && w.starsLit() == 1, "the rating crossing a threshold lights a star");

    // one lit star decays while no cop sees the player; losing it starts the no-crime probation
    for (int i = 0; i < 2000 && w.actualLevel() == 1 && !w.onProbation(); ++i) w.process(frame++, here, false);
    check(w.onProbation() && w.level() == 0 && w.actualLevel() == 1 && w.starsFlashing(),
          "a lost star stays dimmed on probation");
    w.registerCrime(11, frame);
    check(!w.onProbation() && w.level() == 1, "a crime on probation brings the star back");
    for (int i = 0; i < 2000 && (w.actualLevel() || w.onProbation()); ++i) w.process(frame++, here, false);
    check(w.actualLevel() == 0 && !w.onProbation() && w.rating() == 0, "a crime-free probation clears the level");

    // instant-level classes
    WantedManager three;
    three.init(dir.string());
    three.registerCrime(5, frame);
    three.process(frame, here, false);
    check(three.level() == 3, "class-3 crimes jump straight to three stars");
    WantedManager two;
    two.init(dir.string());
    two.registerCrime(6, frame);
    two.process(frame, here, false);
    check(two.level() == 2, "class-1 crimes outside 0x3FFFABF jump to two stars");

    // two or more lit stars never decay; taking cops down dims them from the top
    const uint32_t before = three.rating();
    for (int i = 0; i < 500; ++i) three.process(frame + i, here, false);
    check(three.rating() == before && three.level() == 3, "two or more lit stars do not decay");
    three.onTakeDown();
    check(three.starsLit() == 3, "the third star needs two takedowns");
    three.onTakeDown();
    check(three.starsLit() == 2 && three.actualLevel() == 3, "two takedowns dim the top star");

    // the maximum level: just below the next threshold
    WantedManager capped;
    capped.init(dir.string());
    capped.setMaxLevel(2);
    capped.registerCrime(5, frame);
    capped.process(frame, here, false);
    check(capped.actualLevel() == 2 && capped.rating() == 500 - 20, "the maximum level holds the rating below the next star");

    std::filesystem::remove_all(dir);
    return failures ? 1 : 0;
}
