// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#define SDL_MAIN_HANDLED
#include "game.h"
#include "hud.h"
#include "plugins.h"
#include "audio/audio.h"
#include "os/os.h"
#include <SDL.h>
#include <glad/gl.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

static int failures;
static void check(bool ok, const char* description) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", description); ++failures; }
}

struct PauseScreenTestAccess {
    static void run() {
        Game game;
        PauseScreen& pause = game.pauseScreen;
        // Arbitrary opaque red texture, with only the resume sprite's native geometry.
        // This tests the layout and hit regions without distributing game artwork.
        const uint32_t count = 28;
        std::vector<uint8_t> defs(4 + count * sizeof(SpriteDef));
        std::memcpy(defs.data(), &count, 4);
        SpriteDef resume{};
        resume.id = 27; resume.w = 79; resume.h = 53; resume.offX = 40; resume.offY = 27;
        std::memcpy(defs.data() + 4 + 27 * sizeof(SpriteDef), &resume, sizeof resume);
        SpriteDef quit{};
        quit.id = 4; quit.w = quit.h = 83; quit.offX = quit.offY = 42;
        std::memcpy(defs.data() + 4 + 4 * sizeof(SpriteDef), &quit, sizeof quit);
        check(pause.sprites_.parse(defs), "synthetic pause sprites load");
        const uint8_t red[] = {255, 0, 0, 255};
        glGenTextures(1, &pause.texture_);
        glBindTexture(GL_TEXTURE_2D, pause.texture_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, red);
        pause.textureWidth_ = pause.textureHeight_ = 1;

        pause.opened(100.0);
        check(std::abs(pause.touchReady_ - (100.0 + 20.0 / 30.0)) < 1e-10,
              "opening pause delays touch input by the original twenty simulation frames");

        // Independent native-coordinate expectations: LoadPauseScreen uses (959 + half aspect
        // extension, 69); OnClick uses stored 79x53 dimensions, integer halves and strict margins.
        struct View {
            int w, h;
            float cx, cy, left, right, top, bottom;
            int drawLeft, drawTop, drawRight, drawBottom;
            float qx, qy, qleft, qright, qtop, qbottom;
        };
        const View views[] = {
            {1024, 768, 959, 69, 910, 1009, 33, 106, 919, 42, 998, 95,
             65, 65, 24, 107, 24, 107},
            {1280, 720, 1218.4375f, 64.6875f, 1172.5f, 1265.3125f, 30.9375f, 99.375f,
             1181, 39, 1255, 89, 61.5625f, 60.9375f, 23.125f, 100.9375f, 22.5f, 100.3125f}
        };
        for (const View& v : views) {
            auto draw = [&](int x, int y, bool click) {
                Host_TestMouse(x, y, click ? 1 : 0, 0);
                glClearColor(1, 0, 1, 1); glClear(GL_COLOR_BUFFER_BIT);
                Plugins_BeginFrame(v.w, v.h);
                Hud_Begin(v.w, v.h);
                game.renderPause(v.w, v.h);
                Hud_End();
            };
            auto colour = [&](int x, int y, int redValue, int blueValue) {
                uint8_t pixel[4]{};
                glReadPixels(x, v.h - 1 - y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
                return std::abs((int)pixel[0] - redValue) <= 1 && pixel[1] <= 1 &&
                       std::abs((int)pixel[2] - blueValue) <= 1;
            };
            float cx = 0, cy = 0;
            PauseScreen::resumeCentre(v.w, v.h, cx, cy);
            check(std::abs(cx - v.cx) < .001f && std::abs(cy - v.cy) < .001f,
                  "resume uses the original position at both 4:3 and widescreen aspect ratios");
            check(!pause.resumeHit(v.w, v.h, v.left, v.cy) &&
                  !pause.resumeHit(v.w, v.h, v.right, v.cy) &&
                  !pause.resumeHit(v.w, v.h, v.cx, v.top) &&
                  !pause.resumeHit(v.w, v.h, v.cx, v.bottom),
                  "all four exact touch-margin boundaries are excluded");
            check(pause.resumeHit(v.w, v.h, v.left + .25f, v.cy) &&
                  pause.resumeHit(v.w, v.h, v.right - .25f, v.cy) &&
                  pause.resumeHit(v.w, v.h, v.cx, v.top + .25f) &&
                  pause.resumeHit(v.w, v.h, v.cx, v.bottom - .25f),
                  "the area immediately inside every native touch margin remains clickable");
            PauseScreen::quitCentre(v.w, v.h, cx, cy);
            check(std::abs(cx - v.qx) < .001f && std::abs(cy - v.qy) < .001f,
                  "quit mission's cross uses its original position at both aspect ratios");
            check(!pause.quitHit(v.w, v.h, v.qleft, v.qy) &&
                  !pause.quitHit(v.w, v.h, v.qright, v.qy) &&
                  !pause.quitHit(v.w, v.h, v.qx, v.qtop) &&
                  !pause.quitHit(v.w, v.h, v.qx, v.qbottom),
                  "quit mission excludes all exact sprite boundaries without a touch margin");
            check(pause.quitHit(v.w, v.h, v.qleft + .25f, v.qy) &&
                  pause.quitHit(v.w, v.h, v.qright - .25f, v.qy) &&
                  pause.quitHit(v.w, v.h, v.qx, v.qtop + .25f) &&
                  pause.quitHit(v.w, v.h, v.qx, v.qbottom - .25f) &&
                  !pause.quitHit(v.w, v.h, v.qleft - .25f, v.qy),
                  "quit mission only accepts points inside its native 83 by 83 hit region");

            check(game.key(SDL_SCANCODE_ESCAPE) && game.pauseOpen && game.uiPaused && Audio_SfxPaused(),
                  "opening the HUD pause screen suspends simulation and sound effects");
            pause.touchReady_ = OS_TimeAccurate() + 10.0;
            draw((int)v.cx, (int)v.cy, true);
            check(game.pauseOpen && game.uiPaused && Audio_SfxPaused(),
                  "a touch on resume during the input delay cannot resume the game");
            draw(0, 0, false);
            check(colour(v.w / 2, v.h / 2, 25, 25),
                  "the native black overlay darkens the frozen world with alpha 230");
            check(colour(v.drawLeft, v.drawTop, 255, 0) &&
                  colour(v.drawRight, v.drawBottom, 255, 0),
                  "resume retains its pixel-inclusive 80 by 54 draw geometry and sprite origin");
            check(colour(v.drawLeft - 1, (int)v.cy, 25, 25) &&
                  colour(v.drawRight + 1, (int)v.cy, 25, 25) &&
                  colour((int)v.cx, v.drawTop - 1, 25, 25) &&
                  colour((int)v.cx, v.drawBottom + 1, 25, 25),
                  "pixels outside the original resume sprite remain part of the darkened world");
            check(colour((int)v.qx, (int)v.qy, 255, 0),
                  "pause renders the original quit-mission cross as well as resume");

            pause.touchReady_ = 0;
            draw((int)v.qx, (int)v.qy, true);
            check(game.pauseOpen && game.uiPaused && Audio_SfxPaused() && !game.quitRequested,
                  "quit mission with no running job keeps pause open and cannot quit to desktop");
            draw((int)std::floor(v.left), (int)v.cy, true);
            check(game.pauseOpen, "touches outside the strict native margin do not resume");
            // This point is outside the visible icon but inside its original ten-unit touch margin.
            draw((int)std::floor(v.left) + 1, (int)v.cy, true);
            check(!game.pauseOpen && !game.uiPaused && !Audio_SfxPaused(),
                  "resume's touch margin restores simulation and sound after the input delay");
            check(colour(v.w / 2, v.h / 2, 255, 255),
                  "resuming by pointer reveals the frozen world without an extra dark frame");
        }
        pause.shutdown();
        Plugins_Shutdown();
    }
};

int main() {
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    if (!Host_Init("Pause screen rendering regression", 1280, 768)) {
        std::fprintf(stderr, "SKIP: no OpenGL test context: %s\n", SDL_GetError());
        Host_Shutdown();
        return 77;
    }
    check(Audio_Init(), "the dummy audio device opens");
    PauseScreenTestAccess::run();
    Audio_Shutdown();
    Host_Shutdown();
    return failures ? 1 : 0;
}
