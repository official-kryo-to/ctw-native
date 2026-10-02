// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// The game (in progress). Runs the game logic at a fixed 30 frames per second like OS_ApplicationTick /
// cGame::Process, and renders every display frame.
#pragma once
#include "carcam.h"
#include "cargens.h"
#include "followcam.h"
#include "peds.h"
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

    WorldRenderer world;
    Collision collision;
    PropLibrary props;                       // street furniture (cDynamicPropManager)
    // The props around the camera: draw = false collects their lights for the world renderer, true draws them.
    void renderProps(const WorldCamera& cam, bool draw);
    void updateCityEmitters();               // cCityEmitters: steam and fountains from the cells around the player
    void updateWorldObjects();               // city emitters, fountains and knocked-over props
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
    uint16_t cameraYaw() const;
    uint32_t nextUid = 1;
    bool canSee(const int32_t pos[3], float radius) const;   // cBaseCam::CanBeSeenByAnyPlayer
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
    } task;
    void startEnter(int target = -1);
    void startExit();
    bool tickTask(bool stickHeld, int16_t stickHeading);   // true while the task drives the player
    const Model* carModel(int infoId);
    uint32_t frame = 0;
    bool clockRunning = true;
    float speedScale = 1.f;              // mods: player speed on foot and in a vehicle
    float gameSpeed = 1.f;               // mods: simulation speed (the clock follows it)
    float renderDistance = 120.f;        // units of city loaded around the camera (the game: one block ring)
    void setRenderDistance(float units);
    struct FreeCamera { bool on = false; WorldCamera cam; } freeCam;   // mods: replaces the game camera
    bool uiPaused = false;               // a full-screen app (the radio) has paused the game
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
    std::string dataDir, modsDir;
    bool sprintHeld_ = false;
    int scriptedDrive = 0;               // testing: 1 = hold throttle, -1 = hold brake/reverse
    int scriptedSteer = 0;               // testing: -1 left, 1 right
    int scriptedHandbrakeFrame = -1;     // testing: hold the handbrake from this frame on
    std::vector<int> scriptedEnterFrames;   // testing: press enter/exit on these frames
private:
    std::map<int, std::unique_ptr<Model>> models_;
    RestartTables restart_;
    struct Explosion { int32_t pos[3]; int delay; };
    std::vector<Explosion> explosions_;
    void processVehicleDeaths();
    void finishShortFrame();             // the effects, sound and plugin updates of a frame that ends early
    void processTimeCycle();
    void drawDebug(int width, int height);   // F3 (debughud.cpp)
    void killInVehicle(Vehicle& car);
    void updateDeath();
    int deathFrames_ = 0;
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
