// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "loadingscreen.h"
#include "gfx/texload.h"
#include "os/datafile.h"
#include "os/dxtbin.h"
#include "os/os.h"
#include "os/pak.h"
#include "os/wad.h"
#include <glad/gl.h>
#include <stb_image.h>
#include <algorithm>
#include <cmath>
#include <cstring>

bool LoadingScreen::init(const std::string& dir) {
    shutdown();
    start_ = OS_TimeAccurate();
    // Android loading-app Init (0x78e5f0): background 87 from the background
    // directory gGameDir[26], and SS_Loading[0]. Read the user's own resources.
    Pak pak;
    std::vector<uint8_t> gameDir, backgrounds;
    if (pak.open(dir + "/game.pak") && pak.read(0, gameDir) && gameDir.size() >= 56) {
        uint16_t directory;
        std::memcpy(&directory, gameDir.data() + 26 * 2, 2);
        if (pak.read(directory, backgrounds) && backgrounds.size() >= 88 * 2) {
            uint16_t id;
            std::memcpy(&id, backgrounds.data() + 87 * 2, 2);
            DxtBin dxt;
            const bool hasDxt = dxt.open(dir + "/dxt.bin");
            TextureLoader loader;
            loader.init(&pak, hasDxt ? &dxt : nullptr);
            TexInfo texture;
            if (loader.load(id, &texture)) {
                background_ = texture.gl;
                backgroundWidth_ = texture.width;
                backgroundHeight_ = texture.height;
            }
        }
    }
    Wad wad;
    std::vector<uint8_t> defs, png;
    if (wad.open(dir) && wad.read("SS_Loading.bin", defs) && sprites_.parse(defs) &&
        !sprites_.sprites().empty() && Data_ReadAll(dir + "/ss_loading.png", png)) {
        int channels;
        auto* pixels = stbi_load_from_memory(png.data(), (int)png.size(), &ringWidth_, &ringHeight_, &channels, 4);
        if (pixels) {
            const auto& s = sprites_.sprites()[0];
            if (2u * (s.x + s.w + 1u) <= (unsigned)ringWidth_ &&
                2u * (s.y + s.h + 1u) <= (unsigned)ringHeight_) {
                glGenTextures(1, &ring_);
                glBindTexture(GL_TEXTURE_2D, ring_);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ringWidth_, ringHeight_, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
            }
            stbi_image_free(pixels);
        }
    }
    // The legal splash is a loose asset; the port's text is English, so it is the English one (GameActivity picks
    // it by the device language).
    std::vector<uint8_t> legal;
    int channels;
    if (Data_ReadAll(dir + "/LegalScreens_ENG.png", legal))
        if (auto* pixels = stbi_load_from_memory(legal.data(), (int)legal.size(), &legalWidth_, &legalHeight_,
                                                 &channels, 4)) {
            glGenTextures(1, &legal_);
            glBindTexture(GL_TEXTURE_2D, legal_);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, legalWidth_, legalHeight_, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
            stbi_image_free(pixels);
        }
    return background_ && ring_;
}

void LoadingScreen::shutdown() {
    if (background_) glDeleteTextures(1, &background_);
    if (ring_) glDeleteTextures(1, &ring_);
    if (legal_) glDeleteTextures(1, &legal_);
    background_ = ring_ = legal_ = 0;
    backgroundWidth_ = backgroundHeight_ = ringWidth_ = ringHeight_ = legalWidth_ = legalHeight_ = 0;
    sprites_ = SpriteSet{};
}

int16_t LoadingScreen::rotation(unsigned frames) {
    // Android loading-app Process (0x78e848) converts frame / 16 * -65535
    // to a saturated signed angle. The short native transition ends at frame 9.
    if (frames >= 9) return -32768;
    return (int16_t)(frames / 16.f * -65535.f);
}

void LoadingScreen::render(int width, int height, double seconds) const { draw(width, height, false, seconds); }

void LoadingScreen::renderLegal(int width, int height) const { draw(width, height, true, 0); }

void LoadingScreen::draw(int width, int height, bool legal, double seconds) const {
    if (width <= 0 || height <= 0) return;
    GLint oldMode, oldProgram, oldActive, oldDraw, oldRead;
    glGetIntegerv(GL_MATRIX_MODE, &oldMode);
    glGetIntegerv(GL_CURRENT_PROGRAM, &oldProgram);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &oldActive);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &oldDraw);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &oldRead);
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glUseProgram(0);
    glActiveTexture(GL_TEXTURE0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width, height);
    glDisable(GL_SCISSOR_TEST); glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);
    glDisable(GL_LIGHTING); glDisable(GL_FOG); glDisable(GL_ALPHA_TEST); glDisable(GL_BLEND);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
    glOrtho(0, width, height, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
    auto quad = [](float x, float y, float w, float h, float u0, float v0, float u1, float v1) {
        glBegin(GL_QUADS);
        glTexCoord2f(u0, v0); glVertex2f(x, y);
        glTexCoord2f(u1, v0); glVertex2f(x + w, y);
        glTexCoord2f(u1, v1); glVertex2f(x + w, y + h);
        glTexCoord2f(u0, v1); glVertex2f(x, y + h);
        glEnd();
    };
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glColor4f(1, 1, 1, 1);
    if (legal) {   // ImageView's default FIT_CENTER over the still black game surface
        if (legal_) {
            const float k = std::min((float)width / legalWidth_, (float)height / legalHeight_);
            glBindTexture(GL_TEXTURE_2D, legal_);
            glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            quad((width - legalWidth_ * k) * .5f, (height - legalHeight_ * k) * .5f, legalWidth_ * k, legalHeight_ * k,
                 0, 0, 1, 1);
        }
    } else {
        // Desktop adaptation: centre the native 480 x 320 loading layout and preserve
        // its aspect ratio on resize. Its background is padded to a 512 x 512 texture.
        const float k = std::min(width / 480.f, height / 320.f);
        glTranslatef((width - 480 * k) * .5f, (height - 320 * k) * .5f, 0);
        glScalef(k, k, 1);
        if (background_) {
            glBindTexture(GL_TEXTURE_2D, background_);
            quad(0, 0, 480, 320, 0, 0, 480.f / backgroundWidth_, 320.f / backgroundHeight_);
        }
        if (ring_ && !sprites_.sprites().empty()) {
            const auto& s = sprites_.sprites()[0];
            glBindTexture(GL_TEXTURE_2D, ring_);
            glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glTranslatef(240, 160, 0);
            // the front end's 30 Hz frames since the app started: a half-turn, then it holds however long loading takes
            const unsigned frame = (unsigned)std::min(std::max(seconds, 0.0) * 30.0, 1e6);
            glRotatef(rotation(frame) * 360.f / 65536.f, 0, 0, 1);
            quad(-(float)s.offX, -(float)s.offY, s.w + 1.f, s.h + 1.f,
                 2.f * s.x / ringWidth_, 2.f * s.y / ringHeight_,
                 2.f * (s.x + s.w + 1.f) / ringWidth_, 2.f * (s.y + s.h + 1.f) / ringHeight_);
        }
    }
    glMatrixMode(GL_MODELVIEW); glPopMatrix();
    glMatrixMode(GL_PROJECTION); glPopMatrix();
    glMatrixMode((GLenum)oldMode);
    // Draw/read buffer enums belong to the bound framebuffer. Restore its
    // binding before popping GL_COLOR_BUFFER_BIT's saved attachment selection.
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)oldDraw);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)oldRead);
    glPopAttrib();
    glUseProgram((GLuint)oldProgram);
    glActiveTexture((GLenum)oldActive);
}

bool LoadingScreen::present() {
    if (!Host_PumpEvents()) return false;
    // Discard startup input; a key pressed while waiting must not open a menu or
    // enter a vehicle as soon as initialization finishes.
    while (Host_PopKey()) {}
    Host_PopClicks();
    while (Host_PopWheel()) {}
    Host_PopText();
    float dx, dy;
    Host_PopMouseDelta(&dx, &dy);
    const double now = OS_TimeAccurate();
    if (legal_ && now < ringStart()) renderLegal((int)OS_ScreenGetWidth(), (int)OS_ScreenGetHeight());
    else render((int)OS_ScreenGetWidth(), (int)OS_ScreenGetHeight(), now - ringStart());
    OS_ScreenSwapBuffers();
    return true;
}

bool LoadingScreen::finish() {
    // the splash stays for its full five seconds, and the loading app ends once more than eight frames have passed
    while (OS_TimeAccurate() < ringStart() + 9.0 / 30.0) {
        if (!present()) return false;
        OS_ThreadSleep(1000000 / 30);
    }
    return true;
}
