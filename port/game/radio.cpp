// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "radio.h"
#include "game.h"
#include "hud.h"
#include "plugins.h"
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
    return !game.player.dead && game.playerCar >= 0 && game.playerCar < (int)game.cars.size() &&
           !game.cars[game.playerCar].isBike() && !game.cars[game.playerCar].dead();
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
            if (station.name == "Chinese") station.name = "SinoWav FM"; // Name on the original station artwork.
            station.label = station.name;
        }
        if (station.duration > 0) station.position = Rand32Critical(1000000) / 1000000.0 * std::max(1.0, station.duration - 15);
        stations_.push_back(std::move(station));
    }
    Station off; off.available = true; off.label = text(labels, 112); off.name = off.label;
    stations_.push_back(off);
    selected_ = 0; playing_ = -1; volume_ = 8; carUid_ = 0;
    dir_ = dir; ambience_ = -1; ambienceLevel_ = 0;
    loadSave();
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
    Audio_SetSfxPaused(false);
    if (texture_) glDeleteTextures(1, &texture_);
    texture_ = 0; stations_.clear(); playing_ = -1;
    ambience_ = -1; ambienceLevel_ = 0;
}

void Radio::sync(Game& game) {
    bool enabled = inCar(game);
    if (!enabled || stations_.empty() || !stations_[selected_].available || stations_[selected_].stream < 0) {
        if (playing_ >= 0) { remember(); Audio_StopMusic(); playing_ = -1; }
        ambience(game);
        return;
    }
    if (playing_ != selected_) {
        remember(); Audio_StopMusic(); playing_ = -1;
        ambience_ = -1;
        Audio_SetMusicVolume(volume_ / 10.f);
        const Station& s = stations_[selected_];
        if (Audio_PlayMusic(s.path, true, s.position)) playing_ = selected_;
    }
}

// cWavStream::WhichAmbienceAmINear / GetFadeVol / ProcessAmbience: east of x = 1540 the airport loops, elsewhere the
// city loop; the rain versions once the time cycle's rain value (cTimeCycle +0xE0) reaches 0x800. Stream volume (of
// 127): city 50, city in rain 25, airport 75 dry / 40 wet, faded 10 a frame. Water ambience (in a boat) and
// positional ambience are not ported. The radio volume setting scales it like the stations.
void Radio::ambience(Game& game) {
    if (dir_.empty() || tables_.streams.size() <= 24) return;
    const bool wet = game.world.timeCycle().value(28) >= 2048.f;
    const int32_t x = game.playerCar >= 0 ? game.cars[game.playerCar].pos[0] : game.player.pos[0];
    const int want = x > 0x604000 ? (wet ? 24 : 23) : (wet ? 3 : 1);
    static const int level[33] = {0, 50, 0, 25, 25, 127, 0, 127, 0, 127, 127, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 127, 127, 75, 40};
    if (want != ambience_) {   // fade the old loop out, then start the new one
        ambienceLevel_ = std::max(0, ambienceLevel_ - 10);
        if (ambienceLevel_ > 0 && ambience_ >= 0) { Audio_SetMusicVolume(volume_ / 10.f * ambienceLevel_ / 127.f); return; }
        Audio_StopMusic();
        ambience_ = Audio_PlayMusic(dir_ + "/" + tables_.streams[want], true) ? want : -1;
        if (ambience_ < 0) return;
    }
    const int target = level[want];
    ambienceLevel_ += std::clamp(target - ambienceLevel_, -10, 10);   // cSoundStream::SetVolumeToFadeTo(vol, 10)
    Audio_SetMusicVolume(volume_ / 10.f * ambienceLevel_ / 127.f);
}

const char* Radio::stationName(int index) const {
    if (index < 0 || index >= stationCount()) return nullptr;
    const auto& s = stations_[index];
    return (s.name.empty() ? s.label : s.name).c_str();
}

bool Radio::stationAvailable(int index) const {
    return index >= 0 && index < stationCount() && stations_[index].available;
}

bool Radio::tune(int index, Game& game) {
    if (!stationAvailable(index) || !inCar(game)) return false;
    adoptCar(game);
    select(index, game);
    return true;
}

bool Radio::setVolume(int level) {
    if (level < 0 || level > 10) return false;
    volume_ = level;
    volume(0);
    save();
    return true;
}

void Radio::adoptCar(Game& game) {
    if (stations_.empty()) return;
    uint32_t uid = inCar(game) ? game.cars[game.playerCar].uid : 0;
    if (app_ && !inCar(game)) openApp(false, game);
    if (uid && uid != carUid_) {
        int& saved = game.cars[game.playerCar].radioStation;
        if (saved < 0 || saved >= (int)stations_.size()) saved = selected_;
        selected_ = saved;
    }
    carUid_ = uid;
}

void Radio::update(Game& game) {
    if (stations_.empty()) return;
    adoptCar(game);
    sync(game);
    if (playing_ >= 0 && Audio_MusicPlaying()) stations_[playing_].listened += 1.0 / 30;
}

void Radio::select(int index, Game& game) {
    if (stations_.empty()) return;
    const int previous = selected_;
    selected_ = (index + (int)stations_.size()) % (int)stations_.size();
    if (inCar(game)) game.cars[game.playerCar].radioStation = selected_;
    sync(game);
    if (previous != selected_ && inCar(game)) {
        const auto& car = game.cars[game.playerCar];
        Plugins_EmitEvent(CTW_EVENT_RADIO_CHANGED, car.uid, car.infoId, selected_);
    }
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
    return appKey(k, game);   // Tab opens the radio app; there is no wheel or shortcut tuning
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

// ---- the radio app (Gui::cRadioApp), drawn on its own 480 x 340 screen ---------------------------------------------
// Layout from cRadioApp: station logos in a carousel centred at x 240, y 85, InitScrolling spacing (width + 40), alpha
// 1 - |240 - x| / 240 (ProcessStationIcons); PrintAllBars' equaliser: eight bars at x 84 + 44 i, up to nine 40 x 12
// segments every 16 pixels up from y 220 in gTopCols, and a perspective floor reflection below in gRefCols; the level
// is min(9, sqrt(band energy x 64) x 9) (GetOutput) from the music's band filters (Audio_MusicBands). ProcessStars:
// the dotted outline (sprite 17) at (181, 246), scale 2.35, and up to five stars (sprite 7, scale 2) at y 250,
// x 273 down to 169, for the five most listened stations. ProcessVolumeIcon: the meter (sprite 20 and the level
// sprite) at (208, 270), fading out after a change. The PDA frame, touch buttons and the station heading are drawn
// in a PC style; the station name position is a PC choice.
namespace {
constexpr float kAppW = 480.f, kAppH = 340.f;
uint32_t rgb555(uint16_t c, uint8_t alpha = 0xFF) {   // DrawDebugFan takes the 5-bit channels x 5
    const uint32_t r = (c & 31) * 5, g = (c >> 5 & 31) * 5, b = (c >> 10 & 31) * 5;
    return r << 24 | g << 16 | b << 8 | alpha;
}
const uint16_t kTopCols[10] = {0x2101, 0x2521, 0x2941, 0x2D61, 0x3181, 0x35A1, 0x39C2, 0x3DE2, 0x4202, 0x4622};
const uint16_t kRefCols[10] = {0x18C1, 0x18C1, 0x14A1, 0x14A1, 0x1081, 0x1081, 0x0C61, 0x0C61, 0x0841, 0x0841};
const int32_t kRefScale[10] = {4096, 4219, 4383, 4588, 4833, 5120, 5448, 5816, 6226, 6595};
const int32_t kRefRow[10] = {819, 983, 1147, 1311, 1475, 1638, 1802, 1966, 2130, 2294};
}

void Radio::openApp(bool open, Game& game) {
    open = open && inCar(game) && !stations_.empty();
    if (open == app_) return;
    app_ = open;
    game.uiPaused = open;            // cRadioApp::Init: cGame::Pause
    Audio_SetSfxPaused(open);
    if (open) {
        appOrder_.clear();
        appOffset_.clear();
        appWidth_ = 0.f;
        for (int i = 0; i < (int)stations_.size(); ++i) {
            if (!stations_[i].available) continue;
            auto def = std::find_if(sprites_.sprites().begin(), sprites_.sprites().end(),
                                    [&](const SpriteDef& s) { return s.id == stations_[i].icon; });
            const float w = def == sprites_.sprites().end() ? 127.f : (float)def->w;
            appOrder_.push_back(i);
            appOffset_.push_back(appWidth_);
            appWidth_ += w + 40.f;
        }
        auto at = std::find(appOrder_.begin(), appOrder_.end(), selected_);
        appScroll_ = at == appOrder_.end() ? 0.f : appOffset_[at - appOrder_.begin()];
        appLast_ = OS_TimeAccurate();
    } else {
        remember();
        save();
    }
}

void Radio::appStation(int step, Game& game) {   // DoStationUp / DoStationDown
    cycle(step, game);
    volumeShownAt_ = -10.0;
}

bool Radio::appKey(int k, Game& game) {
    if (!app_) {
        if (k == SDL_SCANCODE_TAB && inCar(game) && !stations_.empty()) { openApp(true, game); return true; }
        return false;
    }
    switch (k) {
    case SDL_SCANCODE_TAB: case SDL_SCANCODE_ESCAPE: case SDL_SCANCODE_BACKSPACE: openApp(false, game); break;
    case SDL_SCANCODE_LEFT: case SDL_SCANCODE_A: appStation(-1, game); break;
    case SDL_SCANCODE_RIGHT: case SDL_SCANCODE_D: appStation(1, game); break;
    case SDL_SCANCODE_UP: case SDL_SCANCODE_W: case SDL_SCANCODE_EQUALS: case SDL_SCANCODE_KP_PLUS:
        volume(1); volumeShownAt_ = OS_TimeAccurate(); break;
    case SDL_SCANCODE_DOWN: case SDL_SCANCODE_S: case SDL_SCANCODE_MINUS: case SDL_SCANCODE_KP_MINUS:
        volume(-1); volumeShownAt_ = OS_TimeAccurate(); break;
    default: break;
    }
    return true;   // the paused game takes no other keys while the app is open
}

void Radio::renderApp(int W, int H, Game& game) {
    if (!app_) return;
    if (!inCar(game)) { openApp(false, game); return; }
    const double now = OS_TimeAccurate();
    const float frames = (float)std::min(4.0, (now - appLast_) * 30.0);   // the app runs per 30 Hz frame
    appLast_ = now;
    // the screen: the world dimmed (the original stops drawing it), the app scaled to fit and centred
    Hud_Rect(0, 0, (float)W, (float)H, 0x000000E6u);
    const float scale = std::min(W / kAppW, H / (kAppH + 60.f));
    const float ox = (W - kAppW * scale) * 0.5f, oy = (H - kAppH * scale) * 0.5f + 10.f * scale;
    glPushMatrix();
    glTranslatef(ox, oy, 0);
    glScalef(scale, scale, 1);
    Hud_Rect(0, -46, kAppW, kAppH + 46, 0x0B0D12FFu);
    Hud_Rect(0, -46, kAppW, 2, 0xF2C230FFu);
    const Station& s = stations_[selected_];
    const std::string& name = s.name.empty() ? s.label : s.name;
    Hud_Text(16, -36, 1.2f, 0xF4F5F8FFu, "RADIO");
    const float nameScale = std::min(1.f, 300.f / std::max(1.f, Hud_TextWidth(name, 1.f)));
    Hud_Text(kAppW - 16 - Hud_TextWidth(name, nameScale), -34, nameScale, 0xF2C230FFu, name);

    // PrintAllBars: the bars, then the floor reflection
    float energy[8];
    Audio_MusicBands(energy);
    int8_t level[8];
    for (int b = 0; b < 8; ++b)
        level[b] = (int8_t)std::min(9, (int)(std::sqrt(std::max(0.f, energy[b]) * 64.f) * 9.f));
    for (int b = 0; b < 8; ++b)
        for (int k = 0; k < level[b]; ++k) Hud_Rect(84.f + 44.f * b, 220.f - 16.f * k, 40, 12, rgb555(kTopCols[k]));
    int32_t rowY = 0x54000 + 0x8E000;
    for (int k = 0; k < 10; ++k) {
        const int32_t refScale = kRefScale[k], height = ((kRefRow[k] * 3) >> 10) & ~1;
        int32_t x = 0x54000 - (int32_t)(((int64_t)(refScale - 4096) * 176));
        const int32_t width = (int32_t)((((int64_t)refScale * 0x14000) >> 12) >> 11) & ~1;
        for (int b = 0; b < 8; ++b) {
            if (k < level[b]) Hud_Rect((float)(x >> 12), (float)((rowY >> 12) + 6), (float)width, (float)height, rgb555(kRefCols[k]));
            x += (int32_t)(((int64_t)(0x14000 + 0x18000) * refScale) >> 12);
        }
        rowY += kRefRow[k] << 4;
    }

    // ProcessStationIcons: ease the carousel towards the station, then draw it
    auto at = std::find(appOrder_.begin(), appOrder_.end(), selected_);
    if (at != appOrder_.end() && appWidth_ > 0.f) {
        float diff = appOffset_[at - appOrder_.begin()] - appScroll_;
        if (diff > appWidth_ * 0.5f) diff -= appWidth_;
        if (diff < -appWidth_ * 0.5f) diff += appWidth_;
        const float k = 1.f - std::pow(0.75f, frames);   // a quarter of the way each frame
        appScroll_ = std::fmod(appScroll_ + diff * k + appWidth_, appWidth_);
        if (std::fabs(diff) < 0.5f) appScroll_ = appOffset_[at - appOrder_.begin()];
    }
    for (size_t i = 0; i < appOrder_.size(); ++i) {
        const Station& st = stations_[appOrder_[i]];
        auto def = std::find_if(sprites_.sprites().begin(), sprites_.sprites().end(), [&](const SpriteDef& d) { return d.id == st.icon; });
        if (def == sprites_.sprites().end()) continue;
        float x = 240.f - def->w * 0.5f + appOffset_[i] - appScroll_;
        if (x < -128.f) x += appWidth_;
        if (x > appWidth_ - 128.f) x -= appWidth_;
        const float alpha = 1.f - std::min(std::fabs(240.f - def->w * 0.5f - x), 240.f) / 240.f;
        if (alpha > 0.f && x > -def->w && x < kAppW) sprite(st.icon, x, 85.f - def->h * 0.5f + 40.f, 1.f, alpha);
    }

    // ProcessStars: the five most listened stations get one to five stars
    std::vector<int> byTime = appOrder_;
    std::stable_sort(byTime.begin(), byTime.end(), [&](int a, int b) { return stations_[a].listened < stations_[b].listened; });
    const auto rankAt = std::find(byTime.begin(), byTime.end(), selected_);
    const int rank = rankAt == byTime.end() ? -1 : (int)(rankAt - byTime.begin()) - std::max(0, (int)byTime.size() - 5);
    if (stations_[selected_].listened > 0 && rank >= 0) {
        sprite(17, 181, 246, 9626 / 4096.f);   // SetSpriteScale is 20.12 fixed point
        static const float starX[5] = {273, 247, 221, 195, 169};
        for (int i = 0; i < 5; ++i) if (rank >= i) sprite(7, starX[i], 250, 2.f);
    }

    // ProcessVolumeIcon: shown after a change, fading out
    const float shown = (float)(now - volumeShownAt_);
    const float volAlpha = shown < 2.f ? 1.f : std::max(0.f, 1.f - (shown - 2.f) * 1.5f);
    if (volAlpha > 0.f) {
        sprite(20, 208, 270, 1.f, volAlpha);
        if (volume_ > 0) sprite(tables_.volumeSprites[volume_ - 1], 208, 270, 1.f, volAlpha);
    }
    Hud_Text(16, kAppH - 18, 0.8f, 0x9A9DAAFFu, "A / D: station    W / S: volume    Tab: back to the game");
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
