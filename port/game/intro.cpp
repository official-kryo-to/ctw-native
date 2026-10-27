// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "intro.h"
#include "audio/audio.h"
#include "os/os.h"
#include "video/movie.h"
#include <SDL.h>
#include <glad/gl.h>

bool IntroMovie::skipHeld(bool held, unsigned nowMs, unsigned& startMs) {
    // UpdateMovieCallback: the first held tick starts the timer, more than 1000 ms held stops the movie
    if (!held) { startMs = 0; return false; }
    if (!startMs) { startMs = nowMs ? nowMs : 1; return false; }
    return nowMs - startMs > 1000;
}

namespace {
bool anythingHeld() {   // the PC's touches and pad buttons: any key, mouse button or controller button
    int count = 0;
    const Uint8* keys = SDL_GetKeyboardState(&count);
    for (int i = 0; i < count; ++i) if (keys[i]) return true;
    if (Host_MouseDown(0) || Host_MouseDown(1) || Host_MouseDown(2)) return true;
    const HostPad& pad = Host_Pad();
    return pad.connected && (pad.a || pad.b || pad.x || pad.y || pad.lb || pad.rb || pad.lt > .5f || pad.rt > .5f);
}

void discardInput() {
    while (Host_PopKey()) {}
    Host_PopClicks();
    while (Host_PopWheel()) {}
    Host_PopText();
    float dx, dy;
    Host_PopMouseDelta(&dx, &dy);
}

void draw(unsigned texture, bool picture) {
    const int w = (int)OS_ScreenGetWidth(), h = (int)OS_ScreenGetHeight();
    glViewport(0, 0, w, h);
    glDisable(GL_SCISSOR_TEST); glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
    glUseProgram(0);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    if (!picture) return;
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0, 1, 1, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glBegin(GL_QUADS);   // the movie's surface fills the screen and the picture is scaled to it
    glTexCoord2f(0, 0); glVertex2f(0, 0);
    glTexCoord2f(1, 0); glVertex2f(1, 0);
    glTexCoord2f(1, 1); glVertex2f(1, 1);
    glTexCoord2f(0, 1); glVertex2f(0, 1);
    glEnd();
    glDisable(GL_TEXTURE_2D);
}
}

bool IntroMovie::play(const std::string& dataDir) {
    Movie movie;
    if (!movie.open(dataDir + "/ctw_iphone_intro.mp4")) return true;   // no movie or no decoder: straight on
    const bool musicWasPaused = !Audio_MusicPlaying();
    Audio_SetMusicPaused(true);
    unsigned texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    bool open = true, uploaded = false;
    const double start = OS_TimeAccurate();
    double nextTick = start;
    unsigned heldSince = 0;
    unsigned shown = 0;
    while ((open = Host_PumpEvents())) {
        discardInput();
        const double now = OS_TimeAccurate();
        if (!movie.update(now - start)) break;
        if (now >= nextTick) {   // the skip check runs with the game's 30 Hz ticks
            nextTick += 1.0 / 30.0;
            if (skipHeld(anythingHeld(), (unsigned)(now * 1000.0), heldSince)) break;
        }
        if (movie.frameReady() && movie.pictures() != shown) {
            shown = movie.pictures();
            glBindTexture(GL_TEXTURE_2D, texture);
            if (!uploaded) glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, movie.width(), movie.height(), 0, GL_RGBA,
                                        GL_UNSIGNED_BYTE, movie.rgba().data());
            else glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, movie.width(), movie.height(), GL_RGBA, GL_UNSIGNED_BYTE,
                                 movie.rgba().data());
            uploaded = true;
        }
        draw(texture, uploaded);
        OS_ScreenSwapBuffers();
    }
    movie.close();
    glDeleteTextures(1, &texture);
    if (!musicWasPaused) Audio_SetMusicPaused(false);   // cIntroDrive::EndedSequence: PauseSound(0)
    discardInput();
    return open;
}
