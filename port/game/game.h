// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// The game (in progress). Runs the game logic at a fixed 30 frames per second like OS_ApplicationTick /
// cGame::Process, and renders every display frame.
#pragma once
#include "carcam.h"
#include "cargens.h"
#include "followcam.h"
#include "gamehud.h"
#include "localarea.h"
#include "peds.h"
#include "pausescreen.h"
#include "player.h"
#include "radio.h"
#include "roads.h"
#include "traffic.h"
#include "vehicle.h"
#include "gfx/model.h"
#include "gfx/pedsprites.h"
#include "world/collision.h"
#include "world/props.h"
#include "propdynamics.h"
#include "watercannon.h"
#include "weather.h"
#include "wanted.h"
#include "deathscreen.h"
#include "weapons.h"
#include "particles.h"
#include "fire.h"
#include "sniper.h"
#include "stats.h"
#include "projectiles.h"
#include "targeting.h"
#include "world/worldrenderer.h"
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

class Game {
public:
    bool init(const std::string& dataDir, const std::string& modsDir);
    void run();
    void shutdown();

    // one fixed game frame / one display frame
    void tick();
    void render(int width, int height);
    bool key(int scancode);   // game input shared by interactive and scripted runs
    Radio radio;
    GameplayHud gameplayHud;

    WorldRenderer world;
    Collision collision;
    PropLibrary props;                       // street furniture (cDynamicPropManager)
    // The props around the camera: draw = false collects their lights for the world renderer, true draws them.
    void renderProps(const WorldCamera& cam, bool draw);
    void updateCityEmitters();               // cCityEmitters: steam and fountains from the cells around the player
    void updateWorldObjects();               // city emitters, fountains and knocked-over props
    // cPed::Damage's blood: count calls of cGeneralParticleController::AddBlood for a victim at pos
    void addBlood(const int32_t pos[3], int count);
    // cGeneralParticleController::AddSpark / AddSteamCloud
    void addSpark(const int32_t pos[3], const int16_t dir[3], uint8_t life, bool useAngle, int16_t angle, bool jitter,
                  uint16_t colour, uint16_t colourRand, int16_t scale);
    void addSteamCloud(const int32_t pos[3], uint16_t colour, int16_t scale);
    std::map<int, std::vector<class Emitter*>> cityEmitters;   // cell key -> its emitters
    std::map<int, std::vector<std::unique_ptr<Fountain>>> fountains;   // cell key -> its fountains (type 1)
    PropDynamics propDynamics;               // street furniture knocked over by vehicles
    PedSprites pedSprites;
    FollowPedCam camera;
    Player player;
    std::vector<VehicleInfo> vehicleInfos;   // resource 1994
    std::vector<Vehicle> cars;
    int playerCar = -1;                      // the car the player drives (index into cars), -1 = on foot
    FollowCarCam carCam;
    bool enterPressed = false;               // the enter/exit button this frame

    int spawnCar(int infoId, const int32_t pos[3], int16_t heading, int palette = -1);   // index or -1
    bool removeCar(int index);               // maintains task/player indices, releases effects and emits removal
    CarGenManager carGens;
    RoadNetwork roads;   // ai.bin
    Traffic traffic;
    Pedestrians peds;                        // random pedestrians on the pavement network
    Weather weather;                         // weather changes, rain and lightning (weather.cpp)
    WeaponManager playerWeapons;             // the player's cWeaponManager (weapons.cpp)
    BulletTraces traces;                     // cBulletTraces
    MuzzleFlashes muzzleFlashes;             // cMuzzleFlashes
    BloodEmitter* blood = nullptr;           // LocalGeneralParticles' cParticleEmitterBlood
    SparksEmitter* sparks = nullptr;         // its cParticleEmitterSparks
    SteamEmitter* steamClouds = nullptr;     // its cParticleEmitterSteam (AddSteamCloud)
    SkidSmokeEmitter* skidSmoke = nullptr;   // its cParticleEmitterSkidSmoke (AddSkidSmoke)
    void addSkidSmoke(const int32_t pos[3], const int16_t velocity[3], bool dark);   // AddSkidSmoke
    Fires fires;                             // cFire
    SniperScope scope;                       // cPlayerAsSniper / cSniperCam
    Stats stats;                             // gStatsManager (cStatsManager)
    WantedManager wanted;                    // the player's wanted level (cPlayer +0x400, wanted.h)
    DeathScreen deathScreen;                 // Gui::cDeathArrestApp (deathscreen.h)
    static constexpr uint32_t kPlayerOnFoot = 0xFFFFFFFFu;   // Vehicle::lastDamagedBy: the player himself
    // crimes.cpp: cops seeing the player and cWantedManager::Process (from updatePopulation, as cPlayer::Process
    // runs it just before cPopulationManager::Update); the player's crimes against a ped (sDamageInfo type, after
    // the damage), against a cop car (cVehicle::HandlePlayerCrime; sourceCar 0 = the player on foot) and jacking
    void updateWanted();
    void pedCrime(const Pedestrians::Ped& victim, int damageType);
    void copCarCrime(Vehicle& car, int damageType, int damage, uint32_t sourceCar);
    void jackCrime(const Vehicle& car);
    PlayerTargeting targeting;               // the player's auto-aim (cPlayerSensorCone, targeting.cpp)
    Projectiles projectiles;                 // grenades, molotovs, flashbangs, mines and rockets (projectiles.cpp)
    int weaponCycle = 0;                     // input: -1 previous / +1 next weapon this frame
    // mods (PC aiming): fire at this world point instead of along the facing; the upper body turns to it
    struct AimOverride { bool on = false; int32_t target[3] = {0, 0, 0}; } aimOverride;
    bool screenToWorld(float x, float y, int width, int height, float out[3]) const;   // the drawn camera's ray
    // The player's view of the world for the game logic (localarea.h): the frame scheduler, the loaded sectors,
    // the camera frustum and the cLocalAreaKnowledge pavement links.
    FrameScheduler scheduler;
    WorldSectors sectors;
    CameraView view;
    LocalArea localArea;
    FrameScheduler::Schedule pedSchedule, carSchedule;   // cPopulationManager +12 (peds) / +14 (vehicles)
    void updateView();                       // cBaseCam::RecalculateMatrix of the game camera
    void updateSectors();                    // the held sector and cPlayer::UpdateCriticalArea
    void updatePopulation();                 // cPopulationManager::Update
    void collectSectors();                   // cWorld::GarbageCollection: deletes what is in unloaded sectors
    int pedPopulation() const;               // cPlayer::GetPopulation(network 1)
    int carPopulation() const;               // cPlayer::GetPopulation(network 0)
    // cPlayer::GetPopulation(network 0) as sPopStats: the vehicles by their driver's ped type (cVehicle +0x964; 1
    // for a car without one). Returns the total.
    int carPopulationByType(int out[20]) const;
    bool forceFreeVehicle();                 // cGlobalPopulationManager::ForceFreeVehicle(false, false)
    uint16_t cameraYaw() const;
    uint32_t nextUid = 1;
    bool canSee(const int32_t pos[3], float radius) const;   // the drawn camera's frustum (rendering only)
    void spawnGenerated(int infoId, const int32_t pos[3], int16_t heading, int palette);   // cCarGenManager -> CreateRandomCar
    void focus(int32_t out[3]) const;           // the player (or his car)
    void viewCamera(WorldCamera& cam) const;    // the active camera
    void removeFarCars();                       // cVehicle::ShouldBeDestroyed
    void enterOrExit();
    // getting in and out (cGetInVehiclePlayer / cNewGetInVehicle, cExitCarPlayer / cExitCar): the ped walks to the
    // door, the door opens, the get-in animation slides him into the seat; getting out brakes first, opens the door
    // and plays the get-out animation.
    struct CarTask {
        enum Op { None, GotoDoor, OpenDoor, SlideIn, Braking, ClimbOut } op = None;
        int car = -1, seat = 0;
        int32_t off[3] = {0, 0, 0};     // attached offset (car space)
        int16_t heading = 0;            // attached heading (relative to the car)
        int16_t vel[3] = {0, 0, 0};     // cOneShotAnimationTask::AddConstantVelocity, per frame
        int32_t end[3] = {0, 0, 0};     // get out: where the ped ends up (car space)
        bool jacked = false;            // the jack crime has been registered (cNewGetInVehicle::HandleJackCrime)
        bool shuffle = false;           // plan {6, 1}: in by the passenger door, then across to the driver's seat
    } task;
    void startEnter(int target = -1);
    bool doorClear(const Vehicle& c, int seat);   // the world part of cVehicle::IsDoorValid
    void finishEnter(Vehicle& c);                 // cNewGetInVehicle::RunPostEnterSeat for the player
    void startExit();
    bool tickTask(bool stickHeld, int16_t stickHeading);   // true while the task drives the player
    const Model* carModel(int infoId);
    uint32_t frame = 0;
    bool clockRunning = true;
    float speedScale = 1.f;              // mods: player speed on foot and in a vehicle
    float gameSpeed = 1.f;               // mods: simulation speed (the clock follows it)
    float renderDistance = 120.f;        // units of city loaded around the camera (the game: one block ring)
    void setRenderDistance(float units);
    void shakeNearExplosion(const int32_t pos[3]);
    void cameraShake(float out[3]) const; // interpolated world-space offset for custom cameras
    void addPropExplosion(const int32_t pos[3], int16_t scale = 4096); // Smash2's generic type-zero blast
    // Explosion::AddExplosion for the weapons: 0 grenade, 1 mine, 3 rocket (all cExplosionGeneric),
    // 2 cExplosionMolotov, 5 cExplosionFlashBang
    void addWeaponExplosion(const int32_t pos[3], int type, int16_t scale = 4096);
    // Explosion::AddExplosion for types 6 (big vehicles) and 7 (petrol pumps): a cExplosionBigVehicle, whose
    // clouds drift along the velocity when it is faster than about 0.6 units a second
    bool addBigExplosion(const int32_t pos[3], int type, int16_t scale, const int32_t velocity[3]);   // false: a duplicate
    // Explosion::AddExplosion for type 11: a cExplosionHotDog, a gas jet along the (normalised) velocity, or
    // straight up when the velocity is tiny, that bursts after about two seconds
    void addHotDogExplosion(const int32_t pos[3], const int32_t velocity[3]);
    // Explosion::AddExplosion for types 8 and 9: a cExplosionWaterSplashPed. sourceMass: the mass of the physical
    // entity that fell in (scale 4 x mass, at most 3), or -1 for none (scale 2 for type 8, 1 for type 9). Nothing
    // happens while ten splash emitters are alive.
    void addWaterSplash(const int32_t pos[3], int type, const int32_t velocity[3], int32_t sourceMass);
    void enableExplosionRadiusCheck(bool enable) { explosionRadiusCheck_ = enable; } // native script switch; off at Reset
    struct FreeCamera { bool on = false; WorldCamera cam; } freeCam;   // mods: replaces the game camera
    bool uiPaused = false;               // a full-screen app (the radio) has paused the game
    // The original HUD pause screen, opened by the PC binding Esc / controller Start.
    bool pauseOpen = false;
    PauseScreen pauseScreen;
    bool quitRequested = false;
    void setPause(bool open);
    bool pauseKey(int scancode);
    void renderPause(int W, int H);
    bool interpolate = true;             // PC: draw between the last two 30 Hz ticks (interpolation.cpp)
    float renderAlpha = 1.f;             // how far the display frame is between them, 0..1
    struct ControlYaw { bool on = false; int16_t yaw = 0; } controlYaw;  // mods: on-foot input relative to this yaw
    float fovOverride = 0.f;             // mods: vertical field of view in degrees, 0 = the camera's own
    // mods: the look of the picture (CtwRenderStyle); the defaults are the game's own (postfx.cpp)
    struct RenderStyle {
        float lightPools = 0.f, headlightPools = 0.f, tint[3] = {1.f, 1.f, 1.f};
        float sepia = 0.f, saturation = 1.f, contrast = 1.f, brightness = 0.f, vignette = 0.f;
        bool graded() const {
            return sepia > 0.f || saturation != 1.f || contrast != 1.f || brightness != 0.f || vignette > 0.f ||
                   tint[0] != 1.f || tint[1] != 1.f || tint[2] != 1.f;
        }
    } renderStyle;
    void applyGrade(int width, int height);   // postfx.cpp: the colour grade of renderStyle over the drawn world
    bool showDebug = false;              // F3: the debug overlay and collision diagnostics (off by default)
    float fps = 0.f;                     // display frames per second, smoothed (the debug overlay)
    bool scriptedInput = false;          // testing: use scriptedMove instead of the keyboard
    float scriptedMove[2] = {0, 0};
    int scriptedState = 2;               // Player::MoveState while scripted input moves
    bool scriptedFire = false;           // testing: hold the fire button
    bool scriptedThrow = false;          // testing: hold the throw (secondary weapon) button
    std::string dataDir, modsDir;
    bool sprintHeld_ = false;
    bool burnoutHeld_ = false;           // the PC's burnout button (accelerate + brake) was down last frame
    int scriptedDrive = 0;               // testing: 1 = hold throttle, -1 = hold brake/reverse
    int scriptedSteer = 0;               // testing: -1 left, 1 right
    int scriptedHandbrakeFrame = -1;     // testing: hold the handbrake from this frame on
    int scriptedBurnoutFrom = -1, scriptedBurnoutTo = -1;   // testing: hold the burnout button over these frames
    std::vector<int> scriptedEnterFrames;   // testing: press enter/exit on these frames
private:
    std::map<int, std::unique_ptr<Model>> models_;
    RestartTables restart_;
    struct Explosion {
        int32_t pos[3]; int delay; int type = 4; bool fired = false; int16_t scale = 4096;
        // cExplosionBigVehicle (types 6 and 7): +0xA0 counter, +0xA4 state, +0xAC heading, +0xAE moving
        int32_t counter = 0; uint8_t state = 0; int16_t heading = 0; bool moving = false;
        int16_t dir[3] = {0, 0, 0}; int sfx = -1;   // cExplosionHotDog (type 11): +0xAC jet direction, +0xA6 sound
    };
    std::vector<Explosion> explosions_;
    bool explosionRadiusCheck_ = false;
    bool queueExplosion(const Explosion& explosion);
    void processVehicleDeaths();
    void processKnockOffs();             // cVehicle::KnockOffDriver for the vehicles that asked for it
    void processVehicleReactions();      // the occupants' reactions to collision damage (Vehicle::hitBy)
    void affectExplosion(const Explosion& explosion);
    void affectBlast(const struct ExplosionBlast& blast);   // cBaseExplosion::AffectEntities
    void processBigExplosion(Explosion& explosion);         // cExplosionBigVehicle::Process
    void processHotDogExplosion(Explosion& explosion);      // cExplosionHotDog::Process
public:
    // The end of cVehicle::Damage: GetReaction(cSimpleMover::GetAction(info), source) for the vehicle's driver, for
    // damage of sDamageInfo type `type` (a collision, 8, only from gAggBigHit = 12 on). The source is the player
    // (sourceCar 0) or a vehicle (its uid); at: where the threat is.
    void vehicleReaction(Vehicle& car, int type, int damage, uint32_t sourceCar, const int32_t at[3]);
private:
    void processSplashExplosion(Explosion& explosion);      // cExplosionWaterSplashPed::Process
    unsigned explosionParticleCount(const int32_t pos[3]) const;
    void finishShortFrame();             // the effects, sound and plugin updates of a frame that ends early
    void processTimeCycle();
    void drawDebug(int width, int height);   // F3 (debughud.cpp)
    void killInVehicle(Vehicle& car);
    void updateDeath();
    int deathFrames_ = -1;          // frames since the player died (the death paths set it to 0 with the camera)
    bool deathStarted_ = false;     // updateDeath has seen this death
    int wastedFrames_ = 0;          // HUDText's busted / wasted text: frames left
    std::string wastedText_;        // e_main.gxt 0x62
    int respawnFrame() const { return deathScreen.ok() ? 62 : 72; }   // the frame the player is reborn on
    int32_t deathEye_[3] = {}, restartFallback_[3] = {};
    uint16_t deathYaw_ = 0, deathPitch_ = 0;
    bool deathCameraSettled_ = false;
    // render interpolation (interpolation.cpp)
    struct Pose { int32_t pos[3]{}; int16_t right[3]{}, fwd[3]{}, up[3]{}; };
    struct Applied {
        enum Kind { Car, Player, Ped } kind;
        uint32_t uid = 0;
        Pose saved{}, set{};
        bool basis = false;
    };
    struct Interp {
        int32_t shake[3]{};
        int shakeCamera = -1;
        bool shakeValid = false;
        std::map<uint32_t, Pose> cars, peds;
        int32_t player[3]{};
        WorldCamera camera;
        bool cameraValid = false, valid = false;
        std::vector<Applied> applied;
    } interp_;
    WorldCamera renderCam_;
    bool renderCamValid_ = false;
    void snapshotPoses();
    void applyInterpolation();
    void restoreInterpolation();
    bool poseTarget(const Applied& a, int32_t*& pos, int16_t*& r, int16_t*& f, int16_t*& u);
    friend struct GameTestAccess;
};

Game& TheGame();
