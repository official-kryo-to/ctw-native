// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "sinkingcar.h"
#include "hud.h"
#include "random.h"
#include "gfx/assets.h"
#include "os/gamefs.h"
#include "os/os.h"
#include "os/pak.h"
#include <SDL.h>
#include <glad/gl.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
// HitScreen's window: rectangle A {x0, y0, x1, y1} and below it B {x2, y1, x3, y3}, from the interior's offset
struct Window { int x0, y0, x1, y1, x2, x3, y3; };
constexpr Window kWindow[4] = {
    {85, 140, 520, 225, 200, 425, 320},
    {90, 140, 530, 275, 230, 405, 330},
    {100, 140, 550, 260, 230, 550, 310},
    {135, 140, 500, 220, 250, 460, 260},
};
constexpr int kInterior[4] = {0xAC, 0xAB, 0xAE, 0xAD};   // cSinkingCarApp::Init's AddBgWindow by style
constexpr int kOutline[4] = {4, 3, 6, 5};                // cSinkingCar::Init's window sprite by style
// cSinkingCar::Process: the outline from the offset (0x79000 less these)
constexpr int kOutlineAt[4][2] = {{0x79 - 0x36, 0x79}, {0x79 - 0x3F, 0x79 + 4}, {0x79 - 0x34, 0x79 - 3}, {0x79, 0x79 - 0x26}};
// qword_485CA8 / dword_485CB4: the first six cracks' rotations and sounds
constexpr uint16_t kCrackRotation[6] = {0xC000, 0x4000, 0x8000, 0xC000, 0x4000, 0x8000};
constexpr int kCrackSfx[6] = {0x23F, 0x240, 0x241, 0x23F, 0x240, 0x241};
constexpr int kInhaleSfx[4] = {0x22F, 0x230, 0x231, 0x232}, kExhaleSfx[4] = {0x1CE, 0x1CF, 0x1D0, 0x1D1};
constexpr int kBubbleSfx[5] = {0x159, 0x15A, 0x15B, 0x15C, 0x15D};   // gBubbleSfx

uint16_t tableEntry(const std::vector<uint8_t>& t, int i) {
    uint16_t v = 0;
    if ((size_t)(i + 1) * 2 <= t.size()) std::memcpy(&v, t.data() + i * 2, 2);
    return v;
}

int32_t fastsinq(int a) {   // the game's fastsin: 12-bit sine of a 16-bit angle
    return (int32_t)std::lround(std::sin((int16_t)a * 3.14159265358979 / 32768.0) * 4096.0);
}

uint32_t nowMs() {   // DoSinkingCarSfx / DoBreathing: (uint64)(OS_TimeAccurate() x 1e6) / 1e6 x 1000
    const double us = (double)(uint64_t)(OS_TimeAccurate() * 1000000.0);
    return (uint32_t)(us / 1000000.0 * 1000.0);
}
}

void SinkingCar::BubbleArea::init(int x, int y, int w, int h, int count, int every) {
    pos[0] = x << 12; pos[1] = y << 12; size[0] = w << 12; size[1] = h << 12;
    interval = every;
    counter = 0;
    bubbles.assign(count, Bubble{});
}

bool SinkingCar::start(const std::string& dataDir, int style, std::function<void(bool)> done) {
    stop();
    style_ = std::clamp(style, 0, 3);
    done_ = std::move(done);
    // the textures: the sprite sheet's slot (gGameDir[23]: slot + 1) and the backgrounds (gGameDir[26])
    std::vector<uint8_t> directory, slots, backgrounds, defs;
    GameFs fs;
    if (!Assets_Pak().read(0, directory) || directory.size() < 27 * 2) return false;
    if (!Assets_Pak().read(tableEntry(directory, 23), slots) || !Assets_Pak().read(tableEntry(directory, 26), backgrounds))
        return false;
    if (!fs.open(dataDir) || !fs.read("SS_SinkingCar.bin", defs) || !sprites_.parse(defs) || sprites_.sprites().size() < 7)
        return false;
    const int sheet = tableEntry(slots, sprites_.sprites()[0].sheetSlot + 1);
    sheet_ = Assets_Texture(sheet);
    if (!sheet_ || !Assets_TextureSize(sheet, &sheetW_, &sheetH_)) return false;
    interior_ = tableEntry(backgrounds, kInterior[style_]);
    frameLine_ = tableEntry(backgrounds, 0xAF);
    if (!Assets_Texture(interior_)) return false;
    // cSinkingCar::Init
    hits_ = 0;
    needed_ = (int)Rand16NonCritical(4) + 4;
    tapsLeft_ = (int)Rand16NonCritical(1) + 1;
    bigCracks_ = 0;
    off_[0] = -0x28000; off_[1] = 0x184000;         // xmmword_469DA0: (-40, 388)
    drift_[0] = 0x1000; drift_[1] = 0;
    shake_[0] = shake_[1] = 0;
    shakeLeft_ = 0;
    outline_ = true;
    flashPhase_ = 1.f;
    flashAlpha_ = 1.f;
    lastShoulder_ = 0;
    crackList_.clear();
    for (Shard& s : shards_) s = Shard{};
    for (BubbleArea& a : leaks_) a.init(0, 0, 80, 80, 10, 8);   // xmmword_4665F0
    ambient_[0].init(120, 650, 10, 10, 8, 15);                  // the app's (xmmword_468140, 466F60, 467B10)
    ambient_[1].init(300, 600, 10, 10, 2, 16);
    ambient_[2].init(230, 620, 90, 70, 5, 35);
    nextBeat_ = nextBreath_ = 0;
    exhale_ = false;
    breathing_ = true;
    state_ = 0;
    counter_ = 0;
    air_ = 0;
    warned_ = false;
    active_ = true;
    return true;
}

void SinkingCar::stop() {
    active_ = false;
    done_ = nullptr;
}

// cSinkingCar::HitScreen
void SinkingCar::hitScreen(int x, int y) {
    outline_ = false;   // (ShowSprite(false), its flash off)
    if (shakeLeft_ > 0) return;
    shakeLeft_ = 3;
    const Window& w = kWindow[style_];
    const int ox = off_[0] >> 12, oy = off_[1] >> 12;
    const bool inA = x > ox + w.x0 && y > oy + w.y0 && x < ox + w.x1 && y < oy + w.y1;
    const bool inB = x > ox + w.x2 && x < ox + w.x3 && y > oy + w.y1 && y < oy + w.y3;
    if (!inA && !inB) {
        if (playSfx) playSfx(0x2F2, 0x7F);
        return;
    }
    for (int call = 0; call < 8; ++call)   // MakeGlassShard x 8: each into the first free slot, if any
        for (Shard& s : shards_) {
            if (s.on) continue;
            s.on = true;
            s.pos[0] = x + (((int32_t)Rand32NonCritical(0xA000) - 0x5000) >> 12);
            s.pos[1] = y + (((int32_t)Rand32NonCritical(0xA000) - 0x5000) >> 12);
            const int16_t a = (int16_t)Rand32NonCritical(0x10000);
            const int32_t sn = fastsinq(a), cs = fastsinq(a + 0x4000);
            const int32_t speed = (int32_t)Rand32NonCritical(0x5000) + 0xF000;
            s.vel[0] = (int32_t)(((int64_t)speed * sn) >> 12);
            s.vel[1] = (int32_t)(((int64_t)speed * cs) >> 12);
            s.life = 6;
            break;
        }
    if (--tapsLeft_ != 0) {
        const int sfx = Rand32NonCritical(100) > 0x32 ? 0x244 : 0x243;
        Rand32NonCritical(0x12C);   // (cAudioManager::PlaySfx's random delay)
        if (playSfx) playSfx(sfx, 0x7F);
        return;
    }
    Crack c;
    c.pos[0] = x - ox;   // (x << 12) - (offset & ~0xFFF)
    c.pos[1] = y - oy;
    if (needed_ - 2 == hits_ || (bigCracks_ <= 2 && Rand16NonCritical(3) != 0)) { c.sprite = 1; ++bigCracks_; }
    else c.sprite = 2;
    leaks_[hits_ % 2].pos[0] = x << 12;
    leaks_[hits_ % 2].pos[1] = y << 12;
    int sfx = 0x242;
    c.rotation = 0;
    if (hits_ <= 5) { c.rotation = kCrackRotation[hits_]; sfx = kCrackSfx[hits_]; }
    if (needed_ - 1 == hits_) sfx = 0x242;
    crackList_.push_back(c);
    ++hits_;
    tapsLeft_ = (int)Rand16NonCritical(1) + 1;
    if (playSfx) playSfx(sfx, 0x7F);
}

void SinkingCar::press(float x, float y, int W, int H) {   // cSinkingCarApp::OnPress (the app's 1024 x 768)
    if (!active_ || state_ != 2 || W <= 0 || H <= 0) return;
    hitScreen((int)(x * 1024.f / W), (int)(y * 768.f / H));
    if (hits_ >= needed_) {
        if (fade) fade(true, 2);
        ++state_;
    }
}

// ProcessController: a new press of R (pad bit 9) or L (bit 8), the other not held and not the last one pressed
void SinkingCar::shoulder(int which) {
    if (!active_ || state_ != 2) return;
    const Uint8* ks = SDL_GetKeyboardState(nullptr);
    const int code = which ? 0x200 : 0x100;
    const bool otherHeld = which ? ks[SDL_SCANCODE_Q] : ks[SDL_SCANCODE_E];
    if (otherHeld || lastShoulder_ == code) return;
    lastShoulder_ = code;
    const int x = 200 + (int)Rand32NonCritical(0xAA);
    const int y = 600 + (int)Rand32NonCritical(0x3E);
    hitScreen(x, y);
    if (hits_ >= needed_) {
        if (fade) fade(true, 2);
        ++state_;
    }
}

void SinkingCar::launch(Bubble& b, int x, int y) {   // cSinkingCarBubble::Launch
    b.on = true;
    b.base[0] = x << 12; b.base[1] = y << 12;
    b.scale = b.drawScale = 0x1000;
    b.off[0] = (int32_t)Rand32NonCritical(0x2000) - 0x1000;
    b.vel[1] = (int32_t)Rand32NonCritical(0x2000) - 0x1000;
    if (Rand32NonCritical(100) >= 0x33) {
        const int sample = kBubbleSfx[Rand32NonCritical(5)];
        const int volume = (int)((0x7F000000LL - (int64_t)air_ * 0x61000) >> 24);
        if (playSfx) playSfx(sample, volume);
    }
}

void SinkingCar::processBubbles(BubbleArea& a) {   // cBubbleArea::Process
    const bool due = (int8_t)(a.counter + 1) >= (int8_t)a.interval;
    a.counter = due ? 0 : a.counter + 1;
    bool launchOne = due;
    for (Bubble& b : a.bubbles) {
        if (!b.on) {
            if (launchOne)
                launch(b, (a.pos[0] >> 12) + (int)Rand32NonCritical((uint32_t)(a.size[0] >> 12)),
                       (a.pos[1] >> 12) + (int)Rand32NonCritical((uint32_t)(a.size[1] >> 12)));
            launchOne = false;
            continue;
        }
        // cSinkingCarBubble::Process
        const int32_t pull = 0x1000 - (b.scale << 11 >> 12);
        const int32_t half = (b.scale + ((b.scale & 0x8000) ? 1 : 0)) >> 1;
        for (int i = 0; i < 2; ++i) {
            b.vel[i] = (int32_t)(((int64_t)(b.vel[i] + (b.off[i] > 0 ? -pull : pull)) * half) >> 12);
            b.off[i] += b.vel[i];
        }
        b.base[0] = (int32_t)(((int64_t)(b.base[0] - 0x200000) * 0xFEB + 0x200000000LL) >> 12);
        b.base[1] = (int32_t)(((int64_t)(b.base[1] - 0x180000) * 0xF33 + 0x180000000LL) >> 12);
        b.drawScale = b.scale;   // (SetSpriteScale before it shrinks)
        bool keep = b.scale >= 1;
        if (keep) {
            b.scale = (int16_t)(((uint32_t)(uint16_t)b.scale * 0xF33) >> 12);
            keep = (uint16_t)b.scale >= 0x334;
        }
        keep = keep && b.base[0] >= -0x20000 && b.base[0] <= 0x420000 && b.base[1] >= -0x20000 && b.base[1] <= 0x320000;
        if (!keep) b.on = false;
    }
}

// DoSinkingCarSfx (the heart) and DoBreathing
void SinkingCar::sounds() {
    const int64_t t = std::min(air_, 0xFD7);
    const uint32_t now = nowMs();
    if (now > nextBeat_) {
        nextBeat_ = now + (uint32_t)((0x2BC000000LL - t * 0x15E000) >> 24);
        if (playSfx) playSfx(0x2C6, 0x5A + (int)((t * 0x25000) >> 24));
    }
    if (!breathing_ || now <= nextBreath_) return;
    nextBreath_ = now + (uint32_t)(exhale_ ? (0x258000000LL - t * 0xC8000) >> 24 : (0x7D0000000LL - t * 0x44C000) >> 24);
    const int* list = exhale_ ? kInhaleSfx : kExhaleSfx;   // (+0x118 clear: an exhale)
    const int sample = list[Rand32NonCritical(4)];
    if (playSfx) playSfx(sample, 0x14 + (int)((t * 0x6B000) >> 24));
    exhale_ = !exhale_;
}

void SinkingCar::process(uint32_t frame) {
    if (!active_) return;
    // cSinkingCar::Process (ProcessController runs as the keys arrive: shoulder)
    for (Shard& s : shards_) {   // ProcessGlassShards: on the DS screen's 256 x 192 bounds
        if (!s.on) continue;
        --s.life;
        for (int i = 0; i < 2; ++i) s.vel[i] = (int32_t)(((int64_t)s.vel[i] * 0xF33) >> 12);
        const int x = s.pos[0] + (s.vel[0] >> 12), y = s.pos[1] + (s.vel[1] >> 12);
        if (s.life == 0 || x < -0x20 || x > 0x120 || y < -0x20 || y >= 0xE1) { s.on = false; continue; }
        s.pos[0] = x; s.pos[1] = y;
    }
    shake_[0] = shake_[1] = 0;
    if (shakeLeft_ >= 1) {
        shake_[0] = ((int32_t)Rand32NonCritical(shakeLeft_ * 2) - shakeLeft_) << 12;
        shake_[1] = ((int32_t)Rand32NonCritical(shakeLeft_ * 2) - shakeLeft_) << 12;
        --shakeLeft_;
    }
    if (hits_ > 0) processBubbles(leaks_[0]);
    if (hits_ >= 2) processBubbles(leaks_[1]);
    if (frame & 2) {   // gGameFrameCounter bit 1: the drift turns back towards (-40, 388), at most 0.75 a frame
        const int32_t rest[2] = {-0x28000, 0x184000};
        for (int i = 0; i < 2; ++i) {
            int32_t push = (int32_t)((((int64_t)Rand32Critical(0x800) + 0x1000) * 0x333) >> 12);
            if (off_[i] > rest[i]) push = -push;
            drift_[i] = std::clamp(drift_[i] + push, -0xC00, 0xC00);
        }
    }
    off_[0] += drift_[0];
    off_[1] += drift_[1];
    sounds();
    // the outline's flash (cSprite::RefreshSprite: sin(phase) x 0.4 + 0.6, the phase 0.2 on)
    if (outline_) {
        flashAlpha_ = std::sin(flashPhase_) * 0.4f + 0.6f;
        flashPhase_ += 0.2f;
    }
    // cSinkingCarApp::Process
    for (BubbleArea& a : ambient_) processBubbles(a);
    switch (state_) {
    case 0:
    case 1:
        state_ = 2;
        [[fallthrough]];
    case 2:
        air_ += 9;
        if (air_ < 0xF0B) break;
        if (!warned_) {
            warned_ = true;
            if (playSfx) playSfx(0x22B, 0x7F);
        }
        if (air_ > 0x1000) {   // drowned
            breathing_ = false;
            state_ = 5;
            if (fade) fade(true, 20);
        }
        break;
    case 3:   // smashed: once faded out, the splash at the car
        if (fading && fading()) break;
        state_ = 4;
        if (splash) splash();
        break;
    case 4:
    case 5:
        if (counter_ >= 20) {   // (5 frames without a minigame task)
            const bool escaped = state_ == 4;
            auto done = std::move(done_);
            stop();
            if (done) done(escaped);
            return;
        }
        ++counter_;
        break;
    }
}

void SinkingCar::background(int texture, float x, float y, int W, int H) const {   // cBg::Render
    int tw = 1, th = 1;
    const unsigned gl = texture ? Assets_Texture(texture) : 0;
    if (!gl || !Assets_TextureSize(texture, &tw, &th)) return;
    const float kx = W / 1024.f, ky = H / 768.f;
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, gl);
    glColor4f(1, 1, 1, 1);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex2f(x * kx, y * ky);
    glTexCoord2f(1, 0); glVertex2f((x + tw) * kx, y * ky);
    glTexCoord2f(1, 1); glVertex2f((x + tw) * kx, (y + th) * ky);
    glTexCoord2f(0, 1); glVertex2f(x * kx, (y + th) * ky);
    glEnd();
    glDisable(GL_TEXTURE_2D);
}

// cSprite::CalculateVerts: the image (w + 1 by h + 1) about its anchor, scaled, turned by the rotation
void SinkingCar::sprite(int index, float x, float y, float scale, uint16_t rotation, float alpha, int W, int H) const {
    if (index < 0 || index >= (int)sprites_.sprites().size()) return;
    const SpriteDef& d = sprites_.sprites()[index];
    const float kx = W / 1024.f, ky = H / 768.f;
    const float w = d.w + 1.f, h = d.h + 1.f;
    const float c = fastsinq(rotation + 0x4000) / 4096.f, s = fastsinq(rotation) / 4096.f;
    const float vx[4] = {-(float)d.offX, w - d.offX, w - d.offX, -(float)d.offX};
    const float vy[4] = {-(float)d.offY, -(float)d.offY, h - d.offY, h - d.offY};
    const float u[4] = {(float)d.x, d.x + w, d.x + w, (float)d.x};
    const float v[4] = {(float)d.y, (float)d.y, d.y + h, d.y + h};
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, sheet_);
    glColor4f(1, 1, 1, alpha);
    glBegin(GL_QUADS);
    for (int i = 0; i < 4; ++i) {
        const float px = (vx[i] * c + vy[i] * s) * scale, py = (-vx[i] * s + vy[i] * c) * scale;
        glTexCoord2f(u[i] / sheetW_, v[i] / sheetH_);
        glVertex2f((x + px) * kx, (y + py) * ky);
    }
    glEnd();
    glDisable(GL_TEXTURE_2D);
}

void SinkingCar::render(int W, int H) const {
    if (!active_ || W <= 0 || H <= 0) return;
    Hud_Begin(W, H);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    const int ox = (off_[0] + shake_[0]) >> 12, oy = (off_[1] + shake_[1]) >> 12;
    // layer 5: the shards; 4: the cracks; 3: the bubbles
    for (const Shard& s : shards_)
        if (s.on) sprite(0, (float)s.pos[0], (float)s.pos[1], 0x4CC / 4096.f, 0, 1.f, W, H);
    for (const Crack& c : crackList_) sprite(c.sprite, (float)(ox + c.pos[0]), (float)(oy + c.pos[1]), 1.f, c.rotation, 1.f, W, H);
    for (const BubbleArea* a : {&leaks_[0], &leaks_[1], &ambient_[0], &ambient_[1], &ambient_[2]})
        for (const Bubble& b : a->bubbles)
            if (b.on)
                sprite(0, (float)((b.off[0] >> 12) + (b.base[0] >> 12)), (float)((b.off[1] >> 12) + (b.base[1] >> 12)),
                       b.drawScale / 4096.f, 0, 1.f, W, H);
    // layer 2: the interior at the car's offset; layer 1: 0xAF at (0, 426); layer 0: the outline
    background(interior_, (float)ox, (float)oy, W, H);
    background(frameLine_, 0.f, 426.f, W, H);
    if (outline_)
        sprite(kOutline[style_], (float)(ox + kOutlineAt[style_][0]), (float)(oy + kOutlineAt[style_][1]), 1.f, 0, flashAlpha_, W, H);
    Hud_End();
}
