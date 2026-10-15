// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#define SDL_MAIN_HANDLED
#include "game.h"
#include "hud.h"
#include "gfx/assets.h"
#include "os/os.h"
#include <SDL.h>
#include <glad/gl.h>
#include <stb_image_write.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

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
    static void markers(GameplayHud& hud) {
        std::vector<SpriteDef> defs(115);
        for (auto& def : defs) def.x = 0xffff; // no frame artwork in this synthetic atlas
        defs[107] = {107, 0, 0, 0, 2, 4, 4, 8};
        defs[108] = {108, 0, 8, 0, 1, 4, 2, 8};
        std::vector<uint8_t> data(4 + defs.size() * sizeof(SpriteDef));
        const uint32_t count = (uint32_t)defs.size();
        std::memcpy(data.data(), &count, sizeof count);
        std::memcpy(data.data() + 4, defs.data(), defs.size() * sizeof(SpriteDef));
        check(hud.sprites_.parse(data), "synthetic radar marker definitions load");
        std::vector<uint8_t> pixels(64 * 32 * 4);
        for (int y = 0; y < 18; ++y) {
            for (int x = 0; x < 10; ++x) {
                pixels[(y * 64 + x) * 4 + 1] = 255;
                pixels[(y * 64 + x) * 4 + 3] = 255;
            }
            for (int x = 16; x < 22; ++x) {
                pixels[(y * 64 + x) * 4] = 255;
                pixels[(y * 64 + x) * 4 + 3] = 255;
            }
        }
        glBindTexture(GL_TEXTURE_2D, hud.texture_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 64, 32, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        hud.textureWidth_ = 64; hud.textureHeight_ = 32;
    }
    static void tile(GameplayHud& hud) {
        const uint16_t words[] = {0, 0, 1, 1, 0xffff, 0xffff, 0xffff, 65000};
        const auto* bytes = reinterpret_cast<const uint8_t*>(words);
        check(hud.map_.load({bytes, bytes + sizeof words}), "synthetic colour tile map loads");
        std::vector<uint8_t> pixels(256 * 256 * 4, 255);
        for (int y = 0; y < 256; ++y) {
            for (int x = 0; x < 256; ++x) {
                pixels[(y * 256 + x) * 4] = y < 128 ? 255 : 0;
                pixels[(y * 256 + x) * 4 + 1] = 0;
                pixels[(y * 256 + x) * 4 + 2] = y < 128 ? 0 : 255;
            }
        }
        const auto path = std::filesystem::temp_directory_path() /
            ("ctw-radar-tile-" + std::to_string(SDL_GetTicks64()) + ".png");
        // The engine builds stb_image_write without stdio, so encode to memory and write the file here.
        std::vector<uint8_t> png;
        const auto append = [](void* out, void* bytes, int size) {
            auto* buffer = static_cast<std::vector<uint8_t>*>(out);
            buffer->insert(buffer->end(), (uint8_t*)bytes, (uint8_t*)bytes + size);
        };
        const bool encoded = stbi_write_png_to_func(append, &png, 256, 256, 4, pixels.data(), 256 * 4) != 0;
        std::ofstream(path, std::ios::binary).write((const char*)png.data(), (std::streamsize)png.size());
        check(encoded && std::filesystem::file_size(path) == png.size(), "synthetic radar tile PNG is written");
        check(Assets_OverrideTexturePNG(65000, path.string()), "synthetic radar tile enters the texture cache");
        std::filesystem::remove(path);
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
        check(empty[1] == 0, "zero armour does not fabricate an armour meter");
        glClear(GL_COLOR_BUFFER_BIT);
        game.player.armour = 127;
        game.gameplayHud.render(game, width, height);
        pixel((int)(223*k), (int)(85*k), height, empty);
        check(empty[2] > empty[1] && empty[1] > empty[0] && empty[1] > 100,
              "full armour fills the mirrored right semicircle in the native blue");
        glClear(GL_COLOR_BUFFER_BIT);
        game.player.armour = 64;
        game.gameplayHud.render(game, width, height);
        pixel((int)(200*k), (int)(30*k), height, empty);
        check(empty[2] > empty[1] && empty[1] > 100, "half armour fills the upper-right quarter");
        pixel((int)(200*k), (int)(140*k), height, empty);
        check(empty[1] == 0, "half armour leaves the lower-right quarter empty");
        game.player.armour = 0;
        glClear(GL_COLOR_BUFFER_BIT);
        game.player.health = 0;
        game.gameplayHud.render(game, width, height);
        pixel((int)(67*k), (int)(85*k), height, health);
        check(health[1] == 0, "zero health removes the filled arc");
        Hud_End();
    }
    GameplayHudTestAccess::markers(game.gameplayHud);
    Hud_Begin(640, 768);
    game.player.setHeading(0x4000); game.camera.yaw = 0x4000;
    check(!game.gameplayHud.rotation(), "radar rotation defaults to the native north-up preference");
    glClear(GL_COLOR_BUFFER_BIT);
    game.gameplayHud.render(game, 640, 768);
    uint8_t p[4];
    pixel(145, 10, 768, p);
    check(p[1] == 255 && p[0] == 0, "north-up north marker is upright at the top edge");
    pixel(148, 85, 768, p);
    check(p[0] == 255 && p[1] == 0, "north-up player marker follows the player's east heading");
    game.gameplayHud.setRotation(true);
    check(game.gameplayHud.rotation(), "rotation preference is exposed to the input/settings owner");
    glClear(GL_COLOR_BUFFER_BIT);
    game.gameplayHud.render(game, 640, 768);
    pixel(70, 85, 768, p);
    check(p[1] == 255 && p[0] == 0, "rotating radar puts north at the left edge for an east-facing camera");
    pixel(145, 10, 768, p);
    check(p[1] == 0, "rotating radar does not leave a second north marker at the old edge");
    pixel(148, 85, 768, p);
    check(p[0] < 20, "camera rotation removes the player marker's old east orientation");
    pixel(145, 82, 768, p);
    check(p[0] == 255 && p[1] == 0, "player and camera sharing a heading point the radar arrow upwards");
    game.camera.yaw = 0x8000;
    glClear(GL_COLOR_BUFFER_BIT);
    game.gameplayHud.render(game, 640, 768);
    pixel(145, 160, 768, p);
    check(p[1] == 255 && p[0] == 0, "half-turn north marker is clipped to the bottom circular edge");
    GameplayHudTestAccess::tile(game.gameplayHud);
    game.player.pos[0] = -3220 * 4096; game.player.pos[1] = 2200 * 4096;
    game.gameplayHud.setRotation(false);
    glClear(GL_COLOR_BUFFER_BIT);
    game.gameplayHud.render(game, 640, 768);
    pixel(145, 55, 768, p);
    check(p[0] > 180 && p[2] < 70, "north-up radar draws the synthetic tile's northern red half above the focus");
    pixel(145, 115, 768, p);
    check(p[2] > 180 && p[0] < 70, "north-up radar draws the synthetic tile's southern blue half below the focus");
    game.gameplayHud.setRotation(true); game.camera.yaw = 0x4000;
    glClear(GL_COLOR_BUFFER_BIT);
    game.gameplayHud.render(game, 640, 768);
    pixel(115, 85, 768, p);
    check(p[0] > 180 && p[2] < 70, "quarter-turn radar rotates the northern tile half to the left with its marker");
    pixel(175, 85, 768, p);
    check(p[2] > 180 && p[0] < 70, "quarter-turn radar rotates the southern tile half to the right");
    pixel(205, 145, 768, p);
    check(p[0] == 255 && p[1] == 0 && p[2] == 255,
          "rotated textured tile corners remain outside the circular stencil");
    Assets_RestoreTexture(65000);
    Hud_End();
    check(glGetError() == GL_NO_ERROR, "radar drawing leaves no OpenGL error");
    game.gameplayHud.shutdown(); game.gameplayHud.shutdown();
    Host_Shutdown();
    return failures ? 1 : 0;
}
