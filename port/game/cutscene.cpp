// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// The game's side of cutscenes: cSequenceManager::Update, CameraImpl's sequence camera and the comic overlay.
#include "game.h"
#include "hud.h"
#include "os/gamefs.h"
#include <SDL.h>
#include <glad/gl.h>
#include <cmath>
#include <algorithm>
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
    if (!seqCamera.active) gameCamera(seqCamera.eye, seqCamera.yaw, seqCamera.pitch);
    seqCamera.active = seq->hasCameras();   // (one without cameras leaves the game's camera on the player)
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
    cutscene = scriptCutscene;
    if (!scriptCutscene) seqCamera.active = false;
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

// ---- missions (mission.h) ----

bool Game::startMission(const std::string& name) {
    std::unique_ptr<Mission> m = Mission_Create(name);
    if (!m) { std::fprintf(stderr, "no mission %s\n", name.c_str()); return false; }
    mission = std::move(m);
    mission->start(*this);
    return true;
}

void Game::showObjective(const std::u16string& text, int frames) {
    objective.text = text;
    objective.frames = text.empty() ? 0 : frames;
}

void Game::addBlip(const int32_t pos[3], uint32_t rgba, int id) {
    removeBlip(id);
    blips.push_back({{pos[0], pos[1], pos[2]}, rgba, id});
}

void Game::removeBlip(int id) {
    blips.erase(std::remove_if(blips.begin(), blips.end(), [id](const RadarBlip& b) { return b.id == id; }), blips.end());
}

std::u16string Game::mainText(int id) const {
    if (id < 0 || id >= (int)mainText_.size()) return {};
    std::u16string s = mainText_[id];
    while (!s.empty() && s.back() == 0) s.pop_back();
    return s;
}

void Game::renderMissionHud(int W, int H) {
    if (screenFade.alpha > 0.f) {   // cScreenFade: black over the picture
        Hud_Begin(W, H);
        Hud_Rect(0, 0, (float)W, (float)H, (uint32_t)(screenFade.alpha * 255.f));
        Hud_End();
    }
    if (objective.frames > 0 && !objective.text.empty() && !(sequence && comic.active())) {
        // HUDText's objective: centred under the middle of the screen, wrapped to two thirds of its width
        Hud_Begin(W, H);
        const float k = H / 768.f, scale = 1.5f * k, width = W * 0.66f;
        const std::u16string wrapped = Hud_Wrap16(objective.text, width, scale);
        int lines = 1;
        for (char16_t c : wrapped) lines += c == u'\n';
        const float y = H - (90.f * k + lines * Hud_LineHeight(scale));
        // each line centred: drawn line by line
        size_t from = 0;
        for (int i = 0; i < lines; ++i) {
            size_t to = wrapped.find(u'\n', from);
            if (to == std::u16string::npos) to = wrapped.size();
            const std::u16string line = wrapped.substr(from, to - from);
            std::string ascii;
            for (char16_t c : line) if (c < 0x80) ascii += (char)c;
            const float w = Hud_TextWidth(ascii, scale);
            Hud_Text16((W - w) * 0.5f + 2.f * k, y + i * Hud_LineHeight(scale) + 2.f * k, scale, 0x000000C0u, line);
            Hud_Text16((W - w) * 0.5f, y + i * Hud_LineHeight(scale), scale, 0xFFFFFFFFu, line);
            from = to + 1;
        }
        Hud_End();
    }
}

// ---- script props ----

Game::ScriptProp* Game::scriptProp(int id) {
    for (ScriptProp& p : scriptProps) if (p.id == id) return &p;
    return nullptr;
}

void Game::removeScriptProp(int id) {
    scriptProps.erase(std::remove_if(scriptProps.begin(), scriptProps.end(), [id](const ScriptProp& p) { return p.id == id; }),
                      scriptProps.end());
}

void Game::renderScriptProps() {
    for (const ScriptProp& p : scriptProps) {
        const Model* m = props.model(p.prop);
        if (!m) continue;
        // SimpleMover::SetHeading turns clockwise: a model's nose is its +y and points along (sin h, cos h)
        // (cPreIntro's plane at heading 0 leaves its contrails to the south; cPlaneLand's at 225 degrees flies
        // south-west). The pitch turns the nose about the model's x axis; a negative pitch lifts it (the flare).
        const float h = p.heading * 3.14159265f / 32768.f, a = -p.pitch * 3.14159265f / 32768.f;
        const float c = std::cos(h), s = std::sin(h), ca = std::cos(a), sa = std::sin(a);
        const float M[16] = {c, -s, 0, 0,
                             s * ca, c * ca, sa, 0,
                             -s * sa, -c * sa, ca, 0,
                             p.pos[0] / 4096.f, p.pos[1] / 4096.f, p.pos[2] / 4096.f, 1};
        PropLibrary::drawModel(*m, M);
    }
}
