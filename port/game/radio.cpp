// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "radio.h"
#include "game.h"
#include "hud.h"
#include "audio/audio.h"
#include "os/datafile.h"
#include "os/gamefs.h"
#include "os/os.h"
#include "text/gxt.h"
#include <SDL.h>
#include <glad/gl.h>
#include <stb_image.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
std::string text(const GxtFile& gxt, int id) {
    if (id < 0 || (size_t)id >= gxt.count()) return {};
    std::string out;
    for (char16_t c : gxt.get(id)) {
        if (!c || c >= 0xFE00) continue;
        if (c < 128) out += (char)c;
        else if (c < 2048) { out += (char)(0xC0 | (c >> 6)); out += (char)(0x80 | (c & 63)); }
        else { out += (char)(0xE0 | (c >> 12)); out += (char)(0x80 | ((c >> 6) & 63)); out += (char)(0x80 | (c & 63)); }
    }
    return out;
}
bool inCar(const Game& game) {
    return game.playerCar >= 0 && game.playerCar < (int)game.cars.size() &&
           game.cars[game.playerCar].infoId != 32 && !game.cars[game.playerCar].dead();
}
}

bool Radio::init(const std::string& dir) {
    shutdown();
    GameFs fs;
    std::vector<uint8_t> data, png;
    if (!tables_.load(dir + "/radio_tables.bin") || !fs.open(dir) || !fs.read("SS_Radio.bin", data) ||
        !sprites_.parse(data) || !Data_ReadAll(dir + "/ss_radio.png", png)) return false;
    int channels;
    unsigned char* pixels = stbi_load_from_memory(png.data(), (int)png.size(), &textureW_, &textureH_, &channels, 4);
    if (!pixels) return false;
    glGenTextures(1, &texture_);
    glBindTexture(GL_TEXTURE_2D, texture_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, textureW_, textureH_, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    stbi_image_free(pixels);
    GxtFile labels, names;
    labels.load(dir + "/e_pda.gxt"); names.load(dir + "/e_main.gxt");
    for (const auto& def : tables_.stations) {
        Station station;
        station.icon = def.icon; station.stream = def.stream;
        station.label = text(labels, def.label); station.name = text(names, def.name);
        station.path = dir + "/" + tables_.streams[def.stream];
        AudioTrackInfo info;
        station.available = Audio_Probe(station.path, &info);
        station.duration = info.seconds;
        // The Android extra-station slots reuse the base game's text IDs. Do not mislabel all of them as Prairie Cartel.
        if (def.stream >= 14) {
            station.name = tables_.streams[def.stream];
            auto dot = station.name.rfind('.'); if (dot != std::string::npos) station.name.resize(dot);
            station.label = station.name;
        }
        if (station.duration > 0) station.position = Rand32Critical(1000000) / 1000000.0 * std::max(1.0, station.duration - 15);
        stations_.push_back(std::move(station));
    }
    Station off; off.available = true; off.label = text(labels, 112); off.name = off.label;
    stations_.push_back(off);
    selected_ = 0; playing_ = -1; volume_ = 8; open_ = paused_ = false; carUid_ = 0;
    loadSave();
    carousel_ = (float)selected_;
    Audio_Init();
    volume(0);
    return true;
}

void Radio::remember() {
    if (playing_ >= 0 && playing_ < (int)stations_.size()) stations_[playing_].position = Audio_MusicPosition();
}

void Radio::shutdown() {
    remember();
    if (!stations_.empty()) save();
    Audio_StopMusic();
    if (texture_) glDeleteTextures(1, &texture_);
    texture_ = 0; stations_.clear(); playing_ = -1; open_ = false;
}

void Radio::sync(Game& game) {
    bool enabled = open_ || inCar(game);
    if (!enabled || stations_.empty() || !stations_[selected_].available || stations_[selected_].stream < 0) {
        remember(); Audio_StopMusic(); playing_ = -1; return;
    }
    if (playing_ != selected_) {
        remember(); Audio_StopMusic(); playing_ = -1;
        const Station& s = stations_[selected_];
        if (Audio_PlayMusic(s.path, true, s.position)) playing_ = selected_;
    }
    Audio_SetMusicPaused(paused_);
}

void Radio::update(Game& game) {
    if (stations_.empty()) return;
    uint32_t uid = inCar(game) ? game.cars[game.playerCar].uid : 0;
    if (uid && uid != carUid_) {
        int& saved = game.cars[game.playerCar].radioStation;
        if (saved < 0 || saved >= (int)stations_.size()) saved = selected_;
        selected_ = saved; carousel_ = (float)selected_; paused_ = false;
    }
    carUid_ = uid;
    sync(game);
    if (playing_ >= 0 && Audio_MusicPlaying()) stations_[playing_].listened += 1.0 / 30;
    float distance = selected_ - carousel_, count = (float)stations_.size();
    if (distance > count / 2) distance -= count;
    if (distance < -count / 2) distance += count;
    carousel_ = std::fmod(carousel_ + distance * 0.25f + count, count);
}

void Radio::select(int index, Game& game) {
    if (stations_.empty()) return;
    selected_ = (index + (int)stations_.size()) % (int)stations_.size();
    paused_ = false;
    if (inCar(game)) game.cars[game.playerCar].radioStation = selected_;
    sync(game);
}

void Radio::cycle(int step, Game& game) {
    if (stations_.empty()) return;
    int index = selected_;
    for (size_t i = 0; i < stations_.size(); ++i) {
        index = (index + step + (int)stations_.size()) % (int)stations_.size();
        if (stations_[index].available) { select(index, game); return; }
    }
}

void Radio::volume(int step) {
    volume_ = std::clamp(volume_ + step, 0, 10);
    Audio_SetMusicVolume(volume_ / 10.f);
}

bool Radio::key(int k, Game& game) {
    if (stations_.empty()) return false;
    if (game.player.dead && !open_) return false;
    if (k == SDL_SCANCODE_R || (open_ && k == SDL_SCANCODE_ESCAPE)) {
        open_ = !open_; sync(game); if (!open_) { remember(); save(); } return true;
    }
    if (!open_) {
        if (!inCar(game)) return false;
        if (k == SDL_SCANCODE_LEFTBRACKET) { cycle(-1, game); return true; }
        if (k == SDL_SCANCODE_RIGHTBRACKET) { cycle(1, game); return true; }
        return false;
    }
    if (k == SDL_SCANCODE_LEFT || k == SDL_SCANCODE_A) cycle(-1, game);
    if (k == SDL_SCANCODE_RIGHT || k == SDL_SCANCODE_D) cycle(1, game);
    if (k == SDL_SCANCODE_UP || k == SDL_SCANCODE_EQUALS) volume(1);
    if (k == SDL_SCANCODE_DOWN || k == SDL_SCANCODE_MINUS) volume(-1);
    if (k == SDL_SCANCODE_SPACE || k == SDL_SCANCODE_RETURN) { paused_ = !paused_; sync(game); }
    return true;
}

void Radio::sprite(int id, float x, float y, float scale, float alpha) const {
    auto it = std::find_if(sprites_.sprites().begin(), sprites_.sprites().end(), [=](const SpriteDef& s) { return s.id == id; });
    if (it == sprites_.sprites().end() || !texture_) return;
    const SpriteDef& s = *it;
    float u0 = s.x * 2.f / textureW_, v0 = s.y * 2.f / textureH_;
    float u1 = (s.x + s.w) * 2.f / textureW_, v1 = (s.y + s.h) * 2.f / textureH_;
    float w = s.w * scale, h = s.h * scale;
    glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, texture_); glColor4f(1, 1, 1, alpha);
    glBegin(GL_QUADS);
    glTexCoord2f(u0,v0); glVertex2f(x,y); glTexCoord2f(u1,v0); glVertex2f(x+w,y);
    glTexCoord2f(u1,v1); glVertex2f(x+w,y+h); glTexCoord2f(u0,v1); glVertex2f(x,y+h);
    glEnd();
}

void Radio::render(int W, int H, Game& game) {
    if (!open_ || stations_.empty()) return;
    float scale = std::min(W / 480.f, H / 448.f), ox = (W - 480 * scale) / 2, oy = (H - 448 * scale) / 2;
    int mx, my; Host_GetMouse(&mx, &my);
    float x = (mx - ox) / scale, y = (my - oy) / scale;
    if (Host_PopClicks() & 1) {
        if (y >= 270 && y <= 335 && x >= 160 && x <= 207) volume(-1);
        else if (y >= 270 && y <= 335 && x >= 270 && x <= 319) volume(1);
        else if (y >= 70 && y <= 220) { if (x < 170) cycle(-1, game); else if (x > 310) cycle(1, game); else { paused_ = !paused_; sync(game); } }
        else if (y >= 396) { open_ = false; remember(); save(); sync(game); }
    }
    Hud_Rect(0, 0, (float)W, (float)H, 0xFF000000);
    glPushMatrix(); glTranslatef(ox, oy, 0); glScalef(scale, scale, 1);
    Hud_Text(240 - Hud_TextWidth("Radio", 1.5f) / 2, 22, 1.5f, 0xFFFFFFFF, "Radio");
    const float count = (float)stations_.size();
    for (size_t i = 0; i < stations_.size(); ++i) {
        float d = (float)i - carousel_;
        if (d > count / 2) d -= count;
        if (d < -count / 2) d += count;
        float px = 240 - 63 + d * 167;
        if (px > 480 || px < -127) continue;
        float alpha = std::max(0.f, 1 - std::abs(d * 167) / 240);
        if (!stations_[i].available) alpha *= 0.3f;
        sprite(stations_[i].icon, px, 85, 1, alpha);
    }
    const Station& s = stations_[selected_];
    float labelScale = std::min(1.f, 440.f / std::max(1.f, Hud_TextWidth(s.label, 1)));
    Hud_Text(240 - Hud_TextWidth(s.label, labelScale) / 2, 222, labelScale, 0xFFFFFFFF, s.label);
    sprite(17, 181, 246, 2.35f);
    if (s.stream >= 0 && s.listened > 0) {
        int rank = 0; for (const auto& other : stations_) if (other.listened > s.listened) ++rank;
        for (int i = 0; i < std::max(0, 5 - rank); ++i) sprite(7, 273 - i * 26.f, 250);
    }
    sprite(13, 176, 288); sprite(15, 272, 288); sprite(20, 208, 270);
    if (volume_ > 0) sprite(tables_.volumeSprites[volume_ - 1], 208, 270);
    sprite(1, 110, 139); sprite(2, 357, 139);
    Hud_Rect(0, 396, 480, 52, 0xFF202020);
    Hud_Text(240 - Hud_TextWidth("Back", 1) / 2, 414, 1, 0xFFFFFFFF, "Back");
    glPopMatrix();
}

void Radio::save() const {
    void* file = nullptr;
    if (OS_FileOpen(OS_AREA_DOCUMENTS, &file, "radio.dat", OS_FILE_WRITE) != OS_OK) return;
    uint32_t head[4] = {0x32524443, (uint32_t)stations_.size(), (uint32_t)selected_, (uint32_t)volume_};
    OS_FileWrite(file, head, sizeof head);
    for (const auto& s : stations_) {
        double values[2] = {s.position, s.listened}; OS_FileWrite(file, values, sizeof values);
    }
    OS_FileClose(file);
}

void Radio::loadSave() {
    void* file = nullptr;
    if (OS_FileOpen(OS_AREA_DOCUMENTS, &file, "radio.dat", OS_FILE_READ) != OS_OK) return;
    uint32_t head[4]{};
    bool valid = OS_FileSize(file) == (int)(16 + stations_.size() * 16) &&
                 OS_FileRead(file, head, sizeof head) == OS_OK && head[0] == 0x32524443 &&
                 head[1] == stations_.size() && head[2] < stations_.size() && head[3] <= 10;
    std::vector<std::array<double, 2>> values(stations_.size());
    for (auto& value : values) if (valid) {
        valid = OS_FileRead(file, value.data(), sizeof value) == OS_OK &&
                std::isfinite(value[0]) && value[0] >= 0 && std::isfinite(value[1]) && value[1] >= 0;
    }
    OS_FileClose(file);
    if (!valid) return;
    selected_ = (int)head[2]; volume_ = (int)head[3];
    for (size_t i = 0; i < stations_.size(); ++i) {
        if (stations_[i].duration > 0) stations_[i].position = std::fmod(values[i][0], stations_[i].duration);
        stations_[i].listened = values[i][1];
    }
}
