// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// The game's side of cutscenes: cSequenceManager::Update, CameraImpl's sequence camera and the comic overlay.
#include "game.h"
#include "hud.h"
#include "os/gamefs.h"
#include <SDL.h>
#include <cstdio>

Sequence* Game::startSequence(const std::string& name, bool loop) {
    GameFs fs;
    auto seq = std::make_unique<Sequence>();
    if (!fs.open(dataDir) || !seq->load(fs, name)) {
        std::fprintf(stderr, "sequence %s not found\n", name.c_str());
        return nullptr;
    }
    std::printf("\n**********\nLoading Sequence %s\n**********\n", name.c_str());
    if (sequence) {   // the one playing ends first (its own callbacks fire)
        Sequence* old = sequence.get();
        old->stop();
        if (sequence.get() == old) retireSequence();
    }
    // the sequence camera starts where the game's camera is, so a first key that is not a cut tweens from it
    gameCamera(seqCamera.eye, seqCamera.yaw, seqCamera.pitch);
    seqCamera.active = true;
    seq->placeCamera = [this](const int32_t pos[3], uint16_t pitch, uint16_t yaw, int frames, int movement) {
        seqCamera.place(pos, pitch, yaw, frames, movement);
    };
    seq->restoreCamera = [this] { seqCamera.active = false; };
    seq->fastForward = [this] { seqCamera.tick(); };
    sequence = std::move(seq);
    cutscene = true;   // cSequence::Play: MakeSafeForCutscene, EnableControls(false), StartCutscene
    if (sequence->hasOverlay()) comic.begin(*sequence, world.timeCycle());
    sequence->play(loop);
    seqTap_ = seqSkip_ = false;
    return sequence.get();
}

void Game::retireSequence() {   // cSequence::Stop's end: EndCutscene, the player's controls back
    if (!sequence) return;
    retiredSequences_.push_back(std::move(sequence));
    cutscene = false;
    seqCamera.active = false;
    comic.end();
}

void Game::stopSequence() {
    if (!sequence) return;
    Sequence* old = sequence.get();
    old->stop();   // its callbacks may start the next one
    if (sequence.get() == old) retireSequence();
}

void Game::updateSequence() {
    retiredSequences_.clear();
    const bool tapped = seqTap_, skip = seqSkip_;
    seqTap_ = seqSkip_ = false;
    if (!sequence) return;
    bool held = false;
    if (!scriptedInput) {   // the touch held down: Space or the left mouse button
        const Uint8* ks = SDL_GetKeyboardState(nullptr);
        held = ks[SDL_SCANCODE_SPACE] || (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON(SDL_BUTTON_LEFT));
    }
    Sequence* seq = sequence.get();
    const bool running = seq->update(tapped, held, skip, false);
    if (sequence.get() != seq) return;   // a callback started another sequence
    if (!running) { retireSequence(); return; }
    comic.process(*seq);
    seqCamera.tick();
}

bool Game::cutsceneKey(int k) {
    if (!sequence) return false;
    if (k == SDL_SCANCODE_ESCAPE) { seqSkip_ = true; return true; }   // the pad's Start: skips to the end
    if (k == SDL_SCANCODE_RETURN || k == SDL_SCANCODE_KP_ENTER || k == SDL_SCANCODE_SPACE || k == SDL_SCANCODE_F) {
        seqTap_ = true;   // cTouchScreen::SingleClicked / the B button
        return true;
    }
    return false;
}

void Game::gameCamera(int32_t eye[3], uint16_t& yaw, uint16_t& pitch) const {
    if (player.dead || arrested_) {
        std::copy(deathEye_, deathEye_ + 3, eye);
        yaw = deathYaw_;
        pitch = deathPitch_;
    } else if (playerCar >= 0) {
        carCam.position(eye);
        yaw = carCam.yaw();
        pitch = carCam.pitch();
    } else if (scope.active) {
        std::copy(scope.pos, scope.pos + 3, eye);
        yaw = scope.yaw;
        pitch = 0xC000;
    } else {
        camera.position(eye);
        yaw = camera.yaw;
        pitch = camera.pitch();
    }
}

void Game::renderCutscene(int W, int H) {
    if (!sequence) return;
    if (comic.hidesWorld()) {   // cRenderWorld::DisableRendering: nothing of the city around the panels
        Hud_Begin(W, H);
        Hud_Rect(0, 0, (float)W, (float)H, 0x000000FFu);
        Hud_End();
    }
    comic.render(W, H);
}
