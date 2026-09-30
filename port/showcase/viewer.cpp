// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// Texture browser: cycles through every texture in DXT.bin and every loose PNG in the data dir.
#include "viewer.h"
#include "os/os.h"
#include "os/dxtbin.h"
#include "os/gamefs.h"
#include "gfx/spriteset.h"
#include "gfx/font.h"
#include "text/gxt.h"
#include "modelview.h"
#include "worldview.h"
#include "audio/audio.h"
#include <SDL.h>
#include <glad/gl.h>
#include <filesystem>
#include <cstdio>
#include <string>
#include <vector>
#include <algorithm>
#include <cstring>
#include <stb_image.h>

namespace fs = std::filesystem;

struct Item { std::string label; int dxtId; std::string png; };
static std::vector<Item> g_items;
static DxtBin g_dxt;
static GameFs g_wad;
static bool g_haveWad = false;
static SpriteSet g_sprites;          // sprite layout for the current sheet, if it has one
static bool g_overlay = true;
static int g_hover = -1;
static std::string g_baseTitle;

static void setTitle() {
    char t[300];
    if (g_hover >= 0) {
        const SpriteDef& d = g_sprites.sprites()[g_hover];
        snprintf(t, sizeof t, "%s | sprite #%d  %dx%d at (%d,%d)  pivot(%d,%d)", g_baseTitle.c_str(),
                 g_hover, d.w, d.h, d.x, d.y, d.offX, d.offY);
    } else {
        snprintf(t, sizeof t, "%s", g_baseTitle.c_str());
    }
    Host_SetTitle(t);
}
static int g_cur = -1;
static GLuint g_tex = 0;
static int g_tw = 1, g_th = 1;

// ---------------------------------------------------------------- text mode (.gxt + game font)
static int g_mode = 0;               // 0 = textures/sprites, 1 = text, 2 = models, 3 = audio, 4 = world
static bool g_modelsOk = false, g_worldOk = false;
static BitmapFont g_font, g_fontJp;
static bool g_fontOk = false, g_fontJpOk = false;

// ---------------------------------------------------------------- audio mode (the game's MP3 tracks)
static std::vector<std::string> g_tracks;
static std::vector<AudioTrackInfo> g_trackInfo;
static int g_track = 0;
static bool g_audioOk = false;
static void audioTitle() {
    char t[300];
    const AudioTrackInfo& i = g_trackInfo[g_track];
    snprintf(t, sizeof t, "CTW audio [%d/%zu] %s  %d Hz %s  %d:%02d / %d:%02d  %s   (Tab: world, Left/Right: track, Space: play/pause)",
             g_track + 1, g_tracks.size(), fs::path(g_tracks[g_track]).filename().string().c_str(), i.sampleRate,
             i.channels == 1 ? "mono" : "stereo", (int)Audio_MusicPosition() / 60, (int)Audio_MusicPosition() % 60,
             (int)i.seconds / 60, (int)i.seconds % 60, Audio_MusicPlaying() ? "playing" : "stopped");
    Host_SetTitle(t);
}
static void renderAudio() {
    int W = (int)OS_ScreenGetWidth(), H = (int)OS_ScreenGetHeight();
    glViewport(0, 0, W, H);
    glClearColor(0.10f, 0.10f, 0.12f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0, W, H, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    float scale = H / 480.f, x = 40 * scale, y = 40 * scale;
    if (g_fontOk) {
        for (size_t i = 0; i < g_tracks.size(); ++i) {
            std::u16string n;
            for (char c : fs::path(g_tracks[i]).filename().string()) n += (char16_t)c;
            if ((int)i == g_track) glColor4f(1, 0.85f, 0.2f, 1); else glColor4f(0.8f, 0.8f, 0.82f, 1);
            float col = (i < 17) ? 0 : W / 2.f, row = (float)(i % 17);
            g_font.draw(n, x + col, y + row * 22 * scale, scale);
        }
    }
    // position bar + level meter for the selected track
    const AudioTrackInfo& inf = g_trackInfo[g_track];
    float bw = W - 2 * x, by = H - 60 * scale, frac = inf.seconds > 0 ? (float)(Audio_MusicPosition() / inf.seconds) : 0;
    frac = std::min(1.f, frac);
    glDisable(GL_TEXTURE_2D);
    glColor4f(0.25f, 0.25f, 0.28f, 1);
    glRectf(x, by, x + bw, by + 8 * scale);
    glColor4f(1, 0.85f, 0.2f, 1);
    glRectf(x, by, x + bw * frac, by + 8 * scale);
    glColor4f(0.3f, 0.8f, 0.4f, 1);
    glRectf(x, by + 16 * scale, x + bw * Audio_MusicLevel(), by + 24 * scale);
    static int tick = 0;
    if (++tick % 15 == 0) audioTitle();   // refresh the time readout twice a second
}
static std::string g_dataDir;
static std::vector<std::string> g_gxtNames;      // mission-text names without the language prefix
static const char g_langs[] = {'e', 'f', 'g', 'i', 's', 'j'};
static const char* g_langNames[] = {"English", "French", "German", "Italian", "Spanish", "Japanese"};
static int g_lang = 0, g_gxtIdx = 0, g_strIdx = 0;
static GxtFile g_gxt;
static void loadCurrent();

static void loadGxt() {
    if (g_gxtNames.empty()) return;
    std::string path = g_dataDir + "/" + g_langs[g_lang] + "_" + g_gxtNames[g_gxtIdx] + ".gxt";
    if (!g_gxt.load(path)) g_gxt = GxtFile();
    if (g_strIdx >= (int)g_gxt.count()) g_strIdx = (int)g_gxt.count() - 1;
    if (g_strIdx < 0) g_strIdx = 0;
    char t[300];
    snprintf(t, sizeof t, "CTW text: %s / %s  string %d of %zu   (Tab: models, Left/Right: file, Up/Down: string, L: language)",
             g_langNames[g_lang], g_gxtNames[g_gxtIdx].c_str(), g_strIdx + 1, g_gxt.count());
    Host_SetTitle(t);
}

static void renderText() {
    int W = (int)OS_ScreenGetWidth(), H = (int)OS_ScreenGetHeight();
    glViewport(0, 0, W, H);
    glClearColor(0.10f, 0.10f, 0.12f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0, W, H, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    if (!g_fontOk || g_gxt.count() == 0) return;
    const BitmapFont& font = (g_langs[g_lang] == 'j' && g_fontJpOk) ? g_fontJp : g_font;
    float scale = H / 480.f;
    float margin = 40.f * scale, y = margin;
    glColor4f(1, 0.85f, 0.2f, 1);
    std::u16string head;
    for (char c : g_gxtNames[g_gxtIdx]) head += (char16_t)c;
    head += u"  #";
    for (char c : std::to_string(g_strIdx)) head += (char16_t)c;
    y += g_font.drawWrapped(head, margin, y, W - 2 * margin, scale) + 12 * scale;
    glColor4f(1, 1, 1, 1);
    font.drawWrapped(g_gxt.get((size_t)g_strIdx), margin, y, W - 2 * margin, scale * 1.25f);
}

static void loadCurrent() {
    if (g_tex) { glDeleteTextures(1, &g_tex); g_tex = 0; }
    const Item& it = g_items[g_cur];
    glGenTextures(1, &g_tex);
    glBindTexture(GL_TEXTURE_2D, g_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (it.dxtId >= 0) {
        DxtTexture t;
        if (g_dxt.get(it.dxtId, t)) {
            g_tw = t.width; g_th = t.height;
            glCompressedTexImage2D(GL_TEXTURE_2D, 0, t.glFormat, t.width, t.height, 0,
                                   (GLsizei)t.data.size(), t.data.data());
        }
    } else {
        int w, h, n;
        unsigned char* px = stbi_load(it.png.c_str(), &w, &h, &n, 4);
        if (px) {
            g_tw = w; g_th = h;
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
            stbi_image_free(px);
        }
    }
    // A sheet like ss_hud.png is described by SS_Hud.bin inside ROM.WAD.
    g_sprites = SpriteSet();
    g_hover = -1;
    if (it.dxtId < 0 && g_haveWad) {
        std::string bin = it.label.substr(0, it.label.rfind('.')) + ".bin";
        std::vector<uint8_t> data;
        if (g_wad.read(bin.c_str(), data)) g_sprites.parse(data);
    }
    char title[256];
    snprintf(title, sizeof title, "CTW viewer [%d/%zu] %s  %dx%d  sprites:%zu  (Tab: text, Left/Right, PgUp/PgDn, R: outlines, Esc)",
             g_cur + 1, g_items.size(), it.label.c_str(), g_tw, g_th, g_sprites.sprites().size());
    g_baseTitle = title;
    setTitle();
}

bool Viewer_Init(const std::string& dataDir) {
    g_modelsOk = ModelView_Init(dataDir);
    g_worldOk = WorldView_Init(dataDir);
    g_haveWad = g_wad.open(dataDir);
    g_dataDir = dataDir;
    {   // game font: glyph table from ROM.WAD, atlas from the loose PNG
        std::vector<uint8_t> bin;
        if (g_haveWad && g_wad.read("IPhone_Hel_16x16.bin", bin))
            g_fontOk = g_font.load(bin, dataDir + "/iphone_hel_16x16.png");
        if (g_haveWad && g_wad.read("GTACTWJapanese.bin", bin)) {
            g_fontJpOk = g_fontJp.load(bin, dataDir + "/gtactwjapanese.png");
            g_fontJp.setJapanese(true);
        }
        std::error_code ec2;
        for (auto& e : fs::directory_iterator(dataDir, ec2)) {
            std::string n = e.path().filename().string();
            if (n.size() > 6 && n[0] == 'e' && n[1] == '_' && e.path().extension() == ".gxt")
                g_gxtNames.push_back(n.substr(2, n.size() - 6));
        }
        std::sort(g_gxtNames.begin(), g_gxtNames.end());
    }
    std::error_code eca;
    for (auto& e : fs::directory_iterator(dataDir, eca))
        if (e.path().extension() == ".mp3") g_tracks.push_back(e.path().string());
    std::sort(g_tracks.begin(), g_tracks.end());
    for (auto& t : g_tracks) { AudioTrackInfo i; Audio_Probe(t, &i); g_trackInfo.push_back(i); }
    g_audioOk = !g_tracks.empty() && Audio_Init();
    if (g_dxt.open(dataDir + "/dxt.bin"))
        for (int id : g_dxt.ids()) g_items.push_back({"dxt.bin id " + std::to_string(id), id, ""});
    std::vector<Item> pngs;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(dataDir, ec))
        if (e.path().extension() == ".png")
            pngs.push_back({e.path().filename().string(), -1, e.path().string()});
    std::sort(pngs.begin(), pngs.end(), [](const Item& a, const Item& b) { return a.label < b.label; });
    g_items.insert(g_items.end(), pngs.begin(), pngs.end());
    if (g_items.empty()) return false;
    g_cur = 0;
    loadCurrent();
    return true;
}

void Viewer_Select(int index) {
    if (g_items.empty()) return;
    g_cur = ((index % (int)g_items.size()) + (int)g_items.size()) % (int)g_items.size();
    loadCurrent();
}

bool Viewer_SaveScreenshot(const char* path) {
    int W = (int)OS_ScreenGetWidth(), H = (int)OS_ScreenGetHeight();
    int stride = (W * 3 + 3) & ~3;                 // BMP rows are padded to 4 bytes
    std::vector<unsigned char> px((size_t)stride * H);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadPixels(0, 0, W, H, GL_BGR, GL_UNSIGNED_BYTE, px.data());
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    unsigned char bf[54] = {'B', 'M'};
    unsigned fileSize = 54 + (unsigned)px.size();
    memcpy(bf + 2, &fileSize, 4);
    unsigned off = 54, dib = 40; short planes = 1, bpp = 24;
    memcpy(bf + 10, &off, 4); memcpy(bf + 14, &dib, 4);
    memcpy(bf + 18, &W, 4); memcpy(bf + 22, &H, 4);
    memcpy(bf + 26, &planes, 2); memcpy(bf + 28, &bpp, 2);
    fwrite(bf, 1, 54, f);
    fwrite(px.data(), 1, px.size(), f);   // bottom-up rows, as BMP expects
    fclose(f);
    return true;
}

bool Viewer_Update() {
    bool quit = false;
    static double lastT = OS_TimeAccurate();
    double now = OS_TimeAccurate();
    float dt = (float)std::min(0.1, now - lastT);
    lastT = now;
    if (g_mode == 2) ModelView_Update();
    if (g_mode == 4) WorldView_Update(dt);
    int n = (int)g_items.size();
    while (int k = Host_PopKey()) {
        if (k == SDL_SCANCODE_TAB) {
            g_mode = (g_mode + 1) % 5;
            if (g_mode == 2 && !g_modelsOk) g_mode = 3;
            if (g_mode == 3 && !g_audioOk) g_mode = 4;
            if (g_mode == 4 && !g_worldOk) g_mode = 0;
            if (g_mode == 1) loadGxt();
            else if (g_mode == 0) loadCurrent();
            else if (g_mode == 2) ModelView_Enter();
            else if (g_mode == 3) audioTitle();
            else WorldView_Enter();
            continue;
        }
        if (g_mode == 4) {
            if (k == SDL_SCANCODE_ESCAPE) quit = true;
            else WorldView_Key(k);
            continue;
        }
        if (g_mode == 2) {
            if (k == SDL_SCANCODE_ESCAPE) quit = true;
            else ModelView_Key(k);
            continue;
        }
        if (g_mode == 3) {
            int nt = (int)g_tracks.size();
            bool play = false;
            if (k == SDL_SCANCODE_ESCAPE) quit = true;
            else if (k == SDL_SCANCODE_RIGHT) { g_track = (g_track + 1) % nt; play = Audio_MusicPlaying(); }
            else if (k == SDL_SCANCODE_LEFT) { g_track = (g_track + nt - 1) % nt; play = Audio_MusicPlaying(); }
            else if (k == SDL_SCANCODE_SPACE) {
                if (Audio_MusicPlaying()) Audio_StopMusic(); else play = true;
            }
            if (play) Audio_PlayMusic(g_tracks[g_track], true);
            else if (k != SDL_SCANCODE_SPACE && k != SDL_SCANCODE_ESCAPE) Audio_StopMusic();
            audioTitle();
            continue;
        }
        if (g_mode == 1) {
            int nf = (int)g_gxtNames.size();
            if (k == SDL_SCANCODE_ESCAPE) quit = true;
            else if (k == SDL_SCANCODE_RIGHT && nf) { g_gxtIdx = (g_gxtIdx + 1) % nf; g_strIdx = 0; loadGxt(); }
            else if (k == SDL_SCANCODE_LEFT && nf) { g_gxtIdx = (g_gxtIdx + nf - 1) % nf; g_strIdx = 0; loadGxt(); }
            else if (k == SDL_SCANCODE_DOWN) { ++g_strIdx; if (g_strIdx >= (int)g_gxt.count()) g_strIdx = 0; loadGxt(); }
            else if (k == SDL_SCANCODE_UP) { --g_strIdx; if (g_strIdx < 0) g_strIdx = (int)g_gxt.count() - 1; loadGxt(); }
            else if (k == SDL_SCANCODE_L) { g_lang = (g_lang + 1) % (int)sizeof g_langs; loadGxt(); }
            continue;
        }
        int prev = g_cur;
        if (k == SDL_SCANCODE_ESCAPE) quit = true;
        else if (k == SDL_SCANCODE_RIGHT) g_cur = (g_cur + 1) % n;
        else if (k == SDL_SCANCODE_LEFT) g_cur = (g_cur + n - 1) % n;
        else if (k == SDL_SCANCODE_PAGEUP) g_cur = (g_cur + 10) % n;
        else if (k == SDL_SCANCODE_PAGEDOWN) g_cur = (g_cur + n - 10) % n;
        else if (k == SDL_SCANCODE_R) g_overlay = !g_overlay;
        if (g_cur != prev) loadCurrent();
    }
    return !quit;
}

void Viewer_ShowModel(int id, float yaw, float pitch) {
    g_mode = 2;
    ModelView_Select(id);
    ModelView_SetCamera(yaw, pitch);
    ModelView_Enter();
}

void Viewer_ShowAudio(int track) {
    if (!g_audioOk) return;
    g_mode = 3;
    g_track = std::max(0, std::min(track, (int)g_tracks.size() - 1));
    Audio_PlayMusic(g_tracks[g_track], true);
    audioTitle();
}

void Viewer_Shutdown() { Audio_Shutdown(); }

void Viewer_Key(char letter) {
    if (letter < 'A' || letter > 'Z') return;
    int sc = SDL_SCANCODE_A + (letter - 'A');
    if (g_mode == 2) ModelView_Key(sc);
    if (g_mode == 4) WorldView_Key(sc);
}

void Viewer_ShowText(const char* name, int strIdx, int lang) {  // lang: index into g_langs
    g_mode = 1;
    g_lang = lang;
    for (size_t i = 0; i < g_gxtNames.size(); ++i)
        if (g_gxtNames[i] == name) g_gxtIdx = (int)i;
    g_strIdx = strIdx;
    loadGxt();
}

void Viewer_ShowWorld(float x, float y, float z, float yaw, float pitch) {
    if (!g_worldOk) return;
    g_mode = 4;
    WorldView_SetCamera(x, y, z, yaw, pitch);
    WorldView_LoadAllNow();
    WorldView_Enter();
}

void Viewer_Render() {
    if (g_mode == 4) { WorldView_Render(); return; }
    if (g_mode == 2) { ModelView_Render(); return; }
    if (g_mode == 3) { renderAudio(); return; }
    if (g_mode == 1) { renderText(); return; }
    int W = (int)OS_ScreenGetWidth(), H = (int)OS_ScreenGetHeight();
    glViewport(0, 0, W, H);
    glClearColor(0.16f, 0.16f, 0.18f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0, W, H, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    float s = std::min((float)W / g_tw, (float)H / g_th);
    float w = g_tw * s, h = g_th * s, x = (W - w) / 2, y = (H - h) / 2;
    glDisable(GL_TEXTURE_2D);
    glColor4f(0.3f, 0.3f, 0.32f, 1.f);       // backdrop so transparent areas are visible
    glBegin(GL_QUADS);
    glVertex2f(x, y); glVertex2f(x + w, y); glVertex2f(x + w, y + h); glVertex2f(x, y + h);
    glEnd();
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, g_tex);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(1, 1, 1, 1);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex2f(x, y);
    glTexCoord2f(1, 0); glVertex2f(x + w, y);
    glTexCoord2f(1, 1); glVertex2f(x + w, y + h);
    glTexCoord2f(0, 1); glVertex2f(x, y + h);
    glEnd();
    glDisable(GL_TEXTURE_2D);

    // Sprite outlines. Layout coordinates are in the sheet's original pixel space; the PNG is k times that.
    const auto& spr = g_sprites.sprites();
    if (g_overlay && !spr.empty()) {
        float k = (float)SpriteSet::pngScaleFor(g_items[g_cur].label);
        int mx, my;
        Host_GetMouse(&mx, &my);
        int hover = -1;
        for (size_t i = 0; i < spr.size(); ++i) {
            float x0 = x + spr[i].x * k * s, y0 = y + spr[i].y * k * s;
            float x1 = x0 + spr[i].w * k * s, y1 = y0 + spr[i].h * k * s;
            bool hit = mx >= x0 && mx < x1 && my >= y0 && my < y1;
            if (hit) hover = (int)i;
            glColor4f(hit ? 1.f : 0.9f, hit ? 1.f : 0.25f, hit ? 0.2f : 0.25f, 1.f);
            glBegin(GL_LINE_LOOP);
            glVertex2f(x0, y0); glVertex2f(x1, y0); glVertex2f(x1, y1); glVertex2f(x0, y1);
            glEnd();
        }
        if (hover != g_hover) { g_hover = hover; setTitle(); }
    } else if (g_hover != -1) {
        g_hover = -1;
        setTitle();
    }
}
