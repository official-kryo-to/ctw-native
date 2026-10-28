// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// kena01::cKEN_A01, the first mission ("Arrival in Liberty"): the prologue on the plane, the landing, the ambush
// at the port and the intro movie, then Huang on the jetty, a car, Uncle Kenny's restaurant and the apartment.
//
// Not ported yet, and passed over here: the clouds, contrails and fountains of cPreIntro, the plane's sound, the
// bodyguards and ambushers that NewIntro2's callbacks move (cKenA01Intro), the credits drive (cIntroDrive's
// DriveAround_01), the sinking car and its minigame (cCarInWater, cSinkingCarApp), swimming to the jetty, the
// police on the shore (cPoliceChase), hotwiring (cHotWiringApp), the GPS route and the help texts' timing.
#include "mission.h"
#include "game.h"
#include "intro.h"
#include <cmath>

namespace {
constexpr int32_t fx(float v) { return (int32_t)(v * 4096.f); }
constexpr int deg(int d) { return d * 0xB6; }   // SimpleMover::SetHeading / SetPitch take degrees
enum { kPlane = 1, kShadow = 2 };                // the script's props

class KenA01 : public Mission {
public:
    void start(Game& game) override {
        g_ = &game;
        loadText(game.dataDir, "KEN_A01");   // DoneIntro's InitMissionText("KEN_A01")
        init();
    }

private:
    Game* g_ = nullptr;
    uint32_t carUid_ = 0;
    int planeFrame_ = 0, planePitch_ = 0;   // cPlaneLand +0x330, +0x328
    bool planeDone_ = false;                // +0x334

    void placePlayer(float x, float y, float z) {   // Ped::SetPosition
        const int32_t at[3] = {fx(x), fx(y), fx(z)};
        std::copy(at, at + 3, g_->player.pos);
    }

    // cKEN_A01::Init: the screen goes black, then FadedInit
    void init() {
        g_->cutscene = g_->scriptCutscene = true;
        g_->screenFade.toBlack(1, [this] { doPlaneIntro(); });
    }

    void setTime(uint32_t t) {
        g_->world.timeCycle().setTime(t);
        g_->world.timeCycle().evaluate();
    }

    // a sequence whose normal end goes on to `next` and whose skip-all (Escape, or holding) goes to the movie
    Sequence* play(const char* name, std::function<void()> next, bool skipAllToMovie = true) {
        Sequence* s = g_->startSequence(name);
        if (!s) { wait(1, next); return nullptr; }
        s->setCallback(Sequence::kEnded, [this, next] { wait(1, next); });
        s->setCallback(Sequence::kSkipped, [this, next] { wait(1, next); });
        if (skipAllToMovie) s->setCallback(Sequence::kSkippedWithB, [this] { wait(1, [this] { startIntro(); }); });
        return s;
    }

    // DoPlaneIntro / cPreIntro::Begin: noon, Huang near the airport (so the city there is loaded), the plane over
    // the sea under the first camera, then his monologue on the plane (NewIntro1), faded in
    void doPlaneIntro() {
        setTime(0xC000);
        placePlayer(1762.45f, -66.53f, 2.f);
        g_->scriptProps.push_back({kPlane, 8, {fx(1411.72f), fx(692.70f), fx(10.f)}, 0, 0});
        play("CSS_KEN_A01_NewIntro1.seq", [this] { g_->removeScriptProp(kPlane); doLandingIntro(); });
        g_->screenFade.in(30);
    }

    // DoLandingIntro / cPlaneLand::IntroSetup: the plane high over the bay heading south-west with its shadow on
    // the water below; callback 0 starts it (StartPlane), then WaitAfterLanding's 30 frames
    void doLandingIntro() {
        g_->removeScriptProp(kPlane);
        g_->scriptProps.push_back({kPlane, 8, {fx(2085.08f), fx(-71.64f), fx(80.f)}, (uint16_t)deg(0xE1), 0});
        g_->scriptProps.push_back({kShadow, 0x8D, {fx(2085.08f), fx(-71.64f), fx(2.f)}, (uint16_t)deg(0xE1), 0});
        placePlayer(1752.17f, 21.06f, 0.f);
        planeFrame_ = planePitch_ = 0;
        planeDone_ = false;
        Sequence* s = play("CSS_KEN_A01_PlaneLand.seq", [this] {
            g_->removeScriptProp(kPlane);
            g_->removeScriptProp(kShadow);
            wait(0x1E, [this] { doPortIntro(); });
        });
        if (s) s->setCallback(0, [this] { wait(1, [this] { movePlane(); }); });   // StartPlane
    }

    // cPlaneLand::MovePlane, every frame: down the glide path, the shadow under it, the nose lifting for the last
    // seventy frames; gone after 181
    void movePlane() {
        const int n = planeFrame_;
        if (Game::ScriptProp* plane = g_->scriptProp(kPlane)) {
            plane->pos[0] = fx(2085.08f) + n * fx(-1.445f);
            plane->pos[1] = fx(-71.64f) + n * fx(-1.45f);
            plane->pos[2] = 0x50000 + n * (int32_t)0xFFFFF8E4;
            if (Game::ScriptProp* shadow = g_->scriptProp(kShadow)) {
                shadow->pos[0] = plane->pos[0];
                shadow->pos[1] = plane->pos[1];
                shadow->pos[2] = 0x2000;
            }
        }
        if (++planeFrame_ >= 0xB5) {
            g_->removeScriptProp(kPlane);
            planeDone_ = true;
        } else if (!planeDone_) wait(1, [this] { movePlane(); });
        if (planeFrame_ >= 0x6F) {
            if (Game::ScriptProp* plane = g_->scriptProp(kPlane)) plane->pitch = (int16_t)deg(planePitch_);
            if (planeFrame_ & 1) --planePitch_;
        }
    }

    // DoPortIntro / cKenA01Intro: the port and the ambush (NewIntro2)
    void doPortIntro() { play("CSS_KEN_A01_NewIntro2.seq", [this] { startIntro(); }); }

    // StartIntro, NextPartStartIntro and cIntroDrive::StartMovie: the intro movie, then DoneIntro
    void startIntro() {
        g_->stopSequence();
        g_->cutscene = g_->scriptCutscene = true;
        wait(0xF, [this] {
            if (g_->startupScreens && !IntroMovie::play(g_->dataDir)) g_->quitRequested = true;
            doneIntro();
        });
    }

    // DoneIntro .. GetToShoreComplete: the port starts Huang on the jetty he swims to (the shore blip at
    // -307.74, -40.05) and plays LeaveWater, his words as he climbs out
    void doneIntro() {
        g_->screenFade.toBlack(1);
        g_->seqCamera.active = false;   // LeaveWater has no cameras: the game's camera on Huang
        int32_t at[3] = {fx(-307.74f), fx(-40.05f), fx(2.f)};
        std::copy(at, at + 3, g_->player.pos);
        g_->player.placeOnGround(&g_->collision);
        g_->player.setHeading(0);
        g_->camera.inherit(g_->player.pos, 0, g_->camera.pitch());
        g_->world.loadAllNow(at[0] / 4096.f, at[1] / 4096.f);
        g_->screenFade.in(30);
        play("CSS_KEN_A01_LeaveWater.seq", [this] { findACar(); }, false);
    }

    // FindACar: cCarToSteal's car on the street behind the jetty, marked on the radar
    void findACar() {
        g_->cutscene = g_->scriptCutscene = false;
        if (!carUid_) {
            const int32_t at[3] = {fx(-281.78f), fx(-64.5f), 0};
            int info = 0;
            for (const char* name : {"Sentinel", "Esperanto", "Pony", "Faction"})
                for (size_t i = 0; i < g_->vehicleInfos.size(); ++i)
                    if (!info && g_->vehicleInfos[i].name() == name) info = (int)i;
            const int car = g_->spawnCar(info, at, 0);
            if (car >= 0) { g_->cars[car].keep = true; carUid_ = g_->cars[car].uid; }
        }
        if (const Vehicle* c = car()) g_->addBlip(c->pos, 0x4080FFFF, 1);
        g_->showObjective(text(0x53B));   // "Get a car."
        when([this] { return g_->playerCar >= 0; }, [this] { findACarComplete(); });
    }

    const Vehicle* car() const {
        for (const Vehicle& c : g_->cars) if (c.uid == carUid_) return &c;
        return nullptr;
    }

    // FindACarComplete -> GetToKennys: the restaurant's chevrons
    void findACarComplete() {
        g_->removeBlip(1);
        const int32_t kenny[3] = {fx(325.86f), fx(-57.59f), 0};
        g_->addBlip(kenny, 0xFFE000FF, 2);
        g_->showObjective(text(0x53E));   // "Meet Uncle Kenny at his restaurant."
        when([this, kenny] { return g_->playerCar >= 0 && near(kenny, 8.f); },
             [this] { g_->showObjective(text(0x537), 150); });   // "Touch the vehicle button to exit..."
        // GetToKennysCheckCar: on foot in the chevrons
        when([this, kenny] { return g_->playerCar < 0 && near(kenny, 1.5f); }, [this] { getToKennysComplete(); });
    }

    bool near(const int32_t at[3], float r) const {
        int32_t p[3];
        g_->focus(p);
        const float dx = (p[0] - at[0]) / 4096.f, dy = (p[1] - at[1]) / 4096.f;
        return dx * dx + dy * dy < r * r;
    }

    // GetToKennysComplete -> cCutscene_AtKennys: the conversation (SCENE_02)
    void getToKennysComplete() {
        g_->removeBlip(2);
        g_->objective.frames = 0;
        g_->cutscene = g_->scriptCutscene = true;
        g_->screenFade.toBlack(15, [this] {
            g_->screenFade.in(15);
            play("CSS_KEN_A01_SCENE_02.seq", [this] { cutsceneAtKennysDoFadeIn(); }, false);
        });
    }

    // CutsceneAtKennysDoFadeIn: the apartment (364.08, 25.02), arrived at within 5 units
    void cutsceneAtKennysDoFadeIn() {
        g_->cutscene = g_->scriptCutscene = false;
        g_->seqCamera.active = false;
        const int32_t home[3] = {fx(364.08f), fx(25.02f), 0};
        g_->addBlip(home, 0xFFE000FF, 3);
        g_->showObjective(text(0x53F));   // "Go to your apartment."
        when([this, home] { return near(home, 5.f); }, [this] { parkAtSafehouseComplete(); });
    }

    // ParkAtSafehouseComplete -> cCutscene_AtSafehouse_Sequence: the apartment's tour, then Passed
    void parkAtSafehouseComplete() {
        g_->removeBlip(3);
        g_->objective.frames = 0;
        g_->cutscene = g_->scriptCutscene = true;
        g_->screenFade.toBlack(15, [this] {
            g_->screenFade.in(15);
            play("CSS_Ken_A01_Safehouse.seq", [this] { passed(); }, false);
        });
    }

    void passed() {
        g_->cutscene = g_->scriptCutscene = false;
        g_->seqCamera.active = false;
        g_->showObjective(g_->mainText(93), 150);   // "MISSION PASSED"
        finish();
    }
};
}

std::unique_ptr<Mission> Mission_Create(const std::string& name) {
    if (name == "kena01") return std::make_unique<KenA01>();
    return nullptr;
}
