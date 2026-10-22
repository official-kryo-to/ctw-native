// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#define SDL_MAIN_HANDLED
#include "game.h"
#include "hud.h"
#include "plugins.h"
#include "weaponhud.h"
#include "audio/audio.h"
#include "gfx/spriteset.h"
#include "os/os.h"
#include "os/wad.h"
#include <SDL.h>
#include <glad/gl.h>
#include <stb_image_write.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <vector>

namespace {
int failures;
void check(bool ok, const char* description) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", description); ++failures; }
}

void png(const std::filesystem::path& file, uint8_t r, uint8_t g, uint8_t b) {
    std::ofstream out(file, std::ios::binary);
    uint8_t pixels[16];
    for (int i = 0; i < 4; ++i) {
        pixels[i * 4] = r; pixels[i * 4 + 1] = g; pixels[i * 4 + 2] = b; pixels[i * 4 + 3] = 255;
    }
    check(stbi_write_png_to_func([](void* context, void* data, int size) {
        static_cast<std::ofstream*>(context)->write(static_cast<const char*>(data), size);
    }, &out, 2, 2, 4, pixels, 8) != 0, "a synthetic PNG is written");
}

void fixtures(const std::filesystem::path& dir) {
    // All sprites and table values here are arbitrary synthetic data. No APK is needed.
    struct Entry { uint32_t hash, offset, size; };
    std::vector<Entry> entries;
    std::ofstream wad(dir / "rom.wad", std::ios::binary);
    const char* names[] = {"SS_Weapons.bin", "SS_AlwaysLoaded.bin", "SS_Hud.bin"};
    const int counts[] = {32, 72, 27};
    for (int sheet = 0; sheet < 3; ++sheet) {
        const uint32_t count = counts[sheet];
        entries.push_back({Wad::hashName(names[sheet]), (uint32_t)wad.tellp(), (uint32_t)(4 + count * sizeof(SpriteDef))});
        wad.write(reinterpret_cast<const char*>(&count), sizeof count);
        for (int i = 0; i < counts[sheet]; ++i) {
            SpriteDef sprite{}; sprite.id = (uint16_t)i;
            sprite.w = sprite.h = sheet == 2 ? 88 : 12;
            sprite.offX = sprite.offY = sheet == 2 ? 0 : 6;
            if (sheet == 0 && i == 10) sprite.w = 0; // an empty sprite must not cause division by zero
            wad.write(reinterpret_cast<const char*>(&sprite), sizeof sprite);
        }
    }
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.hash < b.hash; });
    std::ofstream toc(dir / "rom.toc", std::ios::binary);
    toc.write(reinterpret_cast<const char*>(entries.data()), entries.size() * sizeof(Entry));
    png(dir / "ss_weapons.png", 255, 0, 0);
    png(dir / "ss_alwaysloaded.png", 0, 0, 255);
    png(dir / "ss_hud.png", 0, 255, 0);
    WeaponTables table{};
    for (auto& info : table.info) { info.maxAmmo = 100; info.slot = 1; }
    table.info[0].slot = 0; table.info[0].maxAmmo = -1;
    table.info[5].slot = 2; table.info[5].clip = 3;
    for (int32_t& life : table.traceLife) life = 3;
    std::ofstream weapons(dir / "weapon_tables.bin", std::ios::binary);
    weapons.write("CTWWPN01", 8);
    weapons.write(reinterpret_cast<const char*>(table.info), sizeof table.info);
    weapons.write(reinterpret_cast<const char*>(table.defaultAmmo), sizeof table.defaultAmmo);
    weapons.write(reinterpret_cast<const char*>(table.traceLife), sizeof table.traceLife);
}

void pixel(int x, int y, int r, int g, int b, const char* description) {
    uint8_t actual[4]{};
    glReadPixels(x, 767 - y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, actual);
    check(std::abs(actual[0] - r) <= 2 && std::abs(actual[1] - g) <= 2 && std::abs(actual[2] - b) <= 2,
          description);
}

void run(const std::filesystem::path& dir) {
    Game& game = TheGame();
    check(Audio_Init(), "the dummy audio device opens");
    check(Weapons_Init(dir.string()), "synthetic weapon tables load");
    game.playerCar = -1; game.player.dead = false; game.uiPaused = false;
    game.playerWeapons.clearAll();
    check(!WeaponHud_Key(game, SDL_SCANCODE_TAB), "Tab is not consumed when the weapon HUD is unavailable");
    check(WeaponHud_Init(dir.string()) && WeaponHud_Ok(), "synthetic HUD sheets load");
    GLint hudTexture = 0, alwaysTexture = 0, weaponTexture = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &hudTexture);
    auto draw = [&]() {
        glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT);
        Plugins_BeginFrame(1024, 768); Hud_Begin(1024, 768);
        WeaponHud_Render(game, 1024, 768); Plugins_DrawHud(1024, 768); Hud_End();
    };
    Host_TestMouse(0, 0, 0, 0); draw();
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &alwaysTexture);
    pixel(835, 575, 0, 0, 255, "the on-foot weapon icon uses the default button position");
    Hud_Begin(1024, 768);
    check(WeaponHud_DrawIcon(0, 20, 30, 40, 40, 1), "the public icon draw accepts a valid sprite");
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &weaponTexture);
    pixel(40, 50, 255, 0, 0, "the public weapon icon is centred in its requested box");
    check(!WeaponHud_DrawIcon(25, 0, 0, 40, 40, 1) && !WeaponHud_DrawIcon(24, 0, 0, 40, 40, 1) &&
          !WeaponHud_DrawIcon(0, 0, 0, 0, 40, 1) &&
          !WeaponHud_DrawIcon(0, 0, 0, std::numeric_limits<float>::infinity(), 40, 1),
          "empty, unsupported and invalid-size weapon icons are rejected");
    Hud_End();
    game.playerCar = 0;
    check(!WeaponHud_Key(game, SDL_SCANCODE_TAB) && !WeaponHud_WheelOpen(), "the on-foot wheel leaves in-car Tab available");
    game.playerCar = -1; game.player.dead = true;
    check(!WeaponHud_Key(game, SDL_SCANCODE_TAB), "a dead player cannot open the weapon wheel or consume Tab");
    game.player.dead = false; game.uiPaused = true;
    check(!WeaponHud_Key(game, SDL_SCANCODE_TAB) && game.uiPaused, "a different paused UI keeps ownership of the pause");
    game.uiPaused = false;
    SDL_Event focus{}; focus.type = SDL_WINDOWEVENT; focus.window.event = SDL_WINDOWEVENT_FOCUS_GAINED;
    focus.window.windowID = SDL_GetWindowID(SDL_GL_GetCurrentWindow());
    SDL_PushEvent(&focus); Host_PumpEvents(); Plugins_HostApi().set_mouse_captured(1);
    check(SDL_GetRelativeMouseMode(), "the gameplay camera captures SDL before opening its weapon wheel");
    check(game.playerWeapons.give(5, 10, true), "the synthetic pistol is available in slot two");
    check(WeaponHud_Key(game, SDL_SCANCODE_TAB) && WeaponHud_WheelOpen() && game.uiPaused && Audio_SfxPaused(),
          "Tab pauses simulation and sound effects while the weapon wheel is open");
    check(!SDL_GetRelativeMouseMode(), "opening the weapon wheel immediately releases the camera's cursor");
    Plugins_HostApi().set_mouse_captured(1);
    check(!SDL_GetRelativeMouseMode(), "a camera cannot recapture the weapon wheel's pointer");
    check(WeaponHud_Key(game, SDL_SCANCODE_Q), "weapon cycling is consumed while the panel pauses the world");
    draw();
    pixel(680, 320, 0, 255, 0, "the held weapon's slot background is fully lit");
    pixel(488, 128, 0, 64, 0, "the other slot backgrounds use the original quarter alpha");
    Host_TestMouse(512, 152, 1, 0); draw();
    check(game.playerWeapons.current()->type == 0 && !WeaponHud_WheelOpen() && !game.uiPaused && !Audio_SfxPaused(),
          "a full click between frames selects the pointed slot and restores simulation and sound");
    check(SDL_GetRelativeMouseMode(), "selection by pointer immediately restores the requested camera capture");
    WeaponHud_Key(game, SDL_SCANCODE_TAB);
    Host_TestMouse(704, 344, 1, 0); draw();
    check(game.playerWeapons.current()->type == 5 && !WeaponHud_WheelOpen(), "a full click can also select the pistol");
    WeaponHud_Key(game, SDL_SCANCODE_TAB);
    Host_TestMouse(10, 10, 1, 0); draw();
    check(!WeaponHud_WheelOpen() && !game.uiPaused && !Audio_SfxPaused(), "a complete click outside the slots closes the wheel");
    WeaponHud_Key(game, SDL_SCANCODE_TAB);
    Plugins_HostApi().set_game_input(0);
    check(game.key(SDL_SCANCODE_ESCAPE) && !WeaponHud_WheelOpen() && !game.uiPaused && !Audio_SfxPaused(),
          "an open wheel receives Escape even when a plugin has disabled gameplay input");
    check(!SDL_GetRelativeMouseMode(), "closing the wheel keeps capture released while the plugin still owns input");
    Plugins_HostApi().set_game_input(1);
    check(SDL_GetRelativeMouseMode(), "returning plugin input restores the requested camera capture after the wheel closes");
    WeaponHud_Key(game, SDL_SCANCODE_TAB); game.player.dead = true; draw();
    check(!WeaponHud_WheelOpen() && !game.uiPaused && !Audio_SfxPaused(), "losing the player closes the panel and releases its pause");
    game.player.dead = false;
    Audio_SetSfxPaused(true); WeaponHud_Key(game, SDL_SCANCODE_TAB); WeaponHud_Key(game, SDL_SCANCODE_TAB);
    check(Audio_SfxPaused(), "closing the wheel preserves a pre-existing sound pause");
    Audio_SetSfxPaused(false);
    WeaponHud_Key(game, SDL_SCANCODE_TAB);
    check(!WeaponHud_Init((dir / "missing").string()) && !WeaponHud_Ok() && !WeaponHud_WheelOpen() &&
          !game.uiPaused && !Audio_SfxPaused(), "a failed reinit clears stale HUD state and releases its pause");
    check(!glIsTexture((GLuint)hudTexture) && !glIsTexture((GLuint)alwaysTexture) && !glIsTexture((GLuint)weaponTexture),
          "reinitialization deletes all three previous sprite textures");
    check(WeaponHud_Init(dir.string()), "the HUD can initialize again after a failed load");
    draw(); pixel(835, 575, 0, 0, 255, "reinit does not move the default weapon button another 25 units");
    check(WeaponHud_Init(dir.string()), "a second consecutive HUD initialization succeeds");
    draw(); pixel(835, 575, 0, 0, 255, "repeated successful init keeps the same default button position");
    pixel(810, 550, 0, 0, 0, "the weapon button no longer drifts across repeated init calls");
    {
        std::vector<uint8_t> config(241);
        const int16_t x = 400, y = 500;
        std::memcpy(config.data() + 1 + 15 * 12 + 4, &x, sizeof x);
        std::memcpy(config.data() + 1 + 15 * 12 + 6, &y, sizeof y);
        std::ofstream out(dir / "buttonconfig", std::ios::binary);
        out.write(reinterpret_cast<const char*>(config.data()), config.size());
    }
    check(WeaponHud_Init(dir.string()), "the HUD reads a configured button position");
    draw(); pixel(375, 475, 0, 0, 255, "buttonconfig coordinates receive the original 25-unit adjustment");
    std::filesystem::remove(dir / "buttonconfig");
    check(WeaponHud_Init(dir.string()), "the HUD reloads after removing buttonconfig");
    draw(); pixel(835, 575, 0, 0, 255, "a new data directory without buttonconfig restores the default coordinates");
    std::filesystem::rename(dir / "ss_alwaysloaded.png", dir / "saved.png");
    check(!WeaponHud_Init(dir.string()) && !WeaponHud_Ok() && !WeaponHud_DrawIcon(0, 0, 0, 40, 40, 1),
          "a partially loaded HUD never exposes stale or incomplete icon rendering");
    std::filesystem::rename(dir / "saved.png", dir / "ss_alwaysloaded.png");
    check(WeaponHud_Init(dir.string()), "the HUD recovers from a partial sprite-sheet failure");
    WeaponHud_Key(game, SDL_SCANCODE_TAB); WeaponHud_Shutdown();
    check(!WeaponHud_Ok() && !WeaponHud_WheelOpen() && !game.uiPaused && !Audio_SfxPaused(),
          "shutdown releases the wheel pause and HUD resources before OpenGL teardown");
    WeaponHud_Shutdown();
    Plugins_Shutdown(); Audio_Shutdown();
}
} // namespace

int main() {
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    if (!Host_Init("Weapon HUD rendering regression", 1024, 768)) {
        std::fprintf(stderr, "SKIP: no OpenGL test context: %s\n", SDL_GetError());
        Host_Shutdown();
        return 77;
    }
    const auto dir = std::filesystem::temp_directory_path() / ("ctw-weaponhud-test-" + std::to_string(SDL_GetTicks64()));
    std::filesystem::create_directories(dir);
    fixtures(dir); run(dir);
    Host_Shutdown(); std::filesystem::remove_all(dir);
    return failures ? 1 : 0;
}
