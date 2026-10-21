// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "radio.h"
#include "game.h"
#include "hud.h"
#include "plugins.h"
#include "sound.h"
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
    loadPdaSprite(fs, dir, "SS_PdaSprites.bin", "ss_pdasprites.png", 13, headingBar_);
    loadPdaSprite(fs, dir, "SS_AlwaysLoaded.bin", "ss_alwaysloaded.png", 96, backButton_);
    GxtFile labels, names;
    labels.load(dir + "/e_pda.gxt"); names.load(dir + "/e_main.gxt");
    appHeading_ = text(names, 0x3F);   // cRadioApp::Init: AddHeading(GlobalText()->GetString(0x3F)), "Music Player"
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
        // cWavStream::RandomiseRadioStart: C rand() (not the game's random streams), a start up to the track length,
        // or up to 15 s before the end for tracks over five minutes
        if (station.duration > 0) {
            const uint32_t ms = (uint32_t)(station.duration * 1000.0);
            const uint32_t span = ms < 300001 ? ms : ms - 15000;
            const uint32_t r = (uint32_t)std::rand() + (uint32_t)std::rand() * 0x10000u;
            station.position = span ? (r % span) / 1000.0 : 0.0;
        }
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
    countListening();
    if (playing_ >= 0 && playing_ < (int)stations_.size()) stations_[playing_].position = Audio_MusicPosition();
}

void Radio::countListening() {
    const double now = OS_TimeAccurate();
    if (listenClock_ > 0 && now >= listenClock_ && playing_ >= 0 && playing_ < stationCount() && Audio_MusicPlaying())
        stations_[playing_].listened += now - listenClock_;
    listenClock_ = now;
}

int Radio::favoriteStars(int station) const {
    if (station < 0 || station >= stationCount() || stations_[station].stream < 0 ||
        stations_[station].listened <= 0 || std::find(appOrder_.begin(),appOrder_.end(),station) == appOrder_.end()) return 0;
    // CalcStationOrder sorts ascending, removes zero-time stations, then ProcessStars caps the rank at five.
    int rank = 1;
    for (int other : appOrder_) {
        if (stations_[other].stream < 0 || stations_[other].listened <= 0) continue;
        if (stations_[other].listened < stations_[station].listened ||
            (stations_[other].listened == stations_[station].listened && other < station)) ++rank;
    }
    return std::min(5,rank);
}

float Radio::volumeAlpha(double now) const {
    const double frames = std::floor(std::max(0.0,now-volumeShownAt_) * 30.0);
    if (frames < 100) return std::min(1.f,0.4f + (float)frames * 0.1f);
    return std::max(0.4f,1.f - (float)(frames-100) * 0.1f);
}

// One sprite of a 2x sprite sheet, uploaded on its own (the sheets are large and the radio app needs two pieces)
bool Radio::loadPdaSprite(GameFs& fs, const std::string& dir, const char* bin, const char* png, int index, PdaSprite& out) {
    std::vector<uint8_t> defs, image;
    SpriteSet set;
    if (!fs.read(bin, defs) || !set.parse(defs) || index >= (int)set.sprites().size() ||
        !Data_ReadAll(dir + "/" + png, image)) return false;
    int w, h, channels;
    unsigned char* pixels = stbi_load_from_memory(image.data(), (int)image.size(), &w, &h, &channels, 4);
    if (!pixels) return false;
    out.def = set.sprites()[index];
    const int x0 = out.def.x * 2, y0 = out.def.y * 2, cw = std::min(out.def.w * 2, w - x0), ch = std::min(out.def.h * 2, h - y0);
    if (cw <= 0 || ch <= 0) { stbi_image_free(pixels); return false; }
    std::vector<uint8_t> crop((size_t)cw * ch * 4);
    for (int y = 0; y < ch; ++y) memcpy(&crop[(size_t)y * cw * 4], pixels + ((size_t)(y0 + y) * w + x0) * 4, (size_t)cw * 4);
    stbi_image_free(pixels);
    glGenTextures(1, &out.texture);
    glBindTexture(GL_TEXTURE_2D, out.texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, cw, ch, 0, GL_RGBA, GL_UNSIGNED_BYTE, crop.data());
    out.u0 = out.v0 = 0; out.u1 = out.v1 = 1;
    return true;
}

void Radio::drawPdaSprite(const PdaSprite& s, float x, float y) {
    if (!s.texture) return;
    x -= s.def.offX; y -= s.def.offY;
    const float w = s.def.w, h = s.def.h;
    glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, s.texture); glColor4f(1, 1, 1, 1);
    glBegin(GL_QUADS);
    glTexCoord2f(s.u0,s.v0); glVertex2f(x,y); glTexCoord2f(s.u1,s.v0); glVertex2f(x+w,y);
    glTexCoord2f(s.u1,s.v1); glVertex2f(x+w,y+h); glTexCoord2f(s.u0,s.v1); glVertex2f(x,y+h);
    glEnd();
}

void Radio::shutdown() {
    remember();
    if (!stations_.empty()) save();
    Audio_StopMusic();
    Audio_SetSfxPaused(false);
    if (texture_) glDeleteTextures(1, &texture_);
    for (PdaSprite* s : {&headingBar_, &backButton_}) { if (s->texture) glDeleteTextures(1, &s->texture); *s = PdaSprite{}; }
    texture_ = 0; stations_.clear(); playing_ = -1;
    app_ = appTuning_ = false; pointerDown_ = pointerSwiped_ = false; listenClock_ = 0;
    ambience_ = -1; ambienceLevel_ = 0;
}

void Radio::sync(Game& game) {
    bool enabled = inCar(game);
    if (enabled && app_ && appTuning_) return;
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
        listenClock_ = OS_TimeAccurate();
    }
}

// cWavStream::WhichAmbienceAmINear / GetFadeVol / ProcessAmbience: east of x = 1540 the airport loops, elsewhere the
// city loop; the rain versions once the time cycle's rain value (cTimeCycle +0xE0) reaches 0x800. Stream volume (of
// 127): city 50, city in rain 25, airport 75 dry / 40 wet, faded 10 a frame. Water ambience (in a boat) and
// positional ambience are not ported. The radio volume setting scales it like the stations.
void Radio::ambience(Game& game) {
    if (dir_.empty() || tables_.streams.size() <= 24) return;
    const bool wet = game.world.timeCycle().value(28) >= 2048.f;
    const int32_t x = game.playerCar >= 0 && game.playerCar < (int)game.cars.size()
                        ? game.cars[game.playerCar].pos[0] : game.player.pos[0];
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
    appTuning_ = false;   // explicit host API tuning is not a carousel gesture
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
    countListening();
    adoptCar(game);
    sync(game);
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
// Layout from cRadioApp: station logos in a carousel at y 85 placed from 240 minus half the selected logo's width,
// Init's spacing (width + 40), scrolled a quarter of the remaining distance each 30 Hz frame in the direction chosen
// when the station changed, the stream switching within three pixels, alpha 1 - |d| / 240 (ProcessStationIcons); PrintAllBars' equaliser: eight bars at x 84 + 44 i, up to nine 40 x 12
// segments every 16 pixels up from y 220 in gTopCols, and a perspective floor reflection below in gRefCols; the level
// is min(9, sqrt(band energy x 64) x 9) (GetOutput) from the music's band filters (Audio_MusicBands). ProcessStars:
// the dotted outline (sprite 17) at (181, 246), scale 2.35, and up to five stars (sprite 7, scale 2) at y 250,
// x 273 down to 169, by ascending nonzero listening-time rank, capped at five. ProcessVolumeIcon: the meter (sprite 20 and the level
// sprite) at (208, 270), fading back to 40 percent after a change; Init adds the volume buttons as sprite windows
// (SS_Radio 13 at (0xB0, 0x120), 15 at (0x110, 0x120)). cRadioApp::Render clears the screen to black: there is no
// panel and no station name, only the logos. The PDA adds the heading (cPdaApp::AddHeading: SS_PdaSprites 13 at
// (8, -32) under GlobalText 0x3F, "Music Player", centred across the 480 pixels) and the task bar's back button
// (SS_AlwaysLoaded 96; its place in the bottom-left corner is assumed, the task bar's layout is not ported). Not
// ported: the help tips, the custom-playlist slot and the phone surround of cPda::RenderPhoneSurround.
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
    appTuning_ = false;
    pointerDown_ = pointerSwiped_ = false;
    game.uiPaused = open;            // cRadioApp::Init: cGame::Pause
    Audio_SetSfxPaused(open);
    if (open) {
        // cRadioApp::Init: each icon at the running width, 40 pixels apart; the carousel starts on the station
        appOrder_.clear();
        appOffset_.clear();
        appIconW_.clear();
        appWidth_ = 0;
        for (int i = 0; i < (int)stations_.size(); ++i) {
            if (!stations_[i].available) continue;
            auto def = std::find_if(sprites_.sprites().begin(), sprites_.sprites().end(),
                                    [&](const SpriteDef& s) { return s.id == stations_[i].icon; });
            const int w = def == sprites_.sprites().end() ? 127 : (int)def->w;
            appOrder_.push_back(i);
            appOffset_.push_back(appWidth_);
            appIconW_.push_back(w);
            appWidth_ += w + 40;
        }
        auto at = std::find(appOrder_.begin(), appOrder_.end(), selected_);
        appScroll_ = at == appOrder_.end() ? 0 : appOffset_[at - appOrder_.begin()];
        appDir_ = 1;
        appFrames_ = 0.f;
        appLast_ = OS_TimeAccurate();
    } else {
        sync(game);
        remember();
        save();
    }
}

void Radio::appStation(int step, Game& game) {   // DoStationUp / DoStationDown
    const int previous = selected_;
    const bool pending = appTuning_;
    appTuning_ = true;
    cycle(step, game);
    if (selected_ == previous) appTuning_ = pending;
    volumeShownAt_ = -10.0;
    // the carousel travels towards the new icon, the long way round when it is more than half the strip away
    auto at = std::find(appOrder_.begin(), appOrder_.end(), selected_);
    if (at == appOrder_.end()) return;
    const int diff = appOffset_[at - appOrder_.begin()] - appScroll_;
    appDir_ = diff > 0 ? 1 : -1;
    if (std::abs(diff) > appWidth_ / 2) appDir_ = -appDir_;
}

void Radio::advanceCarousel(Game& game) {   // cRadioApp::ProcessStationIcons, the scrolling
    auto at = std::find(appOrder_.begin(), appOrder_.end(), selected_);
    if (at == appOrder_.end() || appWidth_ <= 0) return;
    const int target = appOffset_[at - appOrder_.begin()];
    const int diff = target - appScroll_;
    int d = std::abs(diff) * appDir_;   // the distance left in the travel direction
    if (std::abs(d) > appWidth_ / 2) d = (d < 0 ? std::abs(diff - appWidth_) : std::abs(diff + appWidth_)) * appDir_;
    // within three pixels the stream changes (PlayPauseStream)
    if ((unsigned)(d + 3) < 7 && appTuning_) {
        appTuning_ = false;
        sync(game);
    }
    int scroll = appScroll_ + d / 4;   // a quarter of the way each frame
    if (scroll >= appWidth_) scroll -= appWidth_;
    if (scroll < 0) scroll += appWidth_;
    appScroll_ = scroll;
}

void Radio::appClick(float x, float y, Game& game) {
    if (x < 0 || x > kAppW || y < -46 || y > kAppH) return;
    if (y >= 312 && x <= 90) { openApp(false, game); return; }
    if (y >= 270 && y <= 321) {
        if (x >= 160 && x <= 207) volume(-1);
        else if (x >= 272 && x <= 319) volume(1);
        else return;
        volumeShownAt_ = OS_TimeAccurate();
    } else if (y >= 55 && y <= 233) {
        if (x < 177) appStation(-1, game);
        else if (x > 304) appStation(1, game);
    }
}

void Radio::appPointer(float x, float y, bool down, bool pressed, Game& game) {
    if (pressed && !down) {   // a complete click between display frames
        pointerDown_ = pointerSwiped_ = false;
        appClick(x, y, game);
        return;
    }
    if (down && !pointerDown_) {
        pointerX_ = x; pointerY_ = y; pointerSwiped_ = false;
    } else if (down && !pointerSwiped_ && pointerX_ >= 0 && pointerX_ <= kAppW &&
               pointerY_ >= 55 && pointerY_ <= 233 && std::fabs(x - pointerX_) >= 5) {
        // cRadioApp::OnDrag: one station per gesture, after five logical pixels, with PlayHudSfx(0x35F, 0x10)
        appStation(x > pointerX_ ? -1 : 1, game);
        pointerSwiped_ = true;
        TheSound().playHud(0x35F, 0x10);
    } else if (!down && pointerDown_ && !pointerSwiped_ && pointerX_ >= 0 && pointerX_ <= kAppW &&
               std::fabs(x - pointerX_) < 5 && std::fabs(y - pointerY_) < 5) {
        appClick(x, y, game);
    }
    pointerDown_ = down;
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
    countListening();   // the broadcast keeps playing while the simulation is paused
    const double now = OS_TimeAccurate();
    const float frames = (float)std::min(4.0, (now - appLast_) * 30.0);   // the app runs per 30 Hz frame
    appLast_ = now;
    const float scale = std::min(W / kAppW, H / (kAppH + 60.f));
    const float ox = (W - kAppW * scale) * 0.5f, oy = (H - kAppH * scale) * 0.5f + 10.f * scale;
    int mouseX, mouseY; Host_GetMouse(&mouseX, &mouseY);
    appPointer((mouseX - ox) / scale, (mouseY - oy) / scale, Host_MouseDown(0),
               Plugins_HostApi().mouse_clicked(0) != 0, game);
    if (!app_) return;
    // cRadioApp::Render: a black screen (world rendering is disabled)
    Hud_Rect(0, 0, (float)W, (float)H, 0x000000FFu);
    glPushMatrix();
    glTranslatef(ox, oy, 0);
    glScalef(scale, scale, 1);

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

    // ProcessStationIcons, once per 30 Hz frame: scroll a quarter of the way, then place every icon relative to
    // 240 minus half the selected icon's width, fading with the distance from there (1 - d / 240)
    appFrames_ = std::min(appFrames_ + frames, 4.f);
    for (; appFrames_ >= 1.f; appFrames_ -= 1.f) advanceCarousel(game);
    auto sel = std::find(appOrder_.begin(), appOrder_.end(), selected_);
    const int base = 240 - (sel == appOrder_.end() ? 0 : appIconW_[sel - appOrder_.begin()] >> 1);
    for (size_t i = 0; i < appOrder_.size(); ++i) {
        const Station& st = stations_[appOrder_[i]];
        auto def = std::find_if(sprites_.sprites().begin(), sprites_.sprites().end(), [&](const SpriteDef& d) { return d.id == st.icon; });
        if (def == sprites_.sprites().end()) continue;
        int x = base + appOffset_[i] - appScroll_;
        if (x <= -0x81) x += appWidth_;
        if (x > appWidth_ - 0x80) x -= appWidth_;
        const float alpha = (float)std::min(std::abs(base - x), 240) * -0.004166667f + 1.f;
        if (alpha > 0.f && x > -def->w && x < kAppW) sprite(st.icon, (float)x, 85.f, 1.f, alpha);   // SetSpritePos(x, 0x55)
    }

    // ProcessStars hides the outline only for the off station.
    if (stations_[selected_].stream >= 0) {
        sprite(17, 181, 246, 9626 / 4096.f);   // SetSpriteScale is 20.12 fixed point
        static const float starX[5] = {273, 247, 221, 195, 169};
        const int stars = favoriteStars(selected_);
        for (int i = 0; i < stars; ++i) sprite(7, starX[i], 250, 2.f);
    }

    // ProcessVolumeIcon: brighten by 0.1/frame, hold for the 100-frame timer, then return to 0.4.
    const float volAlpha = volumeAlpha(now);
    sprite(20, 208, 270, 1.f, volAlpha);
    if (volume_ > 0) sprite(tables_.volumeSprites[volume_ - 1], 208, 270, 1.f, volAlpha);
    sprite(13, 176, 288); sprite(15, 272, 288);
    // the PDA: the heading bar and its centred title, the back button
    drawPdaSprite(headingBar_, 8, -32);
    const std::string& heading = appHeading_.empty() ? std::string("Music Player") : appHeading_;
    Hud_Text(240.f - Hud_TextWidth(heading, 1.f) * 0.5f, -30, 1.f, 0xFFFFFFFFu, heading);
    drawPdaSprite(backButton_, 8, kAppH - backButton_.def.h - 4);
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
