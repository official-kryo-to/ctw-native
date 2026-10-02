// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// Cars: a port of cPhysical + cVehicle + cWheeledVehicle + CTyre + CEngine and the static-world part of
// cPhysicalIntegrator, in the game's fixed point (20.12 units, Q12 vectors, angles 0x10000 = 360°).
//
// Data: the vehicle info table (resource 1994, 0x138-byte records) - SetPropertiesFromVehicleInfo:
//   +0x28 +0x2A +0x2C  width, length, height (collision box half-size = width*1.1, length*1.1, height)
//   +0x2E..+0x32       centre of gravity (local)       +0x34 drive split (front)  +0x36 brake split (front)
//   +0x38 wheel radius  +0x3A front axle y  +0x3C rear axle y  +0x3E drag
//   +0x40/+0x42 front tyre, +0x44/+0x46 rear tyre, +0x48 grip, +0x4A engine, +0x4C gears, +0x4E top speed
//   +0x50 mass  +0x54 brake force  +0x58/+0x5C tyre params  +0x60 steering lock (degrees)  +0x64 steering rate
//   +0x68 steering reduction with speed  +0x6C engine torque  +0x70 max rpm  +0x74..+0x88 body sway
//
// Each game frame (cVehicle::Process -> cWheeledVehicle::Act -> UpdatePhysics, then cPhysicalIntegrator):
//   UpdateSteering, CEngine::Update (gears, torque curve), CTyre::CalcGroundContact / CalcForces /
//   ProcessTyreVelocity for both axles, CalcForces (tyre forces, drag, gravity), HandleSettling;
//   then RecalcKinematics (forces -> velocities), the swept collision against the world (spheres along the car,
//   the 8 box corners, the ground plane) with impulses (CalcImpactEnv), and springs (FullSpringCollision:
//   the corners float 0.3 above the ground). On flat ground with both axles down the car switches to "simple"
//   physics (no gravity, upright), like the game.
// Not ported yet: boat/helicopter/tank controllers, full vehicle-water behavior and pedestrian population.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

class Collision;
struct Model;
class SmokeEmitter;
class FireEmitter;

struct VehicleInfo {   // one record of resource 1994
    uint8_t raw[0x138];
    int type() const { return raw[0] | raw[1] << 8; }
    int model() const { return raw[2] | raw[3] << 8; }
    uint32_t paletteMask() const { return raw[4] | raw[5] << 8 | raw[6] << 16 | (uint32_t)raw[7] << 24; }
    std::string name() const;
    int16_t s16(int off) const { return (int16_t)(raw[off] | raw[off + 1] << 8); }
    int32_t s32(int off) const { return (int32_t)(raw[off] | raw[off + 1] << 8 | raw[off + 2] << 16 | (uint32_t)raw[off + 3] << 24); }
};
bool LoadVehicleInfos(std::vector<VehicleInfo>& out);   // needs Assets_Open

class Vehicle {
public:
    struct Controls { int32_t throttle = 0, steer = 0; bool handbrake = false; };   // sVirtYoke +0x38, +0x3C, +0x60

    void init(const VehicleInfo& info, int infoId, const int32_t pos[3], int16_t heading, int palette);
    void act(const Controls& c, bool playerDriving, Collision* col);   // cVehicle::Process / cWheeledVehicle::Act
    void integrate(Collision* col);                                      // cPhysicalIntegrator::Process (this car)
    void processAlways();                                                // cWheeledVehicle::ProcessAlways (body sway)
    void render(const Model* model) const;
    // cPhysicalIntegrator::PhysicalToPhysicalProcess for the cars (before their integrate): moving cars against
    // every other car (sphere pairs -> ResolvePhysicalCollision -> CalcImpactCar). Wakes up parked cars it hits.
    static void collideCars(std::vector<Vehicle>& cars, int playerCar, uint32_t frame);

    // doors (cVehicle::OpenDoor / CloseDoor / cVehicleDoor::Update / UpdateDoorMatrix); seats 0..3, 4 = boot
    void openDoor(int seat);
    void closeDoor(int seat);
    bool doorOpen(int seat) const { return doorOpenBits_ >> seat & 1; }   // +0x944 bits 14..18
    bool hasDoor(int seat) const { return seat >= 0 && seat < 4 && doorNode_[seat] > 0; }
    void doorSpawnPoint(int seat, int32_t out[3]) const;   // cVehicle::GetDoorSpawnPointLocalPosition
    void seatOffset(int seat, int32_t out[3]) const;       // cVehicle::GetSeatOffsetLocalSpace
    void localToWorld(const int32_t local[3], int32_t out[3]) const;   // TransformCoords with the entity matrix
    int numSeats() const { return numSeats_; }

    // damage (cVehicle::DoOnCollisionWorking / Damage / SetHealth / Process / SetDead)
    int health() const { return health_; }        // +0x99D: 255 = new; smoke below 190, burning below 31
    bool dead() const { return dead_; }
    void damage(int amount);                      // cVehicle::Damage (collision damage)
    bool repair();                               // mod API: restore a living vehicle, without resurrecting wrecks
    void teleport(const int32_t pos[3], int16_t heading); // rebuild the physics transform, upright and stationary
    void processDamage(uint32_t frame);           // the damage part of cVehicle::Process
    bool justDied = false;
    void releaseEffects();
    // what the sound code reads (CTyre +0x50 burst / +0x52 wheel spin / +0x53 on the ground, CEngine rpm, the
    // driver's gas input, the last acceleration)
    struct SoundState { bool rearSpin, frontSpin, frontGround, rearGround, burst; int32_t rpm, maxRpm; bool gas, reverseGear; int32_t accel[3]; };
    SoundState soundState() const;
    bool engineOn = false;                        // cWheeledVehicle::TurnEngine
    // lights (cCar::RenderHeadlights, cWheeledVehicle::RenderBrakeLights / RenderIndicators)
    bool railBraking = false;                     // a traffic car slowing down (its yoke brake)
    uint8_t indicators = 0;                       // +0x948 bits 7 / 8: 1 left, 2 right
    void renderLights(const struct WorldCamera& cam, uint32_t frame, bool night, Collision* col) const;                        // the car is being removed: let its smoke fade out                        // set on the frame it blew up (the game ejects / kills occupants)
    int seatUser[4] = {-1, -1, -1, -1};
    bool generated = false;   // cVehicle::SetIsGeneratedCar
    uint32_t uid = 0;         // stable id (the game's entity id byte +0x63 = uid & 0xFF)
    int radioStation = -1;    // station selection stays with the vehicle
    // traffic (cVehicle +0x95C, +0xE9, +0x9C8): VehicleSimpleProximityProcess's nearest obstacle ahead
    int32_t leastCollide = 0x3E8000;
    bool blocked = false;
    int32_t lookAhead = 0x6000;   // Rand32Critical(0x3000) + 0x6000
    bool setLeastCollideDistance(int32_t d, bool physics) {   // cVehicle::SetLeastCollideDistance
        if (d >= leastCollide) return false;
        leastCollide = d;
        blocked = true;
        if (physics) setToPhysics(true);
        return true;
    }
    // on rails (cWanderRoads): upright at (x, y, z) facing dir (Q12 unit), moving at speed along it
    void placeOnRail(int32_t x, int32_t y, int32_t z, const int32_t dir[2], int32_t speed);
    int32_t boundRadius() const;          // cSimpleMover +0x68 (SetCollisionPrimitiveBox: |half size|)
    bool keep = false;        // not removed when far away (the player's car)   // cSeats: who sits where (-2 = the player)

    // entity matrix (cEntity +0x3C): rows right, forward, up (Q12), position +0x50
    int16_t right[3], fwd[3], up[3];
    int32_t pos[3];
    int32_t vel[3] = {0, 0, 0}, angVel[3] = {0, 0, 0};   // +0xB8 (units/s), +0xEC
    int infoId = 0, palette = 26;
    int32_t hx = 0, hy = 0, hz = 0;   // collision box half size (+0xC4)

    int16_t heading() const;
    int32_t speed() const;
    int gear() const { return gear_; }
    bool physicsActive() const { return physics_; }
    bool isBike() const { return bike_; }        // cBike (class 0x2E, vehicle type 1)
    bool bikeLeaning() const;                    // cBike::IsLeaning: stopped with a foot down
    bool bikeReversing() const { return gear_ == -1; }   // cBike::IsReversing
    uint8_t bikeMounted = 1;                     // +0xB94 (set again when a rider gets on)
    // cBike::GetPedRenderPos: where the rider's upper body / legs are drawn (world, Q12)
    void riderRenderPos(int32_t upper[3], int32_t legs[3]) const;
    bool simple() const { return simple_; }
    bool hasHeadlights() const { return infoFlags8e_ >> 8 & 1; }   // HeadLightsOn: info +0x8E bit 8

private:
    struct Tyre {   // CTyre (+0xA00 front, +0xA58 rear)
        int32_t k0, k4, gripMax, grip, axleY, spin, k18, torqueFb;
        int32_t force[3], contact[3], normal[3];
        bool front, burst = false, skid, spinning, onGround = false, slide;
    };
    struct Rec { int32_t p[3]; int32_t n[3]; int32_t t; int32_t depth; int surface; };   // sCollisionRecord

    // cPhysical
    void recalcKinematics();
    void integrateStep(int32_t dt);
    void applyWorldForce(const int32_t p[3], const int32_t f[3], int type);
    void worldCG(int32_t out[3]) const;
    void velocityAt(const int32_t p[3], int32_t out[3]) const;
    void placeUpright(const int32_t p[3], int16_t heading);
public:
    void setToPhysics(bool on);
    void collisionSpheres(int32_t (*out)[4], int& n) const { spheres(out, n); }   // cPhysical::CalcSpheres
private:
    void setToSimple(bool on);
    bool velocityBelow(int32_t tol) const;
    void spheres(int32_t (*out)[4], int& n) const;
    void bboxVerts(int32_t out[8][3]) const;
    void syncFromIntegrator();
    int32_t impactTerm(const int32_t n[3], const int32_t r[3]) const;
    // cWheeledVehicle
    void updateSteering();
    void updateEngine();
    int32_t finalDriveRatio() const;
    void handleGearChange();
    void updateTyres(Collision* col);
    void tyreGroundContact(Tyre& t, Collision* col);
    void tyreForces(Tyre& t);
    void tyreVelocity(Tyre& t);
    void applyTyreForce(Tyre& t);
    void calcForces();
    void handleSettling(Collision* col);
    void updatePhysics(Collision* col);
    // cPhysicalIntegrator
    void fullCollision(Collision* col, bool meshNear);
    void simpleCollision(Collision* col);
    void fullSprings(Collision* col);
    void simpleSprings(Collision* col);
    void calcImpactEnv(const Rec& r, bool friction);
    void springImpact(const Rec& r);
    static bool resolvePair(Vehicle& a, Vehicle& b);
    static bool calcImpactCar(Vehicle& a, Vehicle& b, const Rec& r, bool first);
    void collisionCentre(int32_t out[3]) const;   // +0x1B4

    // physical state
    int32_t cgLocal_[3] = {0, 0, 0};        // +0x11C
    int32_t collOffset_[3] = {0, 0, 0};     // +0x190 (box centre, local)
    int32_t sphereFirst_[3] = {0, 0, 0}, sphereStep_[3] = {0, 0, 0};   // +0x19C, +0x128
    int sphereCount_ = 2;                    // +0x1D8
    int32_t sphereR_ = 0x800;                // +0x1B0
    int32_t mass_ = 0x1000, invMass_ = 0x1000;       // +0x1A8, +0x1AC
    int32_t invInertia_[3] = {0, 0, 0};      // +0x134 (local diagonal)
    float q_[4] = {0, 0, 0, 1};              // +0x140 (x, y, z, w)
    float rot_[3][3];                         // +0x150 rows = local axes in world
    int32_t cgWorld_[3] = {0, 0, 0};         // +0x164
    float invIWorld_[3][3];                   // +0x170
    int32_t force_[3] = {0, 0, 0}, torque_[3] = {0, 0, 0};   // +0x104, +0x110
    bool physics_ = false;                    // +0x1DA
    bool simple_ = false;                     // +0x956
    bool collided_ = false, collidedPrev_ = false;   // +0x1DF, +0x1E0
    bool meshNear_ = false;                   // +0x1DE
    bool playerDriving_ = false;              // +0x1E7
    // cBike
    bool bike_ = false;
    int32_t bikeLean_ = 0;                    // +0xB6C: lean into the turn (radians, Q12)
    int32_t bikePitch_ = 0, bikePitchPrev_ = 0, bikePitchV_ = 0;   // +0xB70 / +0xB74 / +0xB84: wheelie (< 0) / stoppie (> 0)
    int32_t bikePrevVel_[3] = {0, 0, 0};      // +0xB78
    int32_t bikeKick_ = 0;                    // +0xB8C: the lean while stopped with a foot down
    bool bikeThrottle_ = false;               // +0xB93
    bool bikeAbandoned_ = false, bikeFastAbandoned_ = false;   // +0xB90, +0xB91
    void bikeAct(const Controls& y);

    // wheeled vehicle state
    Tyre tyre_[2];
    int32_t weightFront_ = 0x800;             // +0xAB0
    int32_t brakeForce_ = 0, drag_ = 0, steerLock_ = 0, steerRate_ = 0, steerSpeedRed_ = 0;   // +0xAB4..+0xAC4
    int32_t driveSplit_ = 0x800, brakeSplit_ = 0x800, wheelRadius_ = 0x800;   // +0xB18, +0xB24, +0xB1C
    int32_t steerInputAbs_ = 0, throttleAbs_ = 0, steerAngle_ = 0, traction_ = 0, sticky_ = 0x1000;   // +0xAE8 +0xAEC +0xB20 +0xB2C +0xB3C
    uint8_t b62_ = 0;                         // what the driver does: 1 gas, 2 brake, 4 left, 8 right, 0x10/0x40 handbrake
    uint16_t b64_ = 0;
    int16_t handbrakeTimer_ = 0;              // +0xB4E (reversing with the handbrake)
    int16_t handbrakeTimerHb_ = 0;            // +0xB56 (frames the handbrake is held)
    int32_t lowGearGrip_ = 0x1000;            // +0xB48
    uint32_t groundSkip_ = 0, frameCounter_ = 0;   // CTyre::CalcGroundContact's 1-in-4 ground queries in simple physics
    uint32_t groundFrames_ = 0, settleFrames_ = 0;   // +0xB58, +0xB5C
    // engine (+0x9D8)
    int32_t driveForce_ = 0, maxTorque_ = 0, engK10_ = 0, maxRpm_ = 0, rpm_ = 0, topSpeed_ = 0, finalDrive_ = 0;
    int8_t gear_ = 1;
    uint8_t numGears_ = 3, shiftDelay_ = 0;
    int32_t gripBase_ = 0;                    // +0xB28
    // body sway (cWheeledVehicle::UpdateSuspension / UpdateModelMatrix); angles in radians (Q12)
    int32_t accel_[3] = {0, 0, 0};            // +0xF8: the last force x inverse mass (RecalcKinematics)
    int32_t swayRoll_ = 0, swayPitch_ = 0, swayRollV_ = 0, swayPitchV_ = 0;   // +0xAC8, +0xACC, +0xAD0, +0xAD4
    int32_t wobble_ = 0, tilt_ = 0, wobbleV_ = 0, tiltV_ = 0;                 // +0xAD8, +0xADC, +0xAE0, +0xAE4
    int32_t swayK_[6] = {0, 0, 0, 0, 0, 0};  // +0xB00..+0xB14 (info +0x74..+0x88): stiffness, damping, lean
    void updateSuspension();
    void addSkidmarks() const;
    void modelMatrix(float out[16]) const;
    void onCollision(const int32_t force[3], const Vehicle* other);   // OnCollision + DoOnCollisionWorking
    void setDead();
    uint8_t health_ = 0xFF;
    bool dead_ = false;
    int16_t burnTimer_ = 0x1E0;                   // +0x970
    SmokeEmitter* smoke_ = nullptr;         // +0x928 (cSmoke)
    FireEmitter* fire_ = nullptr;
    int16_t smokeLife_ = -1;                      // cSmoke +0xA0
    uint8_t smokeColour_ = 0;                     // cSmoke +0xA2
    int32_t smokeOffset_[3] = {0, 0, 0};          // where it is attached (car space)
    int32_t smokeVel_[3] = {0, 0, 0};             // cSmoke +0xA4
    // doors
    struct Door { uint16_t angle = 0; int8_t speed = 0; uint8_t mode = 0; };   // cVehicleDoor (+0x930 + seat x 4)
    Door doors_[5];
    int8_t doorNode_[5] = {-1, -1, -1, -1, -1};   // info +0x94..+0x97, +0x110 (model node ids)
    int32_t doorOff_[4][2] = {};                   // info +0x98 (local x, y)
    int32_t seatOff_[4][3] = {};                   // info +0xDC
    uint16_t infoFlags8e_ = 0;                     // info +0x8E
    int32_t info_[6] = {};                         // info +0xC4 (rear lamps x, y, z), +0xD0 (headlamps x, y, z)
    uint8_t doorOpenBits_ = 0;
    int numSeats_ = 1;                             // info +0x10C
    void updateDoors();
    void setDoorClosed(int seat);
    friend struct VehicleTestAccess;
    friend class PropDynamics;
};
