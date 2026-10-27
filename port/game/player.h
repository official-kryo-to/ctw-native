// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// The player on foot: a port of the cPed / cPlayer movement code, in the game's own fixed point (20.12 units,
// angles 0x10000 = 360°, headings clockwise from +y).
//
// Setup (cWorld::AddPlayer -> cPed::SetPedType(13, 0), pedinfo.bin has no override for it): body set 1,
// palettes 4 (upper body) and 5 (legs); collision radius 0x1400 (cPed::Reset), sphere radius x0.7.
//
// Each game frame (cPed::Process -> cPlayer::Act -> cPed::Act, then cPed::Integrate):
//   UpdateSpeed  - speed level 0..3 (stand / walk / run / sprint) ramps up one step every PED_BASE_ACCELFRAME
//                  (8) frames while the stick is held, drops one step every 4 frames when released, and resets
//                  to 0 when the wanted direction is more than 19000 (~104°) off the facing. Without the sprint
//                  button the player tops out at run; sprint lasts 10 frames after it is released.
//   CurrentSpeed - PED_BASE_SPEED (0.2) x level, per frame (x30 per second).
//   velocity     - along the current facing (projected onto the ground slope) - only while on the ground.
//   TurnTo       - the facing turns towards the wanted heading by at most 4000 per frame.
//   Integrate    - ConstrainByCollision: the sphere is swept along the move against the boxes, cylinders and
//                  triangles of its collision cell (3 slide iterations), steps down onto ground below
//                  (1.5 units), and is pushed out of overlaps; then GetGround decides on-ground / gravity
//                  (-0x1ED0 per frame).
//   AnimateWalkRunCycle - the sprite pair for the level, stepped by CurrentSpeed x 0x88 >> 16.
//   AnimateWalkRunCycleWeapons - the held weapon replaces the upper-body animation while it fires or reloads
//                  (and for some weapons always); weapons.cpp fills in weaponType / weaponState / weaponFlags.
// Not ported: swimming (the player won't step into water), jumping/diving, melee and the fight idle.
#pragma once
#include <cstdint>
#include <vector>
#include "world/collision.h"
#include "ctw_types.h"

class PedSprites;
struct PedLight;

class Player {
public:
    // input for one frame (the sVirtYoke fields the on-foot controller fills in)
    struct Input {
        bool moving = false;        // yoke +0xAF: the stick is held
        int16_t heading = 0;        // yoke +0x20: wanted heading (camera yaw + stick angle)
        bool sprint = false;        // yoke +0x6B: the sprint button is held
        bool sprintReleased = false;// yoke +0x6C
        uint8_t maxLevel = 3;       // yoke +0xAD (3 for the player; a PC walk key lowers it to 1)
        bool taskAnimation = false; // yoke +0xA9/+0xAA: a task supplies sprite cycles instead of locomotion
    };

    int32_t pos[3] = {-1000 * 4096, -1590 * 4096, 0};   // feet
    int32_t vel[3] = {0, 0, 0};                        // units per second (cSimpleMover +0xB8)
    float speedScale = 1.f;                            // mods: multiplies the on-foot speed
    int16_t fwd[3] = {0, 0x1000, 0};                   // facing (cEntity +0x42)
    int16_t groundNormal[3] = {0, 0, 0x1000};          // cPed +0x324
    int bodySet = 1, palUpper = 4, palLegs = 5;
    bool blockedByWater = false;

    void placeOnGround(Collision* col);
    void update(const Input& in, Collision* col, const PedSprites* sprites);
    // Act, dynamic contacts, then Integrate: contacts must not be overwritten by walking velocity.
    void act(const Input& in, const PedSprites* sprites);
    void advance(Collision* col, bool avoidWater = true);
    void dynamicContact(const int32_t normal[3], const int32_t otherVelocity[3], int32_t depth = 0);
    bool vehicleImpact(const class Vehicle& vehicle, const int32_t normal[3], bool isPlayer);
    // cPed::Damage / cPlayer::FixDamageForArmour, then SetHealth. Run-over damage (native kinds 8/9)
    // bypasses the scale and armour; vehicleImpact has its own matching path. True if it killed.
    bool takeDamage(int damage, bool bypassArmour = false);
    void setArmour(unsigned amount);
    void giveArmour(unsigned amount);
    unsigned reduceArmour(unsigned damage); // unabsorbed damage, including double-armour adjustment
    unsigned fixDamageForArmour(unsigned damage);
    void setHasDoubleArmour(bool enabled);
    void setDamagePercentScale(unsigned percent); // native cPed stores the low byte
    unsigned damagePercentScale() const { return damagePercentScale_; }
    void resetDamageState(); // health / armour part of cPlayer::Reset, including reward state
    bool explosionHit(int damage, const int32_t direction[3]); // effect 2: health first, then a high knockdown
    // cPed::Damage for a water jet (sDamageInfo type 13, no damage, eEffect 3): ApplyDamageResponse's state-14
    // knockdown, unless a reaction is already playing. True when it started one.
    bool waterHit(const int32_t direction[3]);
    // cPed::Damage for being knocked off a vehicle (sDamageInfo type 21, one point, eEffect 1): state 6, thrown at 20
    // units a second along direction (one time in eight with a hop of up to 14 units a second). True when it started.
    bool knockedOff(const int32_t direction[3]);
    bool reacting() const { return reaction_ >= 0; }
    // true while a thrown ped (state 7) is on the ground past its held frame: cAnimation::HandleFrameResets calls
    // cAudioManager::PedThrownBackSfx then
    bool actReaction(const PedSprites* sprites, bool vehicleAbove = false);
    void resetReaction();
    int health = 127, armour = 0; // cPed::Reset / cPlayer::Reset; each bar has a maximum of 127
    // the current weapon as cPed::AnimateWalkRunCycleWeapons sees it (cWeapon Type / +0xC state / +0x10 flags)
    int weaponType = 0, weaponState = 0;
    uint32_t weaponFlags = 0;
    int16_t fireDir[3] = {0, 0x1000, 0};   // cPed +0x32A (FireDirection)
    bool upperAim = false;                 // cPed::Render draws the upper body along fireDir (+0x338 bit 30)
    int16_t weaponMovementScale = 4096;     // selected cWeaponInfo +0x34/+0x36 for the current fire input
    uint8_t punch = 0;
    int throwPower = 0;                    // cPed +0x1E0: the thrown weapon's power, 0..128 (StandardConvertFromPower)                     // cPlayer +0x118D: which hand punches next (GetMeleeAnim)
    bool playerControlled = true; // cPlayer's extra static slide/grace behavior
    // cPed::ConstrainByCollision (player): still, upright vehicles nearby act as extra boxes (set by the game)
    std::vector<Collision::Box> obstacles;
    void render(const PedSprites* sprites, const PedLight* light) const;

    // Attached to a vehicle (cAttachedManager, while getting in or out): the game places the ped every frame and
    // plays one-shot animations (cOneShotAnimationTask: upper body + legs, optionally flipped).
    bool attached = false;
    bool hidden = false;   // inside a vehicle
    bool dead = false;
    bool invulnerable = false;   // PC: a mod's puppet (another player's character) ignores damage and knockdowns here
    void setHeading(int16_t h);
    void playOneShot(int upper, int legs, bool flip, bool preserveSpeed = false); // body-set-0 animation ids
    bool stepOneShot(const PedSprites* sprites);        // true when both halves have finished
    // cPed::AnimatePedInVehicle on a bike: loop this pair (switching keeps the phase), one step a frame
    void ride(int upper, int legs, const PedSprites* sprites);
    // cPed::Render in a vehicle with GetPedRenderPos: the legs and upper body drawn at their own points with the
    // seated heights (cPedSprite pose 1: upper 1.0 / 0.75, legs 0.5 / 0.3)
    void renderRiding(const PedSprites* sprites, const PedLight* light, const int32_t upper[3], const int32_t legs[3], int16_t heading) const;

    int16_t heading() const;          // cEntity::Heading
    int16_t spriteHeading() const {
        const int offset=animationRotation_==1 ? 0x4000 : animationRotation_==2 ? -0x4000 : animationRotation_==3 ? 0x8000 : 0;
        return (int16_t)(heading()+offset);
    }
    void setAnimationRotation(uint8_t rotation) { animationRotation_=rotation&3; }
    int level() const { return level_; }
    bool onGround() const { return onGround_; }
    int walkFrame() const;            // cSoundEvents chooses the upper or legs cycle for the held weapon
    bool enteredUpperFrame(int frame) const { return (frameUpper_ >> 8)==frame && previousUpperFrame_==frame-1; }
    void posf(float out[3]) const { for (int i = 0; i < 3; ++i) out[i] = pos[i] / 4096.f; }
    int32_t currentSpeed() const;     // cPed::CurrentSpeed, units per second (20.12)
    CtwCharacterPose renderPose(const PedSprites* sprites, uint32_t id, int type, uint32_t flags, float alpha = 1.f) const;

    static constexpr int32_t kRadius = 0x1400;                          // cPed::Reset
    static constexpr int32_t kSphere = (int32_t)((0x1400LL * 0xB33) >> 12);   // GetStaticCollsiionRadius

private:
    void updateSpeed(const Input& in);
    void turnTo(int16_t target, int16_t maxStep);
    void integrate(Collision* col);
    void constrainByCollision(Collision* col, int32_t moveLen, bool& walkable, bool& hit);
    void animate(const PedSprites* sprites);
    bool fightIdle(int& upper, int& legs);
    void reactionPose(int state);

    uint8_t level_ = 0, counter_ = 0, exhaustion_ = 0;   // cPed +0x357, +0x35A, +0x365
    bool slowing_ = false;                                // +0x338 bit 25
    bool onGround_ = true;                                // +0x338 bit 9
    uint8_t airFrames_ = 0, stuckFrames_ = 0;             // +0x36E, +0x36D
    int32_t clearance_ = 0, lastPos_[3] = {0, 0, 0};     // +0x320, +0x2F8
    int animUpper_ = -1, animLegs_ = -1, frameUpper_ = 0, frameLegs_ = 0;
    int previousUpperFrame_ = -1; // cPedSprite's previous integer frame, used by melee sound cues
    uint8_t fightIdleFrames_ = 0; // cPlayer +4497
    bool flip_ = false, doneUpper_ = false, doneLegs_ = false;
    uint8_t animationRotation_ = 0; // native sprite rotation, independent of collision/AI facing
    uint32_t oneShotTicks_ = 0;
    int reaction_ = -1, reactionTicks_ = 0;
    uint32_t reactionAge_ = 0;
    uint32_t recoveryAge_ = 0;
    bool reactionForward_ = false;
    uint8_t damagePercentScale_ = 100;
    bool doubleArmour_ = false, armourHalfTick_ = false;
    friend struct PlayerTestAccess;
};
