// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// kena01::cKEN_A01, the first mission ("Arrival in Liberty"): the prologue on the plane, the landing, the ambush
// at the port and the intro movie, then Huang on the jetty, a car, Uncle Kenny's restaurant and the apartment.
//
// Not ported yet, and passed over here: the plane's sound, the sinking car and its minigame (cCarInWater,
// cSinkingCarApp), swimming to the jetty, the police on the shore (cPoliceChase), hotwiring (cHotWiringApp), the
// GPS route and the help texts' timing. At the port the people are puppets the scene moves (getting in and out of
// cars without the door animations) and only the bodyguards' deaths are decided, as the story has them.
#include "mission.h"
#include "game.h"
#include "intro.h"
#include "driving.h"
#include "hud.h"
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
        play("CSS_KEN_A01_NewIntro1.seq", [this] { endPlaneIntro(); doLandingIntro(); });
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
    void makeCar(int which, int info, float x, float y, int headingDeg) {
        const int32_t at[3] = {fx(x), fx(y), 0};
        sceneCars_[which] = SceneCar{};
        const int i = g_->spawnCar(info, at, (int16_t)deg(headingDeg));
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
    // Vehicle::SetGoTo + SetSpeed
    void driveTo(int which, float x, float y, int speed) {
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
        // (WaitForFadeInAirport fades in from black: the apron is loaded meanwhile)
        g_->screenFade.toBlack(0);
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
        wait(0x28, [this] { g_->screenFade.in(15); });   // WaitForFadeInAirport, 40 frames in
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
