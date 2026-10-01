// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#define SDL_MAIN_HANDLED
#include "game.h"
#include "hud.h"
#include "os/os.h"
#include <SDL.h>
#include <glad/gl.h>
#include <cstdio>
#include <cstring>
#include <filesystem>

static int failures;
static void check(bool ok, const char* description) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", description); ++failures; }
}

struct RadioTestAccess {
    static void run() {
        Game game;
        game.cars.resize(1); game.cars[0].uid = 1; game.playerCar = 0;
        Radio& radio = game.radio;
        // Synthetic icons and a one-pixel texture: rendering checks require no game files or font.
        std::vector<uint8_t> defs(4 + 3 * sizeof(SpriteDef));
        uint32_t count = 3; std::memcpy(defs.data(), &count, 4);
        for (int i = 0; i < 3; ++i) {
            SpriteDef def{}; def.id = (uint16_t)(11 + i); def.w = def.h = 126;
            std::memcpy(defs.data() + 4 + i * sizeof def, &def, sizeof def);
        }
        check(radio.sprites_.parse(defs), "synthetic radio sprites load");
        radio.stations_.resize(3);
        for (int i = 0; i < 3; ++i) {
            radio.stations_[i].available = true; radio.stations_[i].icon = 11 + i;
        }
        const uint8_t red[] = {255,0,0,255};
        glGenTextures(1, &radio.texture_); glBindTexture(GL_TEXTURE_2D, radio.texture_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, red);
        radio.textureW_ = radio.textureH_ = 1;
        radio.key(SDL_SCANCODE_R, game);

        auto draw = [&]() {
            // The world is rendered each frame before the overlay.
            glClearColor(1,0,1,1); glClear(GL_COLOR_BUFFER_BIT);
            Hud_Begin(640,448); radio.render(640,448,game); Hud_End();
        };
        auto pixels = [&]() {
            std::vector<uint8_t> out(640 * 448 * 3);
            glReadPixels(0,0,640,448,GL_RGB,GL_UNSIGNED_BYTE,out.data());
            return out;
        };
        glClearColor(1,0,1,1); glClear(GL_COLOR_BUFFER_BIT);
        draw();
        auto image = pixels();
        const size_t margin = (200 * 640 + 10) * 3;
        check(image[margin] == 255 && image[margin+1] == 0 && image[margin+2] == 255,
              "compact selector preserves the game view outside its own overlay");
        for (int frame = 0; frame < 30; ++frame) {
            if (frame % 4 == 0) radio.key(SDL_SCANCODE_RIGHT, game);
            radio.update(game); draw();
        }
        auto animated = pixels();
        glClearColor(1,0,1,1); glClear(GL_COLOR_BUFFER_BIT);
        draw();
        check(animated == pixels(), "station animation leaves no trails from earlier frames");

        int selected = radio.station();
        Host_TestMouse(320,150,0,-1); draw();
        check(radio.station() == (selected + 1) % 3, "mouse wheel tunes the next station");
        selected = radio.station();
        Host_TestMouse(320,150,1,0); draw();
        check(radio.open() && radio.station() == selected, "clicking station artwork does not pause the radio");
        Host_TestMouse(10,420,1,0); draw();
        check(radio.open(), "clicks outside the overlay have no hidden Back action");
        int volume = radio.volume_;
        Host_TestMouse(260,300,1,0); draw();
        check(radio.volume_ == volume, "removed touch buttons cannot change volume");
        radio.key(SDL_SCANCODE_DOWN,game);
        check(radio.volume_ == volume - 1, "arrow keys adjust the original volume levels");
        Host_TestMouse(320,420,1,0); draw();
        check(radio.open(), "removed Back button does not leave an invisible click target");
        check(!radio.key(SDL_SCANCODE_A,game) && !radio.key(SDL_SCANCODE_D,game),
              "radio leaves WASD driving controls available");
        radio.key(SDL_SCANCODE_ESCAPE,game);
        check(!radio.open(), "Escape dismisses the compact selector");
        radio.shutdown();
    }
};

int main() {
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    if (!Host_Init("Radio rendering regression",640,448)) {
        std::fprintf(stderr, "SKIP: no OpenGL test context: %s\n", SDL_GetError());
        Host_Shutdown();
        return 77;
    }
    const auto saves = std::filesystem::temp_directory_path() / ("ctw-radio-test-" + std::to_string(SDL_GetTicks64()));
    std::filesystem::create_directories(saves);
    OS_SetDocumentsRoot(saves.string().c_str());
    RadioTestAccess::run();
    Host_Shutdown();
    std::filesystem::remove_all(saves);
    return failures ? 1 : 0;
}
