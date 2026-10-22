// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "weaponhud.h"
#include "game.h"
#include "hud.h"
#include "weapons.h"
#include "plugins.h"
#include "audio/audio.h"
#include "gfx/spriteset.h"
#include "os/datafile.h"
#include "os/gamefs.h"
#include "os/os.h"
#include <SDL.h>
#include <glad/gl.h>
#include <stb_image.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
struct Sheet {   // a sprite set and its loose 2x PNG sheet
    SpriteSet set;
    GLuint texture = 0;
    int w = 0, h = 0;
    void clear() {
        if (texture) glDeleteTextures(1, &texture);
        texture = 0;
        w = h = 0;
        set = SpriteSet{};
    }
    bool load(const GameFs& fs, const std::string& dir, const char* bin, const char* png) {
        std::vector<uint8_t> data, pixels;
        if (!fs.read(bin, data) || !set.parse(data) || !Data_ReadAll(dir + "/" + png, pixels)) return false;
        int channels;
        unsigned char* rgba = stbi_load_from_memory(pixels.data(), (int)pixels.size(), &w, &h, &channels, 4);
        if (!rgba) return false;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        stbi_image_free(rgba);
        return true;
    }
    const SpriteDef* def(int index) const {
        return index >= 0 && index < (int)set.sprites().size() ? &set.sprites()[index] : nullptr;
    }
    // drawn in pixels: the anchor (cSprite position) at (x, y), offsets and size times scale
    void draw(int index, float x, float y, float scale, float alpha) const {
        const SpriteDef* s = def(index);
        if (!s || !texture || !s->w || !s->h || w <= 0 || h <= 0) return;
        const float u0 = s->x * 2.f / w, v0 = s->y * 2.f / h, u1 = (s->x + s->w) * 2.f / w, v1 = (s->y + s->h) * 2.f / h;
        const float x0 = x - s->offX * scale, y0 = y - s->offY * scale, x1 = x0 + s->w * scale, y1 = y0 + s->h * scale;
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, texture);
        glColor4f(1, 1, 1, alpha);
        glBegin(GL_QUADS);
        glTexCoord2f(u0, v0); glVertex2f(x0, y0);
        glTexCoord2f(u1, v0); glVertex2f(x1, y0);
        glTexCoord2f(u1, v1); glVertex2f(x1, y1);
        glTexCoord2f(u0, v1); glVertex2f(x0, y1);
        glEnd();
        glDisable(GL_TEXTURE_2D);
        glColor4f(1, 1, 1, 1);
    }
};

Sheet gWeapons, gAlways, gHud;
bool gOk = false;
int16_t gButtonX = 860, gButtonY = 600;   // buttonConfig entry 15 (the weapon button on foot)
bool gWheel = false;
Game* gWheelGame = nullptr;                 // the wheel owns this game's pause until it closes
bool gSoundWasPaused = false;
int gPressed = -1;                         // the slot pressed on the wheel
bool gWasDown = false;
bool gPointerPressed = false;               // a press that began after the wheel opened

// Gui::cHudWeaponSelect::GetSpriteFromWeaponType: true = SS_Weapons (the wheel), false = SS_AlwaysLoaded (button)
int spriteFor(int type, bool big) {
    static const int8_t kBig[26] = {13, 21, 2, 28, 5, 22, 11, 9, 31, 20, 26, 25, 8, 0, 6, 18, 14, 24, 27, 30, 17, 19, 15, 23, -1, 10};
    static const int8_t kSmall[26] = {53, 60, 42, 67, 45, 61, 51, 49, 70, 59, 65, 64, 48, 40, 46, 57, 54, 63, 66, 69, 56, 58, 55, 62, -1, 50};
    if (type < 0 || type > 25) return -1;
    return big ? kBig[type] : kSmall[type];
}

// the wheel's slot positions (0x4836F0): slots 0..6, 7 the secondary weapon, 8 the held weapon in the middle
const int16_t kSlotPos[9][2] = {{512, 152}, {647, 208}, {704, 344}, {647, 479}, {512, 536}, {376, 479}, {320, 344}, {376, 208}, {512, 344}};

struct Space {   // HUD units -> pixels
    float k, extra;
    Space(int W, int H) {
        k = H / 768.f;
        const int wide = H ? 768 * W / H : 1024;
        extra = (float)(((wide - 1024) >= 0 ? wide - 1024 : wide - 1023) >> 1);
    }
    float x(float hudX) const { return (hudX + extra) * k; }   // HUD x already in the 1024-wide frame
    float y(float hudY) const { return hudY * k; }
};

std::string ammoText(const Weapon& w, bool clipOnly) {   // "%d/%d": the clip and the rest
    const int ammo = w.ammoCount(), clip = w.ammoInClip();
    if (ammo == -1) return {};
    if (clipOnly || (uint8_t)Weapons_Info(w.type).clip) return std::to_string(clip) + "/" + std::to_string(ammo - clip);
    return std::to_string(ammo - clip);
}

void textCentred(float x, float y, float scale, const std::string& s, float alpha) {
    if (s.empty()) return;
    const uint32_t a = (uint32_t)std::clamp(alpha * 255.f, 0.f, 255.f);
    Hud_Text(x - Hud_TextWidth(s, scale) * 0.5f, y, scale, 0xFFFFFF00u | a, s);
}

int hit(const Space& sp, int mx, int my, const WeaponManager& m) {   // GetWeaponHit: 64 x 80 around each icon
    for (int slot = 0; slot < 9; ++slot) {
        if (slot == 7) continue;   // (PositionWeaponSprite never places slot 7: the secondary icon has no hit box)
        const Weapon* w = slot == 8 ? m.current() : m.inSlot(slot);
        if (!w || !w->ammoCount()) continue;
        const float cx = sp.x(kSlotPos[slot][0]), cy = sp.y(kSlotPos[slot][1]);
        if (std::fabs(mx - cx) < 32 * sp.k && std::fabs(my - cy) < 40 * sp.k) return slot;
    }
    return -1;
}
}   // namespace

bool WeaponHud_Init(const std::string& dataDir) {
    WeaponHud_Shutdown();
    gButtonX = 860;
    gButtonY = 600;
    GameFs fs;
    if (!fs.open(dataDir)) return false;
    gOk = gWeapons.load(fs, dataDir, "SS_Weapons.bin", "ss_weapons.png") &&
          gAlways.load(fs, dataDir, "SS_AlwaysLoaded.bin", "ss_alwaysloaded.png") &&
          gHud.load(fs, dataDir, "SS_Hud.bin", "ss_hud.png");
    if (!gOk) { WeaponHud_Shutdown(); return false; }
    // cIPhonePad::LoadInitialButtons: the default buttonConfig (a byte, then 20 x {i32 id, i16 x, i16 y, f32})
    std::vector<uint8_t> cfg;
    if (Data_ReadAll(dataDir + "/buttonconfig", cfg) && cfg.size() >= 1 + 240) {
        memcpy(&gButtonX, &cfg[1 + 15 * 12 + 4], 2);
        memcpy(&gButtonY, &cfg[1 + 15 * 12 + 6], 2);
    }
    gButtonX = (int16_t)(gButtonX - 25);
    gButtonY = (int16_t)(gButtonY - 25);
    return gOk;
}

void WeaponHud_Shutdown() {
    if (gWheelGame) WeaponHud_SetWheel(*gWheelGame, false);
    gOk = false;
    gWeapons.clear();
    gAlways.clear();
    gHud.clear();
    gPressed = -1;
    gWasDown = gPointerPressed = false;
}

bool WeaponHud_Ok() { return gOk; }
bool WeaponHud_WheelOpen() { return gWheel; }

void WeaponHud_SetWheel(Game& g, bool open) {
    if (open == gWheel) return;
    if (open && (!gOk || !Weapons_Ok() || g.playerCar >= 0 || g.player.dead || g.uiPaused)) return;
    gWheel = open;
    gPressed = -1;
    gWasDown = Host_MouseDown(0);
    gPointerPressed = false;
    if (open) {
        gWheelGame = &g;
        gSoundWasPaused = Audio_SfxPaused();
        g.uiPaused = true;   // the wheel shares the pause panel (cGame::Pause)
        Audio_SetSfxPaused(true);
    } else {
        if (gWheelGame) gWheelGame->uiPaused = false;
        gWheelGame = nullptr;
        Audio_SetSfxPaused(gSoundWasPaused);
    }
    Plugins_RefreshMouseCapture();
}

bool WeaponHud_Key(Game& g, int k) {
    if (k == SDL_SCANCODE_TAB) {
        const bool wasOpen = gWheel;
        WeaponHud_SetWheel(g, !gWheel);
        return wasOpen || gWheel;   // failed opens leave Tab available to other UI
    }
    if (gWheel && k == SDL_SCANCODE_ESCAPE) { WeaponHud_SetWheel(g, false); return true; }
    return gWheel;   // the paused game takes no other keys while the wheel is open
}

bool WeaponHud_DrawIcon(int type, float x, float y, float w, float h, float alpha) {
    const int idx = spriteFor(type, true);
    const SpriteDef* s = gWeapons.def(idx);
    if (!gOk || !s || !s->w || !s->h || !(w > 0) || !(h > 0) || !std::isfinite(w) ||
        !std::isfinite(h) || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(alpha)) return false;
    const float scale = std::min(w / s->w, h / s->h);
    // centre the picture in the box: the anchor sits at the picture's centre plus its offset
    const float cx = x + w * 0.5f - (s->w * 0.5f - s->offX) * scale, cy = y + h * 0.5f - (s->h * 0.5f - s->offY) * scale;
    gWeapons.draw(idx, cx, cy, scale, std::clamp(alpha, 0.f, 1.f));
    return true;
}

void WeaponHud_Render(Game& g, int W, int H) {
    if (gWheel && (!gOk || !Weapons_Ok() || g.playerCar >= 0 || g.player.dead)) WeaponHud_SetWheel(g, false);
    if (!gOk || !Weapons_Ok() || W <= 0 || H <= 0) return;
    const Space sp(W, H);
    const WeaponManager& m = g.playerWeapons;
    const Weapon* cur = m.current();
    int mx, my;
    Host_GetMouse(&mx, &my);
    const bool down = Host_MouseDown(0), clicked = Plugins_HostApi().mouse_clicked(0) != 0;
    // the weapon button (on foot)
    if (cur && g.playerCar < 0 && !g.player.dead && !g.uiPaused && !gWheel) {
        const float scale = (float)(int)((60.f / 42.f * 0.8f) * 4096.f + 0.5f) / 4096.f;   // SetSpriteScaleNoClamp
        const float bx = sp.x(gButtonX), by = sp.y(gButtonY);
        gAlways.draw(spriteFor(cur->type, false), bx, by, scale * sp.k, 1.f);
        textCentred(bx, by + 10 * sp.k, 1.5f * sp.k, ammoText(*cur, false), 1.f);
    }
    if (!gWheel) return;
    // the wheel
    const int over = hit(sp, mx, my, m);
    const bool completeClick = clicked && !down && !gWasDown;
    if ((down && !gWasDown) || completeClick) {          // OnPress: SelectWeapon(params, false)
        gPressed = over;
        gPointerPressed = true;
    }
    if (!down && (gWasDown || completeClick) && gPointerPressed) { // OnRelease: SelectWeapon(params, true)
        gPointerPressed = false;
        if (over >= 0 && over == gPressed) {
            const Weapon* w = over == 8 ? m.current() : m.inSlot(over);
            if (w) g.playerWeapons.setUse(w->type, false);   // cPda::TsCommand(1, type)
            WeaponHud_SetWheel(g, false);
            return;
        }
        if (over < 0) { WeaponHud_SetWheel(g, false); return; }   // a tap outside the slots closes it
        gPressed = -1;
    }
    gWasDown = down;
    for (int slot = 0; slot < 9; ++slot) {
        if (slot == 7) continue;
        const Weapon* w = slot == 8 ? cur : m.inSlot(slot);
        const float cx = sp.x(kSlotPos[slot][0]), cy = sp.y(kSlotPos[slot][1]);
        if (slot < 8) {   // the slot background: lit for the held slot, or the one being pressed
            const bool lit = slot == 8 || slot == m.cur || slot == gPressed;
            gHud.draw(26, cx - 44 * sp.k, cy - 44 * sp.k, sp.k, lit ? 1.f : 0.25f);
        }
        if (!w || !w->ammoCount()) continue;
        gWeapons.draw(spriteFor(w->type, true), cx, cy, sp.k, 1.f);
        textCentred(cx, cy + 23 * sp.k, 1.33f * sp.k, ammoText(*w, true), 1.f);
    }
    if (const Weapon* s = m.inSlot(m.secondary); s && m.secondary >= 8 && s->ammoCount()) {   // the thrown weapon
        const bool type25 = s->type == 25;
        const float cx = sp.x(type25 ? 356 : 376), cy = sp.y(type25 ? 200 : 208);
        gHud.draw(1, sp.x(376), sp.y(208), sp.k, 1.f);
        gWeapons.draw(spriteFor(s->type, true), cx, cy, sp.k, 1.f);
        if (s->ammoCount() != -1) textCentred(cx, cy + 24 * sp.k, 1.33f * sp.k, std::to_string(s->ammoCount()), 1.f);
    }
}
