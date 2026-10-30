// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// A script's door: cDynDoorProp (WorldImpl::CreateDoor) and cDoorHandler, which walks the player in or out through it.
//
// cDynDoorProp has no model: the building shows the closed door. While it moves, ManagedRender draws the opening in
// the doorway, 0.15 in front of the entity along its facing (cEntity +0x42), as two black triangle strips of the
// global sprite sheet: a quad of rect 20 from the hinge (half the width to the entity's left) across the doorway, as
// wide as it is open and as high as the door, and a triangle of rect 11's centre from the far side's foot to the
// hinge, up to the opened fraction of the height. Progress (+0x4F6) runs from 100 down by the speed a frame: from
// 100 to 50 it opens, from 25 to 0 it closes again; at 100 and 0 nothing is drawn.
#pragma once
#include <cstdint>
#include <functional>
#include <memory>

class Game;

class DoorProp {
public:
    // WorldImpl::CreateDoor(player, open, width, height, hold): the half width and height as cFixed<4,12> (+0x4F8,
    // +0x4FA: the 20.12 values halved), the frames to hold it open before closing by itself (+0x4FC)
    // (its cPropEffectRenderable goes into the effect list, holding it weakly)
    static std::shared_ptr<DoorProp> create(const int32_t pos[3], int heading, int32_t width, int32_t height,
                                            uint8_t hold, bool open);
    DoorProp(const int32_t pos[3], int heading, int32_t width, int32_t height, uint8_t hold);
    void open(bool autoClose);     // cDynDoorProp::Open
    void close(bool autoClose);    // cDynDoorProp::Close
    void setSpeed(int speed) { speed_ = (uint8_t)speed; }   // cDynDoorProp::SetSpeed
    void process();                // cDynDoorProp::Process (a script holds it, so it is never time-sliced)
    void render() const;           // cDynDoorProp::ManagedRender
    void offsetInWorld(const int32_t local[3], int32_t out[3]) const;   // Entity::GetOffsetInWorldCoords
    int32_t pos[3];
    int16_t right[3], fwd[3];      // cEntity +0x3C, +0x42

private:
    int16_t halfWidth_, height_;   // +0x4F8, +0x4FA
    uint8_t hold_;                 // +0x4FC
    uint8_t speed_ = 1;            // +0x4FD
    uint8_t state_ = 0;            // +0x4FE: 0 shut, 1 opening, 2 open, 3 closing, 4 (shut by itself)
    bool autoClose_ = false;       // +0x4FF
    uint8_t progress_ = 100;       // +0x4F6
};

// cDoorHandler (cScriptProcess<cDoorHandler>): the player and a door made for him. Enter: he walks to 2 units in
// front of the door (or is already within 2), stops, turns to 3 units behind it, starts the open-door animation
// as the door opens (sfx 0x27F), and 5 frames later walks through the wall to that point. The door closes once he
// is within 1 unit of it or 90 frames on, 15 frames later sfx 0x189 plays and he is collidable again; then the
// callback. Leave: he is put 3 units behind it facing out, walks to 2 behind, and the same follows outwards.
class DoorHandler {
public:
    enum Action { kEnter = 0, kLeave = 1 };   // eDoorAction
    // CreateDoor + DoMainStuff, as cDoorHandler::Start and the scripts that inline it do
    void start(Game& game, const int32_t pos[3], int heading, Action action, std::function<void()> done, bool placeInside);
    void tick(Game& game);   // its waits and vicinity checks, once a frame
    bool active() const { return door_ != nullptr || state_ != nullptr; }

private:
    using State = void (DoorHandler::*)(Game&);
    void doMainStuff(Game& game, Action action, bool placeInside);
    void stop();   // cScriptProcessBase::Stop: what it was waiting for is dropped
    void waitThen(int frames, State s) { waitFrames_ = frames; waitState_ = s; }
    void whenNear(const int32_t at[3], int32_t radius, State s);   // Entity::WhenEntersVicinityOf (2D, within r)
    void doTurn(Game& game);
    void finishTurn(Game& game);
    void openDoor(Game& game);
    void finishWalking(Game& game);
    void closeDoor(Game& game);
    void finishClose(Game& game);
    void finished(Game& game);
    void turnTo(Game& game, const int32_t at[3]);   // Entity::TurnTo

    DoorProp* door_ = nullptr;
    int32_t through_[3] = {0, 0, 0};   // +0x104: the point on the far side
    std::function<void()> done_;       // +0x128
    int waitFrames_ = 0;
    State waitState_ = nullptr;
    int32_t near_[2] = {0, 0}, nearR_ = 0;
    State nearState_ = nullptr;
    State state_ = nullptr;            // (only to know it is running)
};
