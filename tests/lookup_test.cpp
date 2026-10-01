// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "lookups.h"
#include "gfx/rendertables.h"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <vector>

static int failures = 0;
static void check(bool ok, const char* description) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", description); ++failures; }
}
static void append(std::vector<char>& bytes, const void* data, size_t size) {
    const char* first = static_cast<const char*>(data);
    bytes.insert(bytes.end(), first, first + size);
}
static void save(const std::filesystem::path& path, const std::vector<char>& data) {
    std::ofstream file(path, std::ios::binary);
    file.write(data.data(), data.size());
}
int main() {
    // Synthetic fixtures use arbitrary values, never the game's lookup data.
    const auto path = std::filesystem::current_path() / "lookup-test.tmp";
    std::vector<char> bytes;
    append(bytes, "CTWZONE1", 8);
    const uint32_t counts[2] = {1, 1};
    append(bytes, counts, sizeof counts);
    ZoneSetup zone{};
    zone.name[0] = 'Z'; zone.vehMakeup = 2; zone.carDensity = 3;
    append(bytes, &zone, sizeof zone);
    const int32_t profile[12] = {7};
    append(bytes, profile, sizeof profile);
    save(path, bytes);
    PopulationTables population;
    check(population.load(path.string()) && population.zones.size() == 1 &&
          population.profiles[0][0] == 7, "population fixture decodes");
    bytes[16 + 8] = 2;  // night selector would index outside the day/night array
    save(path, bytes);
    check(!population.load(path.string()) && population.zones.empty(), "invalid population selector rejected");
    bytes[16 + 8] = 0;
    bytes.pop_back();
    save(path, bytes);
    check(!population.load(path.string()), "truncated population rejected");

    SoundTables fixture;
    for (EventInfo& event : fixture.events) { event.mode = 2; event.pan = -1; }
    fixture.gears[1].horn = 9;
    bytes.clear();
    append(bytes, "CTWSND1", 8);
    append(bytes, fixture.events, sizeof fixture.events);
    append(bytes, fixture.gears, sizeof fixture.gears);
    append(bytes, fixture.collisionLow, sizeof fixture.collisionLow);
    append(bytes, fixture.collisionMed, sizeof fixture.collisionMed);
    append(bytes, fixture.collisionHigh, sizeof fixture.collisionHigh);
    append(bytes, fixture.revAfterShift, sizeof fixture.revAfterShift);
    save(path, bytes);
    SoundTables sound;
    check(sound.load(path.string()) && sound.gears[1].horn == 9 && !sound.hasPropSfx, "legacy sound fixture decodes without inventing prop samples");
    bytes.push_back(0);
    save(path, bytes);
    check(!sound.load(path.string()) && sound.gears[1].horn == 0, "extra sound data rejected and state cleared");
    bytes.pop_back();
    bytes[8 + 4] = 0;
    save(path, bytes);
    check(!sound.load(path.string()), "invalid event mode rejected");
    bytes[8 + 4] = 2;
    bytes[6] = '2';
    fixture.propSfx[4] = {17, 91, 0};
    append(bytes, fixture.propSfx, sizeof fixture.propSfx);
    save(path, bytes);
    check(sound.load(path.string()) && sound.hasPropSfx && sound.propSfx[4].sample == 17 && sound.propSfx[4].volume == 91,
          "version 2 sound fixture retains the object's sample and volume mapping");
    bytes.pop_back(); save(path, bytes);
    check(!sound.load(path.string()) && !sound.hasPropSfx, "truncated prop mapping rejected and state cleared");
    bytes.push_back(0); bytes[bytes.size() - sizeof fixture.propSfx + 2] = 128; save(path, bytes);
    check(!sound.load(path.string()), "invalid prop volume rejected");

    GameplayTables gameFixture;
    for (int32_t& top : gameFixture.topRatio) top = 1;
    gameFixture.gearRatio[2] = 1LL << 32;
    bytes.clear();
    append(bytes, "CTWGAME1", 8);
    append(bytes, gameFixture.gearRatio, sizeof gameFixture.gearRatio);
    append(bytes, gameFixture.topRatio, sizeof gameFixture.topRatio);
    append(bytes, gameFixture.laneRow, sizeof gameFixture.laneRow);
    append(bytes, gameFixture.laneCol, sizeof gameFixture.laneCol);
    append(bytes, gameFixture.laneFlag5, sizeof gameFixture.laneFlag5);
    append(bytes, gameFixture.lanePlain, sizeof gameFixture.lanePlain);
    append(bytes, gameFixture.doorMax, sizeof gameFixture.doorMax);
    append(bytes, gameFixture.spawnOffset, sizeof gameFixture.spawnOffset);
    save(path, bytes);
    GameplayTables game;
    check(game.load(path.string()) && game.gearRatio[2] == (1LL << 32), "64-bit gear table decodes");
    bytes[8 + sizeof gameFixture.gearRatio] = 0;
    save(path, bytes);
    check(!game.load(path.string()), "invalid top ratio rejected");

    RenderTables renderFixture;
    renderFixture.renderList[0] = 3;
    renderFixture.layerSlot[0][0] = 5;
    renderFixture.textColour[0][0] = 7;
    bytes.clear();
    append(bytes, "CTWREND1", 8);
    append(bytes, renderFixture.renderList, sizeof renderFixture.renderList);
    append(bytes, renderFixture.layerSlot, sizeof renderFixture.layerSlot);
    append(bytes, renderFixture.angle, sizeof renderFixture.angle);
    append(bytes, renderFixture.textColour, sizeof renderFixture.textColour);
    save(path, bytes);
    RenderTables render;
    check(render.load(path.string()) && render.renderList[0] == 3 && render.layerSlot[0][0] == 5 &&
          render.textColour[0][0] == 7, "render table sections decode at the correct offsets");
    bytes[8 + sizeof renderFixture.renderList] = 16;
    save(path, bytes);
    check(!render.load(path.string()), "out-of-range palette slot rejected");
    bytes.clear();
    append(bytes, "CTWRAD2", 8);
    append(bytes, counts, sizeof counts);
    const uint8_t volumeSprites[10] = {1,2,3,4,5,6,7,8,9,10};
    append(bytes, volumeSprites, sizeof volumeSprites);
    const RadioTables::Station station{3, 0, 6, 7};
    append(bytes, &station, sizeof station);
    char stream[40] = "synthetic.mp3";
    append(bytes, stream, sizeof stream);
    save(path, bytes);
    RadioTables radio;
    check(radio.load(path.string()) && radio.stations[0].icon == 3 && radio.streams[0] == "synthetic.mp3",
          "radio mappings and stream names decode");
    bytes[26 + 4] = 1;
    save(path, bytes);
    check(!radio.load(path.string()) && radio.stations.empty(), "invalid radio stream index rejected");
    bytes[26 + 4] = 0; bytes[42] = '/';
    save(path, bytes);
    check(!radio.load(path.string()), "radio paths cannot escape asset directory");
    bytes[42] = 's'; bytes.pop_back();
    save(path, bytes);
    check(!radio.load(path.string()), "truncated radio mappings rejected");
    std::filesystem::remove(path);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
