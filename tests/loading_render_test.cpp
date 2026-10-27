// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#define SDL_MAIN_HANDLED
#include "game.h"
#include "loadingscreen.h"
#include "os/os.h"
#include "os/wad.h"
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
static void check(bool ok, const char* what) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
}
static void write(const std::filesystem::path& path, const std::vector<uint8_t>& bytes) {
    std::ofstream file(path, std::ios::binary);
    file.write((const char*)bytes.data(), (std::streamsize)bytes.size());
}
static void put(std::vector<uint8_t>& bytes, size_t at, uint32_t value, size_t count = 4) {
    for (size_t i = 0; i < count; ++i) bytes[at + i] = (uint8_t)(value >> (i * 8));
}
static std::vector<uint8_t> png(int width, int height, uint8_t r, uint8_t g, uint8_t b, bool marker = false) {
    std::vector<uint8_t> pixels((size_t)width * height * 4), bytes;
    for (size_t i = 0; i < pixels.size(); i += 4) {
        const bool marked = marker && (i / 4) % width < 60;
        pixels[i] = r; pixels[i + 1] = marked ? 255 : g; pixels[i + 2] = b; pixels[i + 3] = 255;
    }
    stbi_write_png_to_func([](void* out, void* data, int count) {
        auto& result = *(std::vector<uint8_t>*)out;
        result.insert(result.end(), (uint8_t*)data, (uint8_t*)data + count);
    }, &bytes, width, height, 4, pixels.data(), width * 4);
    return bytes;
}

struct LoadingScreenTestAccess {
    static void run() {
        const auto dir = std::filesystem::temp_directory_path() /
                         ("ctw-loading-test-" + std::to_string(SDL_GetTicks64()));
        std::filesystem::create_directories(dir);
        // Entirely synthetic native-format data: blue background and solid red
        // sprite. No game binary, extracted resource table or artwork is needed.
        std::vector<uint8_t> pak(4 * 4096);
        put(pak, 4, 3); put(pak, 8, 3); put(pak, 12, 3);
        put(pak, 16, 3); put(pak, 20, 4);
        put(pak, 24, 1, 2); put(pak, 26, 2, 2); put(pak, 28, 3, 2);
        put(pak, 4096 + 26 * 2, 1, 2); put(pak, 8192 + 87 * 2, 2, 2);
        auto background = png(512, 512, 0, 0, 180);
        const size_t endPage = 3 + (background.size() + 12 + 4095) / 4096;
        pak.resize(endPage * 4096);
        put(pak, 20, (uint32_t)endPage);
        put(pak, 12288, 512, 2); put(pak, 12290, 512, 2); put(pak, 12292, 0xBEEF, 2);
        put(pak, 12296, (uint32_t)background.size());
        std::memcpy(pak.data() + 12300, background.data(), background.size());
        write(dir / "game.pak", pak);
        std::vector<uint8_t> defs(20), toc(12);
        put(defs, 0, 1); put(defs, 12, 60, 2); put(defs, 14, 60, 2);
        put(defs, 16, 119, 2); put(defs, 18, 119, 2);
        put(toc, 0, Wad::hashName("SS_Loading.bin")); put(toc, 8, 20);
        write(dir / "rom.toc", toc); write(dir / "rom.wad", defs);
        write(dir / "ss_loading.png", png(256, 256, 255, 0, 0, true));
        write(dir / "LegalScreens_ENG.png", png(1024, 512, 0, 200, 0));
        LoadingScreen loading;
        check(loading.init(dir.string()), "loading app reads WAD sprite, loose atlas and packed background directory");
        const unsigned firstBackground = loading.background_, firstRing = loading.ring_;
        check(firstBackground && firstRing, "loading textures exist before the world asset cache is initialized");
        check(LoadingScreen::rotation(0) == 0 && LoadingScreen::rotation(1) == -4095 &&
              LoadingScreen::rotation(4) == -16383 && LoadingScreen::rotation(8) == -32767 &&
              LoadingScreen::rotation(9) == -32768,
              "rotation follows the native 16-frame rate and signed-angle saturation");

        auto pixel = [](int x, int y, int height, uint8_t r, uint8_t g, uint8_t b) {
            uint8_t p[4]{};
            glReadPixels(x, height - 1 - y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
            return std::abs((int)p[0] - r) <= 1 && std::abs((int)p[1] - g) <= 1 &&
                   std::abs((int)p[2] - b) <= 1;
        };
        for (const auto view : {std::pair<int, int>{960, 640}, {1280, 720}, {480, 720}}) {
            const int w = view.first, h = view.second;
            // GameActivity's legal splash: an ImageView fitting the 1024 x 512 image, centred, over black
            loading.renderLegal(w, h);
            const float legal = std::min(w / 1024.f, h / 512.f);
            check(pixel(w / 2, h / 2, h, 0, 200, 0), "the legal splash covers the middle of the window");
            if ((h - 512 * legal) / 2 > 1) check(pixel(w / 2, 0, h, 0, 0, 0), "the fitted legal splash leaves black bands");
            const float k = std::min(w / 480.f, h / 320.f);
            const float x = (w - 480 * k) / 2, y = (h - 320 * k) / 2;
            // Deliberately leave hostile world-render state active. The loading
            // screen must still cover the window, and restore it for the next upload.
            glViewport(3, 4, 100, 101); glEnable(GL_SCISSOR_TEST); glScissor(1, 1, 1, 1);
            glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
            glEnable(GL_DEPTH_TEST); glEnable(GL_FOG); glMatrixMode(GL_TEXTURE);
            loading.render(w, h, 0);
            check(pixel(w / 2, h / 2, h, 255, 0, 0), "native ring stays centred through landscape and portrait resize");
            check(pixel((int)(x + 20 * k), (int)(y + 20 * k), h, 0, 0, 180),
                  "background fills the native layout using its padded texture dimensions");
            if (x > 1) check(pixel(0, h / 2, h, 0, 0, 0), "wide windows receive black side borders");
            if (y > 1) check(pixel(w / 2, 0, h, 0, 0, 0), "tall windows receive black top borders");
            GLint viewport[4], mode;
            GLboolean mask[4];
            glGetIntegerv(GL_VIEWPORT, viewport); glGetIntegerv(GL_MATRIX_MODE, &mode);
            glGetBooleanv(GL_COLOR_WRITEMASK, mask);
            check(viewport[0] == 3 && viewport[1] == 4 && viewport[2] == 100 && viewport[3] == 101 &&
                  mode == GL_TEXTURE && !mask[0] && !mask[1] && !mask[2] && !mask[3] &&
                  glIsEnabled(GL_SCISSOR_TEST) && glIsEnabled(GL_DEPTH_TEST) && glIsEnabled(GL_FOG),
                  "loading rendering restores viewport, matrices, masks and world-render capabilities");
            loading.render(w, h, 2.0 / 30.0);
            check(pixel((int)(w / 2 + 54 * k), (int)(h / 2 + 54 * k), h, 0, 0, 180),
                  "wall-clock animation rotates the sprite during startup");
            loading.render(w, h, 8.0 / 30.0);
            check(pixel((int)(w / 2 + 45 * k), h / 2, h, 255, 255, 0),
                  "the native half-turn carries the asymmetric marker to the opposite side");
            loading.render(w, h, 9.0 / 30.0);
            check(pixel((int)(w / 2 + 45 * k), h / 2, h, 255, 255, 0),
                  "the ring holds its half-turn after the native transition");
            loading.render(w, h, 40.0);
            check(pixel((int)(w / 2 + 45 * k), h / 2, h, 255, 255, 0),
                  "a long desktop start-up keeps the ring at its half-turn instead of spinning it again");
            loading.render(w, h, 0); loading.render(0, h, 0); loading.render(w, 0, 0);
            check(glGetError() == GL_NO_ERROR, "resize and zero drawable dimensions produce no OpenGL errors");
        }
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE); glDisable(GL_SCISSOR_TEST);
        glDisable(GL_DEPTH_TEST); glDisable(GL_FOG); glMatrixMode(GL_MODELVIEW);
        GLuint offscreenTexture, offscreen;
        const uint8_t magenta[4] = {255, 0, 255, 255};
        glGenTextures(1, &offscreenTexture); glBindTexture(GL_TEXTURE_2D, offscreenTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, magenta);
        glGenFramebuffers(1, &offscreen); glBindFramebuffer(GL_FRAMEBUFFER, offscreen);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, offscreenTexture, 0);
        glDrawBuffer(GL_COLOR_ATTACHMENT0); glReadBuffer(GL_COLOR_ATTACHMENT0);
        check(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "synthetic offscreen target is complete");
        loading.render(1280, 720, 0);
        GLint drawFramebuffer, readFramebuffer;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFramebuffer);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFramebuffer);
        check(drawFramebuffer == (GLint)offscreen && readFramebuffer == (GLint)offscreen && pixel(0, 0, 1, 255, 0, 255) &&
              glGetError() == GL_NO_ERROR, "loading presents to the window and safely restores offscreen framebuffer attachments");
        glBindFramebuffer(GL_FRAMEBUFFER, 0); glDeleteFramebuffers(1, &offscreen); glDeleteTextures(1, &offscreenTexture);
        SDL_Window* window = SDL_GL_GetCurrentWindow();
        SDL_SetWindowSize(window, 960, 640);
        SDL_PumpEvents();
        check(loading.present() && OS_ScreenGetWidth() == 960 && OS_ScreenGetHeight() == 640,
              "a real host-window resize updates the drawable used by the next loading frame");
        SDL_SetWindowSize(window, 1280, 768);
        SDL_PumpEvents();
        loading.present();
        SDL_Event key{}; key.type = SDL_KEYDOWN; key.key.keysym.scancode = SDL_SCANCODE_ESCAPE;
        SDL_PushEvent(&key);
        check(loading.present() && !Host_PopKey(), "present pumps window events and discards startup key presses");
        SDL_Event close{}; close.type = SDL_QUIT; SDL_PushEvent(&close);
        check(!loading.present(), "close while loading immediately cancels the next startup phase");
        loading.shutdown();
        check(!glIsTexture(firstBackground) && !glIsTexture(firstRing), "loading textures are released on cancellation");
        check(!loading.init((dir / "missing").string()), "missing optional loading artwork fails safely");
        loading.render(1280, 720, 0);
        check(pixel(640, 360, 720, 0, 0, 0), "missing artwork clears stale window contents to black");
        Game cancelled;
        SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
        check(SDL_PushEvent(&close) == 1, "startup cancellation event enters the host queue");
        check(!cancelled.init(dir.string(), "") && cancelled.quitRequested,
              "Game initialization honours startup quit before reading missing gameplay tables");
        cancelled.shutdown();
        loading.shutdown();
        // Cancel between radio probes after one station has been built. Its
        // shutdown must neither save that partial list nor leak its textures.
        std::vector<uint8_t> radioTable(8 + 8 + 10 + 2 * sizeof(RadioTables::Station) + 40);
        std::memcpy(radioTable.data(), "CTWRAD2", 8);
        put(radioTable, 8, 2); put(radioTable, 12, 1);
        std::memcpy(radioTable.data() + radioTable.size() - 40, "missing.mp3", 12);
        write(dir / "radio_tables.bin", radioTable);
        put(toc, 0, Wad::hashName("SS_Radio.bin"));
        write(dir / "rom.toc", toc); write(dir / "ss_radio.png", png(256, 256, 255, 0, 0));
        std::filesystem::create_directories(dir / "saves");
        const std::vector<uint8_t> previousSave{11, 22, 33, 44, 55};
        write(dir / "saves/radio.dat", previousSave);
        OS_SetDocumentsRoot((dir / "saves").string().c_str());
        Radio radio;
        int probes = 0;
        check(!radio.init(dir.string(), [&]() { return ++probes < 2; }) && probes == 2 && radio.stationCount() == 0,
              "radio startup can cancel between stations and discard its incomplete list");
        radio.shutdown();
        std::vector<uint8_t> saved;
        check(Data_ReadAll((dir / "saves/radio.dat").string(), saved) && saved == previousSave,
              "cancelling startup during radio loading preserves the existing radio save byte for byte");
        std::filesystem::remove_all(dir);
    }
};

int main(int argc, char** argv) {
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    if (!Host_Init("Loading screen regression", 1280, 768)) {
        std::fprintf(stderr, "SKIP: no OpenGL context: %s\n", SDL_GetError());
        Host_Shutdown(); return 77;
    }
    // Optional local visual QA with the user's extracted data. CTest always uses
    // the synthetic path, and the resulting original-art screenshot stays local.
    if (argc == 4 && std::strcmp(argv[1], "--proof") == 0) {
        LoadingScreen loading;
        bool ok = loading.init(argv[2]);
        const int w = (int)OS_ScreenGetWidth(), h = (int)OS_ScreenGetHeight();
        loading.render(w, h, 2.0 / 30.0);
        std::vector<uint8_t> pixels((size_t)w * h * 4);
        glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        stbi_flip_vertically_on_write(1);
        std::vector<uint8_t> proof;
        ok = stbi_write_png_to_func([](void* out, void* data, int count) {
            auto& bytes = *(std::vector<uint8_t>*)out;
            bytes.insert(bytes.end(), (uint8_t*)data, (uint8_t*)data + count);
        }, &proof, w, h, 4, pixels.data(), w * 4) && ok;
        write(argv[3], proof);
        loading.shutdown(); Host_Shutdown();
        return ok ? 0 : 1;
    }
    LoadingScreenTestAccess::run();
    Host_Shutdown();
    return failures ? 1 : 0;
}
