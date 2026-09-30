// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "player.h"
#include "followcam.h"
#include "gfx/pedsprites.h"
#include "world/timecycle.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

static int failures = 0;
static void check(bool ok, const char* description) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", description); ++failures; }
}

static void movement() {
    Player player;
    check(player.bodySet == 1 && player.palUpper == 4 && player.palLegs == 5, "player appearance");
    check(Player::kRadius == 5120 && Player::kSphere == 3583, "player collision radius");
    Player::Input input;
    input.moving = true;
    for (int frame = 0; frame < 30; ++frame) {
        player.update(input, nullptr, nullptr);
        int expected = frame < 2 ? 0 : frame < 11 ? 1 : 2;
        check(player.level() == expected, "acceleration timing without sprint");
    }
    check(player.currentSpeed() == 49140, "unarmed run speed");
    input.moving = false;
    for (int frame = 0; frame < 8; ++frame) player.update(input, nullptr, nullptr);
    check(player.level() == 0 && player.vel[0] == 0 && player.vel[1] == 0, "release comes to a stop");
    int32_t stoppedY = player.pos[1];
    player.update(input, nullptr, nullptr);
    check(player.pos[1] == stoppedY, "standing has no drift");

    Player sprint;
    input.moving = input.sprint = true;
    for (int frame = 0; frame < 21; ++frame) sprint.update(input, nullptr, nullptr);
    check(sprint.level() == 3 && sprint.currentSpeed() == 73710, "sprint reaches the third speed level");

    Player turning;
    input = Player::Input{};
    input.moving = true;
    input.heading = 0x4000;
    turning.update(input, nullptr, nullptr);
    check(turning.heading() > 3900 && turning.heading() <= 4000, "turning is limited per frame");
    for (int i = 0; i < 5; ++i) turning.update(input, nullptr, nullptr);
    check(std::abs((int)turning.heading() - 0x4000) < 5, "turn reaches requested heading");
    check(turning.vel[0] > 0 && std::abs(turning.vel[1]) < 20, "east heading moves east");
}

static void camera() {
    FollowPedCam cam;
    int32_t target[3] = {0, 0, 0}, still[3] = {0, 0, 0}, moving[3] = {49140, 0, 0};
    cam.reset(target, 0);
    cam.update(target, 0x4000, still, nullptr);
    check(cam.yaw == 0, "camera holds heading while stationary");
    cam.update(target, 0x4000, moving, nullptr);
    check(cam.yaw == 33, "camera turns gradually while moving");
    cam.reset(target, 32760);
    cam.update(target, -32760, moving, nullptr);
    check((int16_t)(cam.yaw - 32760) > 0 && (int16_t)(cam.yaw - 32760) < 33,
          "camera takes the short path across angle wrap");
    cam.reset(target, 0);
    cam.update(target, -32768, moving, nullptr);
    check((int16_t)cam.yaw == -1, "camera avoids snapping around at opposite heading");
    float p[3];
    cam.position(p);
    check(std::isfinite(p[0]) && std::isfinite(p[1]) && p[2] > 20, "camera position stays finite and above target");
}

static void lighting() {
    TimeCycle cycle;
    struct Sample { uint32_t time; bool on; float day; };
    const Sample samples[] = {{0, false, 0}, {0x7000, true, 0}, {0x7800, true, 0.5f},
                              {0x8000, true, 1}, {0x13000, true, 1}, {0x13800, true, 0.5f},
                              {0x14000, false, 0}, {0x17FFF, false, 0}};
    for (const Sample& s : samples) {
        cycle.setTime(s.time);
        PedLight light = PedLight::fromTimeCycle(cycle);
        check(light.on == s.on && light.day == s.day, "daylight fades at sunrise and sunset");
    }
    check(!PedLight::shadesLayer(0, 0) && !PedLight::shadesLayer(2, 0), "upper head layer stays unshaded");
    check(PedLight::shadesLayer(0, 1) && PedLight::shadesLayer(2, 1) &&
          PedLight::shadesLayer(1, 0) && PedLight::shadesLayer(3, 0), "clothing and leg layers receive shading");
    PedLight light;
    light.on = true;
    light.day = 1;
    light.dir[0] = 1; light.dir[2] = 0;
    light.ambient = 0xFF646464;
    float centre[3] = {0, 0, 0}, facing[3] = {1, 0, 0}, opposite[3] = {-1, 0, 0};
    uint8_t colour[4];
    light.cornerColour(facing, centre, 0x00C8C8C8, colour);
    check(colour[0] == 100 && colour[1] == 100 && colour[2] == 100 && colour[3] == 255,
          "light-facing corner reaches ambient colour");
    light.cornerColour(opposite, centre, 0x00C8C8C8, colour);
    check(colour[0] == 200, "opposite corner retains its palette colour");
    light.day = 0.5f;
    light.cornerColour(facing, centre, 0x00C8C8C8, colour);
    check(colour[0] == 150, "half daylight blends halfway");
}

static void collision() {
    Collision::Box box{0, 0, 0, 4096, 4096, 4096, 0, 0};
    int32_t a[3] = {-16384, 0, 0}, b[3] = {16384, 0, 0}, hit[3], fraction;
    check(Collision::sweptSphereVBox(a, b, 4096, box, hit, fraction), "sweep detects a wall crossed in one frame");
    check(std::abs(fraction - 1024) <= 1 && std::abs(hit[0] + 4096) <= 8, "wall contact occurs before penetration");
    a[1] = b[1] = 16384;
    check(!Collision::sweptSphereVBox(a, b, 4096, box, hit, fraction), "clear path beside wall stays clear");
}

int main() {
    movement();
    camera();
    lighting();
    collision();
    if (failures) return EXIT_FAILURE;
    std::puts("Movement, camera, daylight and collision checks passed (synthetic inputs, no game assets).");
    return EXIT_SUCCESS;
}
