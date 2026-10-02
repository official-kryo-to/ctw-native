// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#define SDL_MAIN_HANDLED
#include "game.h"
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
};

struct PlayerTestAccess {
    static void gait(Player& p, int frame, int level = 2, bool grounded = true) {
        p.frameUpper_ = frame << 8; p.level_ = (uint8_t)level; p.onGround_ = grounded;
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
        cell->propShapes.push_back(shapes);
        g.collision.cells_[2020] = std::move(cell);
    }
    static bool solid(const Game& g) { return g.collision.cells_.at(2020)->propShapes[0].solid; }
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

static void movingCars() {
    Player player; player.pos[0] = player.pos[1] = player.pos[2] = 0;
    int32_t previous[3] = {0,0,0};
    Collision::Box before{-5*4096,0,4096,4096,8192,4096,0,0}, after = before; after.cx = 5*4096;
    player.collideMovingCar(previous, before, after);
    check(player.pos[0] >= after.cx + after.hx + Player::kSphere, "fast moving car cannot pass through a stationary pedestrian");
    player.pos[0] = player.pos[1] = 0; player.pos[2] = 10*4096; previous[2] = player.pos[2];
    player.collideMovingCar(previous, before, after);
    check(player.pos[0] == 0, "cars do not collide with a pedestrian on an overhead level");
    player.pos[2] = previous[2] = 0; player.attached = true;
    player.collideMovingCar(previous, before, after);
    check(player.pos[0] == 0, "attached pedestrian is not pushed out during entry animations");
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
}

struct RadioTestAccess {
    static void run() {
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
        check(radio.key(SDL_SCANCODE_TAB,game) && !radio.appOpen(), "Tab closes the app and resumes the game");
        radio.key(SDL_SCANCODE_TAB,game); game.playerCar = -1; radio.update(game);
        check(!radio.appOpen() && radio.playing_ == -1, "leaving a car closes the app and stops music");
        game.playerCar = 0; game.player.dead = true;
        check(!radio.key(SDL_SCANCODE_TAB,game) && !radio.appOpen(), "dead player cannot open the radio");
        game.player.dead = false; VehicleTestAccess::bike(game.cars[0], true);
        check(!radio.key(SDL_SCANCODE_TAB,game), "bike rider cannot open the car radio");
        VehicleTestAccess::bike(game.cars[0], false);
        game.cars[0].infoId = 32;
        check(radio.key(SDL_SCANCODE_TAB,game) && radio.appOpen(), "vehicle model IDs are not mistaken for radio availability");
        game.playerCar = 100; radio.update(game);
        check(!radio.appOpen(), "removed vehicle cannot leave the radio app open");
        game.playerCar = 0; radio.key(SDL_SCANCODE_TAB,game);
        game.cars[0].act(Vehicle::Controls{},false,nullptr); game.cars[0].damage(255); radio.update(game);
        check(!radio.appOpen() && radio.playing_ == -1, "destroyed vehicle closes the radio and stops music");
        radio.shutdown();
        std::filesystem::remove_all(dir);
    }
};

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
    SDL_Delay(180);
    check(voice && Audio_SfxPlaying(voice), "paused sound effects do not advance or finish while browsing radio");
    Audio_SetSfxPaused(false);
    for (int i = 0; i < 50 && Audio_SfxPlaying(voice); ++i) SDL_Delay(20);
    check(!Audio_SfxPlaying(voice), "resumed sound effect finishes normally");
    SoundTestAccess::run(); CollisionTestAccess::run(); CollisionTestAccess::cameraTests(); PropTestAccess::run(); PropDynamicsTestAccess::run();
    movingCars();
    garbage(); randomStreams(); fireAndCamera(); RadioTestAccess::run();
    Audio_Shutdown();
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
