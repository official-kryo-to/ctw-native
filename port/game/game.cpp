// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "game.h"
#include "hud.h"
#include "particles.h"
#include "sound.h"
#include "skidmarks.h"
#include "plugins.h"
#include "os/os.h"
#include "os/pak.h"
#include "gfx/assets.h"
#include <SDL.h>
#include <glad/gl.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

static Game g_game;
Game& TheGame() { return g_game; }

bool Game::init(const std::string& data, const std::string& mods) {
    dataDir = data;
    modsDir = mods;
    if (!LoadGameplayTables(dataDir)) {
        fprintf(stderr, "gameplay_tables.bin missing or invalid: run scripts/setup_game.py\n");
        return false;
    }
    if (!world.init(dataDir)) return false;
    world.setRadius(1);   // the game keeps a 3 x 3 sector window; one block ring around the player covers it
    Hud_Init(dataDir);
    if (props.load()) collision.setPropLibrary(&props);   // before any collision cell loads
    else fprintf(stderr, "prop definitions not found: no street furniture\n");
    if (!collision.init(dataDir)) fprintf(stderr, "world.bin not found: no collision\n");
    player.placeOnGround(&collision);
    if (!pedSprites.init()) fprintf(stderr, "ped sprites not found\n");
    camera.reset(player.pos, player.heading());
    float p[3];
    player.posf(p);
    world.loadAllNow(p[0], p[1]);
    if (!roads.load(dataDir)) fprintf(stderr, "ai.bin not found: no traffic\n");
    if (!TheSound().init(dataDir)) fprintf(stderr, "sound_tables.bin / resbnk.bin invalid or no audio device: no sound effects (run scripts/setup_game.py)\n");
    if (!traffic.init(dataDir)) fprintf(stderr, "population_tables.bin / infozones.bin / popinfo.bin invalid: no traffic (run scripts/setup_game.py)\n");
    LoadVehicleInfos(vehicleInfos);   // parked cars come from the map's car generators (cargens.cpp)
    if (getenv("CTW_VEHDBG"))
        for (size_t i = 0; i < vehicleInfos.size(); ++i)
            { const VehicleInfo& v = vehicleInfos[i];
                fprintf(stderr, "veh %zu type %d model %d size %d %d %d axles %d %d door0 %d %d seat0 %d %d %d seats %d node %d\n", i, v.type(), v.model(),
                        v.s16(0x28), v.s16(0x2A), v.s16(0x2C), v.s16(0x3A), v.s16(0x3C), v.s32(0x98), v.s32(0x9C), v.s32(0xDC), v.s32(0xE0), v.s32(0xE4), v.s32(0x10C), (int8_t)v.raw[0x94]); }
    carGens.reset();
    Plugins_Init(modsDir);
    return true;
}

const Model* Game::carModel(int infoId) {
    if (infoId < 0 || infoId >= (int)vehicleInfos.size()) return nullptr;
    int id = vehicleInfos[infoId].model();
    auto it = models_.find(id);
    if (it != models_.end()) return it->second.get();
    auto m = std::make_unique<Model>();
    std::vector<uint8_t> raw;
    if (!Assets_Pak().read(id, raw) || !m->parse(raw)) m.reset();
    const Model* r = m.get();
    models_[id] = std::move(m);
    return r;
}

int Game::spawnCar(int infoId, const int32_t at[3], int16_t heading, int palette) {
    if (infoId < 0 || infoId >= (int)vehicleInfos.size()) return -1;
    if (palette < -1 || palette > 26) return -1;
    const VehicleInfo& vi = vehicleInfos[infoId];
    if (vi.type() > 1) return -1;   // cars and bikes (0 = car, 1 = bike; boats, the jet ski, helicopters and the tank are not ported)
    if (palette < 0) {               // cVehicleInfo::RandomPalette: bits 0..24 of the allowed mask, else 26
        std::vector<int> allowed;
        for (int b = 0; b < 25; ++b)
            if (vi.paletteMask() >> b & 1) allowed.push_back(b);
        palette = allowed.empty() ? 26 : allowed[Rand32Critical((uint32_t)allowed.size())];
    }
    Collision::Ground g = collision.ground(at[0] / 4096.f, at[1] / 4096.f, at[2] / 4096.f + 3.f);
    int32_t p[3] = {at[0], at[1], (int32_t)lroundf(g.z * 4096.f) + 0x4CC};   // resting on its springs (0.3 up)
    cars.emplace_back();
    cars.back().init(vi, infoId, p, heading, palette);
    cars.back().uid = nextUid++;
    if (const char* h = getenv("CTW_HEALTH")) cars.back().damage(255 - atoi(h));   // (testing: start damaged)
    cars.back().lookAhead = (int32_t)Rand32Critical(0x3000) + 0x6000;
    carGens.noteLoaded(infoId);
    return (int)cars.size() - 1;
}

void Game::spawnGenerated(int infoId, const int32_t pos[3], int16_t heading, int palette) {
    if (infoId < 0 || infoId >= (int)vehicleInfos.size()) return;
    int i = spawnCar(infoId, pos, heading, palette);
    if (i >= 0) cars[i].generated = true;
}

void Game::renderProps(const WorldCamera& cam, bool draw) {   // cDynamicPropManager + cLightManager::AddPropLights
    const int32_t ex = (int32_t)(cam.eye[0] * 4096.f), ey = (int32_t)(cam.eye[1] * 4096.f);
    int cx, cy;
    Collision::cellOfPos(ex, ey, cx, cy);
    const int64_t range = (int64_t)(renderDistance + 40.f) << 12;   // about the streamed city around the camera
    const int cells = (int)(range >> 12) / 50 + 1;                   // collision cells are 50 units
    for (int dy = -cells; dy <= cells; ++dy)
        for (int dx = -cells; dx <= cells; ++dx) {
            const std::vector<Collision::Prop>* list = collision.props(cx + dx, cy + dy);
            if (!list) continue;
            for (const Collision::Prop& p : *list) {
                const int64_t ddx = p.x - ex, ddy = p.y - ey;
                if (ddx * ddx + ddy * ddy > range * range || p.state != 0) continue;   // (knocked: PropDynamics)
                if (!draw) { props.lights(p, world.propLights); continue; }
                const int32_t at[3] = {p.x, p.y, p.z};
                if (canSee(at, props.radius(p.prop))) props.draw(p);
            }
        }
    if (draw) {
        propDynamics.render(*this);
        glDisable(GL_LIGHTING);
        glDisable(GL_BLEND);
        glDisable(GL_ALPHA_TEST);
        glDepthMask(GL_TRUE);
    }
}

void Game::setRenderDistance(float units) {
    renderDistance = std::max(120.f, std::min(units, 720.f));
    world.setRadius((int)std::ceil(renderDistance / 120.f));   // streaming blocks are 120 units
}

void Game::updateCityEmitters() {   // cWorldSector::DataLoaded -> cCityEmitters::SpawnAllEmittersInSector
    int32_t f[3];
    focus(f);
    int cx, cy;
    Collision::cellOfPos(f[0], f[1], cx, cy);
    for (auto it = cityEmitters.begin(); it != cityEmitters.end();) {   // cells left behind
        const int kx = it->first / 100, ky = it->first % 100;
        if (std::abs(kx - cx) <= 2 && std::abs(ky - cy) <= 2) { ++it; continue; }
        for (Emitter* e : it->second) TheParticles().remove(e);
        fountains.erase(it->first);
        it = cityEmitters.erase(it);
    }
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
            const int key = (cx + dx) * 100 + (cy + dy);
            if (cx + dx < 0 || cy + dy < 0 || cityEmitters.count(key)) continue;
            std::vector<Emitter*>& list = cityEmitters[key];
            if (const std::vector<Collision::CityEmitter>* es = collision.emitters(cx + dx, cy + dy))
                for (const Collision::CityEmitter& e : *es)
                    if (e.type == 0) {
                        const int32_t at[3] = {e.x, e.y, e.z};
                        list.push_back(TheParticles().add<SteamEmitter>(at, true));
                    } else if (e.type == 1) {   // cFountainStream(nullptr, pos, (0, 0, 2.0), -1, 1, 0, 0x7F39)
                        const int32_t at[3] = {e.x, e.y, e.z};
                        const int16_t up[3] = {0, 0, 0x2000};
                        fountains[key].push_back(std::make_unique<Fountain>(at, up));
                    }
        }
}

void Game::updateWorldObjects() {   // once per game frame, after the vehicles moved
    updateCityEmitters();
    for (auto& kv : fountains)
        for (auto& f : kv.second) f->tick(frame);
    propDynamics.update(*this);
}

void Game::focus(int32_t o[3]) const {
    const int32_t* p = playerCar >= 0 ? cars[playerCar].pos : player.pos;
    o[0] = p[0]; o[1] = p[1]; o[2] = p[2];
}

void Game::viewCamera(WorldCamera& cam) const {
    if (freeCam.on) cam = freeCam.cam;
    else if (playerCar >= 0) carCam.toWorldCamera(cam);
    else camera.toWorldCamera(cam);
}

bool Game::canSee(const int32_t p[3], float r) const {   // the active camera's view frustum against a sphere
    WorldCamera cam;
    viewCamera(cam);
    float d[3] = {p[0] / 4096.f - cam.eye[0], p[1] / 4096.f - cam.eye[1], p[2] / 4096.f - cam.eye[2]};
    float z = d[0] * cam.fwd[0] + d[1] * cam.fwd[1] + d[2] * cam.fwd[2];
    if (z < cam.zNear - r || z > cam.zFar + r) return false;
    float x = d[0] * cam.right[0] + d[1] * cam.right[1] + d[2] * cam.right[2];
    float y = d[0] * cam.up[0] + d[1] * cam.up[1] + d[2] * cam.up[2];
    float ty = tanf(cam.fovY * 3.14159265f / 360.f), tx = ty * (float)OS_ScreenGetWidth() / std::max(1.f, (float)OS_ScreenGetHeight());
    if ((std::fabs(y) - z * ty) / std::sqrt(1 + ty * ty) > r) return false;
    if ((std::fabs(x) - z * tx) / std::sqrt(1 + tx * tx) > r) return false;
    return true;
}

void Game::removeFarCars() {   // cVehicle::ShouldBeDestroyed: more than ~74 units from the player (and off screen)
    int32_t f[3];
    focus(f);
    for (int i = (int)cars.size() - 1; i >= 0; --i) {
        const Vehicle& c = cars[i];
        if (i == playerCar || i == task.car || c.keep) continue;
        int64_t dx = f[0] - c.pos[0], dy = f[1] - c.pos[1];
        if (dx * dx + dy * dy <= 0x1563FFFFFFLL) continue;
        if (canSee(c.pos, 5.f)) continue;   // (only when off screen)
        cars[i].releaseEffects();
        cars.erase(cars.begin() + i);
        if (playerCar > i) --playerCar;
        if (task.car > i) --task.car;
    }
}

// cPlayerOnFoot::HandlePlayerEnterExitVehicle / cPlayerInVehicle: the enter/exit button starts the get-in or get-out task
void Game::enterOrExit() {
    if (task.op != CarTask::None) return;
    if (playerCar >= 0) startExit();
    else startEnter();
}

static int32_t dist2d(const int32_t a[3], const int32_t b[3]) {
    int64_t dx = a[0] - b[0], dy = a[1] - b[1];
    return (int32_t)std::sqrt((double)(dx * dx + dy * dy));
}

// cOneShotAnimationTask::AddConstantVelocity: spread an offset over the animation's length
// (frames = numFrames x 30 / (rate x 16 >> 12))
static void constantVelocity(const PedSprites& ps, int anim, const int32_t delta[3], int16_t out[3]) {
    int n = ps.numFrames(anim);
    uint32_t r = (uint32_t)(ps.rate(anim) << 4) >> 12;
    uint32_t frames = r ? (uint32_t)(n * 30) / r : 0;
    int32_t k = frames ? 0x1000 / (int32_t)frames : 0;
    for (int i = 0; i < 3; ++i) out[i] = (int16_t)(((uint32_t)(k * (int16_t)delta[i])) >> 12);
}

void Game::startEnter() {   // FindSuitableVehicles (within 10 units) -> the driver's seat
    int best = -1;
    int64_t bestD = 0;
    for (int i = 0; i < (int)cars.size(); ++i) {
        int64_t dx = cars[i].pos[0] - player.pos[0], dy = cars[i].pos[1] - player.pos[1];
        int64_t d2 = dx * dx + dy * dy;
        if (d2 < (int64_t)0xA000 * 0xA000 && (best < 0 || d2 < bestD)) { best = i; bestD = d2; }
    }
    if (best < 0) return;
    task = CarTask{};
    task.op = CarTask::GotoDoor;
    task.car = best;
    task.seat = 0;
}

void Game::startExit() {   // cExitCar: which way out depends on the speed (mInstructionsWheeledVehicle)
    Vehicle& c = cars[playerCar];
    int32_t sp = (int32_t)std::sqrt((double)c.vel[0] * c.vel[0] + (double)c.vel[1] * c.vel[1]);
    if (sp >= 164637) return;   // too fast to get out
    task = CarTask{};
    task.car = playerCar;
    task.seat = 0;
    task.op = sp < 14634 ? CarTask::ClimbOut : CarTask::Braking;   // (54879..164637 would roll out: not ported, brakes)
    if (task.op == CarTask::ClimbOut) task.heading = -1;           // OP_OPENDOOR this frame, the animation next frame
}

bool Game::tickTask(bool stickHeld, int16_t stickHeading) {
    if (task.op == CarTask::None) return false;
    Vehicle& c = cars[task.car];
    const int body = player.bodySet * 0x113;
    auto place = [&]() {   // cAttachedManager: the ped follows the car
        c.localToWorld(task.off, player.pos);
        player.setHeading((int16_t)(c.heading() + task.heading));
        player.vel[0] = player.vel[1] = player.vel[2] = 0;
    };
    switch (task.op) {
    case CarTask::GotoDoor: {   // cGotoTargetOnFootStraightLine to the door (arrives within 2 units)
        // cGetInVehiclePlayer: pushing the stick away from the way the ped faces cancels
        if (stickHeld) {
            int s = (int)(sinf(stickHeading * 9.587378e-05f) * 4096), co = (int)(cosf(stickHeading * 9.587378e-05f) * 4096);
            if ((int64_t)player.fwd[0] * s + (int64_t)player.fwd[1] * co < 0) { task.op = CarTask::None; return false; }
        }
        int32_t sp[3], w[3];
        c.doorSpawnPoint(task.seat, sp);
        c.localToWorld(sp, w);
        if (dist2d(player.pos, w) > 0x2000) return false;   // the normal on-foot update walks there (see tick)
        task.off[0] = sp[0]; task.off[1] = sp[1]; task.off[2] = sp[2];
        player.attached = true;
        if (c.isBike()) {   // no door: straight on (AddSlideIntoSeatAnim 0x5A / 0x5B, rising 1.0 above the seat height)
            task.heading = 0;
            int32_t seat[3];
            c.seatOffset(task.seat, seat);
            int32_t d[3] = {seat[0] - task.off[0], seat[1] - task.off[1], seat[2] - task.off[2] + 0x1000};
            int32_t rel[3] = {player.pos[0] - c.pos[0], player.pos[1] - c.pos[1], player.pos[2] - c.pos[2]};
            bool right = (int64_t)c.right[0] * rel[0] + (int64_t)c.right[1] * rel[1] + (int64_t)c.right[2] * rel[2] > 0;
            player.playOneShot(0x5A, 0x5B, right);
            constantVelocity(pedSprites, 0x5A + body, d, task.vel);
            task.op = CarTask::SlideIn;
            place();
            return true;
        }
        // op 3: face the car and open the door (6/7; the right side plays 8/9 flipped)
        task.heading = (task.seat & 1) ? -0x4000 : 0x4000;
        if (c.hasDoor(task.seat) && !c.doorOpen(task.seat)) c.openDoor(task.seat);
        if (task.seat & 1) player.playOneShot(8, 9, true);
        else player.playOneShot(6, 7, false);
        task.op = CarTask::OpenDoor;
        place();
        return true;
    }
    case CarTask::OpenDoor: {
        place();
        if (!player.stepOneShot(&pedSprites)) return true;
        // op 8: AddSlideIntoSeatAnim - facing forward, slide from the door to the seat while sinking 0.8
        task.heading = 0;
        int32_t seat[3];
        c.seatOffset(task.seat, seat);
        int32_t d[3] = {seat[0] - task.off[0], seat[1] - task.off[1], -0xCCC};
        int32_t rel[3] = {player.pos[0] - c.pos[0], player.pos[1] - c.pos[1], player.pos[2] - c.pos[2]};
        bool right = (int64_t)c.right[0] * rel[0] + (int64_t)c.right[1] * rel[1] + (int64_t)c.right[2] * rel[2] > 0;
        player.playOneShot(0xC, 0xD, right);
        constantVelocity(pedSprites, 0xC + body, d, task.vel);
        task.op = CarTask::SlideIn;
        place();
        return true;
    }
    case CarTask::SlideIn: {
        bool done = player.stepOneShot(&pedSprites);
        if (!done) for (int i = 0; i < 3; ++i) task.off[i] += task.vel[i];
        place();
        if (!done) return true;
        // RunPostEnterSeat: in the seat; the door shuts
        if (!c.isBike()) c.closeDoor(task.seat);
        else c.bikeMounted = 1;
        c.seatUser[task.seat] = -2;
        c.engineOn = true;
        c.keep = true;   // a car the player used is not cleaned up (cVehicle +0x945 bit 3)
        player.hidden = true;
        player.attached = false;
        playerCar = task.car;
        carCam.setBehind(c);
        task.op = CarTask::None;
        return false;
    }
    case CarTask::Braking: {   // OP 2: stop the car first (SetNextOperation each frame)
        int32_t sp = (int32_t)std::sqrt((double)c.vel[0] * c.vel[0] + (double)c.vel[1] * c.vel[1]);
        if (sp < 14634) { task.op = CarTask::ClimbOut; task.heading = -1; }
        return true;
    }
    case CarTask::ClimbOut: {
        if (task.heading == -1) {   // OP_OPENDOOR, then OP_PLAY_ANIM next frame
            if (!c.isBike()) c.openDoor(task.seat);
            task.heading = -2;
            return true;
        }
        if (task.heading == -2) {   // OP_PLAY_ANIM: from the seat (2.5 lower) to the door's spawn point, 0.5 back
            c.seatOffset(task.seat, task.off);
            task.off[2] += c.isBike() ? 0x1000 : -0x2800;   // (a bike's rider starts 1.0 above the seat)
            task.heading = 0;
            c.doorSpawnPoint(task.seat, task.end);
            task.end[1] -= 0x800;
            int32_t d[3] = {task.end[0] - task.off[0], task.end[1] - task.off[1], task.end[2] - task.off[2]};
            int anim = c.isBike() ? 0x54 : 0xE;   // AddExitBikeAnimation / AddExitCarAnimation
            player.playOneShot(anim, anim + 1, (task.seat & 1) != 0);
            constantVelocity(pedSprites, anim + body, d, task.vel);
            player.hidden = false;
            player.attached = true;
            c.seatUser[task.seat] = -1;
            place();
            return true;
        }
        bool done = player.stepOneShot(&pedSprites);
        if (!done) for (int i = 0; i < 3; ++i) task.off[i] += task.vel[i];
        place();
        if (!done) return true;
        if (!c.isBike()) c.closeDoor(task.seat);   // "ExitCar closeDoor"
        c.engineOn = false;
        c.localToWorld(task.end, player.pos);
        player.attached = false;
        player.placeOnGround(&collision);
        playerCar = -1;
        camera.reset(player.pos, player.heading());
        task.op = CarTask::None;
        return false;
    }
    default: break;
    }
    return false;
}

// the stick angle in game units (clockwise from "up"), as cPad::PadAngle
static int16_t padAngle(float x, float y) { return (int16_t)(atan2f(x, y) * 10430.f); }

void Game::tick() {
    ++frame;
    player.speedScale = speedScale;
    Plugins_BeginFrameInput();
    // input -> the player's yoke (cPlayerOnFoot::HandleStrafe: wanted heading = camera yaw + stick angle)
    float mx = 0, my = 0;
    bool sprint = false, walk = false;
    if (Plugins_GameInput()) {
        const Uint8* ks = SDL_GetKeyboardState(nullptr);
        if (ks[SDL_SCANCODE_W] || ks[SDL_SCANCODE_UP]) my += 1;
        if (ks[SDL_SCANCODE_S] || ks[SDL_SCANCODE_DOWN]) my -= 1;
        if (ks[SDL_SCANCODE_D] || ks[SDL_SCANCODE_RIGHT]) mx += 1;
        if (ks[SDL_SCANCODE_A] || ks[SDL_SCANCODE_LEFT]) mx -= 1;
        sprint = ks[SDL_SCANCODE_LSHIFT] || ks[SDL_SCANCODE_RSHIFT];
        walk = ks[SDL_SCANCODE_LCTRL] || ks[SDL_SCANCODE_RCTRL];
    }
    if (scriptedInput) {
        mx = scriptedMove[0]; my = scriptedMove[1];
        sprint = scriptedState == 3;
        walk = scriptedState == 1;
    }
    bool enter = enterPressed || std::find(scriptedEnterFrames.begin(), scriptedEnterFrames.end(), (int)frame) != scriptedEnterFrames.end();
    enterPressed = false;
    if (enter) {
        if (task.op == CarTask::GotoDoor) task.op = CarTask::None;   // pressing again while walking there cancels
        else enterOrExit();
    }
    bool stickHeld = mx != 0 || my != 0;
    int16_t stickHeading = stickHeld ? (int16_t)(camera.yaw + padAngle(mx, my)) : 0;
    if (playerCar >= 0) {   // driving: cPlayer::FillInDrivingYoke (accelerate / brake-reverse / steer / handbrake)
        Vehicle::Controls dc;
        if (Plugins_GameInput()) {
            const Uint8* ks = SDL_GetKeyboardState(nullptr);
            bool gas = ks[SDL_SCANCODE_W] || ks[SDL_SCANCODE_UP], brake = ks[SDL_SCANCODE_S] || ks[SDL_SCANCODE_DOWN];
            if (gas != brake) dc.throttle = gas ? 0x1000 : -0x1000;
            bool l = ks[SDL_SCANCODE_A] || ks[SDL_SCANCODE_LEFT], r = ks[SDL_SCANCODE_D] || ks[SDL_SCANCODE_RIGHT];
            if (l != r) dc.steer = l ? -0x1000 : 0x1000;
            dc.handbrake = ks[SDL_SCANCODE_SPACE];
            TheSound().horn = ks[SDL_SCANCODE_H] || ks[SDL_SCANCODE_LALT];
        }
        if (scriptedInput) { dc.throttle = scriptedDrive * 0x1000; dc.steer = scriptedSteer * 0x1000; dc.handbrake = scriptedHandbrakeFrame >= 0 && (int)frame >= scriptedHandbrakeFrame; }
        if (task.op == CarTask::Braking) {   // cExitCar OP 2: brake against the motion (handbrake when going forward)
            Vehicle& c = cars[playerCar];
            bool back = (int64_t)c.vel[0] * c.fwd[0] + (int64_t)c.vel[1] * c.fwd[1] + (int64_t)c.vel[2] * c.fwd[2] < 0;
            dc = Vehicle::Controls{};
            dc.throttle = back ? 0x1000 : -0x1000;
            dc.handbrake = !back;
        } else if (task.op == CarTask::ClimbOut) dc = Vehicle::Controls{};
        tickTask(false, 0);
        if (playerCar < 0) { Plugins_Tick(); return; }   // got out this frame
        traffic.update(*this);
        for (int i = 0; i < (int)cars.size(); ++i) cars[i].act(i == playerCar ? dc : Vehicle::Controls{}, i == playerCar, &collision);
        Vehicle::collideCars(cars, playerCar, frame);
        propDynamics.checkImpacts(*this);
        for (Vehicle& c : cars) { c.integrate(&collision); c.processAlways(); c.processDamage(frame); }
        if (speedScale != 1.f && dc.throttle > 0) {   // mods: the speed changer nudges the driven vehicle's speed
            Vehicle& me = cars[playerCar];
            if (speedScale < 1.f || me.speed() < (int32_t)(30.f * 4096.f * speedScale)) {
                float k = 1.f + (speedScale - 1.f) * 0.04f;
                for (int i = 0; i < 3; ++i) me.vel[i] = (int32_t)(me.vel[i] * k);
            }
        }
        updateWorldObjects();
        TheParticles().update(frame);
        TheSkidmarks().process();
        if (cars[playerCar].isBike() && player.hidden) {   // cPed::AnimatePedInVehicle on a bike
            const Vehicle& c = cars[playerCar];
            bool backwards = (int64_t)c.vel[0] * c.fwd[0] + (int64_t)c.vel[1] * c.fwd[1] + (int64_t)c.vel[2] * c.fwd[2] <= 0;
            if (c.bikeLeaning()) player.ride(0xA6, 0xA7, &pedSprites);                        // stopped, a foot down
            else if (c.bikeReversing() && backwards) player.ride(0xA4, 0xA5, &pedSprites);   // pushing it backwards
            else player.ride(0x5C, 0x5D, &pedSprites);
        }
        if (cars[playerCar].justDied) {   // (the game kills the occupants here; the player is thrown out instead)
            cars[playerCar].justDied = false;
            Vehicle& c = cars[playerCar];
            int32_t out[3];
            c.doorSpawnPoint(0, out);
            c.localToWorld(out, player.pos);
            player.hidden = false;
            player.placeOnGround(&collision);
            playerCar = -1;
            task.op = CarTask::None;
            camera.reset(player.pos, player.heading());
            Plugins_Tick();
            return;
        }
        Vehicle& me = cars[playerCar];
        if (player.hidden) { player.pos[0] = me.pos[0]; player.pos[1] = me.pos[1]; player.pos[2] = me.pos[2]; }
        carCam.update(me, &collision);
        sprintHeld_ = false;
        world.tick();
        if (clockRunning) { world.timeCycle().advanceFrames(1); world.timeCycle().evaluate(); }
        if (!freeCam.on) world.stream(me.pos[0] / 4096.f, me.pos[1] / 4096.f, 2);
        carGens.update(*this);
        removeFarCars();
        TheSound().update(*this);
        Plugins_Tick();
        return;
    }
    bool busy = tickTask(stickHeld, stickHeading);   // attached to a car (opening the door / getting in)
    Player::Input in;
    in.moving = stickHeld;
    in.heading = stickHeading;
    if (task.op == CarTask::GotoDoor) {   // cGotoTargetOnFootStraightLine: head for the door (running at most)
        const Vehicle& c = cars[task.car];
        int32_t sp[3], w[3];
        c.doorSpawnPoint(task.seat, sp);
        c.localToWorld(sp, w);
        in.moving = true;
        in.heading = padAngle((float)(w[0] - player.pos[0]), (float)(w[1] - player.pos[1]));
        walk = false;
    }
    in.sprint = sprint;
    in.sprintReleased = sprintHeld_ && !sprint;
    in.maxLevel = walk ? 1 : 3;   // PC extra: hold Ctrl to walk (the phone game has no walk button)
    if (task.op == CarTask::GotoDoor) in.maxLevel = 2;   // sVirtYoke::ConstrainWalkSpeed
    sprintHeld_ = sprint;
    player.obstacles.clear();
    for (const Vehicle& c : cars) {   // cPed::ConstrainByCollision: still, upright cars are boxes for the player
        if (c.vel[0] || c.vel[1] || c.vel[2] || c.up[2] < 0xFD8) continue;
        Collision::Box b{};
        int32_t cz = c.pos[2] + c.hz;   // GetWorldCollisionPos: the box centre
        b.cx = c.pos[0]; b.cy = c.pos[1]; b.cz = cz - c.hz;
        b.hx = c.hx; b.hy = c.hy; b.hz = c.hz;
        b.angle = (int16_t)-c.heading();
        player.obstacles.push_back(b);
    }
    if (!busy) player.update(in, &collision, &pedSprites);
    traffic.update(*this);
    for (Vehicle& c : cars) c.act(Vehicle::Controls{}, false, &collision);
    Vehicle::collideCars(cars, -1, frame);
    propDynamics.checkImpacts(*this);
    for (Vehicle& c : cars) { c.integrate(&collision); c.processAlways(); c.processDamage(frame); c.justDied = false; }
    updateWorldObjects();
    TheParticles().update(frame);
    TheSkidmarks().process();
    camera.update(player.pos, player.heading(), player.vel, &collision);
    world.tick();
    if (clockRunning) { world.timeCycle().advanceFrames(1); world.timeCycle().evaluate(); }   // cTimeCycle::Process
    float p[3];
    player.posf(p);
    if (!freeCam.on) world.stream(p[0], p[1], 2);   // (the free camera streams around itself in render)
    carGens.update(*this);
    removeFarCars();
    TheSound().horn = false;
    TheSound().update(*this);
    Plugins_Tick();
}

void Game::render(int W, int H) {
    WorldCamera cam;
    viewCamera(cam);
    if (freeCam.on) world.stream(cam.eye[0], cam.eye[1], 2);   // also while the game is paused
    world.propLights.clear();
    renderProps(cam, false);   // their lights, drawn by the world renderer with its own
    world.drawBeforeLights = [&] { renderProps(cam, true); };
    world.render(cam, W, H);
    // the world renderer leaves the view matrix set: draw the player in the same space
    glEnable(GL_DEPTH_TEST);
    TimeCycle& tc = world.timeCycle();
    uint32_t t = tc.time();
    PedLight light = PedLight::fromTimeCycle(tc);
    TheSkidmarks().render();
    for (const Vehicle& c : cars) c.render(carModel(c.infoId));   // (lit by the world renderer's lights)
    {   // cCar::UpdateHeadLights: headlights on outside 07:00 .. 20:00
        bool night = t - 0x7000u >= 0xD000u;
        for (const Vehicle& c : cars) c.renderLights(cam, frame, night, &collision);
    }
    {   // fountains, tinted by the time cycle's ambient colour (cTimeCycle::Colour(0xD))
        const uint32_t ambient = world.timeCycle().ok() ? world.timeCycle().colour(13) : 0x808080u;
        for (auto& kv : fountains)
            for (auto& f : kv.second) f->render(cam, ambient);
    }
    TheParticles().render(cam);
    glEnable(GL_DEPTH_TEST);
    player.render(&pedSprites, &light);   // (hidden while sitting in a car)
    if (playerCar >= 0 && cars[playerCar].isBike() && player.hidden) {   // riding: drawn where the bike says
        int32_t up[3], legs[3];
        cars[playerCar].riderRenderPos(up, legs);
        player.renderRiding(&pedSprites, &light, up, legs, cars[playerCar].heading());
    }
    float p[3];
    player.posf(p);
    if (showCollision) { collision.debugDraw(p[0], p[1], 40.f); roads.debugDraw(p[0], p[1], 80.f); }
    glDisable(GL_DEPTH_TEST);

    Hud_Begin(W, H);
    char clock[16];
    snprintf(clock, sizeof clock, "%02u:%02u", t >> 12, (t & 0xFFF) * 60 >> 12);
    Hud_Text(W - 20 - Hud_TextWidth(clock, 1.5f), 16, 1.5f, 0xFFFFFFFFu, clock);
    Hud_Text(16, H - 30, 1.f, 0xFFFFFFB0u, playerCar >= 0 ? "Gameplay prototype.  W/S: accelerate/brake-reverse, A/D: steer, Space: handbrake, F: get out, Esc: quit"
                                                         : "Gameplay prototype.  WASD: move, Shift: sprint, Ctrl: walk, F / Enter: get in a car, F5: spawn car, F3: collision, Esc: quit");
    Plugins_DrawHud(W, H);
    Hud_End();
}

void Game::run() {
    const double step = 1.0 / 30.0;   // OS_ApplicationTick: the game logic runs at 30 fps
    double acc = 0, last = OS_TimeAccurate();
    bool running = true;
    while (running) {
        running = Host_PumpEvents();
        while (int k = Host_PopKey()) {
            if (Plugins_Key(k)) continue;   // a plugin (e.g. an open mod menu) took it
            if (k == SDL_SCANCODE_ESCAPE) running = false;
            else if (k == SDL_SCANCODE_F3) showCollision = !showCollision;
            else if (k == SDL_SCANCODE_F || k == SDL_SCANCODE_RETURN) enterPressed = true;
            else if (k == SDL_SCANCODE_F5 && playerCar < 0 && !vehicleInfos.empty()) {   // testing: a car in front of the player
                for (int tries = 0; tries < (int)vehicleInfos.size(); ++tries) {
                    spawnIndex = (spawnIndex + 1) % (int)vehicleInfos.size();
                    if (vehicleInfos[spawnIndex].type() <= 1) break;
                }
                int32_t at[3] = {player.pos[0] + player.fwd[0] * 6, player.pos[1] + player.fwd[1] * 6, 0};
                spawnCar(spawnIndex, at, player.heading());
            }
        }
        double now = OS_TimeAccurate();
        acc += (now - last) * std::max(0.f, std::min(gameSpeed, 20.f));
        last = now;
        acc = std::min(acc, 0.25 * std::max(1.f, gameSpeed));   // don't spiral after a stall
        while (acc >= step) { tick(); acc -= step; }
        render((int)OS_ScreenGetWidth(), (int)OS_ScreenGetHeight());
        OS_ScreenSwapBuffers();
    }
}

void Game::shutdown() { Plugins_Shutdown(); }
