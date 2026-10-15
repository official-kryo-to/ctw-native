// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#define SDL_MAIN_HANDLED
#include "game.h"
#include "hud.h"
#include "os/os.h"
#include <SDL.h>
#include <glad/gl.h>
#include <cmath>
#include <cstdio>

static int failures;
static void check(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); ++failures; }
}
struct GameplayHudTestAccess {
    static void prepare(GameplayHud& hud) {
        const uint16_t words[] = {0, 0, 1, 1, 0xffff, 0xffff, 0xffff, 0xffff};
        const auto* bytes = reinterpret_cast<const uint8_t*>(words);
        check(hud.map_.load({bytes, bytes + sizeof words}), "synthetic empty radar map loads");
        glGenTextures(1, &hud.texture_);
        const uint8_t white[] = {255, 255, 255, 255};
        glBindTexture(GL_TEXTURE_2D, hud.texture_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
        hud.textureWidth_ = hud.textureHeight_ = 1;
    }
};
static void pixel(int x, int y, int height, uint8_t out[4]) {
    glReadPixels(x, height - 1 - y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, out);
}
int main() {
    if (!Host_Init("Gameplay HUD rendering regression", 640, 768)) {
        std::fprintf(stderr, "SKIP: no OpenGL test context: %s\n", SDL_GetError());
        Host_Shutdown(); return 77;
    }
    Game game;
    GameplayHudTestAccess::prepare(game.gameplayHud);
    for (const auto size : {std::pair<int, int>{640, 768}, {640, 448}, {320, 768}}) {
        const int width = size.first, height = size.second;
        const float k = height / 768.f;
        Hud_Begin(width, height);
        glClearColor(1, 0, 1, 1); glClear(GL_COLOR_BUFFER_BIT);
        glStencilMask(0x55); glClearStencil(7);
        glColor4f(.2f, .3f, .4f, .5f);
        game.player.health = 127;
        game.gameplayHud.render(game, width, height);
        GLint mask, clear;
        glGetIntegerv(GL_STENCIL_WRITEMASK, &mask); glGetIntegerv(GL_STENCIL_CLEAR_VALUE, &clear);
        GLfloat colour[4]; glGetFloatv(GL_CURRENT_COLOR, colour);
        check(mask == 0x55 && clear == 7 && !glIsEnabled(GL_STENCIL_TEST), "radar restores stencil state for subsequent UI");
        check(std::fabs(colour[0] - .2f) < .001f && std::fabs(colour[3] - .5f) < .001f,
              "radar restores the caller's drawing colour");
        uint8_t outside[4], inside[4], health[4], empty[4];
        pixel(10, 10, height, outside);
        pixel((int)(145*k), (int)(85*k), height, inside);
        pixel((int)(67*k), (int)(85*k), height, health);
        pixel((int)(223*k), (int)(85*k), height, empty);
        check(outside[0] == 255 && outside[1] == 0 && outside[2] == 255, "circular map does not paint outside its bounds");
        check(inside[0] < 20 && inside[1] > 10 && inside[2] > inside[1], "radar interior has its own map background");
        check(health[1] > health[0] && health[1] > 100, "full health fills the original left semicircle");
        check(empty[1] == 0, "unported armour does not fabricate an armour meter");
        glClear(GL_COLOR_BUFFER_BIT);
        game.player.health = 0;
        game.gameplayHud.render(game, width, height);
        pixel((int)(67*k), (int)(85*k), height, health);
        check(health[1] == 0, "zero health removes the filled arc");
        Hud_End();
    }
    check(glGetError() == GL_NO_ERROR, "radar drawing leaves no OpenGL error");
    game.gameplayHud.shutdown(); game.gameplayHud.shutdown();
    Host_Shutdown();
    return failures ? 1 : 0;
}
