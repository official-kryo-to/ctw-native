// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#define SDL_MAIN_HANDLED
#include "game.h"
#include "padinput.h"
#include "sound.h"
#include "particles.h"
#include "audio/audio.h"
#include "os/os.h"
#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>

static int failures;
static void check(bool ok, const char* description) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", description); ++failures; }
}

struct VehicleTestAccess {
    static void drive(Vehicle& v, bool gas, bool reverse, int32_t accel) {
        v.engineOn = true; v.b62_ = gas ? 1 : 0; v.gear_ = reverse ? -1 : 1;
        v.accel_[0] = accel; v.tyre_[0].onGround = v.tyre_[1].onGround = true;
    }
    static bool fire(const Vehicle& v) { return v.fire_ != nullptr; }
    static void bike(Vehicle& v, bool bike) { v.bike_ = bike; }
    static void burnouts() {
        VehicleInfo info{}; info.raw[0x8F] = 2;   // info +0x8E bit 9: burnouts allowed
        info.raw[0x51] = 0x20;                     // mass 0x2000 (heavier than 0xBFF)
        const int32_t at[3] = {};
        Vehicle v; v.init(info,0,at,0,0);
        v.engineOn = true; v.gear_ = 1; v.tyre_[1].onGround = true;
        Vehicle::Controls held; held.burnout = true; held.throttle = -0x1000;
        v.act(held,true,nullptr);
        check(v.burnout_ == 1 && v.tyre_[1].spinning && v.burnoutPower_ == 0x1B && v.burnoutWindow_ == 0x800,
              "the burnout button spins the back wheels on the spot and builds up its power");
        for (int i = 1; i < 60; ++i) v.act(held,true,nullptr);
        Vehicle::Controls none;
        v.act(none,true,nullptr);
        check(v.burnout_ == 2, "letting go of the burnout opens the launch window");
        const int32_t before = v.vel[1];
        Vehicle::Controls gas; gas.throttle = 0x1000;
        v.act(gas,true,nullptr);
        check(v.burnout_ == 3 && v.vel[1] - before >= 0xC000 - 0x1000, "accelerating in the window launches it forward");
        Vehicle closed; closed.init(VehicleInfo{},0,at,0,0); closed.tyre_[1].onGround = true;
        closed.act(held,true,nullptr);
        check(closed.burnout_ == 0, "a vehicle without the burnout flag cannot burn out");
        // CEngine::Update: damage takes the power away
        const_cast<GameplayTables&>(TheGameplayTables()).gearRatio[2] = 0x100000000LL;   // (no game binary here)
        Vehicle engine; engine.init(info,0,at,0,0); engine.engineOn = true; engine.b62_ = 1;
        engine.maxTorque_ = 0x100000; engine.throttleAbs_ = 0x1000; engine.finalDrive_ = 0x1000;
        auto force = [&](uint8_t health, uint8_t burnout) {   // one Update in first gear, no shift pending
            engine.gear_ = 1; engine.shiftDelay_ = 0; engine.health_ = health; engine.burnout_ = burnout;
            engine.updateEngine();
            return engine.driveForce_;
        };
        const int32_t full = force(0xFF, 0);
        check(full != 0 && force(0x40, 0) == full >> 1, "a damaged engine has half its power");
        check(force(0x10, 0) == (int32_t)(((int64_t)full * 0x333) >> 12), "a burning engine has a fifth of its power");
        check(force(0xFF, 3) == (int32_t)(((int64_t)full * 0xA000) >> 12), "a heavy vehicle's burnout launch has ten times the power");
    }
    static void knockOff() {
        VehicleInfo info{}; info.raw[0] = 1;   // a bike
        const int32_t at[3] = {};
        Vehicle bike; bike.init(info,0,at,0,0); bike.seatUser[0] = -2; bike.playerDriving_ = true;
        const int32_t soft[3] = {100 * 4096, 0, 0}, medium[3] = {300 * 4096, 0, 0}, hard[3] = {450 * 4096, 0, 0};
        bike.onCollision(medium, nullptr);
        check(!bike.knockOff.pending, "a moderate knock against the world keeps the player on an upright bike");
        bike.onCollision(hard, nullptr);
        check(bike.knockOff.pending && bike.knockOff.dir[0] == hard[0],
              "a collision force above 400 against the world throws the player off along the impact");
        Vehicle traffic; traffic.init(info,0,at,0,0); traffic.seatUser[0] = 7;
        traffic.onCollision(soft, nullptr);
        check(!traffic.knockOff.pending, "other riders keep their seat below 141");
        traffic.onCollision(medium, nullptr);
        check(traffic.knockOff.pending, "other riders fall off above 141");
        traffic.knockOff.pending = false;
        traffic.up[2] = 0x300; traffic.onCollision(soft, nullptr);
        check(traffic.knockOff.pending, "any hit throws the rider off a bike lying tipped over");
        Vehicle car; car.init(VehicleInfo{},0,at,0,0); car.seatUser[0] = -2; car.playerDriving_ = true;
        car.onCollision(hard, nullptr);
        check(!car.knockOff.pending, "car drivers are never thrown out by a collision");
    }
    static void flaps() {
        VehicleInfo info{}; info.raw[0x94]=1; info.raw[0x110]=2;
        const int32_t at[3]={};
        Vehicle v; v.init(info,0,at,0,0);
        v.flapBits_=1; v.flapDoors();
        check(v.doors_[0].speed==11 && v.doors_[0].mode==0,
              "a damaged door has the reference restoring force without a scripted opening animation");
        v.doors_[0]={40,0,0}; v.vel[1]=30*4096; v.flapDoors();
        check(v.doors_[0].speed==-33, "forward acceleration swings an unlatched door shut");
        v.doors_[0]={40,0,0}; v.vel[1]=0; v.flapDoors();
        check(v.doors_[0].speed==30, "deceleration swings an unlatched door outward");
        v.doors_[0]={40,0,0}; v.angVel[2]=4096; v.flapDoors();
        check(v.doors_[0].speed==2, "turning contributes twice the absolute spin along the car up axis");
        v.debris_[0].detached=true; v.doors_[0]={25,10,0}; v.updateDoors();
        check(v.doors_[0].angle==25 && v.doors_[0].speed==10,
              "the hinge controller cannot move a detached panel");
        v.flapBits_=16; v.angVel[2]=0; v.doors_[4]={40,0,0}; v.vel[1]=30*4096;
        v.flapDoors();
        check(v.doors_[4].speed==32, "the hatch uses its separate acceleration and gravity controller");
        Model m; m.nodes.resize(3); m.nodes[2].t[1]=-1; v.processDebris(&m);
        v.doors_[4]={40,0,0}; std::fill(v.flapPreviousVelocity_,v.flapPreviousVelocity_+3,0);
        v.flapDoors();
        check(v.bootReverse_ && v.doors_[4].speed==-33, "a rear hinge reverses the hatch response");
        v.releaseEffects();
    }
    static void debris() {
        VehicleInfo info{}; info.raw[0x94]=1; info.raw[0x95]=255; info.raw[0x96]=255; info.raw[0x97]=255;
        info.raw[0x110]=255;
        const int32_t at[3]={10*4096,20*4096,4096};
        Vehicle v; v.init(info,0,at,0,0); v.vel[0]=2*4096;
        Model model;
        NodeMatrix root{}; root.r[0][0]=root.r[1][1]=root.r[2][2]=1;
        model.world={root,root}; model.world[1].t[0]=-1; model.world[1].t[2]=2;
        RandInit(0);
        check(v.createDebris(0,0xA000,model) && !v.hasDoor(0), "detaching a door removes it from usable door slots");
        check(v.debris_[0].pos[0]==at[0]-4096 && v.debris_[0].pos[2]==at[2]+2*4096,
              "fragment starts at its model node in the vehicle's world transform");
        check(!v.createDebris(0,0xA000,model) && !v.createDebris(1,0xA000,model),
              "a missing node or already detached door cannot emit another fragment");
        auto& d=v.debris_[0];
        d.velocity[0]=d.velocity[1]=0; d.velocity[2]=40960;
        std::fill(d.angular,d.angular+3,0);
        const int32_t oldZ=d.pos[2]; v.processDebris(&model);
        check(d.velocity[2]==35173 && d.pos[2]==oldZ+1167, "debris uses the reference gravity, drag and 17/512 time step");
        const int32_t worldX=d.pos[0]; v.pos[0]+=100*4096;
        float matrix[16]; v.modelMatrix(matrix); NodeMatrix node;
        check(v.debrisNode(1,matrix,node) && std::abs(node.t[0]+matrix[12]-worldX/4096.f)<0.001f,
              "moving the wreck cannot drag its detached door through the world");
        d.pos[2]=-20*4096; v.processDebris(&model);
        check(d.pos[2]==-20*4096, "debris stops below the original -20-unit cutoff");
        check(v.repair() && v.hasDoor(0) && !v.debris_[0].detached, "repair restores missing door geometry and usability");
        v.engineOn=true; v.indicators=3; v.setDead();
        // cVehicle::SetDead masks +0x948 with 0xFB_FFFF_FD7F: the left indicator and the hazards go, the right bit
        // stays (a wreck has no driver, and without one only both bits together are drawn)
        check(!v.engineOn && v.indicators == 2 && !v.hazards && v.dead(), "a burnt wreck keeps only the right indicator bit");
        v.releaseEffects();
    }
};

struct WaterTestAccess {
    static void fountain(const Fountain& f, const int32_t at[3]) {
        check(std::equal(f.pos_,f.pos_+3,at) && f.vel_[0]==-1024 && f.vel_[1]==0 && f.vel_[2]==6144,
              "hydrant fountain stays at its base with the reference opposing horizontal jet and 1.5-unit lift");
    }
    static void run() {
        WaterCannon jet; const int32_t at[3]={},v[3]={0,0,6144};
        jet.input(at,v,0);
        check(jet.splash_.has_value(), "water input creates the missing nozzle splash emitter");
        jet.step();
        for(int i=1;i<90;++i) { jet.input(at,v,i); jet.step(); }
        check(jet.splash_.has_value() && jet.age_[(jet.head_-1)&15]==2, "sustained water input keeps its spray and ring stream alive");
        WaterCannon dry; dry.flags&=~8; dry.input(at,v,0);
        check(!dry.splash_, "the water splash flag controls spray emission");
    }
};

struct PlayerTestAccess {
    static void fightIdle() {
        Player player;int upper=0,legs=1;
        player.upperAim=true;
        check(player.fightIdle(upper,legs) && player.fightIdleFrames_==89 &&
              upper==player.bodySet*275+104 && legs==player.bodySet*275+105,
              "unarmed aiming refreshes ninety fight-idle ticks before consuming the current tick");
        player.upperAim=false;
        for(int i=0;i<89;++i) check(player.fightIdle(upper,legs),"fight idle retains its original ninety-frame duration");
        check(!player.fightIdle(upper,legs),"fight idle releases its pose on the exact timer boundary");
        for(int weapon:{5,7,6,8,9}) {
            player.weaponType=weapon;player.upperAim=true;
            check(player.fightIdle(upper,legs) && player.fightIdleFrames_==0 &&
                  upper==player.bodySet*275+(weapon==6?231:weapon==8||weapon==9?239:234),
                  "aimed sidearms use their native pose without retaining an unarmed fight timer");
        }
        player.weaponType=0;player.upperAim=true;player.level_=3;
        check(!player.fightIdle(upper,legs) && player.fightIdleFrames_==0,"sprinting cancels the unarmed fight-idle timer");
        player.level_=2;player.upperAim=false;player.fightIdleFrames_=10;
        player.frameUpper_=0;player.frameLegs_=6<<8;
        check(player.walkFrame()==6,"fight-idle footsteps use the moving legs rather than the independent combat upper body");
        player.weaponType=3;
        check(!player.fightIdle(upper,legs) && !player.fightIdleFrames_,"equipping a melee weapon ends unarmed fight idle");
    }
    static void gait(Player& p, int frame, int level = 2, bool grounded = true, int legsFrame = -1) {
        p.frameUpper_ = frame << 8; p.frameLegs_ = (legsFrame < 0 ? frame : legsFrame) << 8;
        p.level_ = (uint8_t)level; p.onGround_ = grounded;
    }
};

struct SoundTestAccess {
    static void run() {
        Game game;
        game.vehicleInfos.resize(1); game.vehicleInfos[0] = VehicleInfo{};
        game.vehicleInfos[0].raw[0x8E] = 64;
        game.cars.resize(1); Vehicle& v = game.cars[0];
        v.infoId = 0; v.uid = 1; game.playerCar = 0;
        Sound sound;
        for (EventInfo& event : sound.tables_.events) { event.mode = 2; event.pan = -1; }
        for (int event : {0x31,0x32,0xD,0x56}) sound.tables_.events[event].mode = 1;
        sound.tables_.gears[0].vol = 20;
        sound.carBankEnum_ = 0;
        VehicleTestAccess::drive(v, true, false, 20000);
        Sound::Entity entity;
        sound.playerCar(game, v, entity);
        check(sound.state_ != 5, "forward acceleration does not trigger reverse sound state");
        check(std::none_of(std::begin(entity.s), std::end(entity.s), [](auto& s){ return s.event == 0x56 || s.event == 0x57; }),
              "firetruck reverse alarm is not emitted in forward gear");
        entity = Sound::Entity{};
        VehicleTestAccess::drive(v, true, true, 20000);
        sound.playerCar(game, v, entity);
        check(sound.state_ == 5, "reverse acceleration enters reversing state");
        check(std::any_of(std::begin(entity.s), std::end(entity.s), [](auto& s){ return s.event == 0x56; }),
              "reverse alarm remains available while actually reversing");
        VehicleTestAccess::drive(v, false, false, 0); v.vel[0] = 20;
        entity = Sound::Entity{};
        sound.playerCar(game, v, entity);
        check(sound.state_ == 0 && sound.volA_ == 0 && sound.volB_ == 0,
              "stopping with tiny physics drift resets the driving loops");
        check(std::all_of(std::begin(entity.s), std::end(entity.s), [](auto& s){ return (s.event != 0x31 && s.event != 0x32) || s.volume == 0; }),
              "engine volume offset cannot unmute a stopped driving loop");
        sound.state_ = 4; sound.revs_ = 400000; sound.volA_ = 100; sound.volB_ = 100;
        v.uid = 2; sound.playerCar(game, v, entity);
        check(sound.playerUid_ == 2 && sound.state_ == 0 && sound.revs_ == 0,
              "changing cars resets engine state even with the same sound bank");
        sound.carBanks_["carbnk1"].data = {1,2,3,4};
        sound.tables_.gears[0].bank = 1; sound.carBankEnum_ = -1;
        game.dataDir = "this-path-does-not-exist";
        sound.playerCar(game,v,entity);
        const auto* cached = sound.activeCarBank_;
        check(cached == &sound.carBanks_.at("carbnk1") && cached->data.size() == 4,
              "first entry selects its preloaded engine bank without opening the data directory");
        v.uid = 4; sound.playerCar(game,v,entity);
        check(sound.activeCarBank_ == cached && cached->data.data() == sound.carBanks_.at("carbnk1").data.data(),
              "a fresh car of the same model shares the cached bank without copying or loading samples");

        Sound::Entity loop;
        loop.s[0].event = 0x31; loop.s[0].radius = 300; loop.s[0].volume = 100;
        loop.s[0].active = false;
        sound.processEntity(game, loop, false);
        check(loop.s[0].event == 0x9C, "unrefreshed loop is removed before it can restart on entering earshot");
        loop.s[0].event = 0x31; loop.s[0].sfx = 0; loop.s[0].freq = 300;
        sound.addEvent(loop, 0x31, 80, 400, 1);
        check(loop.s[0].sfx == 1 && loop.s[0].freq == 0 && loop.s[0].radius == 400,
              "changing a looping sample replaces its sample and pitch state");

        Sound::Entity door;
        sound.addEvent(door, 0x61, 120, 1000, -1);
        v.uid = 3;
        sound.playerCar(game, v, door);
        check(std::any_of(std::begin(door.s), std::end(door.s), [](auto& s){ return s.event == 0x61; }),
              "taking the driver's seat preserves the queued closing-door sound");
        sound.stopLoops(door);
        check(std::any_of(std::begin(door.s), std::end(door.s), [](auto& s){ return s.event == 0x61; }) &&
              std::none_of(std::begin(door.s), std::end(door.s), [](auto& s){ return s.event == 0x31 || s.event == 0x32; }),
              "leaving the driver's seat stops engine loops but preserves closing-door one-shots");

        game.playerCar = -1;
        auto footfalls = [&]() { return std::count_if(std::begin(sound.ped_.s), std::end(sound.ped_.s), [](auto& s){ return s.event == 0x35; }); };
        PlayerTestAccess::gait(game.player, 0); sound.playerPed(game);
        check(footfalls() == 0, "walking cycle starts before the first footfall");
        PlayerTestAccess::gait(game.player, 6); sound.playerPed(game); sound.playerPed(game);
        check(footfalls() == 1 && sound.ped_.s[0].volume == 0x23 && sound.ped_.s[0].radius == 200,
              "first footfall follows the original unarmed frame, event volume and radius without repeats");
        PlayerTestAccess::gait(game.player, 12); sound.playerPed(game);
        check(footfalls() == 1, "first half-cycle produces one footfall");
        PlayerTestAccess::gait(game.player, 13); sound.playerPed(game);
        check(footfalls() == 2, "second footfall follows the second animation phase");
        PlayerTestAccess::gait(game.player, 0); sound.playerPed(game);
        PlayerTestAccess::gait(game.player, 7, 3); sound.playerPed(game);
        check(footfalls() == 3, "sprinting and skipped animation frames still alternate footfalls");
        PlayerTestAccess::gait(game.player, 14, 2, false); sound.playerPed(game);
        check(footfalls() == 3, "airborne player does not emit footsteps");
        PlayerTestAccess::gait(game.player, 6, 0); sound.playerPed(game);
        check(footfalls() == 3, "stationary player does not emit footsteps");
        game.player.attached = true; sound.playerPed(game);
        check(footfalls() == 0, "entry animations discard pending footsteps");
        game.player.attached = false; game.player.dead = true; sound.playerPed(game);
        check(footfalls() == 0, "dead player does not emit footsteps");
        game.player.dead = false;
        for (int weapon = 0; weapon < 28; ++weapon) {
            sound.stopSlots(sound.ped_); sound.firstFoot_ = true; sound.lastWalkFrame_ = -1;
            game.player.weaponType = weapon; game.player.weaponState = 0;
            PlayerTestAccess::gait(game.player, 0, 2, true, 6);
            sound.playerPed(game);
            const bool legs = weapon < 20 && ((1u << weapon) & 0x3FC1Eu);
            check(footfalls() == (legs ? 1 : 0),
                  "held weapons select the original upper/legs footstep cycle, including the sword");
            if (legs) {
                PlayerTestAccess::gait(game.player, 0, 2, true, 13); sound.playerPed(game);
                check(footfalls() == 2, "a fixed weapon upper pose cannot suppress alternating leg footfalls");
            }
        }
        for (int state = 0; state <= 4; ++state) {
            sound.stopSlots(sound.ped_); sound.firstFoot_ = true; sound.lastWalkFrame_ = -1;
            game.player.weaponType = 19; game.player.weaponState = state;
            PlayerTestAccess::gait(game.player, 0, 2, true, 6); sound.playerPed(game);
            check(footfalls() == ((state == 1 || state == 2) ? 1 : 0),
                  "taser footsteps select the legs only while firing or cooling down");
        }
        game.player.weaponType = game.player.weaponState = 0;
        sound.stopSlots(sound.ped_);

        // Arbitrary synthetic mapping and PCM, not the game's sound table or recordings.
        sound.res_.data.resize(0x20 + 2048, 128);
        const uint32_t length = 2048, rate = 22050;
        std::memcpy(sound.res_.data.data() + 4, &length, 4);
        std::memcpy(sound.res_.data.data() + 0x14, &rate, 4);
        sound.res_.entries.resize(18);
        sound.res_.entries[17] = {0, 0x20 + length, rate};
        sound.tables_.propSfx[4] = {17, 91, 0};
        const int32_t at[3] = {0, 0, 0};
        std::copy(at, at + 3, game.player.pos);
        sound.propSmash(at, -1); sound.propSmash(at, 57);
        check(sound.script_[0].s[0].event == 0x9C, "invalid smash effects do not index the sound table");
        sound.propSmash(at, 4);
        const auto& smash = sound.script_[0].s[0];
        check(smash.event == 0x30 && smash.sfx == 17 && smash.volume == 91 && smash.radius == 300,
              "object smash uses its mapped resident sample, volume and original positional event");
        for (int i = 0; i < 9; ++i) sound.propSmash(at, 4);
        check(std::count_if(std::begin(sound.script_), std::end(sound.script_), [](auto& e){ return e.s[0].event == 0x30; }) == 8,
              "simultaneous object sounds occupy the original eight one-shot entities without overwriting");
        Audio_SetSfxPaused(true);
        game.cars.clear(); sound.update(game);
        check(std::all_of(std::begin(sound.script_), std::end(sound.script_), [](auto& e){ return e.s[0].voice && Audio_SfxPlaying(e.s[0].voice); }),
              "object sounds start from resident PCM and survive without any vehicle entity");
        for (auto& e : sound.script_) sound.stopSlots(e);
        sound.propSmash(at, 4); sound.script_[0].pos[0] = 1000 * 4096; sound.update(game);
        check(sound.script_[0].s[0].event == 0x9C, "distant object sounds expire and release their script slot");
        sound.tables_.events[0x6C].mode=1; sound.tables_.events[0x6C].sfx=17;
        sound.waterStream(100,at,60);
        check(sound.water_.at(100).s[0].event==0x6C && sound.water_.at(100).s[0].radius==300,
              "water streams use their resident looping event and reference positional radius");
        sound.update(game); const int waterVoice=sound.water_.at(100).s[0].voice;
        check(waterVoice && Audio_SfxPlaying(waterVoice), "water loop starts from the resident sound bank");
        sound.waterStream(100,at,50); sound.update(game);
        check(sound.water_.at(100).s[0].voice==waterVoice, "refreshing a water stream preserves its voice instead of restarting it");
        sound.update(game);
        check(sound.water_.empty() && !Audio_SfxPlaying(waterVoice), "removing a fountain stops its loop and releases its audio entity");
        Audio_SetSfxPaused(false);
    }
};

struct CollisionTestAccess {
    static void cameraTests() {
        Collision col;
        col.world_.assign(40000,0);
        auto cell = std::make_unique<Collision::Cell>(); cell->loaded = true;
        const int32_t x = -2475*4096, y = -1475*4096;
        auto& boxes = cell->boxes;
        boxes.push_back({x,y,6*4096,4096,4*4096,2048,0,0});
        col.cells_[2020] = std::move(cell);
        const int32_t target[3] = {x+3686,y,0}, vertical[3] = {x+3686,y,28*4096};
        int32_t camera[3] = {x-10*4096,y,28*4096};
        CameraObstruction state;
        for (int i = 0; i < 5; ++i) state.count(target,vertical,true,&col);
        state.recover(target,camera,&col);
        check(state.blockedFrames() == 5 && camera[0] == x-10*4096,
              "brief occlusion does not teleport the camera before six blocked frames");
        state.count(target,vertical,true,&col); state.recover(target,camera,&col);
        check(camera[0] > x+4096 && camera[1] == y && camera[2] == 28*4096,
              "sustained roof occlusion escapes the nearest edge while preserving camera height");
        state.count(target,camera,true,&col);
        check(!state.blockedFrames(), "a recovered clear view resets the consecutive obstruction counter");
        state.reset(6); camera[0] = x-25*4096;
        boxes.clear(); boxes.push_back({x-10*4096,y,8*4096,4096,4096,8*4096,0,0});
        state.recover(target,camera,&col);
        check(camera[0] == target[0] && camera[1] == target[1],
              "distant blocked view recentres above the target when both vertical checks are clear");
        camera[0] = x-25*4096;
        boxes.push_back({target[0],y,45*4096,4096,4096,4096,0,0});
        state.recover(target,camera,&col);
        check(camera[0] == x-25*4096, "overhead obstruction prevents unsafe camera recentering");
        boxes.clear(); boxes.push_back({x,y,6*4096,4096,4096,4096,0,2});
        Collision::LineHit hit;
        const int32_t a[3] = {x,y,0}, b[3] = {x,y,20*4096};
        check(col.staticLine(a,b,0x200,&hit) && hit.isBox && hit.box.flags == 2 && !col.staticLine(a,b,0x2200),
              "camera box filter preserves the original flag-2 cell gate");
        boxes.push_back({x,y,12*4096,4096,4096,4096,0,0});
        check(col.staticLine(a,b,0x80002200,&hit) && hit.box.flags == 2,
              "an unflagged hit opens the cell gate for its flagged boxes too");
        boxes[0].flags = 4;
        check(col.staticLine(a,b,0x1200,&hit) && hit.box.cz == 12*4096,
              "line filter excludes box flag 4 independently of the cell gate");
        std::reverse(boxes.begin(),boxes.end());
        check(col.staticLine(a,b,0x40000200,&hit) && hit.box.cz == 6*4096,
              "nearest line result uses contact fraction rather than shape-list order");
        auto next = std::make_unique<Collision::Cell>(); next->loaded = true;
        next->boxes.push_back({x+50*4096,y,6*4096,2*4096,4096,4096,0,2});
        col.cells_[2120] = std::move(next);
        const int32_t acrossA[3] = {x-10*4096,y,6*4096}, acrossB[3] = {x+60*4096,y,6*4096};
        check(!col.staticLine(acrossA,acrossB,0x80002200),
              "a failed gate ends a multi-cell query instead of testing earlier cells");
        check(col.staticLine(acrossA,acrossB,0x80000200), "ordinary box queries still visit the same blocked segment");
        col.cells_.erase(2120);
        boxes.clear(); col.cells_[2020]->cyls.push_back({x,y,0,4096,8*4096,0});
        const int32_t sideA[3] = {x-5*4096,y,4096}, sideB[3] = {x+5*4096,y,4096};
        check(col.staticLine(sideA,sideB,0x400,&hit) && !hit.isBox && hit.fraction > 0,
              "static line queries support cylinders without inventing a box result");
        FollowCarCam car; FollowPedCam ped;
        ped.reset(target,0); ped.inherit(vertical,0,55000,6);
        car.inherit(camera,0,55000,ped.blockedFrames());
        check(car.blockedFrames() == 6, "entry carries shared obstruction state to the vehicle camera");
        ped.reset(target,0);
        check(!ped.blockedFrames(), "fresh camera reset discards stale occlusion state");
    }
    static void propScene(Game& g, int32_t height = 0) {
        Collision::Prop p{}; p.x = -2475 * 4096; p.y = -1475 * 4096; p.z = height;
        g.player.pos[0] = p.x; g.player.pos[1] = p.y; g.player.pos[2] = p.z;
        auto cell = std::make_unique<Collision::Cell>(); cell->loaded = true;
        g.collision.world_.assign(10000*4,0);
        cell->groundMap.assign(400, 0x55); // synthetic land
        if (height > 0) cell->boxes.push_back({p.x,p.y,height/2,24*4096,24*4096,height/2,0,0});
        cell->props.push_back(p);
        Collision::Cell::PropShapes shapes;
        shapes.box0 = (uint32_t)cell->boxes.size();
        g.props.shapes(p, cell->boxes, cell->cyls, cell->meshes);
        shapes.boxes = (uint32_t)cell->boxes.size() - shapes.box0;
        shapes.cyls = (uint32_t)cell->cyls.size(); shapes.meshes = (uint32_t)cell->meshes.size();
        shapes.physical = true;
        cell->propShapes.push_back(shapes);
        g.collision.cells_[2020] = std::move(cell);
    }
    static bool solid(const Game& g) { return g.collision.cells_.at(2020)->propShapes[0].solid; }
    static void physical(Game& g,bool on) { g.collision.cells_.at(2020)->propShapes[0].physical=on; }
    static void wall(Game& g) {
        const auto& p=g.collision.cells_.at(2020)->props[0];
        g.collision.setPropSolid(20,20,0,false);
        g.collision.cells_.at(2020)->boxes.push_back({p.x+5*4096,p.y,6*4096,4096,12*4096,6*4096,0,0});
    }
    static void run() {
        Collision col;
        int32_t p[3] = {-0xDAC000 + 20 * 0x32000 - 500, -0x9C4000 + 20 * 0x32000 + 1000, 4096};
        auto cell = std::make_unique<Collision::Cell>(); cell->loaded = true;
        cell->boxes.push_back({p[0] + 1000,p[1],4096,1000,1000,4096,0,0});
        col.cells_[20 * 100 + 20] = std::move(cell);
        Collision::Candidates candidates;
        col.candidates(p, 4096, false, candidates);
        check(candidates.boxes.size() == 1, "prop across a collision-cell boundary stays a candidate");
        auto& boxes = col.cells_[2020]->boxes;
        for (int i = 0; i < 70; ++i) boxes.push_back(boxes[0]);
        col.candidates(p, 4096, false, candidates);
        check(candidates.boxes.size() == 71, "dense collision lists do not discard later prop shapes");
        Collision::Cell::PropShapes dynamic; dynamic.box0=0; dynamic.boxes=1; dynamic.physical=true;
        col.cells_[2020]->propShapes.push_back(dynamic);
        col.candidates(p,4096,false,candidates,false);
        check(candidates.boxes.size()==70, "vehicle world queries omit only props handled by the physical contact pass");
        col.candidates(p,4096,false,candidates);
        check(candidates.boxes.size()==71, "pedestrian and camera queries still collide with unbroken movable props");
    }
};

struct PropTestAccess {
    static std::unique_ptr<Model> cuboid(int top, float nodeZ = 0) {
        auto m = std::make_unique<Model>(); m->scale = 0.25f;
        NodeMatrix node{}; node.r[0][0] = node.r[1][1] = node.r[2][2] = 1; node.t[2] = nodeZ;
        m->world.push_back(node);
        for (int x : {-2,2}) for (int y : {-4,4}) for (int z : {0,top}) {
            ModelVertex v{}; v.x = (int16_t)x; v.y = (int16_t)y; v.z = (int16_t)z;
            m->verts.push_back(v);
        }
        ModelBatch batch{}; batch.count = 8; m->batches.push_back(batch);
        return m;
    }
    static void furniture(Game& g, int effect, uint16_t broken = 101) {
        PropLibrary::Def def{}; def.model = 100; def.broken = broken; def.kind = 1;
        def.shapes = {{1,{0,0,0,4096,8192,16384}}};
        g.props.defs_.push_back(def);
        g.props.kinds_.push_back({0,0,-1,-1,0,0}); // test canonical lookup with a differing packed kind
        g.props.kinds_.push_back({1,15,5,-1.f/4096,(uint8_t)effect});
        g.props.models_[100] = cuboid(16, 0.25f);
        g.props.models_[101] = cuboid(4, 0.25f);
    }
    static void movable(Game& g) { g.props.kinds_[1].smashForce=-1; g.props.kinds_[1].uprootForce=.1f; }
    static void allowUproot(Game& g) { g.props.kinds_[1].uprootForce=.1f; }
    static void wide(Game& g) { g.props.defs_[0].shapes={{1,{0,0,0,12*4096,4*4096,2*4096}}}; }
    static void run() {
        PropLibrary lib;
        PropLibrary::Def def{};
        def.shapes = {{1,{0,0,0,8192,8192,8192}}, {4,{4096,0,0}},
                      {5,{0,0,0}}, {5,{8192,0,0}}, {5,{0,8192,0}}, {6,{1,2,3}}};
        lib.defs_.push_back(def);
        Collision::Prop prop{}; prop.x = 4096; prop.y = 8192;
        std::vector<Collision::Box> boxes; std::vector<Collision::Cyl> cyls; std::vector<Collision::Mesh> meshes;
        lib.shapes(prop, boxes, cyls, meshes);
        check(boxes.size() == 1 && boxes[0].cz == 4096 && boxes[0].hz == 4096, "prop boxes retain the resource's bottom-centre convention");
        check(meshes.size() == 1 && meshes[0].tris.size() == 1 && meshes[0].verts[0] == 8192 && meshes[0].tris[0].v[2] == 2,
              "prop mesh offset, vertices and one-based triangle indices are preserved");
        float radius, height; lib.footprint(0, radius, height);
        check(std::abs(radius - 3.f) < 0.001f && height == 2.f, "impact footprint includes the mesh header's offset");
        def.shapes = {{3,{0,0,8192,4096}}}; lib.defs_[0] = def;
        lib.footprint(0, radius, height);
        check(radius == 1.f && height == 3.f, "sphere footprint ends one radius above its centre");

        Game g; furniture(g, 36);
        check(g.props.smashedModel(0) == g.props.brokenModel(0), "valid broken model is selected");
        g.props.defs_[0].broken = 0xFFFE;
        check(!g.props.smashedModel(0), "remove-model sentinel cannot fall back to the intact model");
        g.props.defs_[0].broken = 0xFFFF;
        check(g.props.smashedModel(0) == g.props.model(0), "retain-model sentinel preserves the original mesh");
        g.props.defs_[0].broken = 102; g.props.models_[102].reset();
        check(!g.props.smashedModel(0), "failed replacement does not resurrect the undamaged object");
    }
};

struct ParticlesTestAccess {
    static size_t count() { return TheParticles().emitters_.size(); }
    static const Emitter* at(size_t i) { return TheParticles().emitters_.at(i).get(); }
    static const Emitter* at(const Particles& p, size_t i) { return p.emitters_.at(i).get(); }
    static const Particle& particle(const Emitter& e, size_t i = 0) { return e.parts_.at(i); }
    static bool billboard(const Emitter& e) { return e.billboard_; }
    static bool additive(const Emitter& e) { return e.additive_; }
    static int sprite(const Emitter& e) { return e.sprite_; }
    static int active(const Emitter& e) { return e.alive_; }
    static void step(Emitter& e, Particle& p, uint32_t frame) { e.frame_ = frame; e.updateParticle(p); }
    static void clear() { TheParticles().emitters_.clear(); }
};

struct GameTestAccess {
    static void snapshot(Game& g) { g.snapshotPoses(); }
    static size_t explosions(const Game& g) { return g.explosions_.size(); }
    static void blastPosition(const Game& g, int32_t out[3]) { std::copy(g.explosions_.front().pos,g.explosions_.front().pos+3,out); }
    static void processExplosions(Game& g) { g.processVehicleDeaths(); }
    static int explosionType(const Game& g) { return g.explosions_.front().type; }
    static int explosionState(const Game& g) { return g.explosions_.front().state; }
    static unsigned cloudCount(const Game& g, const int32_t at[3]) { return g.explosionParticleCount(at); }
};

struct PropDynamicsTestAccess {
    static int32_t lowest(const RigidBody& body) {
        int32_t vertices[8][3]; body.bboxVerts(vertices);
        int32_t z = INT32_MAX;
        for (auto& v : vertices) z = std::min(z, v[2]);
        return z;
    }
    static void run() {
        const int32_t force[3] = {200*4096,0,0};
        Game hydrant; PropTestAccess::furniture(hydrant,9,0xFFFF); CollisionTestAccess::propScene(hydrant);
        auto tap=hydrant.propDynamics.makeBody(hydrant,20,20,0);
        const int32_t tapAt[3]={tap.prop.x,tap.prop.y,tap.prop.z};
        hydrant.propDynamics.applyForce(hydrant,tap,tapAt,force);
        check(tap.broken && !tap.collidable && tap.fountain && !tap.body.active(),
              "hydrant smash tilts its anchored model, clears contacts and starts a fountain");
        if(tap.fountain) WaterTestAccess::fountain(*tap.fountain,tapAt);
        float tilted[16]; tap.body.matrix(tilted);
        check(std::abs(tilted[8])>0.4f && std::abs(tilted[8])<0.44f, "hydrant tilt is capped at 25 degrees");
        auto upright=hydrant.propDynamics.makeBody(hydrant,20,20,0);
        const int32_t vertical[3]={0,0,200*4096};
        hydrant.propDynamics.applyForce(hydrant,upright,tapAt,vertical);
        check(!upright.broken && !upright.fountain && upright.collidable, "vertical hydrant impacts cannot substitute a sideways break");
        Game newsstand; PropTestAccess::furniture(newsstand,42); CollisionTestAccess::propScene(newsstand);
        auto news = newsstand.propDynamics.makeBody(newsstand,20,20,0);
        int32_t newsAt[3] = {news.prop.x,news.prop.y,news.prop.z+4096};
        const size_t firstEmitter = ParticlesTestAccess::count();
        newsstand.propDynamics.applyForce(newsstand,news,newsAt,force);
        check(ParticlesTestAccess::count() == firstEmitter+2 &&
              dynamic_cast<const WoodEmitter*>(ParticlesTestAccess::at(firstEmitter)) &&
              dynamic_cast<const PaperEmitter*>(ParticlesTestAccess::at(firstEmitter+1)),
              "newsstand breakage dispatches both wooden fragments and its distinct four-paper burst");
        newsstand.propDynamics.applyForce(newsstand,news,newsAt,force);
        check(ParticlesTestAccess::count() == firstEmitter+2, "hitting a broken prop cannot replay its smash burst");
        // Smash3's petrol pump: a type-7 big-vehicle explosion at its jittered centre and a petrol fire
        Game pump; PropTestAccess::furniture(pump,21); CollisionTestAccess::propScene(pump);
        auto pumpBody = pump.propDynamics.makeBody(pump,20,20,0);
        pump.propDynamics.applyForce(pump,pumpBody,newsAt,force);
        check(GameTestAccess::explosions(pump) == 1 && GameTestAccess::explosionType(pump) == 7 &&
              pump.fires.list().size() == 1 && pump.fires.list()[0].petrol && pump.fires.list()[0].life == 900,
              "a smashed petrol pump queues a type-7 explosion and starts a 900-frame petrol fire");
        pump.fires.clear();
        Game explosive; PropTestAccess::furniture(explosive,28); PropTestAccess::allowUproot(explosive);
        CollisionTestAccess::propScene(explosive);
        auto barrel = explosive.propDynamics.makeBody(explosive,20,20,0);
        int32_t blastCentre[3], blast[3]; barrel.body.worldCG(blastCentre);
        RandInit(0); const size_t beforeBlast = ParticlesTestAccess::count();
        explosive.propDynamics.applyForce(explosive,barrel,newsAt,force);
        GameTestAccess::blastPosition(explosive,blast);
        check(GameTestAccess::explosions(explosive) == 1 && !barrel.collidable &&
              blast[0] == blastCentre[0]-409 && blast[1] == blastCentre[1]-136 && blast[2] == blastCentre[2]+104 &&
              Rand32Critical(0) == 0xC910A194 && ParticlesTestAccess::count() == beforeBlast,
              "explosive prop queues one blast at its jittered world centre using three critical draws and clears contacts");
        explosive.propDynamics.applyForce(explosive,barrel,newsAt,force);
        check(GameTestAccess::explosions(explosive) == 1, "a smashed explosive prop cannot queue a second explosion");
        for (int i = 0; i < 2; ++i) GameTestAccess::processExplosions(explosive);
        check(ParticlesTestAccess::count() == beforeBlast, "generic prop explosion waits through its first two Process calls");
        GameTestAccess::processExplosions(explosive);
        check(ParticlesTestAccess::count() == beforeBlast+2 &&
              dynamic_cast<const ExplosionFlash*>(ParticlesTestAccess::at(beforeBlast)) &&
              dynamic_cast<const ExplosionCloud*>(ParticlesTestAccess::at(beforeBlast+1)),
              "third Process produces the typed flash and cloud without inventing vehicle debris");
        for (int i = 0; i < 10; ++i) GameTestAccess::processExplosions(explosive);
        check(GameTestAccess::explosions(explosive) == 1 && ParticlesTestAccess::count() == beforeBlast+2,
              "generic blast remains in its final state without replaying particles");
        GameTestAccess::processExplosions(explosive);
        check(GameTestAccess::explosions(explosive) == 0, "generic explosion leaves its final state after eleven Process calls");
        for (int effect : {23,30}) {
            Game vendor; PropTestAccess::furniture(vendor,effect); CollisionTestAccess::propScene(vendor);
            auto cart = vendor.propDynamics.makeBody(vendor,20,20,0);
            const size_t before = ParticlesTestAccess::count();
            const int32_t source[3] = {}, velocity[3] = {20*4096,0,0};
            RandInit(0); vendor.propDynamics.applyForce(vendor,cart,newsAt,force,source,velocity,true);
            check(GameTestAccess::explosions(vendor) == 1 && Rand32Critical(0) == 0x269EC3,
                  "both hotdog cart variants queue a small generic vehicle-impact blast without jitter or camera-shake random draws");
            for (int i = 0; i < 3; ++i) GameTestAccess::processExplosions(vendor);
            const Emitter* flash = ParticlesTestAccess::at(before);
            check(ParticlesTestAccess::count() == before+2 && dynamic_cast<const ExplosionFlash*>(flash) &&
                  ParticlesTestAccess::particle(*flash).grow == 2815 && ParticlesTestAccess::particle(*flash).size == 112,
                  "vehicle-struck hotdog cart preserves the reference 0x8CC scale through its delayed flash");
            // ApplyWorldForce hands Smash the force's magnitude: a hard push bursts the cart as the small blast
            Game other; PropTestAccess::furniture(other,effect); CollisionTestAccess::propScene(other);
            auto pushed = other.propDynamics.makeBody(other,20,20,0);
            other.propDynamics.applyForce(other,pushed,newsAt,force);
            check(GameTestAccess::explosions(other) == 1 && GameTestAccess::explosionType(other) == 0,
                  "a hard nonvehicle push bursts a hotdog cart at once with the small generic blast");
            // Damage hands Smash the damage beyond the health: finishing it off softly opens the gas jet
            Game jet; PropTestAccess::furniture(jet,effect); CollisionTestAccess::propScene(jet);
            auto shot = jet.propDynamics.makeBody(jet,20,20,0);
            const int32_t along[3] = {4096,0,0};
            jet.propDynamics.damage(jet,shot,20,7,along);
            check(GameTestAccess::explosions(jet) == 1 && GameTestAccess::explosionType(jet) == 11,
                  "a hotdog cart finished off by a little damage opens its type-11 gas jet");
            jet.muzzleFlashes.beginTick(); GameTestAccess::processExplosions(jet);
            check(jet.muzzleFlashes.count() == 1, "the gas jet draws its type-1 flame every frame");
            int frames = 1;
            while (GameTestAccess::explosionState(jet) == 0 && frames < 100) { GameTestAccess::processExplosions(jet); ++frames; }
            check(frames == 64, "the gas jet burns for sixty-three frames before it bursts");
        }
        for (int effect : {10,41}) {
            Game glass; PropTestAccess::furniture(glass,effect); PropTestAccess::allowUproot(glass);
            CollisionTestAccess::propScene(glass);
            auto prop = glass.propDynamics.makeBody(glass,20,20,0);
            // Force an exact quarter turn to catch debris incorrectly placed on the world X axis.
            prop.body.right[0]=0; prop.body.right[1]=4096;
            prop.body.fwd[0]=-4096; prop.body.fwd[1]=0;
            const size_t before = ParticlesTestAccess::count();
            glass.propDynamics.applyForce(glass,prop,newsAt,force);
            const size_t woodCount = effect == 41 ? 1 : 0;
            check(ParticlesTestAccess::count() == before+woodCount+6 && prop.collidable,
                  "glass emits six pairs of shards, preserving contact; effect 41 also emits wood");
            if (woodCount) check(dynamic_cast<const WoodEmitter*>(ParticlesTestAccess::at(before)),
                                 "combined Smash3 branch emits wood before falling through to glass");
            for (int i=0;i<6;++i) {
                const Emitter* shard = ParticlesTestAccess::at(before+woodCount+i);
                check(dynamic_cast<const GlassEmitter*>(shard) && shard->pos[0] == prop.prop.x &&
                      shard->pos[1] == prop.prop.y-prop.body.hx+i*(2*prop.body.hx/7) &&
                      shard->pos[2] == prop.prop.z+prop.body.hz,
                      "glass origins use the original transformed upper edge and seven-part width spacing");
            }
            glass.propDynamics.applyForce(glass,prop,newsAt,force);
            check(ParticlesTestAccess::count() == before+woodCount+6, "glass cannot replay on subsequent impacts");
        }
        for (int effect : {26,27}) {
            Game gate; PropTestAccess::furniture(gate,effect); CollisionTestAccess::propScene(gate);
            auto prop = gate.propDynamics.makeBody(gate,20,20,0);
            const int32_t impact[3] = {prop.prop.x+2048,prop.prop.y+4096,prop.prop.z+1024};
            const int32_t source[3] = {impact[0]-4096,impact[1],impact[2]}, velocity[3] = {0,20*4096,0};
            const size_t before = ParticlesTestAccess::count();
            gate.propDynamics.applyForce(gate,prop,impact,force,source,velocity);
            const Emitter* debris = ParticlesTestAccess::at(before);
            check(ParticlesTestAccess::count() == before+1 && dynamic_cast<const SlatsEmitter*>(debris) &&
                  debris->pos[0] == impact[0] && debris->pos[1] == impact[1] && debris->pos[2] == impact[2],
                  "both gate smash paths emit slats at the contact point rather than the prop placement");
            gate.propDynamics.applyForce(gate,prop,impact,force,source,velocity);
            check(prop.broken && !prop.uprooted && ParticlesTestAccess::count() == before+1,
                  "gate debris emits once without inventing physics for its anchored remnant");
        }
        for (int effect : {11,12,20,24,29,32,33,34,36,39,43,44,45,46,47,48,49,50,51,52,53,54,55}) {
            Game furniture; PropTestAccess::furniture(furniture,effect); CollisionTestAccess::propScene(furniture);
            auto prop = furniture.propDynamics.makeBody(furniture,20,20,0);
            const size_t before = ParticlesTestAccess::count();
            furniture.propDynamics.applyForce(furniture,prop,newsAt,force);
            check(ParticlesTestAccess::count() == before+1 &&
                  dynamic_cast<const WoodEmitter*>(ParticlesTestAccess::at(before)),
                  "verified furniture smash branch emits fragments instead of only switching meshes");
            Game movable; PropTestAccess::furniture(movable,effect); PropTestAccess::allowUproot(movable);
            CollisionTestAccess::propScene(movable);
            auto loose = movable.propDynamics.makeBody(movable,20,20,0);
            movable.propDynamics.applyForce(movable,loose,newsAt,force);
            check(loose.uprooted && loose.collidable == (effect != 20 && effect != 50),
                  "smash contact flags remain distinct from the original bullet-ignore flag");
        }
        Game g; PropTestAccess::furniture(g,36); CollisionTestAccess::propScene(g);
        auto& dynamics = g.propDynamics;
        auto anchored = dynamics.makeBody(g,20,20,0);
        check(anchored.prop.kind == 0 && anchored.body.mass_ == 4096,
              "prop mass comes from the canonical definition even if the packed kind differs");
        const int32_t point[3] = {anchored.prop.x,anchored.prop.y,anchored.prop.z+4096};
        dynamics.applyForce(g,anchored,point,force);
        check(anchored.broken && !anchored.uprooted && !anchored.body.active(),
              "smashing alone preserves anchored remnants instead of giving them a made-up impulse");
        dynamics.loose_.push_back(anchored);
        const int32_t z = anchored.body.pos[2];
        for (int i=0;i<90;++i) dynamics.update(g);
        check(dynamics.loose_[0].body.pos[2] == z, "anchored broken model keeps its placement");
        check(!CollisionTestAccess::solid(g) && (*g.collision.props(20,20))[0].state != 0,
              "broken prop loses its standing collision and light/render state");
        check(PropDynamics::drawnModel(g,anchored) == g.props.brokenModel(0), "broken-model state is preserved");
        check(!anchored.collidable, "negative Q12 uproot sentinel disables collisions on an anchored broken remnant");

        Game lamp; PropTestAccess::furniture(lamp,7,0xFFFF); CollisionTestAccess::propScene(lamp);
        auto falling = lamp.propDynamics.makeBody(lamp,20,20,0);
        int32_t at[3]={falling.prop.x,falling.prop.y,falling.prop.z+4096};
        lamp.propDynamics.applyForce(lamp,falling,at,force);
        lamp.propDynamics.loose_.push_back(falling);
        for (int i=0;i<7;++i) lamp.propDynamics.update(lamp);
        auto& post = lamp.propDynamics.loose_[0];
        check(post.lampTimer == 53 && !post.body.active(), "lamp bends during the original countdown before enabling physics at 53");
        lamp.propDynamics.update(lamp);
        check(post.lampTimer == 52 && post.body.active(), "lamp enters full physics at the original countdown phase");
        for (int i=0;i<60;++i) lamp.propDynamics.update(lamp);
        check(!post.lampTimer && !post.body.active(), "lamp fall window ends instead of integrating forever");

        PropLibrary::Physics shape{}; shape.half[0]=2048; shape.half[1]=4096; shape.half[2]=8192;
        shape.centre[2]=shape.cg[2]=8192;
        Collision::Prop placement{}; placement.z=10*4096;
        RigidBody light,heavy;
        light.init(placement,shape,4096,false); heavy.init(placement,shape,8192,false);
        light.setToPhysics(true); heavy.setToPhysics(true);
        const int32_t push[3]={30*4096,0,0}; int32_t cg[3]; light.worldCG(cg);
        light.applyWorldForce(cg,push); heavy.applyWorldForce(cg,push);
        light.recalcKinematics(); heavy.recalcKinematics();
        check(light.vel[0] == 4080 && heavy.vel[0] == 2040, "original reciprocal mass and 17/512 frame step control acceleration");
        int32_t offset[3]={cg[0]+4096,cg[1]+4096,cg[2]+4096};
        const int32_t twist[3]={0,40*4096,20*4096};
        light.applyWorldForce(offset,twist); light.recalcKinematics();
        check(light.angVel[0] && light.angVel[1] && light.angVel[2], "off-centre forces rotate the object about all three axes");
        const int32_t before=light.pos[0]; light.integrateStep(4096);
        check(light.pos[0] != before, "rigid-body velocity moves its centre of gravity");
        RigidBody fixed; fixed.init(placement,shape,-4096,false); fixed.setToPhysics(true);
        fixed.applyWorldForce(offset,twist); fixed.recalcKinematics();
        check(!fixed.active() && fixed.vel[0] == 0, "infinite-mass props cannot be uprooted by ordinary forces");
        RigidBody planar; planar.init(placement,shape,4096,true); planar.setToPhysics(true);
        planar.angVel[0]=planar.angVel[1]=planar.angVel[2]=4096;
        planar.integrateStep(4096); planar.damp();
        check(planar.up[0] == 0 && planar.up[1] == 0 && planar.angVel[0] == 0 && planar.angVel[1] == 0 && planar.angVel[2] != 0,
              "kind's planar flag preserves upright props while allowing yaw");
        light.vel[0]=1000*4096; light.recalcKinematics();
        check(light.speed() < 90*4096, "dynamic prop speed uses the original cap");

        RigidBody rotation; rotation.init(placement,shape,4096,false); rotation.setToPhysics(true);
        const int32_t quaternion[4]={100,-200,300,4079},angular[3]={4096,-2048,1024};
        const int32_t centre[3]={12345,-23456,34567},velocity[3]={4096,-8192,12288};
        std::copy(quaternion,quaternion+4,rotation.q_); std::copy(angular,angular+3,rotation.angVel);
        std::copy(centre,centre+3,rotation.cgWorld_); std::copy(velocity,velocity+3,rotation.vel);
        rotation.integrateStep(4096);
        const int32_t expectedQuat[4]={169,-230,319,4073},expectedCG[3]={12481,-23728,34975};
        check(std::equal(rotation.q_,rotation.q_+4,expectedQuat) && std::equal(rotation.cgWorld_,rotation.cgWorld_+3,expectedCG),
              "quaternion normalisation and Q12 frame arithmetic retain original numerical results");

        Game bridge; PropTestAccess::furniture(bridge,0,0xFFFF); CollisionTestAccess::propScene(bridge,3*4096);
        placement.x=bridge.player.pos[0]; placement.y=bridge.player.pos[1]; placement.z=8*4096;
        RigidBody body; body.init(placement,shape,4096,false);
        const int32_t axis[3]={0,4096,0}; body.rotate(axis,0x4000); body.setToPhysics(true); body.vel[2]=-80*4096;
        bool roadContact=false;
        for (int i=0;i<4;++i) { body.process(bridge.collision); roadContact |= body.contact(); }
        check(lowest(body) >= 3*4096-16, "swept box corners prevent fast falls through an elevated road");
        check(roadContact, "raised-road collision is reported to settling logic");

        Game wall; PropTestAccess::furniture(wall,0,0xFFFF); CollisionTestAccess::propScene(wall);
        CollisionTestAccess::wall(wall);
        placement.x=wall.player.pos[0]; placement.y=wall.player.pos[1]; placement.z=3*4096;
        RigidBody sliding; sliding.init(placement,shape,4096,false); sliding.setToPhysics(true); sliding.vel[0]=80*4096;
        bool wallContact=false;
        for(int i=0;i<6;++i) {
            sliding.process(wall.collision); wallContact |= sliding.contact();
            int32_t vertices[8][3]; sliding.bboxVerts(vertices);
            for(auto& v:vertices) check(v[0] <= placement.x+4*4096+16, "fast prop remains on the approach side of a wall");
        }
        check(wallContact, "rigid prop hits world geometry on horizontal axes as well as the ground");

        Game impacts; PropTestAccess::furniture(impacts,0,0xFFFF); CollisionTestAccess::propScene(impacts);
        PropTestAccess::movable(impacts);
        auto prop=impacts.propDynamics.makeBody(impacts,20,20,0);
        VehicleInfo info{};
        auto put16=[&](int off,int v){info.raw[off]=(uint8_t)v;info.raw[off+1]=(uint8_t)(v>>8);};
        put16(0x28,4096); put16(0x2A,8192); put16(0x2C,4096);
        const int32_t mass=2*4096; std::memcpy(info.raw+0x50,&mass,4);
        int32_t startPos[3]={prop.prop.x-4096,prop.prop.y,0};
        Vehicle car; car.init(info,0,startPos,0,0);
        car.vel[0]=-20*4096; car.vel[1]=0;
        impacts.propDynamics.hit(impacts,car,prop);
        check(!prop.uprooted, "motion away from a prop cannot break it using total vehicle speed");
        car.vel[0]=20*4096; car.vel[1]=0;
        check(impacts.propDynamics.hit(impacts,car,prop) && prop.uprooted && prop.body.active(),
              "contact impulse uproots the prop and activates full rigid-body physics");
        check(car.vel[0] < 20*4096 && prop.body.vel[0] > 0, "car and loose prop exchange reciprocal collision impulses");

        auto moving=impacts.propDynamics.makeBody(impacts,20,20,0),standing=moving;
        moving.uprooted=true; moving.body.setToPhysics(true);
        moving.body.cgWorld_[0]-=4096; moving.body.syncFromIntegrator(); moving.body.vel[0]=20*4096;
        check(impacts.propDynamics.hit(impacts,moving,standing) && standing.uprooted && standing.body.active(),
              "loose furniture can uproot another prop instead of passing through it");
        check(moving.body.vel[0] < 20*4096 && standing.body.vel[0] > 0,
              "prop pairs exchange mass and inertia based contact forces");
        moving.body.vel[0]=-20*4096; standing.body.vel[0]=20*4096;
        check(!impacts.propDynamics.hit(impacts,moving,standing), "separating props do not receive another collision impulse");
        int reactions[2]{};
        for(int separate=0;separate<2;++separate) {
            Game scene; PropTestAccess::furniture(scene,0,0xFFFF); PropTestAccess::movable(scene); PropTestAccess::wide(scene);
            CollisionTestAccess::propScene(scene); CollisionTestAccess::physical(scene,separate!=0);
            int32_t from[3]={scene.player.pos[0],scene.player.pos[1]-12*4096,0};
            scene.cars.resize(1); auto& vehicle=scene.cars[0]; vehicle.init(info,0,from,0,0);
            vehicle.setToPhysics(true); vehicle.vel[1]=30*4096;
            for(int frame=0;frame<90 && !scene.propDynamics.looseCount();++frame) {
                scene.propDynamics.checkImpacts(scene); vehicle.integrate(&scene.collision);
            }
            reactions[separate]=scene.propDynamics.looseCount();
        }
        check(!reactions[0] && reactions[1]==1,
              "a wide movable prop must receive its impact instead of stopping the car as an infinite-mass world box first");
    }
};

class FireProbe : public FireEmitter {
public:
    using FireEmitter::FireEmitter;
    int active() const { return alive_; }
    const Particle& first() const { return parts_[0]; }
};

class PaperProbe : public PaperEmitter {
public:
    using PaperEmitter::PaperEmitter;
    const Particle& first() const { return parts_[0]; }
    int active() const { return alive_; }
    bool billboard() const { return billboard_; }
    void terminalFall() { parts_[0].v[2] = -1000; updateParticle(parts_[0]); }
};

class WoodProbe : public WoodEmitter {
public:
    using WoodEmitter::WoodEmitter;
    const Particle& first() const { return parts_[0]; }
    int active() const { return alive_; }
    bool billboard() const { return billboard_; }
    void terminalFall() { parts_[0].v[2]=-0x800; parts_[0].p[2]=-0x6FFF; parts_[0].size=1; updateParticle(parts_[0]); }
};

class GlassProbe : public GlassEmitter {
public:
    using GlassEmitter::GlassEmitter;
    const Particle& first() const { return parts_[0]; }
    int active() const { return alive_; }
    bool billboard() const { return billboard_; }
};

class GarbageProbe : public GarbageEmitter {
public:
    using GarbageEmitter::GarbageEmitter;
    const Particle& first() const { return parts_[0]; }
    int active() const { return alive_; }
};

class SlatsProbe : public SlatsEmitter {
public:
    using SlatsEmitter::SlatsEmitter;
    const Particle& particle(int i) const { return parts_[i]; }
    int active() const { return alive_; }
    int sprite() const { return sprite_; }
    void step(Particle& p) { updateParticle(p); }
    void quad(const Particle& p, float out[4][3]) const { vertices(p,out); }
};

static void slats() {
    const int32_t origin[3] = {}, a[3] = {-4*4096,0,0}, b[3] = {4*4096,0,0}, velocity[3] = {0,4096,0};
    RandInit(0);
    SlatsProbe textured(origin,velocity,a,b,0xFFFFFF,0xFFFFFF,true,true,0x808080);
    check(textured.active() == 6 && textured.sprite() == 24, "textured gate fragments launch six strips during construction");
    for (int i=0;i<6;++i) {
        const Particle& p = textured.particle(i);
        check(p.life == 300 && p.grow == 0 && p.alpha == 31 && p.colour == 0x5AD6 &&
              p.p[0] == ((i & 1) ? 144 : -144) && p.p[1] == 0 && p.p[2] == 72 && p.v[2] == -1 &&
              p.size >= 18 && p.size < 72 && ((i & 1) ? p.spin >= 0x71C : p.spin <= -0x71C) &&
              p.angle == ((i & 1) ? 0x3FFF : 0xC001),
              "gate strips alternate endpoints, retain inverse-range quantisation and spin in opposite directions");
    }
    Particle fall = textured.particle(0);
    const Particle before = fall; textured.step(fall);
    check(fall.p[2] == before.p[2]+before.v[2] && fall.v[2] == -2 &&
          fall.spin == ((before.spin*0xE66) >> 12) && fall.life == 298 &&
          fall.v[0] == ((before.v[0]*0xF33) >> 12),
          "slats advance before damping horizontal motion and accelerating their downward speed");
    fall.p[2] = 0; fall.v[2] = -2; fall.life = 16;
    textured.step(fall);
    check(fall.p[2] == 3 && !fall.v[0] && !fall.v[1] && !fall.v[2] && !fall.spin && fall.alphaStep == -3,
          "slats stop below the original zero-height floor and begin fading in the final fifteen lifetime units");
    textured.step(fall);
    check(fall.p[2] == 3 && fall.alpha == 28, "resting slats stay at the reference offset while fading");
    const int32_t raised[3] = {0,0,10*4096};
    SlatsProbe elevated(raised,velocity,a,b,0xFFFFFF,0xFFFFFF,false,true,0);
    Particle belowOrigin = before; belowOrigin.p[2] = -144; belowOrigin.v[2] = -2;
    elevated.step(belowOrigin);
    check(belowOrigin.v[2] == -3 && belowOrigin.p[2] == -146,
          "slats below an elevated impact point still fall while they remain above the world-zero plane");
    belowOrigin.p[2] = -400; elevated.step(belowOrigin);
    check(belowOrigin.p[2] == 3 && !belowOrigin.v[2], "the slat floor test includes the emitter's world height");
    for (int i=0;i<160;++i) textured.process();
    check(textured.finished(), "six gate fragments expire and allow their emitter to be removed");
    RandInit(0);
    SlatsProbe slow(origin,velocity,a,b,0x4B74FD,0x4B74FD,false,true,0x808080);
    RandInit(0);
    SlatsProbe fast(origin,velocity,a,b,0x4B74FD,0x4B74FD,false,false,0x808080);
    check(slow.sprite() == 0 && slow.particle(0).colour == 0x35F6 && fast.particle(0).v[1] > slow.particle(0).v[1],
          "plain slats keep the warm ambient tint and the constructor distinguishes slow and fast launches");
    const int32_t small[3] = {1,1,0}, large[3] = {4096,4096,0};
    RandInit(0); SlatsProbe smallLaunch(origin,small,a,b,0xFFFFFF,0xFFFFFF,false,false,0);
    RandInit(0); SlatsProbe largeLaunch(origin,large,a,b,0xFFFFFF,0xFFFFFF,false,false,0);
    check(smallLaunch.particle(0).v[0] == largeLaunch.particle(0).v[0] &&
          smallLaunch.particle(0).v[1] == largeLaunch.particle(0).v[1],
          "slat launch normalisation rounds the original float result and is independent of force magnitude");
    const int32_t tiny[3] = {1,1,1};
    SlatsProbe invalid(origin,velocity,tiny,tiny,0xFFFFFF,0xFFFFFF,false,true,0);
    check(invalid.finished(), "degenerate gate endpoints cannot emit uninitialised particle data");
    Particle geometry{}; geometry.p[0]=144; geometry.p[2]=72; geometry.angle=0; geometry.size=64;
    float quad[4][3]; slow.quad(geometry,quad);
    check(std::abs(quad[0][0]-quad[1][0]) < 0.0001f && quad[0][1] > quad[1][1] &&
          quad[3][0]-quad[0][0] > 0.49f && quad[3][0]-quad[0][0] < 0.51f &&
          quad[0][1]-quad[1][1] > 3.5f && std::abs(quad[0][2]-2.f) < 0.05f,
          "slats render as long narrow horizontal strips, using the reconstructed range rather than square debris");
    geometry.angle=0x4000; slow.quad(geometry,quad);
    check(quad[1][0] > quad[0][0] && std::abs(quad[1][1]-quad[0][1]) < 0.0001f && quad[3][1] > quad[0][1],
          "slat geometry rotates both the long and short axes with its particle angle");
}

static void garbage() {
    int32_t origin[3] = {};
    GarbageProbe burst(origin, 0x300, false);
    check(!burst.active(), "garbage waits for its first process before emitting");
    burst.process(12u << 12);
    check(burst.active() == 8 && burst.first().life == 28 && burst.first().spin == 3000 && burst.first().grow == -8,
          "a bin smashed loose bursts eight pieces with the original spin, lifetime and shrink");
    check(burst.first().v[2] >= 0x199 - 0x51 && burst.first().v[2] < 0x199 + 0xA4 - 0x51,
          "garbage launches upwards and then falls with its own gravity");
    for (int i = 0; i < 20; ++i) burst.process(12u << 12);
    check(burst.finished(), "garbage pieces expire after thirty ticks");

    GarbageProbe spill(origin, 0x300, true);
    spill.process(12u << 12);
    check(spill.active() == 4, "a bin knocked over spills four pieces at first");
    spill.follow(origin, 0x4000001); spill.process(12u << 12);
    check(spill.active() == 4, "a rolling bin spills only on every other frame");
    spill.process(12u << 12);
    check(spill.active() == 5, "a bin rolling faster than 2 units/s keeps spilling");
    spill.follow(origin, 0x3FFFFFF); spill.process(12u << 12); spill.process(12u << 12);
    check(spill.active() == 5 && !spill.finished(), "a slow bin stops spilling but stays attached");
    spill.release();
    for (int i = 0; i < 20; ++i) spill.process(12u << 12);
    check(spill.finished(), "a released bin's rubbish expires");

    GarbageProbe day(origin, 0x300, false), night(origin, 0x300, false);
    RandInit(0); day.process(12u << 12);
    RandInit(0); night.process(0);
    check(night.first().colour != day.first().colour, "garbage is darker at night");
}

static void randomStreams() {
    RandInit(0);
    const uint32_t values32[] = {0x269EC3,0x55AE9CB2,0xA0C3B2FD,0xC910A194};
    const uint32_t values16[] = {0x269EC3,0x719F22B2,0xD2F5F4FD,0xE72DA594};
    for (int i=0;i<4;++i) {
        check(Rand32Critical(0) == values32[i] && Rand32NonCritical(0) == values32[i],
              "32-bit random streams follow the Android recurrence, including unbounded draws");
        check(Rand16Critical(0) == values16[i] && Rand16NonCritical(0) == values16[i],
              "16-bit random streams have their own recurrence and unbounded state");
    }
    RandInit(0);
    check(Rand32Critical(7) == 0 && Rand32Critical(1000) == 334 &&
          Rand16Critical(7) == 4 && Rand16Critical(1000) == 135,
          "bounded random draws use multiply-and-shift scaling instead of modulo");
    RandInit(0);
    check(Rand32Critical(0) == values32[0], "random stream reseeding restarts the sequence");
    int32_t origin[3] = {};
    GlassProbe glass(origin); glass.process();
    WoodProbe wood(origin,0,0,0xC00,0xFFFFFF,0xFFFFFF,false,0);
    PaperProbe paper(origin,7,0x1EB,0xFFFFFF,0xFFFFFF,true,0x808080); paper.process();
    check(Rand32Critical(0) == values32[1] && Rand16Critical(0) == values16[0],
          "glass, wood and paper cannot consume traffic or other critical random streams");
}

static void explosionsAndShake() {
    const int32_t origin[3] = {}, still[3] = {};
    RandInit(0); ExplosionDebris debris(origin);
    check(Rand16NonCritical(0) == 0x19D934AA && Rand32NonCritical(0) == 0xA9910645 &&
          Rand16Critical(0) == 0x269EC3 && Rand32Critical(0) == 0x269EC3,
          "debris birth consumes twenty-five 16-bit and ten 32-bit noncritical draws without changing simulation streams");
    ExplosionDebris fuelDebris(origin,2);
    const auto& chip = ParticlesTestAccess::particle(debris);
    check(!ParticlesTestAccess::billboard(debris) && ParticlesTestAccess::active(debris) == 5 && chip.life == 31 &&
          chip.alpha == 31 && chip.v[2] == 2048 && chip.p[2] == 1024 &&
          ParticlesTestAccess::particle(debris,1).p[2] == 256 && ParticlesTestAccess::particle(debris,4).p[2] == 4 &&
          ParticlesTestAccess::particle(fuelDebris).colour == 0x1D8E,
          "debris uses five horizontal fragments, repeated height conversion and type-specific colour");
    Particle falling{}; falling.active=1; falling.life=11; falling.alpha=31; falling.alphaStep=-1;
    falling.p[2]=1024; falling.v[2]=2048; falling.v[0]=-100; falling.size=600; falling.spin=10;
    ParticlesTestAccess::step(debris,falling,0);
    check(falling.p[2] == 2663 && falling.v[2] == 1474 && falling.v[0] == -90 && falling.angle == 10 &&
          falling.life == 9 && falling.alpha == 31, "debris applies gravity before travel and damping without fading or an early lifetime cutoff");
    for (int i=0;i<16;++i) debris.process();
    check(debris.finished(), "visible debris still expires at its original lifetime");
    RandInit(0);
    ExplosionFlash flash(origin,0), petrol(origin,2), white(origin,5,2048);
    const Particle& first = ParticlesTestAccess::particle(flash), &second = ParticlesTestAccess::particle(flash,1);
    FireEmitter fire(origin); SmokeEmitter ordinarySmoke(origin,1);
    check(first.colour == 0x37F && ParticlesTestAccess::additive(flash) && ParticlesTestAccess::additive(petrol) &&
          ParticlesTestAccess::additive(white) && ParticlesTestAccess::additive(fire) &&
          !ParticlesTestAccess::additive(ordinarySmoke) && !ParticlesTestAccess::additive(debris),
          "original flash tint stays yellow while flash/fire use additive blending and smoke/debris use ordinary alpha");
    check(!ParticlesTestAccess::billboard(flash) && first.angle == 0 && second.angle == 0x8000 &&
          first.spin == 2048 && second.spin == -2048 && first.p[2] == 512 && second.p[2] == 512 &&
          first.grow == 5120 && second.grow == 5120 && first.size == 204 && second.size == 204,
          "explosion flash consists of two horizontal expanding sprites with opposite spin and a shared height");
    check(Rand32NonCritical(0) == 0x269EC3, "flash construction does not consume an invented random angle");
    const auto& fuel = ParticlesTestAccess::particle(petrol);
    check(fuel.colour == 0x7C00 && fuel.alpha == 10 && fuel.alphaStep == -1 && fuel.life == 48 &&
          fuel.v[0] == 4096 && fuel.grow == 2560 && ParticlesTestAccess::sprite(white) == 14 &&
          ParticlesTestAccess::particle(white).colour == 0x7FFF && ParticlesTestAccess::particle(white).grow == 2560,
          "flash type controls colour, sprite, opacity, duration, damping and scale");
    flash.process(); petrol.process();
    check(ParticlesTestAccess::particle(flash).grow == 5120 && ParticlesTestAccess::particle(petrol).grow == 2303 &&
          ParticlesTestAccess::particle(flash).angle == 2048 && ParticlesTestAccess::particle(flash,1).angle == 30720,
          "only the petrol flash damps its growth while both flash layers rotate in opposite directions");
    for (int i = 0; i < 12; ++i) flash.process();
    check(flash.finished(), "flash layers fade and release their emitter");

    RandInit(0); ExplosionCloud main(origin,4,0,4096,4096,-4096,8);
    check(ParticlesTestAccess::additive(main), "the original complex explosion cloud belongs to the emissive pass");
    const Particle& big = ParticlesTestAccess::particle(main);
    check(ParticlesTestAccess::active(main) == 1 && big.size == 6144 && big.grow == 0 && big.v[0] == 1024 &&
          big.v[1] == -1024 && big.v[2] == 1024 && big.p[2] == 0 && Rand32NonCritical(0) == 0x269EC3,
          "phase-zero main cloud uses inherited velocity and emits exactly once without random draws");
    RandInit(0); ExplosionCloud burst(origin,0,1,4096,0,0,2);
    const Particle& a = ParticlesTestAccess::particle(burst), &b = ParticlesTestAccess::particle(burst,1);
    check(a.v[0] == -819 && a.v[1] == -271 && a.v[2] == 205 && a.p[2] == 1952 && a.size == 3905 && a.grow == 13 &&
          b.v[0] == -541 && b.v[1] == -470 && b.v[2] == 94 && b.p[2] == 2358 && b.size == 3741 && b.grow == 16 &&
          !a.angle && !b.angle && Rand32NonCritical(0) == 0xA9910645 && Rand32Critical(0) == 0x269EC3,
          "generic cloud retains Android birth values, in-place height conversion and five noncritical draws per particle");
    Particle fade{}; fade.active=1; fade.life=6; fade.alpha=28; fade.alphaStep=-1;
    fade.colour=0x7FFF; fade.v[0]=-100; fade.v[1]=100; fade.v[2]=10;
    ParticlesTestAccess::step(burst,fade,1);
    check(fade.life == 4 && fade.alpha == 28 && fade.colour == 0x7BDE && fade.v[0] == -90 && fade.v[1] == 91 && fade.v[2] == 10,
          "cloud damps signed velocity and darkens RGB independently of the global frame's opacity cadence");
    ParticlesTestAccess::step(burst,fade,2);
    check(fade.life == 6 && fade.alpha == 23, "cloud fades only on even game frames and extends life while visible");
    fade.colour = 1 | (0 << 5) | (2 << 10); fade.life=10;
    ParticlesTestAccess::step(burst,fade,3);
    check(fade.colour == (1 | (1 << 10)), "cloud darkening preserves zero channels and stops each channel at one");

    Particles pool; const int32_t at[3] = {10*4096,0,0};
    auto* cloud = pool.add<ExplosionCloud>(at,4,0,4096,0,0,1);
    for (uint32_t i = 0; i < 17; ++i) pool.update(i,12u << 12,origin);
    check(pool.emitterCount() == 1, "cloud waits until lifetime twenty-four before creating its secondary smoke");
    pool.update(17,12u << 12,origin);
    const Emitter* smoke = ParticlesTestAccess::at(pool,1);
    const Particle& cloudParticle = ParticlesTestAccess::particle(*cloud), &smokeParticle = ParticlesTestAccess::particle(*smoke);
    check(pool.emitterCount() == 2 && dynamic_cast<const SmokeEmitter*>(smoke) && smokeParticle.life == 20 &&
          smokeParticle.alpha == 30 && smokeParticle.p[0] == 2047 && smokeParticle.size == 1228 && smokeParticle.grow == 81 &&
          smokeParticle.p[2] == ((cloudParticle.p[2]*819) >> 12) &&
          smokeParticle.v[2] == ((cloudParticle.v[2]*819) >> 13),
          "cloud hands off a scaled particle to player-relative smoke at lifetime twenty-four without invalidating the update loop");
    const int32_t moved[3] = {4096,0,0}; pool.update(18,12u << 12,moved);
    check(smoke->pos[0] == moved[0] && ParticlesTestAccess::particle(*smoke).alpha == 29 &&
          ParticlesTestAccess::particle(*smoke).p[0] == 1843,
          "secondary smoke follows its player's controller while its world position remains fixed");
    for (uint32_t i = 19; i < 100; ++i) pool.update(i,12u << 12,moved);
    check(!pool.emitterCount(), "cloud and its secondary smoke both expire instead of accumulating");

    RandInit(0); CameraShake shake; int32_t pos[3] = {100,200,300};
    shake.start(8192,2867); shake.apply(pos);
    check(pos[0] == 100-8183 && pos[1] == 200-2709 && pos[2] == 300+5144 &&
          Rand32Critical(0) == 0xC910A194 && Rand32NonCritical(0) == 0x269EC3 && Rand16Critical(0) == 0x269EC3,
          "camera shake samples signed horizontal and upward vertical offsets from the critical 32-bit stream only");
    for (int i = 0; i < 20; ++i) shake.apply(pos);
    check(pos[0] == 100 && pos[1] == 200 && pos[2] == 300, "shake subtracts its prior offset and returns to the unshaken pose at cutoff");
    const int32_t near[3] = {50*4096-1,0,0}, boundary[3] = {50*4096,0,0}, above[3] = {0,0,50*4096};
    check(CameraShake::nearExplosion(origin,near) && !CameraShake::nearExplosion(origin,boundary) &&
          !CameraShake::nearExplosion(origin,above), "blast shake uses a strict fifty-unit three-dimensional distance");
    Game game; game.camera.reset(origin,0); game.camera.shake(8192,2867);
    Game clean; clean.camera.reset(origin,0); RandInit(0);
    game.camera.update(origin,0,still,nullptr); clean.camera.update(origin,0,still,nullptr);
    int32_t shaken[3], plain[3]; game.camera.position(shaken); clean.camera.position(plain);
    check(shaken[0]-plain[0] == -8183 && shaken[1]-plain[1] == -2709 && shaken[2]-plain[2] == 5144,
          "on-foot follow camera applies shake after following and collision recovery");
    game.freeCam.on = true; GameTestAccess::snapshot(game);
    game.camera.update(origin,0,still,nullptr);
    int32_t current[3]; game.camera.shakeOffset(current); float offset[3];
    game.renderAlpha = 0; game.cameraShake(offset);
    check(offset[0] == -8183/4096.f, "custom camera reads the previous tick's shake at interpolation alpha zero");
    game.renderAlpha = 0.5f; game.cameraShake(offset);
    check(offset[0] == (-8183 + (int32_t)std::lround((current[0]+8183)*0.5))/4096.f,
          "custom-camera shake interpolates even while a free camera supplies the view");
    game.renderAlpha = 1; game.cameraShake(offset);
    check(offset[0] == current[0]/4096.f, "custom-camera shake reads the current tick at alpha one");
    game.freeCam.on = false;
    Vehicle car; VehicleInfo info{}; car.init(info,0,origin,0,0);
    game.carCam.setBehind(car); clean.carCam.setBehind(car); game.playerCar=0; game.cars.push_back(car);
    game.shakeNearExplosion(near); RandInit(0); game.carCam.update(car,nullptr); clean.carCam.update(car,nullptr);
    game.carCam.position(shaken); clean.carCam.position(plain);
    check(shaken[0]-plain[0] == -8183 && shaken[1]-plain[1] == -2709 && shaken[2]-plain[2] == 5144,
          "nearby explosion selects the active driving camera and shakes its completed follow pose");
    clean.carCam.inherit(plain,0,0xD4E4); game.carCam.inherit(plain,0,0xD4E4);
    game.carCam.update(car,nullptr); clean.carCam.update(car,nullptr);
    game.carCam.position(shaken); clean.carCam.position(plain);
    check(std::equal(shaken,shaken+3,plain), "camera handoff clears old shake offsets");
    const int32_t eye[3] = {};
    game.playerCar=-1; game.camera.inherit(eye,0,0);
    const int32_t close[3]={15*4096,0,0}, far[3]={50*4096,0,0};
    check(GameTestAccess::cloudCount(game,close) == 1 && GameTestAccess::cloudCount(game,far) == 8,
          "generic explosion cloud count follows the original one-to-eight camera-distance rule");
    Game exploding; exploding.cars.emplace_back(); exploding.cars.back().justDied = true;
    ParticlesTestAccess::clear();   // only one explosion-debris emitter may exist at a time
    const size_t before = ParticlesTestAccess::count(); RandInit(0);
    GameTestAccess::processExplosions(exploding); GameTestAccess::processExplosions(exploding);
    check(ParticlesTestAccess::count() == before && Rand32Critical(0) == 0x269EC3,
          "type-four generic explosion waits two Process calls without an invented random delay or early flash");
    GameTestAccess::processExplosions(exploding);
    check(ParticlesTestAccess::count() == before+3 && GameTestAccess::explosions(exploding) == 1,
          "type-four flash, debris and cloud start together on the third Process call");
    for (int i=0;i<11;++i) GameTestAccess::processExplosions(exploding);
    check(!GameTestAccess::explosions(exploding), "type-four generic explosion retains its eleven-call final state");

    // cVehicle::Damage: vehicle 7 blows up as a type-6 cExplosionBigVehicle
    Game bus; bus.cars.emplace_back(); bus.cars.back().infoId = 7; bus.cars.back().justDied = true;
    Pedestrians::Ped bystander; bystander.uid = 3; bystander.body.playerControlled = false;
    bystander.body.pos[0] = 12 * 4096; bystander.body.health = 127;
    bus.peds.peds.push_back(bystander);
    bus.player.pos[0] = 100 * 4096;
    ParticlesTestAccess::clear();
    GameTestAccess::processExplosions(bus);
    check(GameTestAccess::explosions(bus) == 1 && GameTestAccess::explosionType(bus) == 6 &&
          GameTestAccess::explosionState(bus) == 1 && ParticlesTestAccess::count() == 2 &&
          dynamic_cast<const ExplosionFlash*>(ParticlesTestAccess::at(0)) &&
          dynamic_cast<const ExplosionTwirls*>(ParticlesTestAccess::at(1)),
          "a big vehicle's blast flashes, spawns its twirls and hits at once on its first Process");
    check(bus.peds.peds[0].body.health < 127 && bus.peds.peds[0].deadFrames == 0,
          "the big-vehicle blast's fifteen-unit damage radius reaches a pedestrian twelve units away");
    int waited = 0;
    while (GameTestAccess::explosionState(bus) == 1 && waited < 20) { GameTestAccess::processExplosions(bus); ++waited; }
    check(waited >= 5 && waited <= 10 && ParticlesTestAccess::count() == 5,
          "the big-vehicle clouds and debris follow five to ten Process calls later");
    const auto* ring = dynamic_cast<const ExplosionCloud*>(ParticlesTestAccess::at(3));
    check(ring && ParticlesTestAccess::particle(*ring).v[2] == (int16_t)((0xA66 * 0x400) >> 12),
          "the big-vehicle ring rises at the reference 0.65 units a frame");
    for (int i = 0; i < 11 && GameTestAccess::explosions(bus); ++i) GameTestAccess::processExplosions(bus);
    check(!GameTestAccess::explosions(bus), "the big-vehicle explosion destroys itself after its final wait");
}

static void fireAndCamera() {
    int32_t origin[3] = {0,0,0};
    GlassProbe glass(origin);
    check(!glass.active(), "glass waits for its first process before emitting");
    Emitter* glassBase = &glass; glassBase->process();
    const Particle firstGlass = glass.first();
    check(glass.active() == 2 && !glass.billboard() && firstGlass.colour == 0x7F55 && firstGlass.alpha == 22 &&
          firstGlass.life == 58 && firstGlass.size == 0x4CC-40 && firstGlass.spin >= 2000 && firstGlass.spin < 6000,
          "polymorphic glass processing emits and advances two horizontal daylight shards on the same frame");
    check(firstGlass.p[2] == 0x999+firstGlass.v[2]+40 && firstGlass.v[2] >= 123 && firstGlass.v[2] < 205,
          "glass first moves at its launch speed and then applies its own gravity");
    glass.process(0);
    check(glass.active() == 2 && glass.first().life == 56 && glass.first().colour == firstGlass.colour &&
          glass.first().v[2] == firstGlass.v[2]-40,
          "glass emits only once and keeps its sampled colour when the clock changes");
    for (int i=0;i<29;++i) glass.process();
    check(glass.finished(), "glass shards expire at their sixty-tick lifetime");
    GlassProbe night(origin); night.process(0);
    check(night.first().colour == 0x3148, "night glass uses the independent forty-percent brightness curve");
    GlassProbe dawn(origin); dawn.process(7u << 12);
    GlassProbe dusk(origin); dusk.process(20u << 12);
    check(dawn.first().colour == dusk.first().colour && dawn.first().colour != firstGlass.colour &&
          dawn.first().colour != night.first().colour, "dawn and dusk interpolate the original glass colour");
    WoodProbe tiny(origin,0,0,0xC00,0xFFFFFF,0xFFFFFF,false,0);
    check(tiny.active() == 3 && !tiny.billboard() && tiny.first().colour == 0x7FFF && tiny.first().size == 0x4CC && tiny.first().grow == -40,
          "wood fragment count scales with collision radius and retains untinted colour and shrinking size");
    WoodProbe wood(origin,292,-292,0xA000,0xDEDEDE,0xDEDEDE,true,0x808080);
    check(wood.active() == 12 && wood.first().spin == 3000 && wood.first().life == 120 && wood.first().colour == 0x5294,
          "large moving furniture uses the original twelve-fragment cap, spin, lifetime and ambient tint");
    const Particle firstWood = wood.first(); wood.process();
    check(wood.first().p[0] == firstWood.p[0]+firstWood.v[0] && wood.first().v[0] == ((firstWood.v[0]*0xF33) >> 12) &&
          wood.first().v[2] == firstWood.v[2]-40,
          "moving wood first advances, then applies horizontal damping and gravity");
    const Particle stationary = tiny.first(); tiny.process();
    check(tiny.first().v[0] == stationary.v[0], "stationary-origin wood does not gain moving-furniture damping");
    wood.terminalFall();
    check(wood.first().v[2] == -0x800 && wood.first().p[2] == -0x7000 && wood.first().size == 0,
          "wood preserves the original terminal speed, fall bound and nonnegative size");
    for (int i=0;i<61;++i) wood.process();
    check(wood.finished(), "wood burst expires instead of accumulating persistent emitters");
    PaperProbe paper(origin,7,0x1EB,0xDEDEDE,0xDEDEDE,true,0x808080);
    check(!paper.active(), "paper waits until its first process, preserving random-stream call order");
    paper.process();
    check(paper.active() == 7 && !paper.billboard() && paper.first().size == 0x1EB && paper.first().life == 118 &&
          paper.first().spin == 2000 && paper.first().alpha == 31 && paper.first().colour == 0x5294,
          "paper burst keeps the original count, size, lifetime, spin and ambient tint");
    const Particle firstPaper = paper.first(); paper.process();
    check(paper.first().p[2] == firstPaper.p[2]+firstPaper.v[2] && paper.first().v[2] == firstPaper.v[2]-0x51,
          "paper first moves with its existing velocity, then accelerates downwards");
    paper.terminalFall();
    check(paper.first().v[2] == -0x199, "paper fall speed is capped at the original terminal velocity");
    for(int i=0;i<61;++i) paper.process();
    check(paper.finished(), "one-shot debris expires and can be removed from the particle system");
    PaperProbe plainPaper(origin,7,0x2E1,0xDEDEDE,0xDEDEDE,false,0x808080);
    plainPaper.process();
    check(plainPaper.first().alpha == 15 && plainPaper.first().size == 0x2E1,
          "newspaper fragments preserve the untextured variant and reduced opacity");
    FireProbe fire(origin); fire.addParticle();
    check(fire.active() == 1 && fire.first().life == 31 && fire.first().alpha == 22, "fire emits the original short-lived flame template");
    int initialZ = fire.first().p[2], size = fire.first().size;
    fire.process();
    check(fire.first().p[2] > initialZ && fire.first().size < size && fire.first().alpha == 21,
          "flames rise, shrink and fade");
    for (int i = 0; i < 50; ++i) fire.process();
    check(fire.active() == 0, "flame particles expire");
    Vehicle car;
    VehicleInfo info{};
    car.init(info,0,origin,0,0); car.damage(235); car.processDamage(1);
    check(VehicleTestAccess::fire(car), "burning vehicles create the missing fire emitter");
    car.releaseEffects();
    check(!VehicleTestAccess::fire(car), "vehicle removal releases its flame emitter");
    Game game; game.freeCam.on = true; game.freeCam.cam.eye[2] = 400; game.freeCam.cam.zFar = 150;
    check(!game.showDebug, "prototype HUD and collision diagnostics are off by default");
    check(game.key(SDL_SCANCODE_F3) && game.showDebug, "F3 enables all debug drawing");
    check(game.key(SDL_SCANCODE_F3) && !game.showDebug, "F3 disables all debug drawing again");
    // PC bindings for the original pause overlay: gameplay keys stay consumed while paused.
    check(game.key(SDL_SCANCODE_ESCAPE) && game.pauseOpen && game.uiPaused && !game.quitRequested,
          "Esc opens the pause menu and pauses the simulation");
    check(game.key(SDL_SCANCODE_W) && game.pauseOpen, "paused gameplay input is consumed");
    check(game.key(SDL_SCANCODE_ESCAPE) && !game.pauseOpen && !game.uiPaused, "Esc resumes");
    game.key(SDL_SCANCODE_ESCAPE);
    check(game.key(SDL_SCANCODE_RETURN) && !game.pauseOpen && !game.quitRequested,
          "the confirm binding resumes the original overlay");
    game.setPause(false); game.quitRequested = false;
    WorldCamera cam; game.setRenderDistance(720); game.viewCamera(cam);
    check(cam.zFar > 720 && cam.zFar > game.freeCam.cam.zFar, "freecam far plane includes the requested radius and camera altitude");
    game.setRenderDistance(120); game.viewCamera(cam);
    check(cam.zFar > 400 && cam.zFar < 720, "reducing distance updates the actual camera without mutating the saved lens");

    Game entry;
    entry.camera.reset(origin, 0);
    entry.cars.emplace_back(); entry.cars[0].init(info,0,origin,0,0);
    entry.task.op = Game::CarTask::SlideIn; entry.task.car = 0;
    entry.player.attached = true;
    entry.scriptedInput = true; entry.scriptedMove[0] = 1;
    int32_t beforeView[3]; entry.camera.position(beforeView);
    entry.tick(); // no sprite data: synthetic one-shot completes immediately
    int32_t afterView[3], outgoingView[3]; entry.carCam.position(afterView); entry.camera.position(outgoingView);
    check(entry.playerCar == 0 && entry.player.hidden && entry.player.level() == 0 &&
          std::equal(entry.player.pos, entry.player.pos + 3, entry.cars[0].pos),
          "taking the seat cannot run an extra walking update or move the hidden player outside the vehicle");
    check(std::equal(beforeView,beforeView+3,outgoingView) && !std::equal(beforeView,beforeView+3,afterView),
          "entry updates the inherited car camera immediately and leaves the outgoing camera alone");

    {   // a controller (padinput.h): the keyboard wins, the pad fills in
        HostPad pad; pad.connected = true; pad.lx = 0.3f; pad.ly = 0.1f; pad.rt = 0.75f; pad.a = true;
        float mx = 0, my = 0; bool walk = false, sprint = false, fire = false, throwing = false, horn = false;
        PadInput_OnFoot(pad, mx, my, walk, sprint);
        check(mx == 0.3f && my == 0.1f && walk && sprint, "the left stick moves, a light push walks, A sprints");
        PadInput_Weapons(pad, fire, throwing);
        check(fire && !throwing, "RT fires, LT throws");
        Vehicle::Controls dc;
        PadInput_Driving(pad, dc, horn);
        check(dc.throttle == 0xC00 && dc.steer == (int32_t)(0.3f * 0x1000) && dc.handbrake && !horn,
              "RT accelerates in proportion, the stick steers, A is the handbrake");
        pad.rt = 0; pad.lt = 1; dc = Vehicle::Controls{};
        PadInput_Driving(pad, dc, horn);
        check(dc.throttle == -0x1000, "LT brakes and reverses");
        dc = Vehicle::Controls{}; dc.throttle = 0x1000; dc.steer = -0x1000;
        PadInput_Driving(pad, dc, horn);
        check(dc.throttle == 0x1000 && dc.steer == -0x1000, "the keyboard wins over the controller");
        HostPad none;
        float x = 0, y = 0; bool w = false, s = false;
        PadInput_OnFoot(none, x, y, w, s);
        check(x == 0 && !s, "no controller changes nothing");
    }

    Game wreck;
    std::copy(origin,origin+3,wreck.player.pos);
    wreck.cars.resize(2);
    for (auto& car : wreck.cars) car.init(info,0,origin,0,0);
    wreck.cars[0].damage(255);
    wreck.startEnter(0);
    check(wreck.task.op == Game::CarTask::None, "explicit entry cannot target a burnt wreck");
    wreck.startEnter();
    check(wreck.task.car == 1, "nearest-car entry skips the burnt wreck and finds an intact car");
    wreck.task.op = Game::CarTask::SlideIn; wreck.player.attached = true;
    wreck.cars[1].damage(255);
    check(!wreck.tickTask(false,0) && wreck.task.op == Game::CarTask::None && !wreck.player.attached && wreck.playerCar < 0,
          "a car destroyed during entry cancels the animation before taking the seat");
    wreck.player.dead = true;
    wreck.startEnter(1);
    check(wreck.task.op == Game::CarTask::None, "a dead player cannot start vehicle entry");

    Game occupant;
    occupant.cars.emplace_back(); occupant.cars[0].init(info,0,origin,0,0);
    occupant.cars[0].uid = 77; occupant.cars[0].seatUser[0] = -2;
    occupant.playerCar = 0; occupant.player.hidden = occupant.player.attached = true;
    occupant.carCam.setBehind(occupant.cars[0]);
    occupant.cars[0].damage(255);
    GameTestAccess::processExplosions(occupant);
    check(occupant.player.dead && occupant.player.health == 0 && !occupant.player.hidden && !occupant.player.attached &&
          occupant.playerCar == -1 && occupant.cars[0].seatUser[0] == -1,
          "a destroyed occupied car kills the player with zero health and clears its driver rather than ejecting a living player");
    for (auto& car : wreck.cars) car.releaseEffects();
    occupant.cars[0].releaseEffects();
}

struct RadioTestAccess {
    static void run() {
        Radio favourites;
        favourites.stations_.resize(7);
        for (int i = 0; i < 7; ++i) {
            favourites.stations_[i].stream = i;
            favourites.appOrder_.push_back(i);
        }
        favourites.stations_[0].listened = 5;
        check(favourites.favoriteStars(0) == 1 && favourites.favoriteStars(1) == 0,
              "zero-time stations are excluded before ranking the first listened station");
        favourites.stations_[1].listened = 1; favourites.stations_[3].listened = 2;
        favourites.stations_[4].listened = 3; favourites.stations_[5].listened = 4;
        favourites.stations_[6].stream = -1; favourites.stations_[6].listened = 100;
        check(favourites.favoriteStars(1) == 1 && favourites.favoriteStars(3) == 2 &&
              favourites.favoriteStars(0) == 5 && favourites.favoriteStars(6) == 0,
              "listening rank awards one through five stars and never ranks the off station");
        favourites.stations_[2].listened = 10;
        check(favourites.favoriteStars(2) == 5, "sixth and higher listening ranks remain capped at five stars");
        favourites.stations_[3].listened = 1;
        check(favourites.favoriteStars(1) == 1 && favourites.favoriteStars(3) == 2,
              "equal listening times keep their original station order");
        favourites.volumeShownAt_ = 10;
        check(std::fabs(favourites.volumeAlpha(10)-0.4f) < 0.001f &&
              std::fabs(favourites.volumeAlpha(10.11)-0.7f) < 0.001f && favourites.volumeAlpha(11) == 1,
              "the volume meter brightens from forty percent in original thirty-Hz steps");
        check(favourites.volumeAlpha(13) == 1 && std::fabs(favourites.volumeAlpha(13.44)-0.7f) < 0.001f &&
              std::fabs(favourites.volumeAlpha(20)-0.4f) < 0.001f,
              "the volume meter holds for one hundred frames and returns to forty percent without disappearing");
        const auto dir = std::filesystem::temp_directory_path() / ("ctw-radio-audio-test-" + std::to_string(SDL_GetTicks64()));
        std::filesystem::create_directories(dir);
        OS_SetDocumentsRoot(dir.string().c_str());
        // Synthetic silent MPEG-1 Layer III frames, not an extracted game recording.
        std::vector<unsigned char> mp3Frame(417, 0);
        mp3Frame[0] = 0xFF; mp3Frame[1] = 0xFB; mp3Frame[2] = 0x90; mp3Frame[3] = 0xC0;
        const auto path = dir / "station.mp3";
        {
            std::ofstream file(path, std::ios::binary);
            for (int i = 0; i < 100; ++i) file.write((const char*)mp3Frame.data(), mp3Frame.size());
        }
        AudioTrackInfo info;
        check(Audio_Probe(path.string(), &info) && info.seconds > 2, "synthetic radio broadcast decodes");
        Game game;
        Radio& radio = game.radio;
        radio.stations_.resize(3);
        radio.stations_[0].available = true; radio.stations_[0].stream = 0;
        radio.stations_[0].path = path.string();
        radio.stations_[1].available = false; radio.stations_[1].stream = 1;
        radio.stations_[2].available = true; radio.stations_[2].stream = -1;
        check(!radio.key(SDL_SCANCODE_R,game) && !radio.appOpen(), "walking player cannot open the radio");
        check(!radio.key(SDL_SCANCODE_RIGHTBRACKET,game), "walking player cannot tune the radio");
        check(!radio.key(SDL_SCANCODE_EQUALS,game) && !radio.key(SDL_SCANCODE_MINUS,game),
              "no shortcut changes the radio outside the Tab app");
        radio.select(2,game); radio.cycle(1,game);
        check(radio.station() == 0, "radio station selection wraps");
        radio.cycle(1,game);
        check(radio.station() == 2, "radio skips unavailable streams and retains the off station");
        radio.volume(-100); check(radio.volume_ == 0, "radio can be muted");
        radio.volume(100); check(radio.volume_ == 10, "radio volume clamps at ten levels");
        game.cars.resize(2); game.cars[0].uid = 1; game.cars[1].uid = 2; game.playerCar = 0;
        radio.select(0,game); game.playerCar = 1; radio.select(2,game); game.playerCar = 0;
        radio.update(game);
        check(radio.station() == 0 && game.cars[1].radioStation == 2, "station selections stay with individual cars");
        check(radio.key(SDL_SCANCODE_TAB,game) && radio.appOpen(), "Tab opens the radio app");
        uint32_t frame = game.frame; game.tick();
        check(game.frame == frame+1, "the paused radio app keeps the simulation ticking underneath");
        double before = Audio_MusicPosition();
        radio.key(SDL_SCANCODE_SPACE,game); radio.key(SDL_SCANCODE_RETURN,game);
        for (int i = 0; i < 50 && Audio_MusicPosition() <= before; ++i) SDL_Delay(20);
        check(Audio_MusicPlaying() && Audio_MusicPosition() > before,
              "Space and Enter cannot pause the broadcast while the radio app is open");
        radio.countListening();
        double listened = radio.stations_[0].listened;
        SDL_Delay(80); radio.countListening();
        check(radio.stations_[0].listened >= listened + 0.06,
              "listening statistics advance while the radio app pauses simulation");
        listened = radio.stations_[0].listened;
        for (int i = 0; i < 100; ++i) radio.update(game);
        check(radio.stations_[0].listened < listened + 0.5,
              "listening time measures playback time rather than counting simulation ticks");
        Audio_SetMusicPaused(true); radio.countListening(); listened = radio.stations_[0].listened;
        SDL_Delay(60); radio.countListening();
        check(radio.stations_[0].listened == listened, "paused audio does not earn listening time");
        Audio_SetMusicPaused(false);
        radio.appPointer(240,125,true,true,game); radio.appPointer(236,125,true,false,game);
        check(radio.station() == 0, "a radio drag below five logical pixels does not tune");
        radio.appPointer(235,125,true,false,game);
        check(radio.station() == 2, "a left swipe tunes forward at the original five-pixel threshold");
        radio.appPointer(200,125,true,false,game); radio.appPointer(200,125,false,false,game);
        check(radio.station() == 2, "holding and releasing a swipe does not tune twice");
        radio.appPointer(240,125,false,true,game);
        check(radio.station() == 2, "clicking the centre station cannot pause or retune the radio");
        radio.appPointer(240,125,true,true,game); radio.appPointer(245,125,true,false,game);
        radio.appPointer(245,125,false,false,game);
        check(radio.station() == 0, "a right swipe tunes backwards past unavailable stations");
        radio.appPointer(-20,125,true,true,game); radio.appPointer(10,125,true,false,game);
        radio.appPointer(10,125,false,false,game);
        check(radio.station() == 0, "a drag starting outside the app cannot tune the radio");
        radio.appPointer(240,260,true,true,game); radio.appPointer(260,260,true,false,game);
        radio.appPointer(260,260,false,false,game);
        check(radio.station() == 0, "a drag below the station region cannot tune the radio");
        radio.appStation(1,game);
        check(radio.station() == 2 && radio.playing_ == 0 && Audio_MusicPlaying(),
              "the previous broadcast keeps playing while its replacement logo scrolls into place");
        radio.update(game); radio.advanceCarousel(game);
        check(radio.playing_ == 0 && radio.appTuning_, "simulation updates cannot switch a pending carousel station early");
        for (int i = 0; i < 40; ++i) radio.advanceCarousel(game);
        check(radio.playing_ == -1 && !radio.appTuning_, "the off station stops the broadcast after its carousel settles");
        radio.appStation(-1,game); radio.advanceCarousel(game); radio.appStation(1,game);
        for (int i = 0; i < 40; ++i) radio.advanceCarousel(game);
        check(radio.station() == 2 && radio.playing_ == -1 && !radio.appTuning_,
              "reversing a swipe before it settles cannot briefly play the skipped station");
        radio.appStation(-1,game);
        check(radio.tune(0,game) && radio.playing_ == 0 && !radio.appTuning_,
              "explicit radio API tuning remains immediate while the app is open");
        radio.appPointer(180,290,false,true,game);
        check(radio.volumeLevel() == 9, "the radio minus sprite lowers volume by one step");
        radio.appPointer(290,290,false,true,game);
        check(radio.volumeLevel() == 10, "the radio plus sprite raises volume by one step");
        radio.appPointer(180,290,true,true,game); radio.appPointer(180,290,false,true,game);
        radio.appPointer(180,290,false,false,game);
        check(radio.volumeLevel() == 9, "a completed click clears prior pointer state instead of repeating on the next frame");
        check(radio.key(SDL_SCANCODE_TAB,game) && !radio.appOpen(), "Tab closes the app and resumes the game");
        radio.key(SDL_SCANCODE_TAB,game); radio.appStation(1,game); radio.key(SDL_SCANCODE_TAB,game);
        check(radio.playing_ == -1 && !radio.appTuning_, "closing the app commits its pending station before resuming gameplay");
        radio.key(SDL_SCANCODE_TAB,game); game.playerCar = -1; radio.update(game);
        check(!radio.appOpen() && radio.playing_ == -1, "leaving a car closes the app and stops music");
        game.playerCar = 0; game.player.dead = true;
        check(!radio.key(SDL_SCANCODE_TAB,game) && !radio.appOpen(), "dead player cannot open the radio");
        game.player.dead = false; VehicleTestAccess::bike(game.cars[0], true);
        check(!radio.key(SDL_SCANCODE_TAB,game), "bike rider cannot open the car radio");
        VehicleTestAccess::bike(game.cars[0], false);
        game.cars[0].infoId = 32;
        check(radio.key(SDL_SCANCODE_TAB,game) && radio.appOpen(), "vehicle model IDs are not mistaken for radio availability");
        radio.dir_ = dir.string(); radio.tables_.streams.resize(25);
        radio.tables_.streams[1] = radio.tables_.streams[3] = "station.mp3";
        game.playerCar = 100; radio.update(game);
        check(!radio.appOpen() && radio.ambience_ >= 0,
              "removed vehicle closes the app and restores ambience without indexing the removed car");
        game.playerCar = 0; radio.key(SDL_SCANCODE_TAB,game);
        game.cars[0].act(Vehicle::Controls{},false,nullptr); game.cars[0].damage(255); radio.update(game);
        check(!radio.appOpen() && radio.playing_ == -1, "destroyed vehicle closes the radio and stops music");
        radio.shutdown();
        std::filesystem::remove_all(dir);
    }
};

static void playerFrameIntegration() {
    // Exercise the complete frame: a second Integrate after the convenience update
    // doubles distance even though CurrentSpeed and the acceleration tests still pass.
    for (int gait : {1, 2, 3}) {
        Game game;
        game.scriptedInput = true;
        game.scriptedMove[1] = 1;
        game.scriptedState = gait;
        game.clockRunning = false;
        game.peds.densityScale = game.traffic.densityScale = 0;
        game.camera.reset(game.player.pos, 0);
        Player reference = game.player;
        Player::Input input;
        input.moving = true;
        input.sprint = gait == 3;
        input.maxLevel = gait == 1 ? 1 : 3;
        bool matches = true;
        for (int frame = 0; frame < 60; ++frame) {
            reference.update(input, nullptr, nullptr);
            game.tick();
            matches = matches && std::equal(reference.pos, reference.pos + 3, game.player.pos);
        }
        check(matches, "a complete walking/running/sprinting game frame integrates the player exactly once");
    }
}

int main() {
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    check(Audio_Init() && Audio_Init(), "audio initialization is idempotent");
    std::vector<unsigned char> source(4096, 140);
    int voice = Audio_SfxPlay(source.data(), (unsigned)source.size(), 22050, 1, 0, true);
    source.clear(); source.shrink_to_fit(); SDL_Delay(60);
    check(voice && Audio_SfxPlaying(voice), "sound voices survive source-bank memory replacement");
    Audio_SfxStop(voice);
    check(!Audio_SfxPlaying(voice), "stopping a voice removes playback");
    Audio_SetSfxPaused(true);
    source.assign(2205, 140);
    voice = Audio_SfxPlay(source.data(), (unsigned)source.size(), 22050, 1, 0, false);
    const int hudVoice = Audio_HudSfxPlay(source.data(), (unsigned)source.size(), 22050, 1);
    SDL_Delay(180);
    check(voice && Audio_SfxPlaying(voice), "paused sound effects do not advance or finish while browsing radio");
    check(hudVoice && !Audio_SfxPlaying(hudVoice), "HUD clicks play and finish while world sound effects remain paused");
    Audio_SetSfxPaused(false);
    for (int i = 0; i < 50 && Audio_SfxPlaying(voice); ++i) SDL_Delay(20);
    check(!Audio_SfxPlaying(voice), "resumed sound effect finishes normally");
    playerFrameIntegration();
    PlayerTestAccess::fightIdle(); SoundTestAccess::run(); CollisionTestAccess::run(); CollisionTestAccess::cameraTests(); PropTestAccess::run(); PropDynamicsTestAccess::run();
    slats(); garbage(); VehicleTestAccess::flaps(); VehicleTestAccess::burnouts(); VehicleTestAccess::knockOff(); VehicleTestAccess::debris(); WaterTestAccess::run(); randomStreams(); explosionsAndShake(); fireAndCamera(); RadioTestAccess::run();
    Audio_Shutdown();
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
