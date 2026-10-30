// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// cGarageManager and cGarage: the city's garages (garages.dat, 16 bytes each until the end: i32 x, y, z, i16 heading,
// u16 door prop). A garage's door is the world's kind-1 prop of that model whose x + y is at most a unit past its
// front area's (the garage's position plus 4.8 units along its heading, 0.5 up), the first such garage in the file
// (cGarageManager::GiveGarageHandleToThisDoor, from cDynamicPropManager::SpawnProp). cGarage::Process puts the door
// at the garage's whole-unit height plus its own (0 shut .. 3 open) every frame: opening (Open) it rises 0.2 a frame
// with sound event 0x58, and once it has reached 2.8 the WhenOpen callback runs and it stays at 3, open; closing it
// comes down 0.2 a frame. An open garage's door does not collide (cSimpleMover::SetCollidable).
#pragma once
#include "world/collision.h"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class Garages {
public:
    enum State { kOpen = 0, kClosing = 1, kShut = 2, kOpening = 3 };   // cGarage +0x10
    bool load(const std::string& dataDir);    // cGarageManager::Init
    int count() const { return (int)garages_.size(); }
    void process();                           // cGarage::Process, each garage once a frame
    int closest(const int32_t pos[3]) const;  // cGarageManager::GetGarageClosestToPoint, -1 if there are none
    void open(int i);                         // cGarage::Open
    void close(int i);                        // cGarage::Close
    void whenOpen(int i, std::function<void()> fn);   // cGarage::WhenOpen
    State state(int i) const { return i >= 0 && i < count() ? garages_[i].state : kShut; }
    // A kind-1 world prop: the garage it is the door of (-1 if none) and where cGarage::Process puts it
    int doorOf(const Collision::Prop& p) const;
    int32_t doorZ(int i) const;
    bool doorCollides(int i) const { return state(i) != kOpen; }
    void position(int i, int32_t out[3]) const;

private:
    struct Garage {
        int32_t pos[3];
        int16_t heading;
        uint16_t doorProp;    // +0x32
        int16_t baseZ;        // +0x36: the z in whole units
        int16_t height = 0;   // +0x30
        State state = kShut;
        int32_t front[2];     // the front area's centre
        std::function<void()> whenOpen;
    };
    std::vector<Garage> garages_;
};
