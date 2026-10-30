// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// kena01::cKEN_A01, the first mission ("Arrival in Liberty"): the prologue on the plane, the landing, the ambush
// at the port and the intro movie, then Huang on the jetty, a car, Uncle Kenny's restaurant and the apartment.
//
// Not ported yet, and passed over here: the plane's sound, the GPS route, the help texts (cInWaterHelpText,
// cFindCarHelpText, the handbrake help: HUDText's help queue is not ported), the markers' sprites and chevrons, the
// safehouse's building swap and purchase, and Vehicle::SetStop on the player's car. At the port and the pier the people
// are puppets the scene moves (getting in and out of cars without the door animations, Ped::SetDive as a step out
// and a walk) and only the bodyguards' deaths are decided, as the story has them.
#include "mission.h"
#include "game.h"
#include "intro.h"
#include "driving.h"
#include "hud.h"
#include "sound.h"
#include "text/gxt.h"
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
    Emitter* clouds_ = nullptr;
    DoorHandler door_;                      // cCutscene_AtKennys +0x340
    static constexpr int32_t kKennysDoor[3] = {0x145A14, -0x3B999, 0};   // qword_47B508: (325.63, -59.60)

    void onFrame(Game& game) override { door_.tick(game); }

    void placePlayer(float x, float y, float z) {   // Ped::SetPosition
        const int32_t at[3] = {fx(x), fx(y), fx(z)};
        std::copy(at, at + 3, g_->player.pos);
    }

    // cKEN_A01::Init: the screen goes black, then FadedInit
    void init() {
        g_->cutscene = g_->scriptCutscene = true;
        fadeToBlack(1, [this] { doPlaneIntro(); });
    }

    void setTime(uint32_t t) {
        g_->world.timeCycle().setTime(t);
        g_->world.timeCycle().evaluate();
    }

    // CameraImpl::FadeToBlack / FadeIn(frames, callback, .., true): the screen's fade (the player's +0x114C, the one
    // cGame::Render draws) and the callback from TimerImpl::Wait(frames + 1), not from the fade's end
    void fadeToBlack(int frames, std::function<void()> fn = nullptr) {
        g_->screenFade.toBlack(frames);
        if (fn) wait(frames + 1, std::move(fn));
    }
    void fadeIn(int frames, std::function<void()> fn = nullptr) {
        g_->screenFade.in(frames);
        if (fn) wait(frames + 1, std::move(fn));
    }
    // CameraImpl::GetScreenBrightNess: 16 - the screen fade's level >> 12 (0 black, 16 clear)
    int brightness() const { return 16 - (g_->screenFade.level() >> 12); }
    // FadeOutAndFadeIn (cGlobalFader::Init): to black over 15 frames unless it is black already, then (Wait) setup,
    // `frames` frames, finished (FadeBackIn), 15 frames, and in over 15 unless it is clear already
    void fadeOutAndFadeIn(std::function<void()> setup, std::function<void()> finished, int frames) {
        auto go = [this, setup, finished, frames] {
            setup();
            wait(frames, [this, finished] {
                finished();
                wait(0xF, [this] { if (brightness() <= 0xF) fadeIn(0xF); });
            });
        };
        if (brightness() != 0) fadeToBlack(0xF, go);
        else go();
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
        // the sky the plane flies through: the clouds below drifting south, a contrail behind each engine and a
        // vapour stream off each wingtip (FXImpl::CreateClouds / CreateContrail / CreateFountain)
        const int16_t drift[3] = {0, -0x800, 0};
        const int32_t cloudAt[3] = {fx(1411.72f), fx(692.70f), fx(10.f)};
        clouds_ = TheParticles().add<CloudsEmitter>(cloudAt, drift);
        const int32_t south[3] = {0, -0x1000, 0};
        for (float x : {1413.22f, 1410.22f}) {
            const int32_t at[3] = {fx(x), fx(682.70f), fx(13.5f)};
            g_->contrails.push_back(std::make_unique<Contrail>(at, south));
        }
        const int16_t jet[3] = {0, -0x4000, 0};
        for (float x : {1421.32f, 1402.12f}) {
            const int32_t at[3] = {fx(x), fx(688.70f), fx(12.f)};
            g_->fountains[Game::kScriptFountains].push_back(std::make_unique<Fountain>(at, jet));
        }
        // cPreIntro::SequenceEnd -> FadeBeforeEnd: to black over 15 frames unless it is already (IsScreenFaded), End
        Sequence* intro = play("CSS_KEN_A01_NewIntro1.seq", [this] {
            auto end = [this] { g_->unloadSequence(); endPlaneIntro(); doLandingIntro(); };   // (End: the next is loaded)
            if (brightness() != 0) fadeToBlack(0xF, end);
            else end();
        });
        if (intro) intro->restoreComic = true;   // cPreIntro::Begin: SetRestorePDA(.., 0x3A)
        g_->screenFade.in(30);
    }

    // cPreIntro::End: the plane and its sky go
    void endPlaneIntro() {
        g_->removeScriptProp(kPlane);
        if (clouds_ && TheParticles().contains(clouds_)) TheParticles().remove(clouds_);
        clouds_ = nullptr;
        g_->contrails.clear();
        g_->fountains.erase(Game::kScriptFountains);
    }

    // DoLandingIntro / cPlaneLand::IntroSetup: the plane high over the bay heading south-west with its shadow on
    // the water below; callback 0 starts it (StartPlane), then WaitAfterLanding's 30 frames
    // (cPlaneLand::Begin: FadeOutAndFadeIn(IntroSetup, IntroFinished, 15))
    void doLandingIntro() {
        fadeOutAndFadeIn([this] {   // IntroSetup
            g_->removeScriptProp(kPlane);
            g_->scriptProps.push_back({kPlane, 8, {fx(2085.08f), fx(-71.64f), fx(80.f)}, (uint16_t)deg(0xE1), 0});
            g_->scriptProps.push_back({kShadow, 0x8D, {fx(2085.08f), fx(-71.64f), fx(2.f)}, (uint16_t)deg(0xE1), 0});
            placePlayer(1752.17f, 21.06f, 0.f);
            planeFrame_ = planePitch_ = 0;
            planeDone_ = false;
        }, [this] {   // IntroFinished: PlaySequence
            Sequence* s = play("CSS_KEN_A01_PlaneLand.seq", [this] {   // SequenceEnd -> End
                auto end = [this] {   // IntroEndFinished, Finish, then WaitAfterLanding
                    g_->removeScriptProp(kPlane);
                    g_->removeScriptProp(kShadow);
                    wait(0x1E, [this] { doPortIntro(); });
                };
                if (brightness() != 0) fadeToBlack(0xF, end);
                else end();
            });
            if (s) s->setCallback(0, [this] { wait(1, [this] { movePlane(); }); });   // StartPlane
        }, 0xF);
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
    // ---- DoPortIntro / cKenA01Intro: the airport, the escort and the ambush (NewIntro2) ----
    // Huang comes down the plane's steps as two bodyguards drive up in Kenny's car (vehicle 67); two cars (21) of
    // ambushers race in, stop the escort and shoot the bodyguards, and Huang is hit. The sequence's callbacks move
    // everything (ids 0..17); people are puppets the scene moves, the cars drive with the traffic AI's yoke.
    enum { kBodyguard1, kBodyguard2, kAmbusherA1, kAmbusherB1, kAmbusherA2, kAmbusherB2, kActors };
    enum { kCarA, kCarB, kEscort, kCars };
    enum { kParkedPlane = 3, kParkedShadow = 4 };
    struct Actor {
        uint32_t ped = 0;
        int car = -1, seat = -1;
        bool walking = false;
        int32_t goal[3] = {0, 0, 0};
        int target = -1, killFrames = 0, fireChance = 50;
    };
    struct SceneCar { uint32_t uid = 0; bool driving = false; int32_t goal[3] = {0, 0, 0}; int speed = 0; };
    Actor actors_[kActors];
    SceneCar sceneCars_[kCars];
    bool sceneOn_ = false;

    Pedestrians::Ped* ped(int actor) {
        if (!actors_[actor].ped) return nullptr;
        for (Pedestrians::Ped& p : g_->peds.peds) if (p.uid == actors_[actor].ped) return &p;
        return nullptr;
    }
    Vehicle* car(int which) {
        if (!sceneCars_[which].uid) return nullptr;
        for (Vehicle& c : g_->cars) if (c.uid == sceneCars_[which].uid) return &c;
        return nullptr;
    }
    static int16_t angleTo(int64_t dx, int64_t dy) { return (int16_t)(atan2f((float)dx, (float)dy) * 10430.f); }

    // WorldImpl::CreateVehicle + SetPosition / SetHeading (degrees) / SetNoSkid
    void makeCar(int which, int info, float x, float y, int headingDeg, int palette = -1) {   // (+ Vehicle::SetColour)
        const int32_t at[3] = {fx(x), fx(y), 0};
        sceneCars_[which] = SceneCar{};
        const int i = g_->spawnCar(info, at, (int16_t)deg(headingDeg), palette);
        if (i < 0) return;
        g_->cars[i].keep = true;
        sceneCars_[which].uid = g_->cars[i].uid;
    }
    // WorldImpl::CreatePed + SetMale + SetColour (ePedPal: the palette plus one) + GiveWeapon + WarpIntoVehicle
    void makeActor(int which, int palette, int carIndex, int seat, int fireChance) {
        Vehicle* c = car(carIndex);
        const int32_t at[3] = {c ? c->pos[0] : g_->player.pos[0], c ? c->pos[1] : g_->player.pos[1],
                               c ? c->pos[2] : g_->player.pos[2]};
        Actor& a = actors_[which];
        a = Actor{};
        a.ped = g_->peds.spawnPuppet(*g_, at, 0, 1, palette - 1, palette - 1);
        a.car = carIndex;
        a.seat = seat;
        a.fireChance = fireChance;
        if (Pedestrians::Ped* p = ped(which)) {
            p->puppet.weapon = 5;   // GiveWeapon(5, 9999)
            p->puppet.vehicle = seat == 0 && c ? c->uid : 0;
            p->body.hidden = true;
        }
    }
    // Vehicle::SetGoTo + SetSpeed: the driver's goto task drives the car under physics
    void driveTo(int which, float x, float y, int speed) {
        if (Vehicle* v = car(which)) v->setToPhysics(true);
        SceneCar& c = sceneCars_[which];
        c.driving = true;
        c.goal[0] = fx(x);
        c.goal[1] = fx(y);
        c.goal[2] = 0;
        c.speed = speed;
    }
    // SimpleMover::SetVelocity(GetForward() x speed)
    void launch(int which, int unitsPerSecond) {
        if (Vehicle* c = car(which))
            for (int k = 0; k < 3; ++k) c->vel[k] = c->fwd[k] * unitsPerSecond;
    }
    void stopCar(int which) {   // Vehicle::ClearAllOrders / SetStop: brakes on
        sceneCars_[which].driving = false;
        Vehicle::Controls brake;
        brake.throttle = -0x1000;
        brake.handbrake = true;
        if (sceneCars_[which].uid) g_->scriptCarControls[sceneCars_[which].uid] = brake;
    }
    // Entity::SetPosition + SetHeading: the port's vehicles take their pose when made, so the car is made again
    // there, its driver kept in it
    void placeCar(int which, float x, float y, int headingDeg) {
        Vehicle* c = car(which);
        if (!c) return;
        const int info = c->infoId;
        g_->scriptCarControls.erase(c->uid);
        for (int a = 0; a < kActors; ++a)
            if (actors_[a].car == which && actors_[a].seat == 0)
                if (Pedestrians::Ped* p = ped(a)) p->puppet.vehicle = 0;
        g_->removeCar((int)(c - g_->cars.data()));
        makeCar(which, info, x, y, headingDeg);
        Vehicle* n = car(which);
        for (int a = 0; a < kActors; ++a)
            if (actors_[a].car == which && actors_[a].seat == 0 && n)
                if (Pedestrians::Ped* p = ped(a)) p->puppet.vehicle = n->uid;
    }
    // Ped::SetExitVehicle: out beside the car
    void getOut(int which) {
        Actor& a = actors_[which];
        Pedestrians::Ped* p = ped(which);
        if (!p) return;
        Vehicle* c = a.car >= 0 ? car(a.car) : nullptr;
        if (c) {
            const int side = a.seat == 1 ? 1 : -1;
            for (int k = 0; k < 3; ++k) p->puppet.target[k] = c->pos[k] + side * c->right[k] * 2;
        } else std::copy(p->body.pos, p->body.pos + 3, p->puppet.target);
        p->puppet.vehicle = 0;
        p->body.hidden = false;
        std::copy(p->puppet.target, p->puppet.target + 3, p->body.pos);
        p->body.placeOnGround(&g_->collision);
        std::copy(p->body.pos, p->body.pos + 3, p->puppet.target);
        a.car = -1;
        a.seat = -1;
    }
    void walkTo(int which, float x, float y) {   // Ped::SetGoTo + SetWalking
        actors_[which].walking = true;
        actors_[which].goal[0] = fx(x);
        actors_[which].goal[1] = fx(y);
    }
    // Ped::SetKill: face the target and shoot; the bodyguards go down (the ambushers live to dump Huang)
    void kill(int shooter, int target) {
        actors_[shooter].target = target;
        actors_[shooter].killFrames = 15 + (100 - actors_[shooter].fireChance) / 4;
    }

    void sceneStep() {   // once a frame while the scene plays
        if (!sceneOn_) return;
        for (int i = 0; i < kCars; ++i) {
            SceneCar& sc = sceneCars_[i];
            Vehicle* c = car(i);
            if (!c || !sc.driving) continue;
            const int64_t dx = (int64_t)sc.goal[0] - c->pos[0], dy = (int64_t)sc.goal[1] - c->pos[1];
            if (dx * dx + dy * dy < (int64_t)0x3000 * 0x3000) { stopCar(i); continue; }
            g_->scriptCarControls[c->uid] = AIDriving::towards(*c, g_->traffic.drivingProps(), sc.goal, sc.speed << 12);
        }
        for (int i = 0; i < kActors; ++i) {
            Actor& a = actors_[i];
            Pedestrians::Ped* p = ped(i);
            if (!p) continue;
            if (a.car >= 0 && a.seat != 0) {   // a passenger rides along out of sight
                if (Vehicle* c = car(a.car)) {
                    std::copy(c->pos, c->pos + 3, p->puppet.target);
                    std::copy(c->pos, c->pos + 3, p->body.pos);
                }
                p->body.hidden = true;
                continue;
            }
            p->puppet.firing = false;
            if (a.target >= 0) {
                Pedestrians::Ped* t = ped(a.target);
                if (t && !p->body.dead && !t->body.dead) {
                    p->puppet.heading = angleTo(t->body.pos[0] - p->body.pos[0], t->body.pos[1] - p->body.pos[1]);
                    p->puppet.move = 0;
                    p->puppet.firing = true;
                    const bool bodyguard = a.target == kBodyguard1 || a.target == kBodyguard2;
                    if (bodyguard && --a.killFrames <= 0) t->puppet.dead = true;
                } else a.target = -1;
                continue;
            }
            if (a.walking && !p->body.dead) {
                const int64_t dx = (int64_t)a.goal[0] - p->puppet.target[0], dy = (int64_t)a.goal[1] - p->puppet.target[1];
                const double d = std::sqrt((double)(dx * dx + dy * dy)), step = 0x180;   // walking pace
                if (d <= step) {
                    a.walking = false;
                    p->puppet.move = 0;
                    std::fill(p->puppet.velocity, p->puppet.velocity + 3, 0);
                } else {
                    p->puppet.target[0] += (int32_t)(dx * step / d);
                    p->puppet.target[1] += (int32_t)(dy * step / d);
                    p->puppet.heading = angleTo(dx, dy);
                    p->puppet.move = 1;
                    p->puppet.velocity[0] = (int32_t)(dx * step / d) * 30;
                    p->puppet.velocity[1] = (int32_t)(dy * step / d) * 30;
                }
            }
        }
        wait(1, [this] { sceneStep(); });
    }

    void doPortIntro() {   // cKenA01Intro::Begin
        endScene();
        sceneOn_ = true;
        // (black since cPlaneLand::End; WaitForFadeInAirport fades in)
        g_->world.loadAllNow(1775.f, -60.f);
        // the apron cleared (Area::ClearEntities; SetCarGensOff, SetRoadNodesOff)
        for (int i = (int)g_->cars.size() - 1; i >= 0; --i)
            if (i != g_->playerCar) g_->removeCar(i);
        placePlayer(1762.45f, -66.53f, 2.f);
        g_->player.setHeading((int16_t)deg(0x5A));
        g_->scriptProps.push_back({kParkedPlane, 0x6C, {fx(1761.47f), fx(-62.53f), 0}, (uint16_t)deg(0xA5), 0});
        g_->scriptProps.push_back({kParkedShadow, 0x8D, {fx(1761.47f), fx(-62.53f), 0x1000}, (uint16_t)deg(0xA5), 0});
        makeCar(kCarA, 0x15, 1738.72f, 25.13f, 0x87);
        makeCar(kCarB, 0x15, 1734.43f, 34.71f, 0x87);
        makeCar(kEscort, 0x43, 1765.02f, -146.77f, 0);
        makeActor(kBodyguard1, 0x1C, kEscort, 0, 45);
        makeActor(kBodyguard2, 0x1C, kEscort, 1, 90);
        makeActor(kAmbusherA1, 3, kCarA, 0, 40);
        makeActor(kAmbusherB1, 3, kCarB, 0, 85);
        makeActor(kAmbusherA2, 3, kCarA, 2, 45);
        makeActor(kAmbusherB2, 3, kCarB, 1, 90);
        Sequence* s = play("CSS_KEN_A01_NewIntro2.seq", [this] { endScene(); startIntro(); });
        if (!s) return;
        s->restoreComic = true;   // cKenA01Intro::Begin: SetRestorePDA(.., 0x3A)
        s->setCallback(0, [this] {   // PlayerMiddleOfStairs
            placePlayer(1762.83f, -66.49f, 1.4f);
            g_->player.setHeading((int16_t)deg(0x5A));
        });
        s->setCallback(1, [this] {   // PlayerBottomOfStairs: he walks towards the car
            placePlayer(1763.47f, -66.42f, 0.f);
            g_->player.placeOnGround(&g_->collision);
            g_->scriptWalk.on = true;
            g_->scriptWalk.target[0] = fx(1776.98f);
            g_->scriptWalk.target[1] = fx(-63.74f);
            g_->scriptWalk.walking = true;
        });
        s->setCallback(2, [this] { driveTo(kEscort, 1770.23f, -95.11f, 0x10); });   // EscortStart
        s->setCallback(3, [this] { driveTo(kEscort, 1763.92f, -86.55f, 0xA); });    // EscortTurn1
        s->setCallback(4, [this] { driveTo(kEscort, 1784.82f, -71.59f, 0xB); });    // EscortTurn2
        s->setCallback(5, [this] { driveTo(kEscort, 1783.27f, -61.45f, 5); });      // EscortTurn3
        s->setCallback(6, [this] { getOut(kBodyguard1); walkTo(kBodyguard1, 1779.87f, -62.11f); });   // FirstBodyguardOut
        s->setCallback(7, [this] { getOut(kBodyguard2); });                         // SecondBodyguardOut
        s->setCallback(8, [this] {   // SetCarsOff: the ambushers race in
            driveTo(kCarA, 1818.80f, -58.56f, 0x78);
            driveTo(kCarB, 1843.64f, -71.64f, 0x78);
            launch(kCarA, 0x32);
            launch(kCarB, 0x32);
        });
        s->setCallback(0xE, [this] {   // RepositionCars
            stopCar(kCarA);
            stopCar(kCarB);
            placeCar(kCarA, 1779.74f, 36.20f, 0x87);
            placeCar(kCarB, 1803.20f, 31.01f, 0x87);
        });
        s->setCallback(9, [this] {   // AndSetOff: down on the escort from the north
            placeCar(kCarA, 1774.59f, 13.54f, 0xB4);
            placeCar(kCarB, 1779.36f, 2.65f, 0xB4);
            driveTo(kCarA, 1775.96f, -78.25f, 0x78);
            driveTo(kCarB, 1783.73f, -78.86f, 0x78);
            launch(kCarA, 0x32);
            launch(kCarB, 0x32);
        });
        s->setCallback(0xB, [this] { stopCar(kCarA); stopCar(kCarB); });   // AllStop1
        s->setCallback(0xD, [this] {   // EverybodyOut, then the shooting (Shoot1, Out2, Out4)
            getOut(kAmbusherB1);
            walkTo(kAmbusherB1, 1794.31f, -44.98f);
            wait(0x23, [this] {
                kill(kAmbusherB1, kBodyguard2);
                wait(2, [this] { kill(kBodyguard2, kAmbusherB1); });   // BodyShoot1
            });
            wait(0x11, [this] {   // Out2
                stopCar(kCarA);
                getOut(kAmbusherA1);
                kill(kAmbusherA1, kBodyguard1);
                kill(kBodyguard1, kAmbusherA1);
                wait(0x14, [this] {   // PlayerRoll: Huang turns to the shooting
                    if (Pedestrians::Ped* p = ped(kAmbusherA1))
                        g_->player.setHeading(angleTo((int64_t)p->body.pos[0] - g_->player.pos[0],
                                                      (int64_t)p->body.pos[1] - g_->player.pos[1]));
                });
            });
            wait(0x1E, [this] { getOut(kAmbusherB2); kill(kAmbusherB2, kBodyguard2); });   // Out4
        });
        s->setCallback(0x10, [this] {   // HuangHit: shot, he falls
            g_->scriptWalk.on = false;
            g_->player.playOneShot(0x2B, 0x2A, false);
            g_->scriptAnim = true;
        });
        wait(0x28, [this] { fadeIn(15); });   // WaitForFadeInAirport, 40 frames in
        wait(1, [this] { sceneStep(); });
    }

    void endScene() {   // cKenA01Intro::End: everything the scene made goes
        sceneOn_ = false;
        for (int i = 0; i < kActors; ++i)
            if (actors_[i].ped) g_->peds.removePuppet(*g_, actors_[i].ped);
        for (int i = 0; i < kCars; ++i) {
            if (!sceneCars_[i].uid) continue;
            g_->scriptCarControls.erase(sceneCars_[i].uid);
            if (Vehicle* c = car(i)) g_->removeCar((int)(c - g_->cars.data()));
        }
        for (Actor& a : actors_) a = Actor{};
        for (SceneCar& c : sceneCars_) c = SceneCar{};
        g_->removeScriptProp(kParkedPlane);
        g_->removeScriptProp(kParkedShadow);
        g_->scriptWalk.on = false;
        g_->scriptAnim = false;
    }

    // StartIntro, NextPartStartIntro and cIntroDrive::StartMovie: the intro movie, then DoneIntro
    void startIntro() {
        endPlaneIntro();   // (skipped straight here)
        endScene();
        g_->removeScriptProp(kShadow);
        g_->stopSequence();
        g_->cutscene = g_->scriptCutscene = true;
        wait(0xF, [this] {
            if (g_->startupScreens && !IntroMovie::play(g_->dataDir, [this](double s, int w, int h) { driveText(s, w, h); }))
                g_->quitRequested = true;
            doneIntro();
        });
    }

    // cIntroDrive::StartSequence / PlayNextLine: over the movie, the talk in the car. Line n is DriveText_n (the
    // cutscene table's string 20 + n) from frame kDriveText[n] of the movie for 149 frames (WhenTimePasses, ms).
    static constexpr int kDriveText[20] = {600,  715,  955,  1145, 1285, 1430, 1670, 1830, 1970, 2485,
                                           2685, 3000, 3115, 3235, 3475, 3730, 3840, 3950, 4060, 4170};
    std::vector<std::u16string> cutText_;
    void driveText(double seconds, int W, int H) {
        if (cutText_.empty()) {
            GxtFile gxt;
            if (gxt.load(g_->dataDir + "/e_ckna01.gxt"))
                for (size_t i = 0; i < gxt.count(); ++i) cutText_.push_back(gxt.get(i));
            if (cutText_.empty()) cutText_.push_back(u"");
        }
        const int frame = (int)(seconds * 30.0);
        int line = -1;
        for (int n = 0; n < 20; ++n)
            if (frame >= kDriveText[n] && frame < kDriveText[n] + 149) line = n;
        if (line < 0 || 20 + line >= (int)cutText_.size()) return;
        std::u16string text = cutText_[20 + line];
        while (!text.empty() && text.back() == 0) text.pop_back();
        Hud_Begin(W, H);   // OS_MovieSetText: centred at the bottom of the picture
        const float k = H / 768.f, scale = 1.25f * k;
        const std::u16string wrapped = Hud_Wrap16(text, W * 0.7f, scale);
        int lines = 1;
        for (char16_t c : wrapped) lines += c == u'\n';
        const float lineH = Hud_LineHeight(scale), y = H - 40.f * k - lines * lineH;
        Hud_Text16(W * 0.15f + 2.f * k, y + 2.f * k, scale, 0x000000C0u, [&] {
            std::u16string shadow;
            for (char16_t c : wrapped) if (c < 0xFE00) shadow += c;
            return shadow;
        }());
        Hud_Text16(W * 0.15f, y, scale, 0xFFFFFFFFu, wrapped);
        Hud_End();
    }

    // DoneIntro -> Callback_FadedOutInit -> Setup -> cCarInWater::Start(EscapeCar): the kidnappers' car (vehicle 0x15,
    // colour 10) races along the pier from (-188.96, 17.46) at speed 25 for (-325.24, 20.69) with Huang in it; two of
    // them jump out (5: to -308.88, 44.86; 6: to -302.79, -23.65) and it goes over the end into the water
    // (CSS_Ken_A01_Splash: 7 RemoveVehicles, the splash at -341.03, 19.37 and its sound 0x313)
    // DoneIntroMakeSureFaded: to black over 15 frames unless it is already (IsScreenFaded), then DoneIntro
    void doneIntro() {
        if (brightness() != 0) fadeToBlack(0xF, [this] { doneIntroFaded(); });
        else doneIntroFaded();
    }
    void doneIntroFaded() {
        g_->seqCamera.active = false;
        // Callback_FadedOutInit: Huang at the pier's end (-294.74, 31.44) facing 135 degrees, then Setup
        placePlayer(-294.74f, 31.44f, 0.f);
        g_->player.placeOnGround(&g_->collision);
        g_->player.setHeading((int16_t)deg(0x87));
        g_->camera.reset(g_->player.pos, g_->player.heading());
        setup();
    }
    // Setup (its first car): the car to steal, vehicle 5 at (-281.78, -64.5) facing 100 degrees in colour 4, hotwire
    // class 1 (cCarToSteal::Start), then cCarInWater::Start(EscapeCar)
    void setup() {
        if (!car()) {
            const int32_t at[3] = {fx(-281.78f), fx(-64.5f), 0};
            const int i = g_->spawnCar(5, at, (int16_t)deg(0x64), 4);
            if (i >= 0) {
                g_->cars[i].keep = true;
                g_->cars[i].setHotWireMode(1);   // (easy, whatever the vehicle's own class)
                carUid_ = g_->cars[i].uid;
            }
        }
        // cCarInWater::Begin: FadeOutAndFadeIn(IntroSetup, IntroFinished, 15)
        fadeOutAndFadeIn([this] { carInWater(); }, [this] { splashSequence(); }, 0xF);
    }

    void carInWater() {   // cCarInWater::IntroSetup
        endScene();
        sceneOn_ = true;
        g_->cutscene = g_->scriptCutscene = true;
        g_->player.hidden = true;   // (in the car)
        g_->world.loadAllNow(-260.f, 18.f);
        makeCar(kCarA, 0x15, -188.96f, 17.46f, 0x10E, 0xA);
        makeActor(kAmbusherA1, 3, kCarA, 0, 50);
        makeActor(kAmbusherA2, 3, kCarA, 1, 50);
        makeActor(kAmbusherB2, 3, kCarA, 2, 50);
        for (int a : {(int)kAmbusherA1, (int)kAmbusherA2, (int)kAmbusherB2})
            if (Pedestrians::Ped* p = ped(a)) p->puppet.weapon = 0;   // (no GiveWeapon here)
        if (Vehicle* c = car(kCarA)) c->engineOn = true;   // (its driver is in it)
        driveTo(kCarA, -325.24f, 20.69f, 0x19);
        wait(1, [this] { sceneStep(); });
    }
    void splashSequence() {   // IntroFinished: PlaySequence
        Sequence* s = g_->startSequence("CSS_Ken_A01_Splash.seq");
        if (!s) { wait(1, [this] { carInWaterEnd(); }); return; }
        s->restoreComic = true;   // cCarInWater::IntroSetup: SetRestorePDA(.., 0x3A)
        s->setCallback(Sequence::kEnded, [this] { wait(1, [this] { carInWaterEnd(); }); });   // SequenceEnd -> End
        s->setCallback(Sequence::kSkipped, [this] {   // Skipped: FadeToBlack(0), then 5 frames
            fadeToBlack(0, [this] { wait(5, [this] { displaySinkingCar(); }); });
        });
        s->setCallback(5, [this] { getOut(kAmbusherA2); walkTo(kAmbusherA2, -308.88f, 44.86f); });    // JumpOutOfVehicle
        s->setCallback(6, [this] { getOut(kAmbusherB2); walkTo(kAmbusherB2, -302.79f, -23.65f); });   // JumpOutOfVehicle2
        s->setCallback(7, [this] {   // RemoveVehicles: FXImpl::CreateExplosion(type 8) and SoundImpl::PlaySFX(0x313)
            const int32_t at[3] = {fx(-341.03f), fx(19.37f), 0}, still[3] = {0, 0, 0};
            g_->addWaterSplash(at, 8, still, -1);
            if (Vehicle* c = car(kCarA)) TheSound().scriptSfx(c->pos, 0x313, 0x7F, 0x384);
        });
    }
    void carInWaterEnd() {   // End: to black over 15 frames, then FadedOutAfterImpact
        fadeToBlack(15, [this] { displaySinkingCar(); });
    }
    void displaySinkingCar() {   // DisplaySinkingCar: DeleteCar, then 30 frames
        endScene();
        wait(0x1E, [this] { introEndFinished(); });
    }
    // IntroEndFinished: Huang at (-339.43, 27.48) on the water (z -7.5), facing 90 degrees, the cutscene over and the
    // screen faded in; then EscapeCar -> EscapeCarPause a frame later
    void introEndFinished() {
        g_->stopSequence();
        g_->seqCamera.active = false;   // (SetBehindPlayer: the game's camera, not the sequence's last one)
        placePlayer(-339.43f, 27.48f, -7.5f);
        g_->player.setHeading((int16_t)deg(0x5A));
        g_->player.hidden = false;
        g_->camera.reset(g_->player.pos, g_->player.heading());
        g_->world.loadAllNow(-339.43f, 27.48f);
        g_->cutscene = g_->scriptCutscene = false;
        g_->screenFade.in(15);
        wait(1, [this] { escapeCarPause(); });
    }
    // EscapeCarPause: the sinking car (ScriptPlayer::SetSinkingCarStart(PlayerSurfaces):
    // the minigame task; on foot, style 0)
    void escapeCarPause() {
        g_->player.setHeading((int16_t)deg(0x69));
        g_->camera.reset(g_->player.pos, g_->player.heading());
        g_->cutscene = true;   // (StartCutscene: no HUD while the minigame runs)
        // (SetPrimaryObjective(0x542) "Survive the kidnapping.": the mission brief, not on screen)
        g_->startSinkingCar(0, [this](bool escaped) {
            if (escaped) playerSurfaces();
            else failed();
        });
    }
    // PlayerSurfaces: the controls back, facing 135 degrees; 10 frames on PlayLeavingCarSequence -> EscapeCarComplete
    void playerSurfaces() {
        g_->cutscene = false;
        g_->player.setHeading((int16_t)deg(0x87));
        wait(0xA, [this] { escapeCarComplete(); });
    }
    // EscapeCarComplete: a splash (type 8) where Huang comes up, the screen fades in; 30 frames on GetToShore
    void escapeCarComplete() {
        g_->restoreCam();   // EndCutscene(1, true): cCamera::RestoreCam over one frame
        const int32_t still[3] = {0, 0, 0};
        g_->addWaterSplash(g_->player.pos, 8, still, -1);
        g_->screenFade.in(15);
        wait(0x1E, [this] { getToShore(); });
    }
    // GetToShore: the jetty's circle (-307.74, -40.05, radius 5) marked; "Get to shore." while in the water. Out of
    // the water and in the circle (LeftWater / OutOfWaterInHalo), or in a vehicle: PlayLeftWaterSequence.
    void getToShore() {
        const int32_t shore[3] = {fx(-307.74f), fx(-40.05f), fx(-6.5f)};
        g_->addBlip(shore, 0xFFE000FF, 4, 1);
        if (g_->player.inWater) g_->showObjective(text(0x53A));
        when([this, shore] {
            if (g_->playerCar >= 0) return true;
            int32_t p[3];
            g_->focus(p);
            const int64_t dx = (int64_t)p[0] - shore[0], dy = (int64_t)p[1] - shore[1];
            return !g_->player.inWater && dx * dx + dy * dy <= (int64_t)0x5000 * 0x5000;
        }, [this] { playLeftWaterSequence(); });
    }
    // PlayLeftWaterSequence: the circle and its marker gone, CSS_KEN_A01_LeaveWater (his words as he climbs out);
    // PlayedWaterSequence -> WaitBeforeGetToShoreComplete: 30 frames -> GetToShoreComplete
    void playLeftWaterSequence() {
        g_->removeBlip(4);
        g_->objective.frames = 0;
        g_->cutscene = g_->scriptCutscene = true;
        play("CSS_KEN_A01_LeaveWater.seq", [this] { wait(0x1E, [this] { getToShoreComplete(); }); }, false);
    }
    // GetToShoreComplete: the brief's "Meet with your Uncle Kenny to explain what happened.", the police chase set up
    // (FindACar does that), then FindACar
    void getToShoreComplete() {
        g_->cutscene = g_->scriptCutscene = false;
        g_->seqCamera.active = false;
        // (SetPrimaryObjective(0x543): the mission brief, not on screen)
        findACar();
    }
    // PlayerDead -> Failed
    void failed() {
        g_->cutscene = g_->scriptCutscene = false;
        g_->showObjective(g_->mainText(0x5C), 150);   // "MISSION FAILED"
        finish();
    }

    // FindACar: cCarToSteal's car on the street behind the jetty, marked on the radar
    // ---- cPoliceChase: on the way to Kenny's, a getaway car (83, a criminal at the wheel) and two police cars (18)
    // race south past the player and turn east, then go their own way ----
    enum { kChaseCar, kCop1, kCop2, kChaseCars };
    SceneCar chase_[kChaseCars];
    bool chaseOn_ = false;
    int chaseFrames_ = 0, chaseStage_ = 0;

    Vehicle* chaseCar(int which) {
        if (!chase_[which].uid) return nullptr;
        for (Vehicle& c : g_->cars) if (c.uid == chase_[which].uid) return &c;
        return nullptr;
    }
    // cPoliceChase::Init / WaitForTrigger: the player fully inside the area east of the jetty
    void policeChaseInit() {
        when([this] {
            int32_t p[3];
            g_->focus(p);
            return p[0] > fx(-120.16f) && p[0] < fx(-6.32f) && p[1] > fx(-129.605f) && p[1] < fx(-14.455f);
        }, [this] { policeChaseCreate(); });
    }
    void policeChaseCreate() {   // CreateStuff / CreateACar: from the positions table, heading south, at full speed
        static const float at[kChaseCars][2] = {{-72.73f, -96.95f}, {-67.76f, -78.04f}, {-72.51f, -36.69f}};
        static const int info[kChaseCars] = {0x53, 0x12, 0x12};
        for (int i = 0; i < kChaseCars; ++i) {
            const int32_t p[3] = {fx(at[i][0]), fx(at[i][1]), 0};
            chase_[i] = SceneCar{};
            const int j = g_->spawnCar(info[i], p, (int16_t)deg(0xB4));
            if (j < 0) continue;
            Vehicle& c = g_->cars[j];
            c.keep = true;
            c.engineOn = true;
            if (i != kChaseCar && c.hasSiren()) c.siren = true;
            chase_[i].uid = c.uid;
            chase_[i].driving = true;
            chase_[i].goal[0] = fx(-70.69f);
            chase_[i].goal[1] = fx(-278.39f);
            chase_[i].speed = 0x78;
        }
        chaseOn_ = true;
        chaseFrames_ = 0;
        chaseStage_ = 0;
        wait(1, [this] { policeChaseStep(); });
    }
    void policeChaseStep() {
        if (!chaseOn_) return;
        ++chaseFrames_;
        Vehicle* lead = chaseCar(kChaseCar);
        // Turn: once the getaway car reaches the corner, everyone heads east
        if (chaseStage_ == 0 && lead && lead->pos[1] < fx(-190.f)) {
            chaseStage_ = 1;
            for (SceneCar& c : chase_) { c.goal[0] = fx(786.27f); c.goal[1] = fx(-208.91f); }
        }
        int32_t me[3];
        g_->focus(me);
        bool anyNear = false;
        for (int i = 0; i < kChaseCars; ++i) {
            Vehicle* c = chaseCar(i);
            if (!c) continue;
            const int64_t dx = (int64_t)c->pos[0] - me[0], dy = (int64_t)c->pos[1] - me[1];
            anyNear |= dx * dx + dy * dy < (int64_t)0x96000 * 0x96000;   // 150 units
            // the police follow the getaway car itself, the getaway car its route
            const int32_t* goal = chase_[i].goal;
            if (i != kChaseCar && lead) goal = lead->pos;
            g_->scriptCarControls[c->uid] = AIDriving::towards(*c, g_->traffic.drivingProps(), goal, chase_[i].speed << 12);
        }
        // CleanUp: after a minute, or once they are far away, they are gone
        if (!anyNear || chaseFrames_ > 30 * 60) { policeChaseEnd(); return; }
        wait(1, [this] { policeChaseStep(); });
    }
    void policeChaseEnd() {
        chaseOn_ = false;
        for (int i = 0; i < kChaseCars; ++i) {
            if (!chase_[i].uid) continue;
            g_->scriptCarControls.erase(chase_[i].uid);
            if (Vehicle* c = chaseCar(i)) {
                const int32_t* p = c->pos;
                int32_t me[3];
                g_->focus(me);
                const int64_t dx = (int64_t)p[0] - me[0], dy = (int64_t)p[1] - me[1];
                if (dx * dx + dy * dy > (int64_t)0x50000 * 0x50000 || !g_->view.canSee(p, 0x5000))
                    g_->removeCar((int)(c - g_->cars.data()));
                else c->keep = false;   // (left to be removed when out of sight)
            }
            chase_[i] = SceneCar{};
        }
    }

    void findACar() {
        g_->cutscene = g_->scriptCutscene = false;
        if (!chaseOn_ && !chase_[kChaseCar].uid) policeChaseInit();   // GetToShoreComplete: cPoliceChase::Init
        if (const Vehicle* c = car()) g_->addBlip(c->pos, 0x4080FFFF, 1);
        g_->suppressHotwireTimer = true;   // ScriptPlayer::SuppressHotwireTimelimit (and EnableMoreTextForEasyHotwire)
        g_->showObjective(text(hotwireFailed_ ? 0x53D : 0x53B));   // "Get back in the car." / "Get a car."
        when([this] { return g_->playerCar >= 0; }, [this] { checkForHotwiring(); });
    }

    // CheckForHotwiring: in the car to steal while it still needs hotwiring, wait for the outcome (WhenSucceedsHotwire,
    // WhenFailsHotwire, WhenExitsVehicle); in any other car, or once it is hotwired, on to Kenny's
    bool hotwireFailed_ = false;
    void checkForHotwiring() {
        const Vehicle* c = g_->playerCar >= 0 ? &g_->cars[g_->playerCar] : nullptr;
        if (!c || c->uid != carUid_ || !c->needsHotwiring()) { findACarComplete(); return; }
        g_->onHotwire = [this](bool success) {
            g_->onHotwire = nullptr;
            if (success) hotwiringComplete();
            else hotwiringFailed();
        };
        // (got out before the outcome: failed; once there is an outcome this check ends without doing anything)
        when([this] { return !g_->onHotwire || g_->playerCar < 0; }, [this] {
            if (!g_->onHotwire) return;
            g_->onHotwire = nullptr;
            hotwiringFailed();
        });
    }
    void hotwiringComplete() {   // HotwiringComplete: the time limit back, on to Kenny's
        g_->suppressHotwireTimer = false;
        findACarComplete();
    }
    void hotwiringFailed() {     // HotwiringFailed: back to FindACar
        hotwireFailed_ = true;
        findACar();
    }

    const Vehicle* car() const {
        for (const Vehicle& c : g_->cars) if (c.uid == carUid_) return &c;
        return nullptr;
    }

    // FindACarComplete -> GetToKennys: the restaurant's chevrons
    void findACarComplete() {
        g_->removeBlip(1);
        const int32_t kenny[3] = {fx(325.86f), fx(-57.59f), 0};
        g_->addChevron(kenny, 0xFFE000FF, 2, 180, 2, 7, 0);   // CreateChevron(.., 180, two, palette 7, style 0)
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

    // GetToKennysComplete -> cCutscene_AtKennys::Begin (StartCutscene) -> LeftVehicle: CameraImpl::FadeToBlack(15,
    // true, false) fades only the second screen, which Android does not draw; the door handler walks him into the
    // restaurant (cDoorHandler::CreateDoor at heading 0, DoMainStuff(enter, true)), then IntroSetup
    void getToKennysComplete() {
        g_->removeBlip(2);
        g_->objective.frames = 0;
        g_->cutscene = g_->scriptCutscene = true;
        door_.start(*g_, kKennysDoor, 0, DoorHandler::kEnter, [this] { atKennysIntroSetup(); }, true);
    }

    // IntroSetup: the conversation (SCENE_02, WalkOut on its callback 0), the player's orders cleared and the wanted
    // level with them (ScriptPlayer::SetWantedLevel(0, false, true, true))
    void atKennysIntroSetup() {
        g_->scriptWalk.on = false;   // Ped::ClearAllOrders
        g_->wanted.scriptSetLevel(0, false, true, true);
        Sequence* s = play("CSS_KEN_A01_SCENE_02.seq", [this] { cutsceneAtKennysDoFadeIn(); }, false);
        if (s) s->setCallback(0, [this] { atKennysWalkOut(); });
    }

    // WalkOut: another door in the same place, and he walks out of it from inside (DoMainStuff(leave, true))
    void atKennysWalkOut() {
        door_.start(*g_, kKennysDoor, 0, DoorHandler::kLeave, nullptr, true);
    }

    // CutsceneAtKennysDoFadeIn: the apartment (364.08, 25.02), arrived at within 5 units
    void cutsceneAtKennysDoFadeIn() {
        g_->cutscene = g_->scriptCutscene = false;
        g_->seqCamera.active = false;
        const int32_t home[3] = {fx(364.08f), fx(25.02f), 0};
        g_->addBlip(home, 0xFFE000FF, 3, 1);
        g_->showObjective(text(0x53F));   // "Go to your apartment."
        when([this, home] { return near(home, 5.f); }, [this] { parkAtSafehouseComplete(); });
    }

    // ParkAtSafehouseComplete: the area, blip and route go, StartCutscene(false); to black over 15 frames unless it
    // is black already, and 60 frames on StartSafehouseCutscene (Vehicle::SetStop on the player's car is not ported)
    void parkAtSafehouseComplete() {
        g_->removeBlip(3);
        g_->objective.frames = 0;
        g_->cutscene = g_->scriptCutscene = true;
        if (brightness() != 0) fadeToBlack(15);
        wait(0x3C, [this] { startSafehouseCutscene(); });
    }

    // StartSafehouseCutscene: cCutscene_AtSafehouse_Sequence::Start (CutsceneAtSafehouseComplete when it is done),
    // and SafehouseDoor 32 frames on
    void startSafehouseCutscene() {
        safehouseSetup();
        wait(0x20, [this] { safehouseDoor(); });
    }

    static constexpr int32_t kGarage[3] = {0x179A14, 0x16DC2, 0};   // qword_479CC0: (377.63, 22.86)
    int garage_ = -1;                // cCutscene_AtSafehouse_Sequence +0x578 (Garage)
    bool garageOpened_ = false;      // +0x584

    // Start (the building swap of group 13 is not ported) -> Setup: StartCutscene, the garage nearest the apartment,
    // the player on foot put at (360.00, 25.07), the wanted level cleared, the tour's callbacks; PlayTheSequence
    // fades in at once if the screen is black
    void safehouseSetup() {
        garage_ = g_->garages.closest(kGarage);
        garageOpened_ = false;
        if (g_->playerCar < 0) placePlayer(360.f, 25.07f, 0.f);   // qword_47D3A0
        g_->wanted.scriptSetLevel(0, false, true, true);
        Sequence* s = play("CSS_Ken_A01_Safehouse.seq", [this] { safehouseTidyUp(); }, false);
        if (brightness() == 0) fadeIn(0);
        if (!s) return;
        auto click = [] { TheSound().playResident(0x22E, 0x7F); };   // SoundImpl::PlaySimpleSFX(0x22E, 0x7F, 2, ..)
        s->setCallback(0, click);   // Callback_TextLoad
        s->setCallback(1, click);   // Callback_TextSave
        s->setCallback(2, [this, click] {   // Callback_TextRadar: the garage's blip (sprite 0x74, drawn as a dot)
            click();
            g_->addBlip(kGarage, 0x30D010FF, 4);   // (palette 11: colour 6, green)
        });
        s->setCallback(3, click);   // Callback_TextSafehouse
        s->setCallback(4, [this, click] {   // Callback_TextGarage: it is bought, and opens
            click();
            g_->garages.open(garage_);
            garageOpened_ = true;
        });
        s->setCallback(5, click);   // Callback_TextGarage2
        s->setCallback(6, [this] { g_->objective.frames = 0; });   // Callback_TidyUp: the queue and print texts
    }

    // TidyUpEndOfSafehouse (the sequence's end or skip): the garage shuts again if it was opened, the blip goes,
    // EndCutscene, then CutsceneAtSafehouseComplete: 60 frames on, Passed
    void safehouseTidyUp() {
        if (garageOpened_) g_->garages.close(garage_);
        g_->removeBlip(4);
        g_->cutscene = g_->scriptCutscene = false;
        g_->seqCamera.active = false;
        wait(0x3C, [this] { passed(); });
    }

    // SafehouseDoor: the safehouses are enabled again (not ported) and the garage gets its chevrons (CreateChevron at
    // -270 degrees, four, palette 11 (green), style 2: not on the radar; Marker::SetRadius(3))
    void safehouseDoor() { g_->addChevron(kGarage, 0x40C040FF, 5, -270, 1, 11, 2, 0x3000); }

    void passed() {   // Passed: the markers go (the safehouse's chevrons), then WorldImpl::MissionFinished
        g_->removeBlip(5);
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
