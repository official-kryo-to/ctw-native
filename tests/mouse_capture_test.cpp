// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// Load the real mod menu and Enhancements against the real game/SDL host. Inject SDL input/focus events;
// assertions read actual SDL relative capture and the actual third-person camera. No game assets are needed.
#define SDL_MAIN_HANDLED
#include "game.h"
#include "plugins.h"
#include "os/os.h"
#include <SDL.h>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace {
int failures;
void check(bool ok, const char* why) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s (%s)\n", why, SDL_GetError()); ++failures; }
}
bool captured() { return SDL_GetRelativeMouseMode() == SDL_TRUE; }

void key(int scancode) {
    SDL_Event event{}; event.type = SDL_KEYDOWN; event.key.keysym.scancode = (SDL_Scancode)scancode;
    check(SDL_PushEvent(&event) == 1 && Host_PumpEvents(), "SDL accepts the test key");
    while (int k = Host_PopKey()) TheGame().key(k);
}

void motion(int x, int y) {
    SDL_Event event{}; event.type = SDL_MOUSEMOTION; event.motion.xrel = x; event.motion.yrel = y;
    check(SDL_PushEvent(&event) == 1 && Host_PumpEvents(), "SDL accepts the relative motion");
}

void focus(SDL_Window* window, bool on) {
    SDL_Event event{}; event.type = SDL_WINDOWEVENT;
    event.window.windowID = SDL_GetWindowID(window);
    event.window.event = on ? SDL_WINDOWEVENT_FOCUS_GAINED : SDL_WINDOWEVENT_FOCUS_LOST;
    check(SDL_PushEvent(&event) == 1 && Host_PumpEvents(), "SDL accepts the window focus event");
}

void run(SDL_Window* window, const std::filesystem::path& dir) {
    Game& game = TheGame();
    game.camera.reset(game.player.pos, game.player.heading());
    Plugins_Init(dir.string());
    const auto& host = Plugins_HostApi();
    Plugins_BeginFrame(640, 480);
    check(!captured() && !game.freeCam.on, "default overhead camera leaves the pointer available");
    key(SDL_SCANCODE_V); Plugins_BeginFrame(640, 480);
    check(captured() && game.freeCam.on && game.controlYaw.on,
          "V activates the actual third-person mod and SDL relative capture");
    motion(25, -10); Plugins_BeginFrame(640, 480);
    int16_t before = game.controlYaw.yaw;
    motion(25, 0); Plugins_BeginFrame(640, 480);
    check(std::abs(game.controlYaw.yaw - before) > 180, "the running third-person camera consumes real SDL deltas");

    motion(100, 0); key(SDL_SCANCODE_ESCAPE);
    check(game.pauseOpen && game.uiPaused && !captured() && !SDL_GetWindowGrab(window),
          "Escape immediately releases relative capture and the window grab for the native pause menu");
    check(SDL_ShowCursor(SDL_QUERY) == SDL_ENABLE, "the paused game's cursor is visible");
    before = game.controlYaw.yaw;
    for (int i = 0; i < 3; ++i) {
        motion(80, -40); Plugins_BeginFrame(640, 480);
        check(!captured() && game.freeCam.on && game.controlYaw.yaw == before,
              "third-person frame callbacks cannot recapture the native pause cursor or turn its camera");
    }
    Host_TestMouse(300, 200, 0, 1); Plugins_BeginFrame(640, 480);
    check(host.mouse_wheel() == 0.f, "native paused UI does not zoom the camera with the mouse wheel");
    key(SDL_SCANCODE_ESCAPE);
    check(!game.pauseOpen && !game.uiPaused && captured(), "Escape resumes the requested camera capture immediately");
    Plugins_BeginFrame(640, 480);
    check(game.controlYaw.yaw == before, "resuming discards motion queued before or during pause");

    key(SDL_SCANCODE_F4); Plugins_BeginFrame(640, 480);
    check(!captured() && game.freeCam.on && !game.pauseOpen, "F4 retains the mod menu's cursor ownership");
    key(SDL_SCANCODE_ESCAPE); Plugins_BeginFrame(640, 480);
    check(captured() && !game.pauseOpen, "Escape closes the mod menu and returns mouse look without opening native pause");
    host.set_game_input(0); host.set_mouse_captured(1);
    check(!captured(), "a plugin that owns keyboard input also keeps the gameplay cursor released");
    host.set_game_input(1);
    check(captured(), "returning plugin input restores its pending camera request");

    focus(window, false);
    check(!captured(), "losing focus immediately releases SDL capture");
    motion(250, 0); Plugins_BeginFrame(640, 480);
    check(!captured() && game.controlYaw.yaw == before, "unfocused third-person callbacks cannot take capture or deltas");
    focus(window, true);
    check(captured(), "regaining focus restores the active camera request");
    Plugins_BeginFrame(640, 480);
    check(game.controlYaw.yaw == before, "focus regain discards external mouse motion");
    key(SDL_SCANCODE_ESCAPE); focus(window, false); focus(window, true);
    check(!captured() && game.pauseOpen, "regaining focus while paused keeps the pointer available");
    key(SDL_SCANCODE_RETURN);
    check(captured() && !game.pauseOpen, "the pause menu's confirm binding also restores mouse look");

    key(SDL_SCANCODE_V); Plugins_BeginFrame(640, 480);
    check(!captured() && !game.freeCam.on && !game.controlYaw.on, "turning third-person off releases capture and camera overrides");
    key(SDL_SCANCODE_ESCAPE); key(SDL_SCANCODE_ESCAPE);
    check(!captured(), "pausing and resuming the overhead camera never acquires capture");
    key(SDL_SCANCODE_V); Plugins_BeginFrame(640, 480); Plugins_Shutdown();
    check(!captured() && !game.freeCam.on, "unloading the real plugins releases capture and their camera");
}
}

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    if (!Host_Init("Mouse capture regression", 640, 480)) {
        std::fprintf(stderr, "SKIP: no OpenGL context: %s\n", SDL_GetError()); Host_Shutdown(); return 77;
    }
    SDL_Window* window = SDL_GL_GetCurrentWindow();
    Host_PumpEvents();
    if (!window) { std::fprintf(stderr, "FAIL: OpenGL context has no window\n"); Host_Shutdown(); return 1; }
    // Headless/remote CI desktops can create a real GL window without granting foreground focus. Exercise the
    // same production focus-event path explicitly, without attempting to steal focus from the developer.
    focus(window, true);
    const auto dir = std::filesystem::temp_directory_path() / ("ctw-mouse-test-" + std::to_string(SDL_GetTicks64()));
    std::filesystem::create_directories(dir / "Enhancements");
    const auto menu = std::filesystem::path(argv[1]), mod = std::filesystem::path(argv[2]);
    std::filesystem::copy_file(menu, dir / menu.filename());
    std::filesystem::copy_file(mod, dir / "Enhancements" / mod.filename());
    { std::ofstream ini(dir / "Enhancements/mod.ini"); ini << "name = Enhancements\ndll = " << mod.filename().string() << '\n'; }
    run(window, dir);
    Host_Shutdown();
    std::filesystem::remove_all(dir);
    if (!failures) std::puts("PASS: real ModMenu + Enhancements, native pause/resume, mod menu, plugin input, focus and unload capture");
    return failures ? 1 : 0;
}
