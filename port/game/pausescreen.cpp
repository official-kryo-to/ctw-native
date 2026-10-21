// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "pausescreen.h"
#include "game.h"
#include "hud.h"
#include "plugins.h"
#include "sound.h"
#include "os/datafile.h"
#include "os/gamefs.h"
#include "os/os.h"
#include "text/gxt.h"
#include <glad/gl.h>
#include <stb_image.h>

bool PauseScreen::init(const std::string& dir) {
    shutdown();
    GameFs fs;
    std::vector<uint8_t> defs, png;
    if (!fs.open(dir) || !fs.read("SS_Hud.bin", defs) || !sprites_.parse(defs) ||
        sprites_.sprites().size() <= 27 || !Data_ReadAll(dir + "/ss_hud.png", png)) return false;
    int channels;
    auto* pixels = stbi_load_from_memory(png.data(), (int)png.size(), &textureWidth_, &textureHeight_, &channels, 4);
    if (!pixels) return false;
    glGenTextures(1, &texture_);
    glBindTexture(GL_TEXTURE_2D, texture_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, textureWidth_, textureHeight_, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    stbi_image_free(pixels);
    GxtFile labels;
    if (labels.load(dir + "/e_main.gxt") && labels.count() > 0x119) {
        for (char16_t c : labels.get(0x119)) {
            if (!c || c >= 0xFE00) continue;
            if (c < 128) quitLabel_ += (char)c;
            else if (c < 2048) {
                quitLabel_ += (char)(0xC0 | (c >> 6)); quitLabel_ += (char)(0x80 | (c & 63));
            } else {
                quitLabel_ += (char)(0xE0 | (c >> 12)); quitLabel_ += (char)(0x80 | ((c >> 6) & 63));
                quitLabel_ += (char)(0x80 | (c & 63));
            }
        }
    }
    return true;
}

void PauseScreen::shutdown() {
    if (texture_) glDeleteTextures(1, &texture_);
    texture_ = 0;
    textureWidth_ = textureHeight_ = 0;
    sprites_ = SpriteSet{};
    quitLabel_.clear();
}

void PauseScreen::resumeCentre(int width, int height, float& x, float& y) {
    // LoadPauseScreen places SS_Hud[27] at (959 + half aspect extension, 69) in the
    // centred 1024 x 768 projection. Its distance from the right edge is 65 units.
    const float k = height / 768.f;
    const int extension = (height > 0 ? 768 * width / height : 1024) - 1024;
    x = (width - 1024.f * k) * .5f + (959 + extension / 2) * k;
    y = 69.f * k;
}

bool PauseScreen::resumeHit(int width, int height, float x, float y) const {
    if (height <= 0 || sprites_.sprites().size() <= 27) return false;
    float cx, cy;
    resumeCentre(width, height, cx, cy);
    const auto& d = sprites_.sprites()[27];
    const float k = height / 768.f;
    // OnClick uses the stored sprite width and height, with a ten-unit touch margin.
    return x > cx - (d.w / 2 + 10) * k && x < cx + (d.w - d.w / 2 + 10) * k &&
           y > cy - (d.h / 2 + 10) * k && y < cy + (d.h - d.h / 2 + 10) * k;
}

void PauseScreen::quitCentre(int width, int height, float& x, float& y) {
    const float k = height / 768.f;
    const int extension = (height > 0 ? 768 * width / height : 1024) - 1024;
    x = (width - 1024.f * k) * .5f + (65 - extension / 2) * k;
    y = 65.f * k;
}

bool PauseScreen::quitHit(int width, int height, float x, float y) const {
    if (height <= 0 || sprites_.sprites().size() <= 4) return false;
    float cx, cy;
    quitCentre(width, height, cx, cy);
    const auto& d = sprites_.sprites()[4];
    const float k = height / 768.f;
    // The odd-job control has strict sprite bounds, without the resume touch margin.
    return x > cx - (d.w / 2) * k && x < cx + (d.w - d.w / 2) * k &&
           y > cy - (d.h / 2) * k && y < cy + (d.h - d.h / 2) * k;
}

void PauseScreen::sprite(int index, float x, float y, float k) const {
    if (!texture_ || index < 0 || index >= (int)sprites_.sprites().size()) return;
    const auto& d = sprites_.sprites()[index];
    const float w = d.w + 1.f, h = d.h + 1.f;
    const float u0 = 2.f * d.x / textureWidth_, v0 = 2.f * d.y / textureHeight_;
    const float u1 = 2.f * (d.x + w) / textureWidth_, v1 = 2.f * (d.y + h) / textureHeight_;
    const float left = x - d.offX * k, top = y - d.offY * k;
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texture_);
    glColor4f(1, 1, 1, 1);
    glBegin(GL_QUADS);
    glTexCoord2f(u0, v0); glVertex2f(left, top);
    glTexCoord2f(u1, v0); glVertex2f(left + w * k, top);
    glTexCoord2f(u1, v1); glVertex2f(left + w * k, top + h * k);
    glTexCoord2f(u0, v1); glVertex2f(left, top + h * k);
    glEnd();
    glDisable(GL_TEXTURE_2D);
}

void PauseScreen::render(Game& game, int width, int height) {
    if (!game.pauseOpen || width <= 0 || height <= 0) return;
    int mx, my;
    Host_GetMouse(&mx, &my);
    if (OS_TimeAccurate() >= touchReady_ && Plugins_HostApi().mouse_clicked(0)) {
        if (resumeHit(width, height, (float)mx, (float)my)) {
            TheSound().playHud(326, 127);
            game.setPause(false);
            return;
        }
        if (quitHit(width, height, (float)mx, (float)my)) {
            // Original 0x77a494 only opens confirmation when an odd-job task exists.
            // The current game host has no mission/replay task to terminate.
            TheSound().playHud(326, 127);
        }
    }
    // Original paused render branch (0x7790b8): black, alpha 230, over the frozen world.
    Hud_Rect(0, 0, (float)width, (float)height, 0x000000E6u);
    float x, y;
    resumeCentre(width, height, x, y);
    sprite(27, x, y, height / 768.f);
    quitCentre(width, height, x, y);
    const float k = height / 768.f;
    sprite(4, x, y, k);
    // Native text-sprite creation adds a one-unit shadow for font zero (0x6c0558).
    Hud_Text(x + 46.f * k, y + k, 1.5f * k, 0x000000FFu, quitLabel_);
    Hud_Text(x + 45.f * k, y, 1.5f * k, 0xFFFFFFFFu, quitLabel_);
}
