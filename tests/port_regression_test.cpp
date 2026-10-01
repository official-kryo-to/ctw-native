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
    }
};

struct CollisionTestAccess {
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
    }
};

class FireProbe : public FireEmitter {
public:
    using FireEmitter::FireEmitter;
    int active() const { return alive_; }
    const Particle& first() const { return parts_[0]; }
};

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

static void fireAndCamera() {
    int32_t origin[3] = {0,0,0};
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
        check(!radio.key(SDL_SCANCODE_R,game) && !radio.open(), "walking player cannot open the radio");
        check(!radio.key(SDL_SCANCODE_RIGHTBRACKET,game), "walking player cannot tune the radio");
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
        radio.key(SDL_SCANCODE_R,game); uint32_t frame = game.frame; game.tick();
        check(radio.open() && game.frame == frame, "radio app suspends gameplay while previewing stations");
        double before = Audio_MusicPosition();
        radio.key(SDL_SCANCODE_SPACE,game); radio.key(SDL_SCANCODE_RETURN,game);
        for (int i = 0; i < 50 && Audio_MusicPosition() <= before; ++i) SDL_Delay(20);
        check(Audio_MusicPlaying() && Audio_MusicPosition() > before,
              "Space and Enter cannot pause the broadcast while the radio menu suspends the world");
        check(radio.key(SDL_SCANCODE_ESCAPE,game) && !radio.open(), "Escape closes the radio without quitting");
        radio.key(SDL_SCANCODE_R,game); game.playerCar = -1; radio.update(game);
        check(!radio.open() && radio.playing_ == -1, "leaving a car closes the selector and stops music");
        game.playerCar = 0; game.player.dead = true;
        check(!radio.key(SDL_SCANCODE_R,game) && !radio.open(), "dead player cannot open the radio");
        game.player.dead = false; VehicleTestAccess::bike(game.cars[0], true);
        check(!radio.key(SDL_SCANCODE_R,game), "bike rider cannot open the car radio");
        VehicleTestAccess::bike(game.cars[0], false);
        game.cars[0].infoId = 32;
        check(radio.key(SDL_SCANCODE_R,game) && radio.open(), "vehicle model IDs are not mistaken for radio availability");
        game.playerCar = 100; radio.update(game);
        check(!radio.open(), "removed vehicle cannot leave the radio open");
        game.playerCar = 0; radio.key(SDL_SCANCODE_R,game); game.cars[0].damage(255); radio.update(game);
        check(!radio.open() && radio.playing_ == -1, "destroyed vehicle closes the radio and stops music");
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
    SoundTestAccess::run(); CollisionTestAccess::run(); PropTestAccess::run();
    movingCars(); fireAndCamera(); RadioTestAccess::run();
    Audio_Shutdown();
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
