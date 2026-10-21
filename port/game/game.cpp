// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "game.h"
#include "weaponhud.h"
#include "hud.h"
#include "particles.h"
#include "explosions.h"
#include "fixedmath.h"
#include "sound.h"
#include "skidmarks.h"
#include "plugins.h"
#include "os/os.h"
#include "audio/audio.h"
#include "padinput.h"
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
    RandInit(0); // Android cGame::Init seeds all four gameplay random streams with zero.
    explosionRadiusCheck_ = false; // Explosion::Reset clears the script-controlled duplicate check.
    dataDir = data;
    modsDir = mods;
    if (!LoadGameplayTables(dataDir)) {
        fprintf(stderr, "gameplay_tables.bin missing or invalid: run scripts/setup_game.py\n");
        return false;
    }
    if (!world.init(dataDir)) return false;
    world.setRadius(1);   // the game keeps a 3 x 3 sector window; one block ring around the player covers it
    Hud_Init(dataDir);
    if (!pauseScreen.init(dataDir)) fprintf(stderr, "SS_Hud missing: pause-screen artwork unavailable\n");
    if (!gameplayHud.init(dataDir)) fprintf(stderr, "radar map / SS_AlwaysLoaded missing: no radar HUD\n");
    if (props.load()) collision.setPropLibrary(&props);   // before any collision cell loads
    else fprintf(stderr, "prop definitions not found: no street furniture\n");
    if (!collision.init(dataDir)) fprintf(stderr, "world.bin not found: no collision\n");
    world.groundBelow = [this](float x, float y, float z, float& ground) {   // light pools (render style)
        ground = collision.ground(x, y, z).z;
        return true;
    };
    player.placeOnGround(&collision);
    if (!pedSprites.init()) fprintf(stderr, "ped sprites not found\n");
    camera.reset(player.pos, player.heading());
    for (int i = 0; i < 3; ++i) restartFallback_[i] = player.pos[i];
    if (!restart_.load(dataDir + "/restart_tables.bin"))
        fprintf(stderr, "restart_tables.bin missing: hospital restarts need scripts/setup_game.py\n");
    float p[3];
    player.posf(p);
    world.loadAllNow(p[0], p[1]);
    if (!roads.load(dataDir)) fprintf(stderr, "ai.bin not found: no traffic\n");
    if (!TheSound().init(dataDir)) fprintf(stderr, "sound_tables.bin / resbnk.bin invalid or no audio device: no sound effects (run scripts/setup_game.py)\n");
    if (!radio.init(dataDir)) fprintf(stderr, "radio_tables.bin / SS_Radio missing or invalid: run scripts/setup_game.py\n");
    if (!traffic.init(dataDir)) fprintf(stderr, "population_tables.bin / infozones.bin / popinfo.bin invalid: no traffic (run scripts/setup_game.py)\n");
    if (!peds.init(dataDir)) fprintf(stderr, "pedinfo.bin missing or invalid: no pedestrians\n");
    if (!Weapons_Init(dataDir))
        fprintf(stderr, "weapon_tables.bin missing or invalid: no weapons or blast health damage (run scripts/setup_game.py)\n");
    else if (!WeaponHud_Init(dataDir)) fprintf(stderr, "SS_Weapons / SS_Hud / SS_AlwaysLoaded missing: no weapon HUD\n");
    if (!weather.init(dataDir))
        fprintf(stderr, "weather_tables.bin missing or invalid: the weather does not change (run scripts/setup_game.py)\n");
    weather.reset(*this, 0);
    weather.rain = TheParticles().add<RainEmitter>(player.pos);
    blood = TheParticles().add<BloodEmitter>(player.pos);   // cGeneralParticleController::Init
    sparks = TheParticles().add<SparksEmitter>(player.pos);
    steamClouds = TheParticles().add<SteamEmitter>(player.pos, false);
    skidSmoke = TheParticles().add<SkidSmokeEmitter>(player.pos);
    LoadVehicleInfos(vehicleInfos);   // parked cars come from the map's car generators (cargens.cpp)
    carGens.reset();
    // cPlayer::cPlayer: cPopulationManager's schedules (peds 64, vehicles 32), then cLocalAreaKnowledge's (16)
    scheduler.reset();
    scheduler.create(pedSchedule, 64);
    scheduler.create(carSchedule, 32);
    localArea.reset(scheduler);
    sectors.reset();
    updateView();
    updateSectors();
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
    if (!nextUid) return -1;   // never recycle a handle after uint32_t exhaustion
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
    cars.back().lookAhead = (int32_t)Rand32Critical(0x3000) + 0x6000;
    carGens.noteLoaded(infoId);
    Plugins_EmitEvent(CTW_EVENT_VEHICLE_SPAWNED, cars.back().uid, infoId);
    return (int)cars.size() - 1;
}

bool Game::removeCar(int index) {
    if (index < 0 || index >= (int)cars.size() || index == playerCar ||
        (task.op != CarTask::None && index == task.car)) return false;
    for (int occupant : cars[index].seatUser) if (occupant != -1) return false;
    const uint32_t uid = cars[index].uid;
    const int model = cars[index].infoId;
    cars[index].releaseEffects();
    cars.erase(cars.begin() + index);
    traffic.forget(uid);
    if (playerCar > index) --playerCar;
    if (task.car > index) --task.car;
    Plugins_EmitEvent(CTW_EVENT_VEHICLE_REMOVED, uid, model);
    return true;
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
            if (const std::vector<Collision::CityEmitter>* es = collision.emitters(cx + dx, cy + dy)) {
                for (const Collision::CityEmitter& e : *es) {
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
        }
}

void Game::updateWorldObjects() {   // once per game frame, after the vehicles moved
    traces.update();
    fires.process(*this);
    if (blood) {   // the general particle controller follows the player, like the rain
        int32_t at[3];
        focus(at);
        blood->setPos(at);
        if (sparks) sparks->setPos(at);
        if (steamClouds) steamClouds->setPos(at);
        if (skidSmoke) skidSmoke->setPos(at);
    }
    updateCityEmitters();
    for (auto& kv : fountains)
        for (auto& f : kv.second) f->process(*this);
    propDynamics.update(*this);
}

void Game::addBlood(const int32_t pos[3], int count) {
    if (blood)
        for (int i = 0; i < count; ++i) blood->addParticle(pos);
}

void Game::addSpark(const int32_t pos[3], const int16_t dir[3], uint8_t life, bool useAngle, int16_t angle, bool jitter,
                    uint16_t colour, uint16_t colourRand, int16_t scale) {
    if (sparks) sparks->addParticle(pos, dir, life, useAngle, angle, jitter, colour, colourRand, scale);
}

void Game::addSkidSmoke(const int32_t pos[3], const int16_t velocity[3], bool dark) {
    if (skidSmoke) skidSmoke->addParticle(pos, velocity, dark);
}

void Game::addSteamCloud(const int32_t pos[3], uint16_t colour, int16_t scale) {
    if (steamClouds) steamClouds->addCloud(pos, colour, scale);
}

void Game::focus(int32_t o[3]) const {
    const int32_t* p = playerCar >= 0 ? cars[playerCar].pos : player.pos;
    o[0] = p[0]; o[1] = p[1]; o[2] = p[2];
}

void Game::viewCamera(WorldCamera& cam) const {
    if (renderCamValid_ && !freeCam.on) { cam = renderCam_; return; }   // interpolated while drawing
    if (freeCam.on) cam = freeCam.cam;
    else if (player.dead) {
        for (int i = 0; i < 3; ++i) cam.eye[i] = deathEye_[i] / 4096.f;
        cam.setYawPitch(-(int16_t)deathYaw_ * 360.f / 65536, (int16_t)deathPitch_ * 360.f / 65536);
    }
    else if (playerCar >= 0) carCam.toWorldCamera(cam);
    else if (scope.active) scope.toWorldCamera(*this, cam);
    else camera.toWorldCamera(cam);
    if (fovOverride > 0.f) cam.fovY = fovOverride;
    if (renderDistance > 120.f || freeCam.on)
        cam.zFar = std::max(cam.zFar, std::hypot(renderDistance * 1.414214f, cam.eye[2]) + 30.f);
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

bool Game::screenToWorld(float x, float y, int W, int H, float out[3]) const {
    if (W <= 0 || H <= 0) return false;
    WorldCamera cam;
    viewCamera(cam);
    const float ty = tanf(cam.fovY * 3.14159265f / 360.f), tx = ty * W / (float)H;
    const float nx = (2.f * x / W - 1.f) * tx, ny = (1.f - 2.f * y / H) * ty;
    float d[3];
    for (int k = 0; k < 3; ++k) d[k] = cam.fwd[k] + cam.right[k] * nx + cam.up[k] * ny;
    const int32_t a[3] = {(int32_t)(cam.eye[0] * 4096), (int32_t)(cam.eye[1] * 4096), (int32_t)(cam.eye[2] * 4096)};
    const int32_t b[3] = {(int32_t)((cam.eye[0] + d[0] * 400.f) * 4096), (int32_t)((cam.eye[1] + d[1] * 400.f) * 4096),
                          (int32_t)((cam.eye[2] + d[2] * 400.f) * 4096)};
    Collision::LineHit hit;
    if (const_cast<Collision&>(collision).staticLine(a, b, 0x40001F00, &hit)) {
        for (int k = 0; k < 3; ++k) out[k] = hit.point[k] / 4096.f;
        return true;
    }
    if (std::fabs(d[2]) < 1e-4f) return false;   // else where it meets the player's height
    const float t = (player.pos[2] / 4096.f - cam.eye[2]) / d[2];
    if (t <= 0) return false;
    for (int k = 0; k < 3; ++k) out[k] = cam.eye[k] + d[k] * t;
    return true;
}

void Game::updateView() {   // the game camera (not a mod's free camera), as cBaseCam::RecalculateMatrix sets it up
    int32_t eye[3];
    uint16_t yaw, pitch;
    if (player.dead) {
        std::copy(deathEye_, deathEye_ + 3, eye);
        yaw = deathYaw_;
        pitch = deathPitch_;
    } else if (playerCar >= 0) {
        carCam.position(eye);
        yaw = carCam.yaw();
        pitch = carCam.pitch();
    } else if (scope.active) {   // cSniperCam
        std::copy(scope.pos, scope.pos + 3, eye);
        yaw = scope.yaw;
        pitch = 0xC000;
    } else {
        camera.position(eye);
        yaw = camera.yaw;
        pitch = camera.pitch();
    }
    view.set(eye, yaw, pitch, (int)OS_ScreenGetWidth(), (int)OS_ScreenGetHeight());
    ExplosionTwirls::setViewAxis(view.m[6], view.m[7]);
}

void Game::updateSectors() {
    int32_t f[3];
    focus(f);
    sectors.hold(f[0], f[1]);                     // the player (and the car he drives) hold their sector
    sectors.updateCriticalArea(player.pos, view);  // cPlayer::UpdateCriticalArea (after cCamera::Update)
}

int Game::pedPopulation() const {   // cWorldSector::GetPopulation(1): every ped in the sectors, the player included
    int n = WorldSectors::inBox(player.pos, 327680, player.pos[0], player.pos[1]) ? 1 : 0;
    for (const Pedestrians::Ped& p : peds.peds)
        n += sectors.loadedAt(p.body.pos[0], p.body.pos[1]) &&
             WorldSectors::inBox(player.pos, 327680, p.body.pos[0], p.body.pos[1]);
    return n;
}

int Game::carPopulation() const {   // cWorldSector::GetPopulation(0): every vehicle in the sectors
    int n = 0;
    for (const Vehicle& c : cars)
        n += sectors.loadedAt(c.pos[0], c.pos[1]) && WorldSectors::inBox(player.pos, 327680, c.pos[0], c.pos[1]);
    return n;
}

// cPopulationManager::Update(false, false) from cPlayer::Process. The ped cap is 20 (the wanted multiplier below
// three stars) x the zone's ped density x 100 / 7500, at most 16; the vehicle cap 16 x car density x 100 / 7500
// (as 1145325 / 2^33), at most 14. The counts are everything in the loaded sectors of the 80-unit box around the
// player (cPlayer::GetPopulation). Each schedule's period follows the player's speed and its cap; a frame where the
// vehicle schedule fires may generate a vehicle, otherwise one where the ped schedule fires a ped.
// Not ported: the pool limits (word_105E8C8 / word_105E8E8: the free ped and vehicle slots also cap the counts),
// the wanted level (WantedModifier; when full, a cop roll frees a vehicle: cGlobalPopulationManager::ForceFreeVehicle)
// and the minimal-AI caps (4 and 4).
void Game::updatePopulation() {
    const uint32_t t = world.timeCycle().time();
    const Traffic::ZoneInfo& z = traffic.zoneOrDefault(player.pos[0], player.pos[1], t - 0x14000u < 0xFFFF3000u);
    const int pedCount = pedPopulation();
    int pedCap = std::min(16, (int)(20u * (uint32_t)z.pedDensity * 100u / 7500u));
    pedCap = std::min(64, (int)(pedCap * peds.densityScale));   // PC mod API (1 = the original)
    int carCap = std::min(14, (int)((uint32_t)((1145325ULL * (16u * (uint32_t)z.carDensity * 100u)) >> 32) >> 1));
    carCap = std::min(56, (int)(carCap * traffic.densityScale));
    peds.maxPeds = pedCap;
    traffic.maxCars = carCap;
    const int carCount = carPopulation();
    // the player ped's speed (driving: his car's)
    const int32_t* vel = playerCar >= 0 ? cars[playerCar].vel : player.vel;
    const uint32_t speed = (uint32_t)std::sqrt((double)(uint64_t)((int64_t)vel[0] * vel[0] + (int64_t)vel[1] * vel[1] +
                                                                  (int64_t)vel[2] * vel[2]));
    auto periodFor = [](int32_t k) {
        return k <= 4096 ? 1 : k <= 12288 ? 2 : k <= 24576 ? 4 : k <= 49152 ? 8 : k <= 98304 ? 16 : k <= 196608 ? 32 : 64;
    };
    if (pedCap) {
        const int32_t k = (int32_t)(30u * ((uint64_t)(0x9100000000000LL / (int)(speed + 16200)) >> 20)) / pedCap;
        const int period = periodFor(k);
        if (period != pedSchedule.period) scheduler.setTiming(pedSchedule, period);
    }
    if (carCap) {
        const int32_t k = (int32_t)(30u * ((uint64_t)(0x7200000000000LL / (int)(speed + 24570)) >> 20)) / carCap;
        const int period = periodFor(k);
        if (period != carSchedule.period) scheduler.setTiming(carSchedule, period);
    }
    if (!FrameScheduler::fired(carSchedule, frame)) {
        if (FrameScheduler::fired(pedSchedule, frame) && pedCount < pedCap) peds.generate(*this, z);
        return;
    }
    traffic.generateRandom(*this, z, carCount < carCap);
}

void Game::collectSectors() {   // cWorld::GarbageCollection (cGame::Render): cWorldSector::Reset deletes the entities
    sectors.collect();
    peds.collect(*this);
    for (int i = (int)cars.size() - 1; i >= 0; --i) {
        // entities that hold their sector (cEntity +0x38): the player's car, one being entered, mods' and missions'
        if (i == playerCar || i == task.car || cars[i].keep) continue;
        if (!sectors.loadedAt(cars[i].pos[0], cars[i].pos[1])) {
            peds.discardOccupants(cars[i]);
            removeCar(i);
        }
    }
}

void Game::removeFarCars() {   // cVehicle::ShouldBeDestroyed: more than ~74 units from the player (and off screen)
    int32_t f[3];
    focus(f);
    for (int i = (int)cars.size() - 1; i >= 0; --i) {
        const Vehicle& c = cars[i];
        if (i == playerCar || i == task.car || c.keep) continue;
        int64_t dx = f[0] - c.pos[0], dy = f[1] - c.pos[1];
        if (dx * dx + dy * dy <= 0x1563FFFFFFLL) continue;
        if (view.canSee(c.pos, 0x5000)) continue;   // (only when off screen)
        peds.discardOccupants(cars[i]);
        removeCar(i);
    }
}

// cPlayerOnFoot::HandlePlayerEnterExitVehicle / cPlayerInVehicle: the enter/exit button starts the get-in or get-out task
void Game::enterOrExit() {
    if (player.dead || player.reacting()) return;
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

void Game::startEnter(int target) {   // FindSuitableVehicles (within 10 units) -> the driver's seat
    if (player.dead) return;
    int best = -1;
    int64_t bestD = 0;
    for (int i = 0; i < (int)cars.size(); ++i) {
        if (target >= 0 && i != target) continue;
        if (cars[i].dead()) continue;
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
    if (task.car < 0 || task.car >= (int)cars.size() || cars[task.car].dead() || player.dead) {
        task = CarTask{};
        player.attached = false;
        return false;
    }
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
        if (!peds.exitOccupant(*this,c,task.seat)) return true;
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
        Plugins_EmitEvent(CTW_EVENT_PLAYER_ENTERED_VEHICLE, c.uid, c.infoId);
        int32_t previousView[3]; camera.position(previousView);
        carCam.inherit(previousView, camera.yaw, camera.pitch(), camera.blockedFrames());
        task.op = CarTask::None;
        return true; // this frame's entry task still owns the ped; do not run walking after taking the seat
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
        Plugins_EmitEvent(CTW_EVENT_PLAYER_EXITED_VEHICLE, c.uid, c.infoId);
        playerCar = -1;
        int32_t previousView[3]; carCam.position(previousView);
        camera.reset(player.pos, player.heading());
        camera.inherit(previousView, carCam.yaw(), carCam.pitch(), carCam.blockedFrames());
        task.op = CarTask::None;
        return false;
    }
    default: break;
    }
    return false;
}

// the stick angle in game units (clockwise from "up"), as cPad::PadAngle
static int16_t padAngle(float x, float y) { return (int16_t)(atan2f(x, y) * 10430.f); }

void Game::killInVehicle(Vehicle& car) {
    carCam.position(deathEye_); deathYaw_ = carCam.yaw(); deathPitch_ = carCam.pitch();
    int32_t door[3]; car.doorSpawnPoint(0, door); car.localToWorld(door, player.pos);
    for (int& occupant : car.seatUser) if (occupant == -2) occupant = -1;
    player.hidden = player.attached = false;
    player.dead = true;
    player.health = 0;
    player.vel[0] = player.vel[1] = player.vel[2] = 0;
    player.placeOnGround(&collision);
    player.playOneShot(0x18, 0x19, false);   // cAnimation: the on-ground death response
    Plugins_EmitEvent(CTW_EVENT_PLAYER_EXITED_VEHICLE, car.uid, car.infoId);
    playerCar = -1; task = CarTask{}; enterPressed = false;
    deathFrames_ = 0; deathCameraSettled_ = false;
    radio.update(*this);
}

void Game::processVehicleDeaths() {
    for (Vehicle& car : cars) {
        if (!car.justDied) continue;
        car.justDied = false;
        car.explosionDebris(carModel(car.infoId));
        peds.releaseOccupants(*this,car,true);
        Plugins_EmitEvent(CTW_EVENT_VEHICLE_DESTROYED, car.uid, car.infoId);
        // cVehicle::Damage: vehicles 7, 15 and 53 blow up as a type-6 cExplosionBigVehicle, the rest as an
        // ordinary type-4 generic explosion
        if (car.infoId == 7 || car.infoId == 15 || car.infoId == 53) {
            const int32_t still[3] = {0, 0, 0};
            addBigExplosion(car.pos, 6, 0x1000, still);
        } else {
            Explosion explosion{{car.pos[0],car.pos[1],car.pos[2]},3};
            if (queueExplosion(explosion)) shakeNearExplosion(car.pos);
        }
        if (playerCar >= 0 && &car == &cars[playerCar]) killInVehicle(car);
    }
    // Blast callbacks can smash explosive props and append to this queue. Copy the firing entry and defer
    // newborn explosions until the next tick; never retain a vector reference across a callback.
    const size_t count = explosions_.size();
    for (size_t i = 0; i < count; ++i) {
        if ((explosions_[i].type == 6 || explosions_[i].type == 7 || explosions_[i].type == 11) &&
            !explosions_[i].fired) {
            Explosion special = explosions_[i];   // its blast may queue more explosions
            if (special.type == 11) processHotDogExplosion(special);
            else processBigExplosion(special);
            explosions_[i] = special;
            continue;
        }
        if (explosions_[i].fired) { // generic explosion remains in its final state for eleven Process calls
            ++explosions_[i].delay;
            if (explosions_[i].type == 2 && explosions_[i].delay == 1) {   // cExplosionMolotov state 2: the blast
                const Explosion explosion = explosions_[i];
                affectExplosion(explosion);
                ExplosionDebris::create(explosion.pos, 2);
                TheParticles().add<ExplosionCloud>(explosion.pos,2,2,explosion.scale,0,0,5u);
            }
            continue;
        }
        if (--explosions_[i].delay > 0) continue;
        const Explosion explosion = explosions_[i];
        explosions_[i].fired = true; explosions_[i].delay = 0;
        if (explosion.type == 2) TheSound().scriptSfx(explosion.pos, 611, 127, 400);   // cExplosionMolotov
        else TheSound().explosion(*this, explosion.pos);
        TheParticles().add<ExplosionFlash>(explosion.pos,explosion.type,explosion.scale);
        if (explosion.type == 2) fires.molotov(*this, explosion.pos, true);   // the player threw it
        // cExplosionMolotov's blast is the next Process (above); the flash bang's DazeEntities is not ported
        if (explosion.type == 2 || explosion.type == 5) continue;
        affectExplosion(explosion);
        if (explosion.type == 4) ExplosionDebris::create(explosion.pos, 4);
        TheParticles().add<ExplosionCloud>(explosion.pos,explosion.type,1,explosion.scale,0,0,explosionParticleCount(explosion.pos));
    }
    explosions_.erase(std::remove_if(explosions_.begin(), explosions_.end(),
        [](const Explosion& e) { return e.fired && e.delay >= 11; }), explosions_.end());
}

void Game::addBigExplosion(const int32_t at[3], int type, int16_t scale, const int32_t velocity[3]) {
    Explosion e{{at[0],at[1],at[2]},0,type,false,scale};
    // cExplosionBigVehicle's constructor (inlined in AddExplosion): the heading of the velocity (-ATan2(-x, y))
    // and whether it moves
    const int angle = std::clamp((int)(atan2f((float)-velocity[0], (float)velocity[1]) * 10430.378f), -32768, 32767);
    e.heading = (int16_t)-angle;
    e.moving = (int64_t)velocity[0] * velocity[0] + (int64_t)velocity[1] * velocity[1] +
               (int64_t)velocity[2] * velocity[2] > 0x28000;
    // AddExplosion's duplicate check only applies to stationary explosions
    if (e.moving) { explosions_.push_back(e); stats.incExplosions(); }
    else queueExplosion(e);
}

void Game::processBigExplosion(Explosion& e) {   // cExplosionBigVehicle::Process
    const int32_t before = e.counter;
    e.counter += 2;
    if (e.state == 0) {
        if ((uint32_t)before > 0xFFFFFFFDu) return;
        shakeNearExplosion(e.pos);   // ShakePlayersCameras
        TheParticles().add<ExplosionFlash>(e.pos, 6, 0x1000);
        // AffectEntities(15, 22, 320, half, whole, 0) with the grenade's damage, whatever the scale
        const int damage = Weapons_Info(20).damage;
        affectBlast(ExplosionBlast(e.pos, e.type, 0xF000, 0x16000, (int32_t)(((uint32_t)damage << 11) & 0xFFFFF000u),
                                   (int32_t)((uint32_t)damage << 12)));
        TheParticles().add<ExplosionTwirls>(e.pos, 15);
    } else if (e.state == 1) {
        if (e.counter < (int)Rand16Critical(10) + 10) return;
        TheSound().explosion(*this, e.pos);   // PlayScriptSfx(0x1D2, 127, pos, 1900)
        ExplosionDebris::create(e.pos, 6);
        const unsigned count = explosionParticleCount(e.pos);
        int32_t dir[2] = {0, 0};
        if (e.moving) {
            dir[0] = (int32_t)(sinf((float)e.heading * 9.587378e-05f) * 4096.f);
            dir[1] = (int32_t)(sinf((float)(e.heading + 0x4000) * 9.587378e-05f) * 4096.f);
        }
        TheParticles().add<ExplosionCloud>(e.pos, 6, (unsigned)e.state, e.scale, dir[0], dir[1], count);   // the ring
        TheParticles().add<ExplosionCloud>(e.pos, 6, 0u, e.scale, dir[0], dir[1], count);   // the main cloud
    } else {
        if (e.counter >= 0x15) { e.fired = true; e.delay = 11; }   // Destroy
        return;
    }
    ++e.state;
    e.counter = 0;
}

void Game::addHotDogExplosion(const int32_t at[3], const int32_t velocity[3]) {
    Explosion e{{at[0],at[1],at[2]},0,11,false,0x1000};
    int32_t dir[3] = {velocity[0], velocity[1], velocity[2]};
    if ((int64_t)dir[0] * dir[0] + (int64_t)dir[1] * dir[1] + (int64_t)dir[2] * dir[2] > 0x63F) Normalise3(dir);
    else { dir[0] = dir[1] = 0; dir[2] = 0x1000; }
    for (int k = 0; k < 3; ++k) e.dir[k] = (int16_t)dir[k];
    queueExplosion(e);
}

void Game::processHotDogExplosion(Explosion& e) {   // cExplosionHotDog::Process
    e.counter += 2;
    if (e.state == 0) {
        const int32_t length2 = e.dir[0] * e.dir[0] + e.dir[1] * e.dir[1] + e.dir[2] * e.dir[2];
        if (length2 >= 1 && e.counter <= 0x7F) {
            // the gas jet: a type-1 muzzle flash two units back along the jet, pointing along it, and a hiss
            int32_t at[3] = {e.pos[0] - (int16_t)(e.dir[0] * 2), e.pos[1] - (int16_t)(e.dir[1] * 2),
                             e.pos[2] - (int16_t)(e.dir[2] * 2)};
            const int angle = std::clamp((int)(atan2f((float)(int16_t)e.dir[0], (float)(int16_t)-e.dir[1]) * 10430.378f),
                                         -32768, 32767);
            muzzleFlashes.add(at, (int16_t)-angle, 1, 0x1000);
            if (e.sfx == -1) e.sfx = TheSound().loopingScriptSfx(e.pos, 0x2A3, 127, 500);
            else TheSound().scriptLoop(e.sfx, e.pos, 127);
            return;
        }
        if (e.sfx != -1) TheSound().stopScriptSfx(e.sfx);
        e.sfx = -1;
        e.counter = 0;
        e.state = 1;
        return;
    }
    if (e.state == 1) {
        // AffectEntities(0, 7, 320, 0, 0, 7): a push with no damage that sets what is close alight
        ExplosionBlast blast(e.pos, 11, 0, 0x7000, 0, 0);
        blast.fireRadius = 0x7000;
        affectBlast(blast);
        TheParticles().add<ExplosionCloud>(e.pos, 11, (unsigned)e.state, (int16_t)0x8CC, 0, 0,
                                           explosionParticleCount(e.pos));
        TheSound().explosion(*this, e.pos);   // PlayScriptSfx(0x1D2, 127, pos, 1900)
        e.counter = 0;
        e.state = 2;
        return;
    }
    if (e.state == 2) { e.counter = 0; e.state = 6; return; }
    if (e.counter >= 0x15) { e.fired = true; e.delay = 11; }   // Destroy
}

void Game::addWeaponExplosion(const int32_t at[3], int type, int16_t scale) {
    // Explosion::AddExplosion: types 0, 1, 3 and 4 are cExplosionGeneric (types 1, 3 and 4 shake the cameras),
    // 2 cExplosionMolotov, 5 cExplosionFlashBang (shakes). The generic and molotov blasts act on their third
    // Process, the flash bang on its first.
    const int delay = type == 5 ? 1 : 3;
    if (!queueExplosion({{at[0],at[1],at[2]},delay,type,false,scale})) return;
    if (type == 1 || type == 3 || type == 5 || (type == 0 && scale >= 0xCCD)) shakeNearExplosion(at);
}

void Game::addPropExplosion(const int32_t at[3], int16_t scale) {
    // cExplosionGeneric: two ticks in state zero, then the blast on the third Process.
    if (queueExplosion({{at[0],at[1],at[2]},3,0,false,scale}) && scale >= 0xCCD) shakeNearExplosion(at);
}

unsigned Game::explosionParticleCount(const int32_t at[3]) const {
    int32_t eye[3];
    if (player.dead) std::copy(deathEye_,deathEye_+3,eye);
    else if (playerCar >= 0) carCam.position(eye);
    else camera.position(eye);
    int64_t d2 = 0;
    for (int k = 0; k < 3; ++k) { const int64_t d = (int64_t)eye[k]-at[k]; d2 += d*d; }
    const int32_t distance = (int32_t)std::sqrt((double)d2);
    return (unsigned)std::clamp(((distance-10*4096)/5) >> 12,1,8);
}

void Game::shakeNearExplosion(const int32_t at[3]) {
    int32_t playerPos[3]; focus(playerPos);
    if (!CameraShake::nearExplosion(playerPos,at)) return;
    if (playerCar >= 0) carCam.shake(0x2000,0xB33);
    else camera.shake(0x2000,0xB33);
}

void Game::processTimeCycle() {   // cTimeCycle::Process: the clock, the weather each hour, rain and lightning
    TimeCycle& tc = world.timeCycle();
    if (clockRunning) {
        const uint32_t before = tc.time();
        tc.advanceFrames(1);
        const uint32_t now = tc.time();
        if (now >> 12 != before >> 12) {   // the end of an hour
            weather.hourChanged(*this);
            stats.processEndOfHour(0);   // cStatsManager::ProcessEndOfHour (the wanted level is not ported: 0)
            if (now < before) stats.incDaysPassedInGame();   // past midnight
        }
        // the minute changed: cStatsManager::UpdateTime by the minutes passed
        const uint32_t m0 = (before & 0xFFF) * 60 >> 12, m1 = (now & 0xFFF) * 60 >> 12;
        if (m1 != m0) stats.updateTime((int)(m1 > m0 ? m1 - m0 : m1 - m0 + 60));
        tc.evaluate();
    }
    weather.tick(*this);
}

uint16_t Game::cameraYaw() const {   // the active camera's yaw (cBaseCam +0xE8)
    return playerCar >= 0 ? carCam.yaw() : camera.yaw;
}

void Game::finishShortFrame() {
    updateView();
    updateSectors();
    updatePopulation();
    updateWorldObjects();
    TheParticles().update(frame, world.timeCycle().time(),playerCar >= 0 ? cars[playerCar].pos : player.pos);
    collectSectors();
    TheSound().update(*this);
    Plugins_Tick();
}

void Game::updateDeath() {
    ++deathFrames_;
    if (deathFrames_ == 1) stats.incNumberOfTimesWasted();   // cDeathArrestWatcher::PlayerWasted
    auto approach = [](int32_t& value, int32_t target, int step) { value += std::clamp(target - value, -step, step); };
    if (!deathCameraSettled_) {
        approach(deathEye_[0], player.pos[0], 0x1000);
        approach(deathEye_[1], player.pos[1], 0x1000);
        approach(deathEye_[2], player.pos[2] + 0x8000, 0x800);
        deathPitch_ += (uint16_t)std::clamp((int)(int16_t)(0xC000 - deathPitch_), -0x38E, 0x38E);
        deathYaw_ += (uint16_t)std::clamp((int)(int16_t)-deathYaw_, -0x38E, 0x38E);
        deathCameraSettled_ = deathPitch_ == 0xC000 && deathYaw_ == 0 &&
            std::abs(deathEye_[0] - player.pos[0]) < 0x800 && std::abs(deathEye_[1] - player.pos[1]) < 0x800 &&
            std::abs(deathEye_[2] - player.pos[2] - 0x8000) < 0x800;
    } else deathEye_[2] = std::min(deathEye_[2] + 0x333, 100 * 4096);
    // The PDA hospital billing app is not ported yet. Keep its fade/rebirth transition playable.
    if (deathFrames_ == 72) {
        const RestartTables::Point* closest = nullptr;
        int64_t best = 0;
        for (const auto& p : restart_.hospitals) {
            int64_t dx = (int64_t)p.pos[0] - player.pos[0], dy = (int64_t)p.pos[1] - player.pos[1];
            int64_t distance = dx * dx + dy * dy;
            if (!closest || distance < best) { closest = &p; best = distance; }
        }
        for (int i = 0; i < 3; ++i) player.pos[i] = closest ? closest->pos[i] : restartFallback_[i];
        if (closest) player.setHeading((int16_t)(closest->heading * 65536 / 360));
        player.placeOnGround(&collision);
        player.dead = false;
        player.health = 127;
        player.resetReaction();
        camera.reset(player.pos, player.heading());
        world.loadAllNow(player.pos[0] / 4096.f, player.pos[1] / 4096.f);
        const uint32_t before = world.timeCycle().time();
        world.timeCycle().advanceFrames(6 * 60 * 30);
        if (world.timeCycle().time() < before) stats.incDaysPassedInGame();   // cTimeCycle::AdvanceTime past midnight
    }
}

void Game::tick() {
    snapshotPoses();
    radio.update(*this);
    ++frame;
    muzzleFlashes.beginTick();
    if (!player.dead && deathFrames_ >= 72 && deathFrames_ < 84) ++deathFrames_;
    localArea.update(*this);   // cPlayer::Think
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
        PadInput_OnFoot(Host_Pad(), mx, my, walk, sprint);   // a controller (padinput.h)
    }
    if (scriptedInput) {
        mx = scriptedMove[0]; my = scriptedMove[1];
        sprint = scriptedState == 3;
        walk = scriptedState == 1;
    }
    bool enter = enterPressed || std::find(scriptedEnterFrames.begin(), scriptedEnterFrames.end(), (int)frame) != scriptedEnterFrames.end();
    enterPressed = false;
    if (enter && !player.dead) {
        if (task.op == CarTask::GotoDoor) task.op = CarTask::None;   // pressing again while walking there cancels
        else enterOrExit();
    }
    bool stickHeld = mx != 0 || my != 0;
    const int16_t inputYaw = controlYaw.on ? controlYaw.yaw : (int16_t)camera.yaw;
    int16_t stickHeading = stickHeld ? (int16_t)(inputYaw + padAngle(mx, my)) : 0;
    if (playerCar >= 0) {   // driving: cPlayer::FillInDrivingYoke (accelerate / brake-reverse / steer / handbrake)
        Vehicle::Controls dc;
        if (Plugins_GameInput()) {
            const Uint8* ks = SDL_GetKeyboardState(nullptr);
            bool gas = ks[SDL_SCANCODE_W] || ks[SDL_SCANCODE_UP];
            bool brake = ks[SDL_SCANCODE_S] || ks[SDL_SCANCODE_DOWN];
            if (gas != brake) dc.throttle = gas ? 0x1000 : -0x1000;
            const HostPad& pad = Host_Pad();
            if (pad.connected && pad.rt > 0.5f && pad.lt > 0.5f) gas = brake = true;
            // the burnout button (cIPhonePad: gBurnoutButton) is accelerate and brake held together: pressed below
            // 2 units a second, it stays down while both are held; the brake stays applied meanwhile
            const bool slow = cars[playerCar].speed() < 0x2000;
            dc.burnout = gas && brake && (burnoutHeld_ || slow);
            burnoutHeld_ = dc.burnout;
            if (dc.burnout) dc.throttle = -0x1000;
            bool l = ks[SDL_SCANCODE_A] || ks[SDL_SCANCODE_LEFT];
            bool r = ks[SDL_SCANCODE_D] || ks[SDL_SCANCODE_RIGHT];
            if (l != r) dc.steer = l ? -0x1000 : 0x1000;
            dc.handbrake = ks[SDL_SCANCODE_SPACE];
            TheSound().horn = ks[SDL_SCANCODE_H] || ks[SDL_SCANCODE_LALT];
            PadInput_Driving(Host_Pad(), dc, TheSound().horn);   // a controller (padinput.h)
            if (dc.burnout) dc.throttle = -0x1000;
        }
        if (scriptedInput) {
            dc.throttle = scriptedDrive * 0x1000; dc.steer = scriptedSteer * 0x1000;
            dc.handbrake = scriptedHandbrakeFrame >= 0 && (int)frame >= scriptedHandbrakeFrame;
            dc.burnout = scriptedBurnoutFrom >= 0 && (int)frame >= scriptedBurnoutFrom && (int)frame < scriptedBurnoutTo;
            if (dc.burnout) dc.throttle = -0x1000;
        }
        if (task.op == CarTask::Braking) {   // cExitCar OP 2: brake against the motion (handbrake when going forward)
            Vehicle& c = cars[playerCar];
            bool back = (int64_t)c.vel[0] * c.fwd[0] + (int64_t)c.vel[1] * c.fwd[1] + (int64_t)c.vel[2] * c.fwd[2] < 0;
            dc = Vehicle::Controls{};
            dc.throttle = back ? 0x1000 : -0x1000;
            dc.handbrake = !back;
        } else if (task.op == CarTask::ClimbOut) dc = Vehicle::Controls{};
        tickTask(false, 0);
        Weapons_PlayerFrame(*this, false, 0);   // the weapon timers keep running in a vehicle
        weaponCycle = 0;
        if (playerCar < 0) { // camera updates once after the exit task, as cPlayer::UpdateCamera does
            camera.update(player.pos, player.heading(), player.vel, &collision);
            finishShortFrame();
            return;
        }
        traffic.update(*this);
        for (int i = 0; i < (int)cars.size(); ++i) cars[i].act(i == playerCar ? dc : traffic.controls(cars[i].uid), i == playerCar, &collision);
        peds.update(*this);
        Vehicle::collideCars(cars, playerCar, frame);
        propDynamics.checkImpacts(*this);
        for (Vehicle& c : cars) { c.integrate(&collision); c.processAlways(); Vehicles_AddSkidmarks(*this, c); c.processDebris(carModel(c.infoId)); c.processDamage(frame); }
        traffic.afterPhysics(*this);
        projectiles.process(*this);
        processVehicleDeaths();
        if (playerCar < 0) {
            finishShortFrame();
            return;
        }
        if (speedScale != 1.f && dc.throttle > 0) {   // mods: the speed changer nudges the driven vehicle's speed
            Vehicle& me = cars[playerCar];
            if (speedScale < 1.f || me.speed() < (int32_t)(30.f * 4096.f * speedScale)) {
                float k = 1.f + (speedScale - 1.f) * 0.04f;
                for (int i = 0; i < 3; ++i) me.vel[i] = (int32_t)(me.vel[i] * k);
            }
        }
        updateWorldObjects();
        TheParticles().update(frame,world.timeCycle().time(),playerCar >= 0 ? cars[playerCar].pos : player.pos);
        TheSkidmarks().process();
        if (cars[playerCar].isBike() && player.hidden) {   // cPed::AnimatePedInVehicle on a bike
            const Vehicle& c = cars[playerCar];
            bool backwards = (int64_t)c.vel[0] * c.fwd[0] + (int64_t)c.vel[1] * c.fwd[1] + (int64_t)c.vel[2] * c.fwd[2] <= 0;
            if (c.bikeLeaning()) player.ride(0xA6, 0xA7, &pedSprites);                        // stopped, a foot down
            else if (c.bikeReversing() && backwards) player.ride(0xA4, 0xA5, &pedSprites);   // pushing it backwards
            else player.ride(0x5C, 0x5D, &pedSprites);
        }
        Vehicle& me = cars[playerCar];
        if (player.hidden) { player.pos[0] = me.pos[0]; player.pos[1] = me.pos[1]; player.pos[2] = me.pos[2]; }
        carCam.update(me, &collision);
        updateView();
        updateSectors();
        updatePopulation();
        sprintHeld_ = false;
        world.tick();
        Stats_PlayerProcess(*this);
        processTimeCycle();
        if (!freeCam.on) world.stream(me.pos[0] / 4096.f, me.pos[1] / 4096.f, 2);
        carGens.update(*this);
        removeFarCars();
        collectSectors();
        TheSound().update(*this);
        Plugins_Tick();
        return;
    }
    {   // cPlayerOnFoot::HandlePlayerSniper: the sniper rifle on foot puts the player in the scope
        const Weapon* current = playerWeapons.current();
        const bool sniping = !player.dead && !player.attached && current && current->type == 18;
        if (sniping && !scope.active) scope.enter(*this);
        else if (!sniping) scope.active = false;
        if (scope.active) {   // the stick moves the aim point, not the player
            scope.update(*this, mx, my);
            stickHeld = false;
        }
    }
    bool busy = player.dead || player.reacting() || tickTask(stickHeld, stickHeading);   // attached to a car / impact response
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
    if (scope.active) {   // cPlayerAsSniper: face the aim point, do not walk (ConstrainWalkSpeed(0, 0))
        const int32_t dx = scope.pos[0] - player.pos[0], dy = scope.pos[1] - player.pos[1];
        in.moving = std::abs(dx) + std::abs(dy) > 0x19A;
        if (in.moving) in.heading = padAngle((float)dx, (float)dy);
        in.maxLevel = 0;
        in.sprint = false;
    }
    if (task.op == CarTask::GotoDoor) in.maxLevel = 2;   // sVirtYoke::ConstrainWalkSpeed
    sprintHeld_ = sprint;
    player.obstacles.clear();
    for (const Vehicle& c : cars) {   // cPed::ConstrainByCollision: still, upright cars are boxes for the player
        if (c.vel[0] || c.vel[1] || c.vel[2] || c.up[2] < 0xFD8) continue;
        Collision::Box b{};
        int32_t cz = c.pos[2] + c.hz;   // GetWorldCollisionPos: the box centre
        b.cx = c.pos[0]; b.cy = c.pos[1]; b.cz = cz;
        b.hx = c.hx; b.hy = c.hy; b.hz = c.hz;
        b.angle = (int16_t)-c.heading();
        player.obstacles.push_back(b);
    }
    {   // fire: Space or the left mouse button (the phone game's fire button, sVirtYoke +0x61);
        // throw: G or the right mouse button (the secondary weapon button: hold for more power)
        bool fire = false, throwing = false;
        if (Plugins_GameInput()) {
            const Uint8* ks = SDL_GetKeyboardState(nullptr);
            const Uint32 mouse = SDL_GetMouseState(nullptr, nullptr);
            fire = ks[SDL_SCANCODE_SPACE] || (mouse & SDL_BUTTON(SDL_BUTTON_LEFT));
            throwing = ks[SDL_SCANCODE_G] || (mouse & SDL_BUTTON(SDL_BUTTON_RIGHT));
            PadInput_Weapons(Host_Pad(), fire, throwing);   // a controller: RT fires, LT throws (hold for power)
        }
        if (scriptedInput) { fire = scriptedFire; throwing = scriptedThrow; }
        Weapons_PlayerFrame(*this, fire && !busy, weaponCycle, throwing && !busy);
        weaponCycle = 0;
    }
    // Prepare movement before dynamic contacts; the shared integration below advances it once.
    if (!busy) player.act(in, &pedSprites);
    else if (player.dead) player.stepOneShot(&pedSprites);
    else if (player.reacting()) player.actReaction(&pedSprites,Pedestrians::vehicleAbove(*this,player));
    traffic.update(*this);
    for (Vehicle& c : cars) c.act(traffic.controls(c.uid), false, &collision);
    const bool wasDead = player.dead;
    peds.update(*this);
    if (!wasDead && player.dead) {
        camera.position(deathEye_);
        deathYaw_ = (uint16_t)camera.yaw; deathPitch_ = camera.pitch();
        deathFrames_ = 0; deathCameraSettled_ = false;
    }
    if (!player.hidden && !player.attached && (!busy || player.reacting())) player.advance(&collision);
    Vehicle::collideCars(cars, -1, frame);
    propDynamics.checkImpacts(*this);
    for (Vehicle& c : cars) { c.integrate(&collision); c.processAlways(); Vehicles_AddSkidmarks(*this, c); c.processDebris(carModel(c.infoId)); c.processDamage(frame); }
    traffic.afterPhysics(*this);
    projectiles.process(*this);
    processVehicleDeaths();
    updateWorldObjects();
    TheParticles().update(frame,world.timeCycle().time(),playerCar >= 0 ? cars[playerCar].pos : player.pos);
    TheSkidmarks().process();
    if (player.dead) updateDeath();
    else if (playerCar >= 0) {
        const Vehicle& c = cars[playerCar];
        std::copy(c.pos, c.pos + 3, player.pos);
        carCam.update(c, &collision); // entry switched the active camera during tickTask
    } else camera.update(player.pos, player.heading(), player.vel, &collision);
    updateView();
    updateSectors();
    updatePopulation();
    world.tick();
    Stats_PlayerProcess(*this);
    processTimeCycle();
    float p[3];
    player.posf(p);
    if (!freeCam.on) world.stream(p[0], p[1], 2);   // (the free camera streams around itself in render)
    carGens.update(*this);
    removeFarCars();
    collectSectors();
    TheSound().horn = false;
    TheSound().update(*this);
    Plugins_Tick();
}

void Game::render(int W, int H) {
    {   // the debug overlay's frame rate, smoothed
        static double previous = 0;
        const double now = OS_TimeAccurate();
        if (previous > 0 && now > previous) fps += ((float)(1.0 / (now - previous)) - fps) * 0.05f;
        previous = now;
    }
    applyInterpolation();
    Plugins_BeginFrame(W, H);   // input, then mods that place their camera before the world is drawn
    WorldCamera cam;
    viewCamera(cam);
    if (freeCam.on) world.stream(cam.eye[0], cam.eye[1], 2);   // also while the game is paused
    world.propLights.clear();
    world.lightPools = renderStyle.lightPools;
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
    projectiles.render(*this);
    {   // cCar::UpdateHeadLights: headlights on outside 07:00 .. 20:00
        bool night = t - 0x7000u >= 0xD000u;
        for (const Vehicle& c : cars) c.renderLights(cam, frame, night, &collision);
        if (renderStyle.headlightPools > 0.f && night) {   // PC render style: the road lit ahead of the car
            std::vector<WorldRenderer::GroundGlow> glows;
            const uint32_t alpha = (uint32_t)std::min(255.f, 75.f * renderStyle.headlightPools);
            for (const Vehicle& c : cars) {
                if (c.dead() || !c.engineOn || !c.hasHeadlights()) continue;
                const float f[2] = {c.fwd[0] / 4096.f, c.fwd[1] / 4096.f}, len = std::sqrt(f[0] * f[0] + f[1] * f[1]);
                if (len < 0.5f) continue;
                const float ax[2] = {f[0] / len, f[1] / len}, reach = c.hy / 4096.f + (c.isBike() ? 4.f : 6.5f);
                const float at[2] = {c.pos[0] / 4096.f + ax[0] * reach, c.pos[1] / 4096.f + ax[1] * reach};
                const float z = collision.ground(at[0], at[1], c.pos[2] / 4096.f + 1.5f).z;
                glows.push_back({{at[0], at[1], z + 0.06f}, {ax[0], ax[1]}, c.isBike() ? 5.f : 7.f, c.isBike() ? 2.2f : 3.4f,
                                 alpha << 24 | 0xFFF2DCu});
            }
            world.drawGlows(glows);
        }
    }
    {   // fountains, tinted by the time cycle's ambient colour (cTimeCycle::Colour(0xD))
        const uint32_t ambient = world.timeCycle().ok() ? world.timeCycle().colour(13) : 0x808080u;
        for (auto& kv : fountains)
            for (auto& f : kv.second) f->render(cam, ambient);
    }
    TheParticles().render(cam);
    traces.render();
    muzzleFlashes.render();
    fires.render(cam);
    if (playerCar < 0) FlameThrower_RenderJet(*playerWeapons.current());   // cFlameThrower::Render
    if (playerCar < 0) Tazer_RenderWire(*playerWeapons.current());          // cTazer::Render
    Weapons_RenderThrowArc(*this);
    targeting.render(*this);
    glEnable(GL_DEPTH_TEST);
    const float characterAlpha = interpolate && interp_.valid ? renderAlpha : 1.f;
    peds.render(&pedSprites, &light, characterAlpha);
    if (!player.hidden) {
        auto pose=player.renderPose(&pedSprites,0,13,CTW_CHARACTER_PLAYER,characterAlpha);
        if (player.attached && task.car >= 0 && task.car < (int)cars.size()) {
            const Vehicle& car = cars[task.car];
            const float angle = task.heading * 3.14159265f / 32768.f;
            const float s = std::sin(angle), c = std::cos(angle);
            for (int i=0;i<3;++i) {
                pose.attachment_basis[i]=(car.right[i]*c-car.fwd[i]*s)/4096.f;
                pose.attachment_basis[3+i]=(car.right[i]*s+car.fwd[i]*c)/4096.f;
                pose.attachment_basis[6+i]=car.up[i]/4096.f;
                pose.attachment_anchor[i]=pose.position[i];
            }
            pose.flags |= CTW_CHARACTER_ATTACHMENT_FRAME;
            pose.attachment_vehicle = car.uid;
        }
        if (!Plugins_RenderCharacter(pose)) player.render(&pedSprites, &light);
    }
    if (playerCar >= 0 && cars[playerCar].isBike() && player.hidden) {   // riding: drawn where the bike says
        int32_t up[3], legs[3];
        cars[playerCar].riderRenderPos(up, legs);
        auto pose=player.renderPose(&pedSprites,0,13,CTW_CHARACTER_PLAYER|CTW_CHARACTER_RIDING,characterAlpha);
        for(int i=0;i<3;++i)pose.position[i]=legs[i]/4096.f;
        pose.heading=-cars[playerCar].heading()*360.f/65536.f;
        cars[playerCar].riderRenderBasis(pose.attachment_basis);
        for (int i=0;i<3;++i) pose.attachment_anchor[i]=up[i]/4096.f;
        pose.flags |= CTW_CHARACTER_ATTACHMENT_FRAME;
        pose.attachment_vehicle = cars[playerCar].uid;
        if (!Plugins_RenderCharacter(pose)) player.renderRiding(&pedSprites, &light, up, legs, cars[playerCar].heading());
    }
    float p[3];
    player.posf(p);
    if (showDebug) { collision.debugDraw(p[0], p[1], 40.f); roads.debugDraw(p[0], p[1], 80.f); }
    if (scope.active && playerCar < 0) scope.renderScope(W, H);   // cSniperCam::Set's scope model
    glDisable(GL_DEPTH_TEST);
    applyGrade(W, H);

    Hud_Begin(W, H);
    if (!pauseOpen) gameplayHud.render(*this, W, H);
    if (showDebug) drawDebug(W, H);
    if (!pauseOpen) WeaponHud_Render(*this, W, H);   // the game's HUD, under the mods' windows
    Plugins_DrawHud(W, H);
    if (deathFrames_ >= 60 && deathFrames_ < 84) {
        int alpha = deathFrames_ < 72 ? (deathFrames_ - 60) * 255 / 12 : (84 - deathFrames_) * 255 / 12;
        Hud_Rect(0, 0, (float)W, (float)H, (uint32_t)alpha);   // RRGGBBAA: black with the fade alpha
    }
    radio.renderApp(W, H, *this);
    renderPause(W, H);
    Hud_End();
    restoreInterpolation();
}

void Game::run() {
    const double step = 1.0 / 30.0;   // OS_ApplicationTick: the game logic runs at 30 fps
    double acc = 0, last = OS_TimeAccurate();
    bool running = true;
    while (running) {
        running = Host_PumpEvents();
        while (int k = Host_PopKey()) {
            if (key(k)) continue;
            if (k == SDL_SCANCODE_F || k == SDL_SCANCODE_RETURN) enterPressed = true;
        }
        if (quitRequested) running = false;
        double now = OS_TimeAccurate();
        acc += uiPaused ? 0.0 : (now - last) * std::max(0.f, std::min(gameSpeed, 20.f));
        last = now;
        acc = std::min(acc, 0.25 * std::max(1.f, gameSpeed));   // don't spiral after a stall
        while (acc >= step) { tick(); acc -= step; }
        renderAlpha = gameSpeed > 0.f ? (float)std::min(1.0, acc / step) : 1.f;
        render((int)OS_ScreenGetWidth(), (int)OS_ScreenGetHeight());
        OS_ScreenSwapBuffers();
    }
}

bool Game::key(int k) {
    if (pauseOpen) return pauseKey(k);
    if (WeaponHud_WheelOpen() && WeaponHud_Key(*this, k)) return true;
    if (k == SDL_SCANCODE_F3) { showDebug = !showDebug; return true; }
    if (Plugins_Key(k)) return true;   // (an open mod menu closes on Esc here)
    if (k == SDL_SCANCODE_ESCAPE && !radio.appOpen()) { setPause(true); return true; }
    if (!Plugins_GameInput()) return false;
    if (radio.key(k, *this)) return true;
    if (WeaponHud_Key(*this, k)) return true;
    // PC: the phone game cycles weapons by tapping the HUD weapon icon (sVirtYoke +0x67 / +0x68)
    if (scope.active && k == SDL_SCANCODE_Z) { scope.zoomPressed = true; return true; }   // the scope's zoom button
    if (playerCar < 0 && (k == SDL_SCANCODE_Q || k == SDL_SCANCODE_E)) { weaponCycle = k == SDL_SCANCODE_E ? 1 : -1; return true; }
    return false;
}

// ---- HUD pause screen ---------------------------------------------------------------------------------------
void Game::setPause(bool open) {
    if (open == pauseOpen) return;
    pauseOpen = open;
    uiPaused = open;
    Audio_SetSfxPaused(open);
    if (open) pauseScreen.opened(OS_TimeAccurate());
}

bool Game::pauseKey(int k) {
    // PC bindings for the original pause/resume action; other input stays out of gameplay.
    if (k == SDL_SCANCODE_ESCAPE || k == SDL_SCANCODE_BACKSPACE || k == SDL_SCANCODE_RETURN) {
        TheSound().playHud(326, 127);
        setPause(false);
    }
    return true;
}

void Game::renderPause(int W, int H) { pauseScreen.render(*this, W, H); }

void Game::shutdown() { pauseScreen.shutdown(); WeaponHud_Shutdown(); gameplayHud.shutdown(); radio.shutdown(); Plugins_Shutdown(); }
