// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// The game's side of cutscenes: cSequenceManager::Update, CameraImpl's sequence camera and the comic overlay.
#include "game.h"
#include "hud.h"
#include "os/gamefs.h"
#include <cstring>
#include "sound.h"
#include <SDL.h>
#include <glad/gl.h>
#include <cmath>
#include <algorithm>
#include <cstdio>

Sequence* Game::startSequence(const std::string& name, bool loop, bool bars) {
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
    widescreen.set(bars);
    comic.end();   // (loading a sequence unloads the last one: cSequence::UnLoad)
    if (sequence->hasOverlay()) comic.begin(*sequence, world.timeCycle());
    sequence->play(loop);
    seqTap_ = seqSkip_ = false;
    return sequence.get();
}

void Game::retireSequence() {   // cSequence::Stop's end: EndCutscene, the player's controls back
    if (!sequence) return;
    const bool keepComic = sequence->restoreComic;
    retiredSequences_.push_back(std::move(sequence));
    cutscene = scriptCutscene;
    widescreen.set(false);   // (CameraImpl::EndCutscene)
    if (!scriptCutscene) seqCamera.active = false;
    comic.stop(keepComic);
}

void Game::unloadSequence() { comic.end(); }   // cScriptSequenceBase::UnLoadSequence (or its process ending)

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
    } else if (minigameCam.active()) {
        minigameCam.position(eye);
        yaw = minigameCam.yaw();
        pitch = minigameCam.pitch();
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
    if (comic.hidesWorld()) {   // cRenderWorld::DisableRendering: nothing of the city around the panels
        Hud_Begin(W, H);
        Hud_Rect(0, 0, (float)W, (float)H, 0x000000FFu);
        Hud_End();
    }
    comic.render(W, H);
}

// ---- the sinking car minigame (sinkingcar.h) ----

bool Game::startSinkingCar(int style, std::function<void(bool)> done) {
    sinkingCar.playSfx = [](int sample, int volume) { TheSound().playHud(sample, volume); };
    sinkingCar.fade = [this](bool toBlack, int frames) { if (toBlack) screenFade.toBlack(frames); else screenFade.in(frames); };
    sinkingCar.fading = [this] { return screenFade.fading(); };
    sinkingCar.splash = [this] {   // CreateWaterExplosion: explosion 8 at the player (or their vehicle), z -7.5
        int32_t at[3];
        focus(at);
        at[2] = -0x7800;
        const int32_t still[3] = {0, 0, 0};
        addWaterSplash(at, 8, still, -1);
    };
    const bool started = sinkingCar.start(dataDir, style, [this, done](bool escaped) {
        if (!escaped) {   // state 5: SetHealth(0), cStatsManager::IncFishesFed
            player.takeDamage(1000, true);
            stats.incFishesFed();
        }
        if (done) done(escaped);
    });
    // cSinkingCarApp::ProcessIntro (or Process), once its backgrounds are loaded: cMinigameCam 10 units along the
    // view's left (qword_47F010), the app's own camera until a script restores the player's (EndCutscene)
    if (started) startMinigameCam(-0xA000);
    return started;
}

// ---- missions (mission.h) ----

bool Game::startMission(const std::string& name) {
    std::unique_ptr<Mission> m = Mission_Create(name);
    if (!m) { std::fprintf(stderr, "no mission %s\n", name.c_str()); return false; }
    mission = std::move(m);
    mission->start(*this);
    return true;
}

// cActStructure::UnloadCurrentMission's HUDImpl::DisplayWinLose(id, 0, font 5, frames, true, true, false)
void Game::showWinLose(const std::u16string& text, int frames) {
    hudText.displayWinLose(text, 5, (unsigned)frames, true, true);
}

void Game::showObjective(const std::u16string& text, int frames) {
    // (the player's +0x118A, a cutscene running, with the camera's bars +0x108D coming in or +0x1090 out in full)
    const bool inBorder = cutscene && (widescreen.dir > 0 || widescreen.amount == 1.f);
    hudText.displayObjective(text, (unsigned)frames, true, inBorder, cutscene);
}

void Game::addBlip(const int32_t pos[3], uint32_t rgba, int id, int marker) {
    removeBlip(id);
    blips.push_back({{pos[0], pos[1], pos[2]}, rgba, id});
    RadarBlip& b = blips.back();
    b.marker = (int8_t)marker;
    if (id < kPluginBlips) {   // iPosition::iPosition: sprite 106; it pops in after the blips made just before it
        b.sprite = 106;
        b.appear = (int16_t)(-0x2000 - blipStagger_);
        b.spriteScale = 0x1000;
        const int next = blipStagger_ + 0x19A;   // cAreaManager::IncrementBlipScaleStagger
        blipStagger_ = (int16_t)(next < 0x3000 ? next : 0x3000);
    }
}

void Game::stepBlips() {
    if (blipStagger_ >= 1) blipStagger_ = (int16_t)(std::max<int>(blipStagger_, 0x19A) - 0x19A);   // cAreaManager::Process
    for (RadarBlip& b : blips) {   // iPosition::DrawBlip: the sprite's scale on its way to +0x44 (1.0)
        if (b.sprite < 0 || b.appear == 0x1000) continue;
        const int cur = b.appear, target = 0x1000;
        if (cur <= -0x2001) {   // waiting its turn, not shown
            b.spriteScale = 0;
            b.appear = (int16_t)(cur > -0x219A ? 0 : cur + 0x199);
        } else if (cur > 0) {   // shrinking back to 1 from 2
            if (target < cur) b.appear = b.spriteScale = (int16_t)std::max(cur - 0x333, target);
        } else if (cur - 0xB33 > -0x2001) {   // growing: 0.7, 1.4 ..
            b.spriteScale = (int16_t)(0xB33 - cur);
            b.appear = (int16_t)(cur - 0xB33);
        } else {
            b.appear = b.spriteScale = 0x2000;
        }
    }
}

void Game::addChevron(const int32_t pos[3], uint32_t rgba, int id, int degrees, int type, int palette, int style,
                      int32_t size) {
    addBlip(pos, rgba, id, 5);
    RadarBlip& b = blips.back();
    b.markerColour = (uint8_t)(palette - 5);
    b.palette = (uint8_t)palette;   // ChangeBlipStyle(.., palette, style, 1, false): SetColour(palette - 5)
    b.onRadar = style <= 3 && ((3 >> style) & 1);   // Marker::SetBlip
    b.markerAngle = (int16_t)((degrees - 180) * 0xB6);   // Marker::SetAngle(degrees - 180)
    b.chevronType = (uint8_t)type;
    b.markerSize = size;
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

// cCamera::DrawWideScreenBars: two 1024 x 133 quads, at y 0 and 643, in OrthoCorrectionBeforeRender's mode 0x20 (the
// 1024 x 768 space at the screen's width a unit, about the screen's middle; y up)
void Game::renderWidescreenBars(int W, int H) const {
    if (widescreen.amount <= 0.f || W <= 0 || H <= 0) return;
    const double s = W / 1024.0;
    glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_CURRENT_BIT | GL_VIEWPORT_BIT);
    glViewport(0, 0, W, H);
    glDisable(GL_LIGHTING);
    glDisable(GL_CULL_FACE);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_FOG);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0.0, 1024.0, 384.0 - H / (2 * s), 384.0 + H / (2 * s), 0.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(0.f, 0.f, 0.f, widescreen.amount);
    for (float y : {0.f, 643.f}) {
        glBegin(GL_TRIANGLE_STRIP);
        glVertex2f(0.f, y); glVertex2f(1024.f, y); glVertex2f(0.f, y + 133.f); glVertex2f(1024.f, y + 133.f);
        glEnd();
    }
    glColor4f(1, 1, 1, 1);
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glPopAttrib();
}

void Game::renderMissionHud(int W, int H) {
    if (screenFade.alpha > 0.f) {   // cScreenFade: black over the picture
        Hud_Begin(W, H);
        Hud_Rect(0, 0, (float)W, (float)H, (uint32_t)(screenFade.alpha * 255.f));
        Hud_End();
    }
    renderWidescreenBars(W, H);   // (cPda::Render: under every sprite list)
    HudText::RenderSprites(W, H);   // the Gfx2d sprites: the objective line, the help box, the win/lose line
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

Vehicle::Controls Game::carControls(const Vehicle& car) const {
    const auto it = scriptCarControls.find(car.uid);
    return it != scriptCarControls.end() ? it->second : traffic.controls(car.uid);
}
