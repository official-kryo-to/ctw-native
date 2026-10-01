// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// The game (in progress). Runs the game logic at a fixed 30 frames per second like OS_ApplicationTick /
// cGame::Process, and renders every display frame.
#pragma once
#include "carcam.h"
#include "cargens.h"
#include "followcam.h"
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
    int spawnIndex = 0;                      // F5: next vehicle type to spawn (testing)

    int spawnCar(int infoId, const int32_t pos[3], int16_t heading, int palette = -1);   // index or -1
    CarGenManager carGens;
    RoadNetwork roads;   // ai.bin
    Traffic traffic;
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
    void startEnter();
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
    bool showDebug = false;              // F3: prototype HUD, controls and collision diagnostics (off by default)
    bool scriptedInput = false;          // testing: use scriptedMove instead of the keyboard
    float scriptedMove[2] = {0, 0};
    int scriptedState = 2;               // Player::MoveState while scripted input moves
    int weather = 0;
    std::string dataDir, modsDir;
    bool sprintHeld_ = false;
    int scriptedDrive = 0;               // testing: 1 = hold throttle, -1 = hold brake/reverse
    int scriptedSteer = 0;
    int scriptedHandbrakeFrame = -1;     // testing: hold the handbrake from this frame on               // testing: -1 left, 1 right
    std::vector<int> scriptedEnterFrames;   // testing: press enter/exit on these frames
private:
    std::map<int, std::unique_ptr<Model>> models_;
    RestartTables restart_;
    struct Explosion { int32_t pos[3]; int delay; };
    std::vector<Explosion> explosions_;
    void processVehicleDeaths();
    void killInVehicle(Vehicle& car);
    void updateDeath();
    int deathFrames_ = 0;
    int32_t deathEye_[3] = {}, restartFallback_[3] = {};
    uint16_t deathYaw_ = 0, deathPitch_ = 0;
    bool deathCameraSettled_ = false;
    friend struct GameTestAccess;
};

Game& TheGame();
