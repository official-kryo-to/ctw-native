// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "gamehud.h"
#include "game.h"
#include "gfx/assets.h"
#include "os/datafile.h"
#include "os/gamefs.h"
#include "os/pak.h"
#include <glad/gl.h>
#include <stb_image.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
constexpr float pi = 3.14159265358979323846f;
void disc(float x, float y, float r) {
    glBegin(GL_TRIANGLE_FAN);
    glVertex2f(x, y);
    for (int i = 0; i <= 64; ++i) {
        const float a = i * (2.f * pi / 64.f);
        glVertex2f(x + std::sin(a) * r, y + std::cos(a) * r);
    }
    glEnd();
}
void meterArc(float x, float y, float inner, float outer, int value, float side) {
    // RenderRadar mirrors its health fan for armour; both fill from twelve towards six.
    const int steps = (std::clamp(value, 0, 127) * 0x20420) >> 19;
    glBegin(GL_TRIANGLE_STRIP);
    for (int i = 0; i <= steps; ++i) {
        const float a = i * (2.f * pi / 64.f);
        const float s = std::sin(a), c = std::cos(a);
        glVertex2f(x + side * s * inner, y - c * inner);
        glVertex2f(x + side * s * outer, y - c * outer);
    }
    glEnd();
}
}

bool GameplayHud::init(const std::string& dir) {
    shutdown();
    std::vector<uint8_t> directory, map, defs, png;
    uint16_t gameDir[23];
    if (!Assets_Pak().read(0, directory) || directory.size() < sizeof gameDir) return false;
    std::memcpy(gameDir, directory.data(), sizeof gameDir);
    if (!Assets_Pak().read(gameDir[22], map) || !map_.load(map)) return false;
    GameFs fs;
    if (!fs.open(dir) || !fs.read("SS_AlwaysLoaded.bin", defs) || !sprites_.parse(defs) ||
        !Data_ReadAll(dir + "/ss_alwaysloaded.png", png)) return false;
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
    return true;
}

void GameplayHud::shutdown() {
    if (texture_) glDeleteTextures(1, &texture_);
    texture_ = 0;
    textureWidth_ = textureHeight_ = 0;
    sprites_ = SpriteSet{};
    map_ = RadarMap{};
}

void GameplayHud::sprite(int index, float x, float y, float scale, float angle) const {
    if (index < 0 || index >= (int)sprites_.sprites().size()) return;
    const SpriteDef& d = sprites_.sprites()[index];
    const float w = d.w + 1.f, h = d.h + 1.f; // cSprite::CalculateVerts includes the last stored pixel.
    if ((d.x + w) * 2 > textureWidth_ || (d.y + h) * 2 > textureHeight_) return;
    const float c = std::cos(angle), s = std::sin(angle);
    const float vx[4] = {-(float)d.offX, w - d.offX, w - d.offX, -(float)d.offX};
    const float vy[4] = {-(float)d.offY, -(float)d.offY, h - d.offY, h - d.offY};
    const float u[4] = {d.x * 2.f, (d.x + w) * 2.f, (d.x + w) * 2.f, d.x * 2.f};
    const float v[4] = {d.y * 2.f, d.y * 2.f, (d.y + h) * 2.f, (d.y + h) * 2.f};
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texture_);
    glColor4f(1, 1, 1, 1);
    glBegin(GL_QUADS);
    for (int i = 0; i < 4; ++i) {
        glTexCoord2f(u[i] / textureWidth_, v[i] / textureHeight_);
        glVertex2f(x + (vx[i] * c - vy[i] * s) * scale, y + (vx[i] * s + vy[i] * c) * scale);
    }
    glEnd();
    glDisable(GL_TEXTURE_2D);
}

void GameplayHud::render(const Game& game, int width, int height) const {
    if (!texture_ || width <= 0 || height <= 0 || !map_.columns()) return;
    // The normal phone radar uses 150 HUD units. Like GetRadarLeft, anchor it to the left edge as aspect changes.
    // cRadar's projection is the 768-high PDA space, with its y=448 offset converted back to top-left pixels.
    const float k = height / 768.f, diameter = 150.f;
    const float cx = (70.f + diameter * .5f) * k, cy = (10.f + diameter * .5f) * k;
    const float radius = diameter * .49f * k, rim = diameter * .54f * k;
    int32_t pos[3]; game.focus(pos);
    float mx, my;
    RadarMap::worldToMap(pos, mx, my);
    const uint16_t yaw = rotation_ ? game.cameraYaw() : 0;
    const RadarProjection projection(yaw);
    glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT | GL_CURRENT_BIT | GL_TEXTURE_BIT);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_ALPHA_TEST);
    sprite(114, cx, cy, (diameter / 118.f) * k);
    glColor4f(0, 0, 0, .4f);
    disc(cx, cy, rim);
    glColor4f(.32f, .48f, .32f, 1.f);
    meterArc(cx, cy, radius, rim, game.player.health, -1.f);
    glColor4f(.2f, .44f, .61f, 1.f);
    meterArc(cx, cy, radius, rim, game.player.armour, 1.f);

    // Isolate the map in a circular stencil; never reuse or overwrite the world's depth buffer.
    glEnable(GL_STENCIL_TEST);
    glStencilMask(0xff);
    glClearStencil(0);
    glClear(GL_STENCIL_BUFFER_BIT);
    glStencilFunc(GL_ALWAYS, 1, 0xff);
    glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    disc(cx, cy, radius);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glStencilMask(0);
    glStencilFunc(GL_EQUAL, 1, 0xff);
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
    glColor4f(.02f, .1f, .13f, 1.f);
    disc(cx, cy, radius);

    const float reach = diameter * .5f;
    const int firstCol = (int)std::floor((mx - reach - map_.originX()) / 256.f);
    const int lastCol = (int)std::floor((mx + reach - map_.originX()) / 256.f);
    const int firstRow = (int)std::floor((my - reach - map_.originY()) / 256.f);
    const int lastRow = (int)std::floor((my + reach - map_.originY()) / 256.f);
    for (int row = firstRow; row <= lastRow; ++row) {
        for (int col = firstCol; col <= lastCol; ++col) {
            int id = map_.texture(col, row);
            if (id < 0) id = map_.defaultTexture();
            const unsigned tex = id < 0 ? 0 : Assets_Texture(id);
            if (!tex) continue;
            const float x0 = map_.originX() + col * 256.f - mx, y0 = map_.originY() + row * 256.f - my;
            const float px[4] = {x0, x0 + 256, x0 + 256, x0}, py[4] = {y0, y0, y0 + 256, y0 + 256};
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, tex);
            glColor4f(1, 1, 1, .75f);
            glBegin(GL_QUADS);
            for (int v = 0; v < 4; ++v) {
                glTexCoord2f(v == 1 || v == 2 ? 1.f : 0.f, v >= 2 ? 1.f : 0.f);
                float x, y;
                projection.mapOffset(px[v], py[v], x, y);
                glVertex2f(cx + x * k, cy + y * k);
            }
            glEnd();
        }
    }
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_STENCIL_TEST);
    const int16_t heading = game.playerCar >= 0 ? game.cars[game.playerCar].heading() : game.player.heading();
    sprite(108, cx, cy, k, (int16_t)(heading - yaw) * (2.f * pi / 65536.f));
    int32_t north[3] = {pos[0], pos[1] + 200 * 4096, pos[2]};
    float nx, ny;
    projection.worldOffset(north, pos, nx, ny, diameter * .5f);
    sprite(107, cx + nx * k, cy + ny * k, k);
    glPopAttrib();
}
