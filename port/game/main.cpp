// GTA: Chinatown Wars - PC port, the game itself (work in progress).
//   ctw_game [--data DIR] [--mods DIR] [--shot out.bmp --frames N]
// --data defaults to "data" (the user's extracted game files), --mods to the "mods" folder next to the exe.
#include "game.h"
#include "hud.h"
#include "plugins.h"
#include "os/os.h"
#include <SDL.h>   // SDL_main
#include <glad/gl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

static bool saveBmp(const char* path) {
    int W = (int)OS_ScreenGetWidth(), H = (int)OS_ScreenGetHeight();
    int stride = (W * 3 + 3) & ~3;
    std::vector<unsigned char> px((size_t)stride * H);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadPixels(0, 0, W, H, GL_BGR, GL_UNSIGNED_BYTE, px.data());
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    unsigned char bf[54] = {'B', 'M'};
    unsigned fileSize = 54 + (unsigned)px.size(), off = 54, dib = 40;
    short planes = 1, bpp = 24;
    memcpy(bf + 2, &fileSize, 4); memcpy(bf + 10, &off, 4); memcpy(bf + 14, &dib, 4);
    memcpy(bf + 18, &W, 4); memcpy(bf + 22, &H, 4); memcpy(bf + 26, &planes, 2); memcpy(bf + 28, &bpp, 2);
    fwrite(bf, 1, 54, f);
    fwrite(px.data(), 1, px.size(), f);
    fclose(f);
    return true;
}

int main(int argc, char** argv) {
    std::string data, mods;
    const char* shot = nullptr;
    int frames = 60;
    const char* keys = nullptr;
    bool trace = false;
    struct XC { float x, y, h; int id = 0; };
    std::vector<XC> extraCars;
    float hour = -1;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--collision")) { TheGame().showCollision = true; continue; }
        if (!strcmp(argv[i], "--trace")) { trace = true; continue; }
        const char* option = argv[i];
        int values = 1;
        if (!strcmp(option, "--walk") || !strcmp(option, "--drive") || !strcmp(option, "--pos")) values = 2;
        else if (!strcmp(option, "--car")) values = 3;
        else if (!strcmp(option, "--veh")) values = 4;
        else if (strcmp(option, "--data") && strcmp(option, "--mods") && strcmp(option, "--shot") &&
                 strcmp(option, "--frames") && strcmp(option, "--keys") && strcmp(option, "--state") &&
                 strcmp(option, "--enter") && strcmp(option, "--handbrake") && strcmp(option, "--hour")) {
            fprintf(stderr, "Unknown option: %s\n", option);
            return 4;
        }
        if (i + values >= argc) {
            fprintf(stderr, "%s requires %d value(s)\n", option, values);
            return 4;
        }
        if (!strcmp(argv[i], "--data")) data = argv[i + 1];
        if (!strcmp(argv[i], "--mods")) mods = argv[i + 1];
        if (!strcmp(argv[i], "--shot")) shot = argv[i + 1];
        if (!strcmp(argv[i], "--frames")) frames = atoi(argv[i + 1]);
        if (!strcmp(argv[i], "--keys")) keys = argv[i + 1];   // shot mode: comma-separated SDL scancodes to press
        if (!strcmp(argv[i], "--walk") && i + 2 < argc) {       // shot mode: hold this input (x y, -1..1) all frames
            TheGame().scriptedInput = true;
            TheGame().scriptedMove[0] = (float)atof(argv[i + 1]);
            TheGame().scriptedMove[1] = (float)atof(argv[i + 2]);
        }
        if (!strcmp(argv[i], "--state")) TheGame().scriptedState = atoi(argv[i + 1]);   // 1 walk, 2 run, 3 sprint
        if (!strcmp(argv[i], "--enter"))   // press enter/exit on these frames (comma-separated)
            for (const char* q = argv[i + 1]; *q;) {
                TheGame().scriptedEnterFrames.push_back(atoi(q));
                while (*q && *q != ',') ++q;
                if (*q) ++q;
            }
        if (!strcmp(argv[i], "--drive") && i + 2 < argc) {   // hold throttle (1 / -1) and steer (-1 / 1) while driving
            TheGame().scriptedInput = true;
            TheGame().scriptedDrive = atoi(argv[i + 1]);
            TheGame().scriptedSteer = atoi(argv[i + 2]);
        }   // shot mode: print the player state every frame
        if (!strcmp(argv[i], "--handbrake")) TheGame().scriptedHandbrakeFrame = atoi(argv[i + 1]);   // hold it from frame N
        if (!strcmp(argv[i], "--car") && i + 3 < argc)   // testing: an extra parked car (x y heading-degrees)
            extraCars.push_back({(float)atof(argv[i + 1]), (float)atof(argv[i + 2]), (float)atof(argv[i + 3])});
        if (!strcmp(argv[i], "--veh") && i + 4 < argc)   // testing: an extra vehicle of this info id (id x y heading-degrees)
            extraCars.push_back({(float)atof(argv[i + 2]), (float)atof(argv[i + 3]), (float)atof(argv[i + 4]), atoi(argv[i + 1])});
        if (!strcmp(argv[i], "--hour")) hour = (float)atof(argv[i + 1]);   // testing: start at this time of day
        if (!strcmp(argv[i], "--pos") && i + 2 < argc) {        // start position (testing)
            TheGame().player.pos[0] = (int32_t)(atof(argv[i + 1]) * 4096.0);
            TheGame().player.pos[1] = (int32_t)(atof(argv[i + 2]) * 4096.0);
        }
        i += values;
    }
    // defaults: "data", "mods" and "saves" next to the exe (or "data" in the current folder while developing)
    char* baseC = SDL_GetBasePath();
    std::string base = baseC ? baseC : "";
    SDL_free(baseC);
    if (data.empty()) data = std::filesystem::exists(base + "data/game.pak") || !std::filesystem::exists("data") ? base + "data" : "data";
    if (mods.empty()) mods = base + "mods";
    OS_SetResourceRoot(data.c_str());
    OS_SetDocumentsRoot((base + "saves").c_str());
    if (!Host_Init("GTA: Chinatown Wars (PC port)", 1280, 720)) return 1;
    Game& game = TheGame();
    if (!game.init(data, mods)) {
        std::string msg = "Could not find the game files.\n\nPut the files from your own copy of GTA: Chinatown Wars in:\n" + data;
        fprintf(stderr, "%s\n", msg.c_str());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "GTA: Chinatown Wars", msg.c_str(), nullptr);
        Host_Shutdown();
        return 2;
    }
    if (hour >= 0) {
        game.world.timeCycle().setTime((uint32_t)(hour * 4096.f));
        game.world.timeCycle().evaluate();
    }
    for (const XC& c : extraCars) {
        int32_t at[3] = {(int32_t)(c.x * 4096), (int32_t)(c.y * 4096), 0};
        game.spawnCar(c.id, at, (int16_t)(c.h * 65536 / 360));
    }
    if (shot) {   // automated test: run N game frames, optionally press keys, save a screenshot
        for (int f = 0; f < frames; ++f) {
            game.tick();
            if (trace && game.playerCar >= 0) {
                const Vehicle& c = game.cars[game.playerCar];
                printf("f%3d car %.2f %.2f %.2f speed %.2f gear %d head %d phys %d simple %d up %d hp %d\n", f, c.pos[0] / 4096.f, c.pos[1] / 4096.f,
                       c.pos[2] / 4096.f, c.speed() / 4096.f, c.gear(), c.heading(), c.physicsActive(), c.simple(), c.up[2], c.health());
                for (size_t k = 0; k < game.cars.size(); ++k)
                    if ((int)k != game.playerCar && game.cars[k].physicsActive())
                        printf("      car%zu %.2f %.2f %.2f speed %.2f head %d\n", k, game.cars[k].pos[0] / 4096.f, game.cars[k].pos[1] / 4096.f,
                               game.cars[k].pos[2] / 4096.f, game.cars[k].speed() / 4096.f, game.cars[k].heading());
            } else if (trace) {
                float p[3];
                game.player.posf(p);
                printf("f%3d pos %.3f %.3f %.3f lvl %d ground %d vz %.2f head %d cam %d pitch %d\n", f, p[0], p[1], p[2], game.player.level(),
                       game.player.onGround(), game.player.vel[2] / 4096.f, game.player.heading(), (int16_t)game.camera.yaw, (int16_t)game.camera.pitch());
            }
        }
        if (keys)
            for (const char* p = keys; *p;) {
                Plugins_Key(atoi(p));
                while (*p && *p != ',') ++p;
                if (*p) ++p;
            }
        game.render((int)OS_ScreenGetWidth(), (int)OS_ScreenGetHeight());
        if (trace) {
            printf("cars %zu:", game.cars.size());
            for (const Vehicle& c : game.cars) printf(" [%s %.1f %.1f]", game.vehicleInfos[c.infoId].name().c_str(), c.pos[0] / 4096.f, c.pos[1] / 4096.f);
            printf("\n");
        }
        bool ok = saveBmp(shot);
        game.shutdown();
        Host_Shutdown();
        return ok ? 0 : 3;
    }
    game.run();
    game.shutdown();
    Host_Shutdown();
    return 0;
}
