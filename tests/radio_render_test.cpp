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
#include <cstdio>
#include <cstring>
#include <filesystem>

static int failures;
static void check(bool ok, const char* description) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", description); ++failures; }
}

struct RadioTestAccess {
    static void run() {
        Game& game = TheGame();
        check(Audio_Init(), "the dummy audio device opens");
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
        check(!radio.key(SDL_SCANCODE_R, game) && !radio.appOpen(), "R does not open the radio");
        check(!radio.key(SDL_SCANCODE_LEFT, game) && !radio.key(SDL_SCANCODE_RIGHT, game) &&
              !radio.key(SDL_SCANCODE_MINUS, game) && !radio.key(SDL_SCANCODE_LEFTBRACKET, game),
              "no shortcut tunes outside the Tab app");

        auto draw = [&]() {
            // The world is rendered each frame before the overlay.
            glClearColor(1,0,1,1); glClear(GL_COLOR_BUFFER_BIT);
            Plugins_BeginFrame(640,448); Hud_Begin(640,448); Plugins_DrawHud(640,448); radio.renderApp(640,448,game); Hud_End();
        };
        glClearColor(1,0,1,1); glClear(GL_COLOR_BUFFER_BIT);
        const int tuned = radio.station();
        Host_TestMouse(320,150,0,-1); draw();
        check(!radio.appOpen() && radio.station() == tuned, "the wheel never tunes: only Tab changes the radio");
        game.playerCar = -1;
        Host_TestMouse(320,150,0,-1); draw();
        check(!radio.appOpen() && radio.station() == tuned, "on foot the wheel does nothing");
        game.playerCar = 0; radio.stations_[1].available = false; radio.selected_ = 0;
        game.cars[0].radioStation = 2;
        game.cars.emplace_back(); game.cars.back().uid = 2; game.cars.back().radioStation = 0;
        game.playerCar = 1; radio.update(game);
        check(radio.station() == 0, "each car restores its own station");
        game.playerCar = 0; radio.update(game);
        check(radio.station() == 2, "switching back restores the previous car's tuning");
        const CtwHostApi& api = Plugins_HostApi();
        check(api.radio_station_count() == 3 && !api.radio_station_available(1) && !api.set_radio_station(1),
              "radio API exposes station availability");
        check(api.set_radio_station(0) && api.get_radio_station() == 0 &&
              api.set_radio_volume(4) && api.get_radio_volume() == 4 && !api.set_radio_volume(11),
              "radio API tuning and volume are reflected by gameplay");
        // The radio app: Tab opens it full screen and pauses the game; A / D tune; Tab closes it again.
        api.set_radio_station(0);
        check(radio.key(SDL_SCANCODE_TAB, game) && radio.appOpen() && game.uiPaused, "Tab opens the radio app and pauses the game");
        check(radio.key(SDL_SCANCODE_D, game) && radio.station() == 2, "the app tunes past unavailable stations");
        check(Audio_SfxPaused(), "changing station keeps the paused game's sounds silent");
        check(radio.key(SDL_SCANCODE_W, game) && radio.volumeLevel() == 5, "the app raises the volume");
        check(radio.key(SDL_SCANCODE_F, game), "the paused game takes no other keys while the app is open");
        radio.renderApp(640, 448, game);
        check(radio.key(SDL_SCANCODE_TAB, game) && !radio.appOpen() && !game.uiPaused && !Audio_SfxPaused(),
              "Tab closes the app and resumes the game and its sounds");
        game.playerCar = -1;
        check(!radio.key(SDL_SCANCODE_TAB, game) && !radio.appOpen(), "the app needs a car with a radio");
        game.playerCar = 0;
        // SDL may report multiple detents in one event, including flipped devices.
        SDL_Event event{}; event.type = SDL_MOUSEWHEEL; event.wheel.y = 3;
        event.wheel.direction = SDL_MOUSEWHEEL_FLIPPED;
        SDL_PushEvent(&event); Host_PumpEvents();
        int total = 0; while (int notch = Host_PopWheel()) total += notch;
        check(total == -3, "SDL preserves all wheel detents and flipped direction");
        radio.shutdown();
        Plugins_Shutdown(); game.cars.clear(); game.playerCar = -1;
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
