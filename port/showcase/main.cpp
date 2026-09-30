// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "os/os.h"
#include "os/license.h"
#include "viewer.h"
#include "worldview.h"
#include <SDL.h>   // renames main() to SDL_main for SDL2main
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <glad/gl.h>
#include "os/pak.h"
#include "os/dxtbin.h"
#include "gfx/texload.h"

static int DumpTexture(const char* data, int id, const char* out) {
    Pak pak;
    DxtBin dxt;
    if (!pak.open(std::string(data) + "/game.pak")) return 2;
    bool haveDxt = dxt.open(std::string(data) + "/dxt.bin");
    TextureLoader tl;
    tl.init(&pak, haveDxt ? &dxt : nullptr);
    TexInfo ti;
    if (!tl.load((uint32_t)id, &ti)) return 4;
    GLint w = 0, h = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);
    std::vector<uint8_t> px((size_t)w * h * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    FILE* f = fopen(out, "wb");
    if (!f) return 3;
    fwrite(&w, 4, 1, f); fwrite(&h, 4, 1, f); fwrite(px.data(), 1, px.size(), f);
    fclose(f);
    printf("texture %d: %dx%d format 0x%04X %s\n", id, w, h, ti.format, ti.fromDxt ? "(dxt.bin)" : "(game.pak)");
    return 0;
}

// Until the game code itself is ported, the executable runs the asset viewer.
//   ctw [--data DIR] [--shot out.bmp [--index N | --gxt NAME --str N --lang N | --model ID --yaw D --pitch D --keys KEYS | --audio N |
//                                  --world X Y Z --yaw D --pitch D]]
//   ctw [--data DIR] --dumptex ID out.raw     (debug: the uploaded GL texture as i32 w, i32 h, RGBA8 pixels)
int main(int argc, char** argv) {
    if (argc == 2 && !strcmp(argv[1], "--license")) { Ctw_PrintLicense(); return 0; }
    const char* data = "data";
    const char* shot = nullptr;
    int index = 0, str = 0, lang = 0, model = -1;
    float yaw = 35.f, pitch = 20.f;
    const char* gxt = nullptr;
    int audio = -1;
    const char* keys = "";
    bool world = false;
    float wx = 0, wy = 0, wz = 0;
    for (int i = 1; i + 1 < argc; ++i) {
        if (!strcmp(argv[i], "--data")) data = argv[i + 1];
        if (!strcmp(argv[i], "--shot")) shot = argv[i + 1];
        if (!strcmp(argv[i], "--index")) index = atoi(argv[i + 1]);
        if (!strcmp(argv[i], "--gxt")) gxt = argv[i + 1];
        if (!strcmp(argv[i], "--model")) model = atoi(argv[i + 1]);
        if (!strcmp(argv[i], "--yaw")) yaw = (float)atof(argv[i + 1]);
        if (!strcmp(argv[i], "--pitch")) pitch = (float)atof(argv[i + 1]);
        if (!strcmp(argv[i], "--keys")) keys = argv[i + 1];   // viewer keys to apply before a --shot, e.g. "T"
        if (!strcmp(argv[i], "--str")) str = atoi(argv[i + 1]);
        if (!strcmp(argv[i], "--lang")) lang = atoi(argv[i + 1]);
        if (!strcmp(argv[i], "--audio")) audio = atoi(argv[i + 1]);
        if (!strcmp(argv[i], "--world") && i + 3 < argc) {   // --world X Y Z  (camera position; --yaw/--pitch aim it)
            world = true;
            wx = (float)atof(argv[i + 1]); wy = (float)atof(argv[i + 2]); wz = (float)atof(argv[i + 3]);
        }
    }
    OS_SetResourceRoot(data);
    OS_SetDocumentsRoot("saves");
    if (!Host_Init("GTA: Chinatown Wars (PC port)", 1280, 720)) return 1;
    for (int i = 1; i + 2 < argc; ++i)
        if (!strcmp(argv[i], "--dumptex")) {   // debug: --dumptex ID out.raw  (the uploaded GL texture as w, h, RGBA8)
            int r = DumpTexture(data, atoi(argv[i + 1]), argv[i + 2]);
            Host_Shutdown();
            return r;
        }
    if (!Viewer_Init(data)) {
        fprintf(stderr, "no assets found in '%s' (run scripts/setup_game.py)\n", data);
        return 2;
    }
    if (world) {
        Viewer_ShowWorld(wx, wy, wz, yaw, pitch);
        for (int i = 1; i + 1 < argc; ++i)
            if (!strcmp(argv[i], "--time")) WorldView_SetTime((float)atof(argv[i + 1]));   // hours, e.g. 21.5
        for (int f = 0; f < 45; ++f) WorldView_Tick();   // let water and animations run for 1.5 s of game time
    }
    else if (audio >= 0) Viewer_ShowAudio(audio);
    else if (model >= 0) Viewer_ShowModel(model, yaw, pitch);
    else if (gxt) Viewer_ShowText(gxt, str, lang);
    if (shot) {
        for (const char* k = keys; *k; ++k) Viewer_Key(*k);
        // In audio mode let it play briefly so the screenshot shows real mixer output (position + level).
        if (audio >= 0) {
            double t0 = OS_TimeAccurate();
            while (OS_TimeAccurate() - t0 < 1.5) { Host_PumpEvents(); Viewer_Update(); Viewer_Render(); OS_ScreenSwapBuffers(); }
        }
        if (!gxt && model < 0 && audio < 0 && !world) Viewer_Select(index);
        for (int f = 0; f < 3; ++f) { Viewer_Render(); OS_ScreenSwapBuffers(); }   // settle (textures load on first use)
        Viewer_Render();
        bool ok = Viewer_SaveScreenshot(shot);
        Viewer_Shutdown();
        Host_Shutdown();
        return ok ? 0 : 3;
    }

    bool running = true;
    while (running) {
        running = Host_PumpEvents();
        running = Viewer_Update() && running;
        Viewer_Render();
        OS_ScreenSwapBuffers();
    }
    Viewer_Shutdown();
    Host_Shutdown();
    return 0;
}
