// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "radar.h"
#include <cmath>
#include <cstdio>
#include <limits>

static int failures;
static void check(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); ++failures; }
}
int main() {
    // Synthetic grid, deliberately unlike the game's table; resources remain local to the user's APK.
    const uint16_t words[] = {31, 47, 2, 2, 7, 8, 9, 100, 101, 0xffff, 103};
    const auto* bytes = reinterpret_cast<const uint8_t*>(words);
    std::vector<uint8_t> data(bytes, bytes + sizeof words);
    RadarMap map;
    check(map.load(data), "synthetic map loads");
    check(map.originX() == 31 && map.originY() == 47 && map.columns() == 2 && map.rows() == 2,
          "origin and rectangular grid dimensions come from the resource header");
    check(map.texture(0, 0) == 100 && map.texture(1, 0) == 101 && map.texture(1, 1) == 103,
          "row-major tiles start after the three overview resource IDs");
    check(map.defaultTexture() == 9 && map.texture(0, 1) == -1, "missing tiles use a separate default texture");
    check(map.texture(-1, 0) == -1 && map.texture(2, 0) == -1 && map.texture(0, -1) == -1 && map.texture(0, 2) == -1,
          "off-map coordinates cannot address an adjacent row");
    data.resize(sizeof words - 1);
    check(!map.load(data) && !map.columns() && map.texture(0, 0) == -1 && map.defaultTexture() == -1,
          "truncated reload clears the old map and cannot read stale IDs");
    data.assign(14, 0);
    check(!map.load(data), "zero-sized grid is rejected");
    data[4] = data[5] = data[6] = data[7] = 255;
    check(!map.load(data), "overflow-sized grid is rejected before allocating tiles");
    float x, y;
    int32_t pos[3] = {-3540 * 4096, 2520 * 4096, 0};
    RadarMap::worldToMap(pos, x, y);
    check(x == 0 && y == 0, "the native map origin maps to zero");
    pos[0] += 10 * 4096; pos[1] -= 10 * 4096;
    RadarMap::worldToMap(pos, x, y);
    check(x == 3 && y == 3, "native 0x666 scale truncates the ten-unit offset below four pixels");
    pos[0] = -3540 * 4096 - 1; pos[1] = 2520 * 4096 + 1;
    RadarMap::worldToMap(pos, x, y);
    check(x == -1 && y == -1, "negative fractions use native arithmetic rounding at the map edge");
    pos[0] = std::numeric_limits<int32_t>::max(); pos[1] = std::numeric_limits<int32_t>::min();
    RadarMap::worldToMap(pos, x, y);
    check(x > 0 && y > 0, "coordinate conversion widens before adding the origin and multiplying");
    const int32_t focus[3] = {123 * 4096, -456 * 4096, 0};
    int32_t marker[3] = {focus[0], focus[1] + 200 * 4096, 0};
    RadarProjection northUp;
    northUp.worldOffset(marker, focus, x, y);
    check(x == 0 && std::fabs(y + 79.98047f) < .001f,
          "marker projection retains native Q12 fractions instead of truncating map pixels");
    northUp.worldOffset(marker, focus, x, y, 75.f);
    check(x == 0 && std::fabs(y + 75.f) < .001f, "north marker clamps to the normal radar edge");
    const RadarProjection eastCamera(0x4000);
    eastCamera.worldOffset(marker, focus, x, y, 75.f);
    check(std::fabs(x + 75.f) < .001f && std::fabs(y) < .001f,
          "quarter-turn camera rotation moves north to the left edge");
    eastCamera.mapOffset(0.f, -75.f, x, y);
    check(std::fabs(x + 75.f) < .001f && std::fabs(y) < .001f,
          "tile vertices and world markers share the clockwise camera convention");
    eastCamera.mapOffset(75.f, 0.f, x, y);
    check(std::fabs(x) < .001f && std::fabs(y + 75.f) < .001f,
          "east moves upwards when the camera faces east");
    marker[0] += 200 * 4096;
    const RadarProjection diagonalCamera(0x2000);
    diagonalCamera.worldOffset(marker, focus, x, y, 75.f);
    check(std::fabs(x) < .01f && std::fabs(y + 75.f) < .01f,
          "diagonal markers clamp by circular distance after rotation");
    marker[0] = focus[0]; marker[1] = focus[1] + 10 * 4096;
    northUp.worldOffset(marker, focus, x, y, 75.f);
    check(x == 0 && y < -3.99f && y > -4.f, "nearby markers remain inside the circle without stretching");
    northUp.worldOffset(focus, focus, x, y, 75.f);
    check(x == 0 && y == 0, "the focus marker stays centred without dividing by zero");
    return failures ? 1 : 0;
}
