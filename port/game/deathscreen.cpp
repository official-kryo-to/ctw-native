// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "deathscreen.h"
#include "random.h"
#include "sound.h"
#include <glad/gl.h>
#include <algorithm>
#include <cmath>

bool DeathScreen::init(const std::string& dataDir) {
    return tables_.load(dataDir + "/shard_tables.bin");
}

void DeathScreen::open(bool wasted) {   // Gui::cDeathArrestApp::Init
    open_ = true;
    captured_ = false;
    counter_ = 0;
    set_ = (int)(Rand32NonCritical(0) % 9);   // +0x198, taken modulo nine by RenderScreenShards
    TheSound().playHud(wasted ? 0x358 : 0x16C, 0x7F);   // cAudioManager::PlayWasted / PlayBusted
}

void DeathScreen::close() { open_ = false; }

void DeathScreen::render(int width, int height) {
    if (!open_ || width <= 0 || height <= 0) return;
    if (!captured_) {   // the frame as it was drawn, kept in a texture (the original renders into its target)
        if (!texture_) glGenTextures(1, &texture_);
        glBindTexture(GL_TEXTURE_2D, texture_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glReadBuffer(GL_BACK);
        glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 0, 0, width, height, 0);
        texW_ = width; texH_ = height;
        captured_ = true;
    }
    if (!counter_ || !tables_.loaded) return;   // Render: nothing over the game until the first Process
    // Gui::cDeathArrestApp::Render: clear to black, then RenderScreenShards(set, (counter - 1) / 30, 1)
    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    const float t = std::min((counter_ - 1) / 30.f, 1.f);
    // RenderScreenShards: every vertex moves from its start towards its end by sin(t * pi / 2) * 1.5; positions
    // are on a 1024 x 768 orthographic screen (y up) and the texture coordinates are twice them, so at t = 0 the
    // shards rebuild the frame exactly
    const float k = std::sin((t < 0.f ? 0.f : t) * 3.14159f * 0.5f) * 1.5f;
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0.0, 1024.0, 0.0, 768.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_LIGHTING);
    glDisable(GL_FOG);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texture_);
    glColor4f(1.f, 1.f, 1.f, 1.f);
    for (const ShardTables::Shard& s : tables_.sets[set_]) {
        glBegin(GL_TRIANGLE_FAN);
        for (int v = 0; v < s.count; ++v) {
            const int16_t* a = s.start[v];
            const int16_t* b = s.end[v];
            const float x = std::clamp(a[0] + (b[0] - a[0]) * k, -32768.f, 32767.f);
            const float y = std::clamp(a[1] + (b[1] - a[1]) * k, -32768.f, 32767.f);
            glTexCoord2f(a[2] / 2048.f, a[3] / 1536.f);
            glVertex2f(std::trunc(x), std::trunc(y));
        }
        glEnd();
    }
    glDisable(GL_TEXTURE_2D);
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
}

void DeathScreen::shutdown() {
    if (texture_) glDeleteTextures(1, &texture_);
    texture_ = 0;
    open_ = captured_ = false;
}
