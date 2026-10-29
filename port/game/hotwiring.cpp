// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "hotwiring.h"
#include "hud.h"
#include "gfx/assets.h"
#include "os/gamefs.h"
#include "os/pak.h"
#include <glad/gl.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
constexpr float kTwoPi = 6.2831853f;   // 0x40C90FDB
constexpr StrokeVec kLineA{624, 720}, kLineB{752, 647}, kWinder{840, 590};   // dword_E05958, E05960, E05968
constexpr float kExitX = 0x2C0, kExitY = 0x1E8;                              // AddMinigameExitButton(0x2C0, 0x1E8)

uint16_t tableEntry(const std::vector<uint8_t>& t, int i) {
    uint16_t v = 0;
    if ((size_t)(i + 1) * 2 <= t.size()) std::memcpy(&v, t.data() + i * 2, 2);
    return v;
}

// cPdaApp::OrthoCorrectionBeforeRender, mode 2: x from -(width - 1024) to 1024, y 0 to 768 (the int maths kept)
float appLeft(int W, int H) {
    const int vw = (int)((unsigned)(W * 3 * 256) / (unsigned)(H ? H : 1));
    int t = vw - 0x400;
    if (t < 0) t = vw - 0x3FF;
    return (float)(-(t >> 1) * 2);
}
}

bool HotWiring::start(const std::string& dataDir, int hotwireClass, bool showTimer, std::function<void(bool, bool)> done) {
    stop();
    if (hotwireClass != 1) return false;   // (medium and hard are not ported)
    (void)showTimer;                       // (the timer bar is not ported)
    std::vector<uint8_t> directory, slots, backgrounds, defs, always;
    GameFs fs;
    if (!Assets_Pak().read(0, directory) || directory.size() < 27 * 2) return false;
    if (!Assets_Pak().read(tableEntry(directory, 23), slots) || !Assets_Pak().read(tableEntry(directory, 26), backgrounds))
        return false;
    if (!fs.open(dataDir) || !fs.read("SS_HotWire_Easy.bin", defs) || !sprites_.parse(defs) || sprites_.sprites().size() < 11)
        return false;
    if (!fs.read("SS_AlwaysLoaded.bin", always) || !always_.parse(always) || always_.sprites().size() < 94) return false;
    const int sheet = tableEntry(slots, sprites_.sprites()[0].sheetSlot + 1);
    const int alwaysSheet = tableEntry(slots, always_.sprites()[93].sheetSlot + 1);
    sheet_ = Assets_Texture(sheet);
    alwaysSheet_ = Assets_Texture(alwaysSheet);
    if (!sheet_ || !Assets_TextureSize(sheet, &sheetW_, &sheetH_) || !alwaysSheet_ ||
        !Assets_TextureSize(alwaysSheet, &alwaysW_, &alwaysH_))
        return false;
    dashboard_ = tableEntry(backgrounds, 0x4E);
    frameLine_ = tableEntry(backgrounds, 0x4F);
    done_ = std::move(done);
    // Init_EasyMode
    screwdriver_ = {5, 0x21C, 0x286, true, 1, 0, 3};
    glow_ = {8, 0x208, 0x272, true, 1, 0, 3};
    line_ = StrokeFatLine{};
    line_.initialise(kLineA, kLineB, 120.f, 120.f, 40.f);
    linePos_ = StrokeValue{};
    linePos_.max = std::sqrt((kLineB.y - kLineA.y) * (kLineB.y - kLineA.y) + (kLineB.x - kLineA.x) * (kLineB.x - kLineA.x));
    momentum_ = StrokeMomentum{&linePos_};
    momentum_.k = 0.8f;   // (0x3F4CCCCD)
    line_.reset();
    line_.unbindAll();
    line_.bind(&momentum_);
    glowMin_ = 0; glowMax_ = 1; glowStep_ = 1.f / 3.f; glowFade_ = 1;
    inserted_[0] = {6, 0x2CB, 0x246, false, 1, 0, 5};
    inserted_[1] = {7, 0x2CB, 0x246, false, 1, 0, 5};
    lock_ = {10, 0x333, 0x23C, false, 1, 0, 6};
    arrows_[0] = {3, kWinder.x, kWinder.y, false, 1, 0, 6};
    arrows_[1] = {2, kWinder.x, kWinder.y, false, 1, 0, 6};
    revCounter_ = {9, 0x42D - 0x19F, 0x335 - 0x172, false, 1, 0, 5};
    exit_ = {93, kExitX, kExitY, true, 0, 0, 0};
    exitAlpha_ = 0;
    wind_ = StrokeWinding{};
    turn_ = StrokeValue{};
    oneWay_ = StrokeOneWay{&turn_};
    arrowMin_ = 0.5f; arrowMax_ = 1.0f; arrowStep_ = 0.1f; arrowFade_ = 1; arrowIn_ = true;
    ratchetStep_ = 0;
    zoomA_[0] = zoomB_[0] = 374; zoomA_[1] = zoomB_[1] = 218; zoomScale_ = 1;   // g_EasyMode_NoZoom
    state_ = 0;
    finished_ = false;
    touchDown_ = false;
    active_ = true;
    return true;
}

void HotWiring::stop() {
    active_ = false;
    done_ = nullptr;
}

void HotWiring::toApp(int x, int y, int W, int H, float& ax, float& ay) const {
    const float left = appLeft(W, H);
    ax = left + x * (1024.f - left) / W;
    ay = y * 768.f / H;
}

void HotWiring::touch(bool down, int x, int y, int W, int H) {
    if (!active_ || W <= 0 || H <= 0) return;
    if (down) toApp(x, y, W, H, touchX_, touchY_);
    touchDown_ = down;
}

void HotWiring::click(int x, int y, int W, int H) {   // the exit button's window (84 x 84 about its centre)
    if (!active_ || finished_ || W <= 0 || H <= 0) return;
    float ax, ay;
    toApp(x, y, W, H, ax, ay);
    if (ax >= kExitX - 42 && ax < kExitX + 42 && ay >= kExitY - 42 && ay < kExitY + 42) finish(false, true);   // OnExitButtonPress
}

void HotWiring::finish(bool success, bool quit) {
    finished_ = true;
    auto done = std::move(done_);
    stop();
    if (done) done(success, quit);
}

// states 2..4: the region, the arrow's fade and its turn (+0x804, +0x810)
void HotWiring::winding(int sprite) {
    wind_.update(touchDown_, !touchDown_, touchX_, touchY_);
    arrowFade_ = arrowIn_ ? std::min(arrowFade_ + arrowStep_, 1.f) : std::max(arrowFade_ - arrowStep_, 0.f);
    Sprite& a = arrows_[sprite];
    a.rotation = (uint16_t)(int16_t)(int32_t)(turn_.value / -6.2831807f * 65536.f);   // (0xC0C90FD0, 16 fraction bits)
    a.alpha = (arrowMax_ - arrowMin_) * arrowFade_ + arrowMin_;
}

void HotWiring::process(uint32_t frame, float timeOfDay) {
    (void)frame;
    if (!active_ || finished_) return;
    exitAlpha_ = std::min(exitAlpha_ + 0.2f, 1.f);   // (the exit button fades in: cSprite +0x70 0.2 towards +0x7C 1)
    exit_.alpha = exitAlpha_;
    switch (state_) {
    case 0: {
        line_.update(touchDown_, !touchDown_, touchX_, touchY_);
        momentum_.update();
        const StrokeVec p = line_.position(linePos_.value);
        const int sx = (int)(p.x + (float)(0x21C - (int)kLineA.x)), sy = (int)(p.y + (float)(0x286 - (int)kLineA.y));
        screwdriver_.x = (float)sx;
        screwdriver_.y = (float)sy;
        const bool moving = momentum_.isMoving();
        glowFade_ = moving ? std::max(glowFade_ - glowStep_, 0.f) : std::min(glowFade_ + glowStep_, 1.f);
        glow_.x = (float)(sx - 0x14);
        glow_.y = (float)(sy - 0x14);
        glow_.alpha = (glowMax_ - glowMin_) * glowFade_ + glowMin_;
        const float dy = p.y - kLineB.y, dx = p.x - kLineB.x;
        if (!(std::sqrt(dy * dy + dx * dx) < 3.0f)) break;
        // in the ignition
        screwdriver_.shown = glow_.shown = false;
        inserted_[0].shown = true;
        lock_.shown = true;
        if (playSfx) playSfx(0x2EB, 0x7F);
        state_ = 1;
        zoomTo_[0] = zoomTo_[2] = 374; zoomTo_[1] = zoomTo_[3] = 218; zoomTo_[4] = 2;   // g_EasyMode_ZoomedIn
        zoomT_ = 0;
        zoomStep_ = 0.0333333351f;   // qword_479660: t 0, step 1/30
        break;
    }
    case 1: {   // the zoom: t towards 1, then the winder
        // (this frame's rectangle is the start zoom moved towards the target by t; the start becomes the target
        // at the end. Easy mode does not draw zoomed, so the rectangle is not used here.)
        zoomT_ = zoomT_ + zoomStep_ > 1.f ? 1.f : zoomT_ + zoomStep_;
        if (zoomT_ != 1.f) break;
        zoomA_[0] = zoomTo_[0]; zoomA_[1] = zoomTo_[1]; zoomB_[0] = zoomTo_[2]; zoomB_[1] = zoomTo_[3];
        zoomScale_ = zoomTo_[4];
        wind_.initialise(kWinder, 0.f, 100.f);
        arrowFade_ = 1; arrowIn_ = true;
        turn_.value = std::min(std::max(turn_.value, -kTwoPi), 0.f);   // (the old range's clamp, then the new)
        turn_.min = -kTwoPi; turn_.max = 0;
        oneWay_.mode = 2;
        arrows_[0].shown = true;
        arrows_[0].alpha = (arrowMax_ - arrowMin_) * arrowFade_ + arrowMin_;
        wind_.reset();
        wind_.unbindAll();
        wind_.bind(&oneWay_);
        state_ = 2;
        break;
    }
    case 2:
    case 4: {
        winding(0);
        if (std::fabs(turn_.value + kTwoPi) < 1e-4f) {
            if (state_ == 2) {   // a full turn: back the other way
                arrows_[0].shown = false;
                wind_.reset();
                wind_.unbindAll();
                arrowFade_ = 1; arrowIn_ = true;
                turn_.min = -kTwoPi; turn_.max = 0;
                oneWay_.mode = 0;
                arrows_[1].shown = true;
                arrows_[1].alpha = (arrowMax_ - arrowMin_) * arrowFade_ + arrowMin_;
                wind_.bind(&oneWay_);
                state_ = 3;
                ratchet();
                break;
            }
            // the second full turn: started
            arrows_[0].shown = false;
            inserted_[0].shown = inserted_[1].shown = false;
            if (playSfx) playSfx(0x191, 0x7F);   // the starter motor (BeginEpilogue)
            if (timeOfDay < 7.f || timeOfDay >= 20.f) revCounter_.shown = true;
            state_ = 5;
            break;
        }
        ratchet();
        break;
    }
    case 3: {
        winding(1);
        if (std::fabs(turn_.value) < 1e-4f) {   // back to the start: round again
            arrows_[1].shown = false;
            wind_.reset();
            wind_.unbindAll();
            arrowFade_ = 1; arrowIn_ = true;
            turn_.min = -kTwoPi; turn_.max = 0;
            oneWay_.mode = 2;
            arrows_[0].shown = true;
            arrows_[0].alpha = (arrowMax_ - arrowMin_) * arrowFade_ + arrowMin_;
            wind_.bind(&oneWay_);
            state_ = 4;
        }
        ratchet();
        break;
    }
    case 5:   // UpdateEpilogue: +0x750 = 0x101, done and started
        finish(true, false);
        return;
    }
}

// loc_808B2C: RatchetSfx::TriggerRatchetSfx (a click whenever the turn passes a quarter) and the inserted
// screwdriver's two frames, alternating every eighth of a turn
void HotWiring::ratchet() {
    const int step = (int)std::floor(turn_.value / 1.5707964f);
    if (step != ratchetStep_) {
        if (playSfx) playSfx(0x22D, 0x7F);
        ratchetStep_ = step;
    }
    const int eighth = (int)((double)turn_.value / 0.7853981633974483);   // (qword_47B328, pi / 4)
    inserted_[0].shown = (eighth & 1) == 0;
    inserted_[1].shown = (eighth & 1) != 0;
}

void HotWiring::drawBackground(int texture, float x, float y, int W, int H) const {   // cBg::Render
    int tw = 1, th = 1;
    const unsigned gl = texture ? Assets_Texture(texture) : 0;
    if (!gl || !Assets_TextureSize(texture, &tw, &th)) return;
    const float left = appLeft(W, H), kx = W / (1024.f - left), ky = H / 768.f;
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, gl);
    glColor4f(1, 1, 1, 1);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex2f((x - left) * kx, y * ky);
    glTexCoord2f(1, 0); glVertex2f((x + tw - left) * kx, y * ky);
    glTexCoord2f(1, 1); glVertex2f((x + tw - left) * kx, (y + th) * ky);
    glTexCoord2f(0, 1); glVertex2f((x - left) * kx, (y + th) * ky);
    glEnd();
    glDisable(GL_TEXTURE_2D);
}

void HotWiring::drawSprite(const SpriteSet& set, unsigned tex, int tw, int th, const Sprite& s, int W, int H) const {
    if (!s.shown || s.index < 0 || s.index >= (int)set.sprites().size() || s.alpha <= 0) return;
    const SpriteDef& d = set.sprites()[s.index];
    const float left = appLeft(W, H), kx = W / (1024.f - left), ky = H / 768.f;
    const float w = d.w + 1.f, h = d.h + 1.f;
    const float a = s.rotation * 6.2831853f / 65536.f, c = std::cos(a), sn = std::sin(a);
    const float vx[4] = {-(float)d.offX, w - d.offX, w - d.offX, -(float)d.offX};
    const float vy[4] = {-(float)d.offY, -(float)d.offY, h - d.offY, h - d.offY};
    const float u[4] = {(float)d.x, d.x + w, d.x + w, (float)d.x};
    const float v[4] = {(float)d.y, (float)d.y, d.y + h, d.y + h};
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, tex);
    glColor4f(1, 1, 1, s.alpha);
    glBegin(GL_QUADS);
    for (int i = 0; i < 4; ++i) {
        const float px = vx[i] * c + vy[i] * sn, py = -vx[i] * sn + vy[i] * c;
        glTexCoord2f(u[i] / tw, v[i] / th);
        glVertex2f((s.x + px - left) * kx, (s.y + py) * ky);
    }
    glEnd();
    glDisable(GL_TEXTURE_2D);
}

void HotWiring::render(int W, int H) const {
    if (!active_ || W <= 0 || H <= 0) return;
    Hud_Begin(W, H);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    // cPda::Render, layers 6 down to 0: each layer's background, then its sprites
    const Sprite* all[] = {&screwdriver_, &glow_, &inserted_[0], &inserted_[1], &arrows_[0], &arrows_[1], &lock_, &revCounter_};
    for (int layer = 6; layer >= 0; --layer) {
        if (layer == 6) drawBackground(dashboard_, 0x400 - 0x19F, 0x300 - 0x172, W, H);
        if (layer == 1) drawBackground(frameLine_, 0x400 - 0x1A2, 0x300 - 0x17A, W, H);
        for (const Sprite* s : all)
            if (s->priority == layer) drawSprite(sprites_, sheet_, sheetW_, sheetH_, *s, W, H);
        if (layer == 0) drawSprite(always_, alwaysSheet_, alwaysW_, alwaysH_, exit_, W, H);
    }
    Hud_End();
}
