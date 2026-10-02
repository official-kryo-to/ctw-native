// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// Pedestrians: ports of cPopulationManager (peds), cWanderPath, cPed::SetPedType / SetMale and cPedInfoManager.
//
// Population (cPopulationManager::Update): at most min(16, 20 x zone ped density x 100 / 7500) random peds live
// around the player. A spawn attempt fires every 1..64 frames (sooner when the player moves fast), unless the vehicle
// schedule fired in the same frame. It rolls a ped type from the zone's PopulationProfile, like traffic, and places
// the ped where a pavement link crosses the circle 60 units out (cPositionConstraints::Reset(network 1)), out of
// sight and clear of others (cLocalAreaKnowledge::GetCreatePosition). Peds more than 85 units away and off screen
// are removed (cPed::ShouldBeDestroyed).
//
// Looks (pedinfo.bin, 32-byte records; cPedInfoManager::Init/GetInfo, cPed::SetPedType): civilians take a subtype
// from the zone's ped makeup (popinfo.bin), are male with probability 60% (cPed::cPed: Rand16Critical(100) > 40)
// unless the zone forces a gender; men use body set 1 and palettes Rand16Critical(5), women body set 0 and
// palettes 30 + Rand16Critical(5). Records with fixed palettes (gangs, special peds) use them, minus one. Cops use
// the SetPedType palettes for their subtype.
//
// Walking (cWanderPath): the pavement network of each world sector (Collision::PedPaths). The target is the next
// node plus up to +-0.75 units of random offset per axis (Rand16Critical(101)); within 0.5 units the ped picks a
// random onward link, not going back unless it must. Bridge nodes on a sector edge continue in the neighbouring
// sector (cSectorNodeData::ResolveBridgeNodeId). The ped body is the ported on-foot controller (player.cpp) at
// walking speed.
//
// Speed (cWanderPath::Process): peds walk; joggers (civilian subtype 1) and everyone in rain (time cycle rain
// >= 0x800) run.
//
// Evasive rolls (cWanderPath::HandleEvasiveManeuvers): within 25 units of the player, a ped in the path of the
// player's car moving at 10 units/s or more rolls to a random side of it (cRollOutOfVehicle: cAnimation state 0xB,
// animations 0x3C / 0x3D, 20 units/s with the cAnimation::ApplyPhysics ground drag), then sprints while the player's
// car stays within 25 units, and walks again after that.
//
// Reactions (cPed::GetReactionHelper / HandleReaction): pedinfo.bin gives each ped type and subtype a list of
// {chance, reaction, action} records; the first record for the action (or for any action, 9) whose
// Rand32Critical(100) roll is within its chance decides. Women turn attacking and rolling into fleeing. On foot:
// 1 and 4 flee (cFleeOnFoot), 5 rolls away, 2 (attack) is not ported and flees, 3 needs a vehicle.
// Fleeing (cFleeOnFoot): sprint along the pavement network, at each node taking the link that leads most directly
// away from the threat (cSectorNodeData::sRandomNode::SetBestDir), reaching nodes within 2 units; off the network,
// straight away from it for 200 units. A ped flees until it is removed.
// A vehicle explosion (cExplosionBigVehicle, type 4) damages every ped within 22 units with weapon type 6 (action 3);
// the damage itself (within 15 units, cPed::Damage) is not ported yet, the reaction is.
//
// Not ported yet (approximations are marked in peds.cpp): attractors and doors, talking, mooching, the roll's
// facing quadrants, combat and ped-on-ped collision. Being run over knocks a ped down with the on-ground death
// animation.
#pragma once
#include "player.h"
#include <cstdint>
#include <string>
#include <vector>

class Game;
class PedSprites;
struct PedLight;

class Pedestrians {
public:
    bool init(const std::string& dataDir);   // pedinfo.bin
    bool ok() const { return !records_.empty(); }
    void update(Game& g);                     // once per game frame, after traffic
    void render(const PedSprites* sprites, const PedLight* light) const;
    void clear() { peds.clear(); }

    struct NodeRef {
        int16_t cx = -1, cy = -1;
        uint16_t index = 0;
        bool valid() const { return cx >= 0; }
        bool operator==(const NodeRef& o) const { return cx == o.cx && cy == o.cy && index == o.index; }
    };
    struct Ped {
        Player body;
        uint32_t uid = 0;
        int type = 1, subtype = 0;
        bool male = true;
        NodeRef cur, prev;           // walking from prev towards cur
        int32_t target[2] = {0, 0};  // cWanderPath's cTarget
        int deadFrames = -1;         // >= 0: knocked down
        int stuckFrames = 0;
        bool evading = false;        // cWanderPath +0x6A: rolled away from the player's car, sprints while it is near
        int rollFrames = -1;         // >= 0 while rolling (cRollOutOfVehicle)
        int32_t rollVel[2] = {0, 0}; // units/s
        struct Flee {                // cFleeOnFoot
            bool on = false, straight = false;
            int32_t from[3] = {0, 0, 0};   // the threat
            uint32_t fromCar = 0;          // follows this vehicle while it exists (0: a fixed point)
            int32_t to[2] = {0, 0};        // straight-line target
        } flee;
    };
    std::vector<Ped> peds;
    int maxPeds = 0;
    float densityScale = 1.f;        // PC mod API: scales the target population; 1 = the original

    int alive() const;

    // Record of pedinfo.bin for a type and subtype (nullptr if there is none).
    struct Reaction { uint8_t chance, reaction, action; };
    struct Record {
        uint8_t type, subtype, maleOnly, femaleOnly, legsPalette, upperPalette;
        std::vector<Reaction> reactions;
    };
    const Record* record(int type, int subtype) const;
    void dress(Ped& p, int zoneSex) const;   // cPed::cPed + SetPedType + SetMale (looks only)
    enum ReactionKind { None = 0, Flee = 1, Attack = 2, VehicleReaction = 3, FleeOrGetIn = 4, Roll = 5, Idle = 6 };
    int reactionFor(const Ped& p, int action) const;   // cPed::GetReactionHelper
    // cPed::HandleReaction on foot: the threat is a point, or follows a vehicle (fromCar != 0)
    void react(Game& g, Ped& p, int action, const int32_t from[3], uint32_t fromCar);
    void explosion(Game& g, const int32_t pos[3]);   // peds near an explosion react to it

private:
    std::vector<Record> records_;
    std::vector<uint16_t> firstRecord_;      // per ped type
    uint32_t nextUid_ = 1;
    uint32_t linkCursor_ = 0;

    void spawn(Game& g, int type);
    bool createPos(Game& g, int32_t pos[3], NodeRef& from, NodeRef& to);
    void wander(Game& g, Ped& p);
    bool advance(Game& g, Ped& p);
    void setTarget(Game& g, Ped& p);
    void runOver(Game& g, Ped& p);
    bool evade(Game& g, Ped& p);   // true while the ped evades the player's car
    void startRoll(Ped& p, const int32_t dir[2]);   // dir: unit vector (Q12)
    bool roll(Game& g, Ped& p);                     // true while rolling
    void fleeStep(Game& g, Ped& p);
    // the node at p.cur (resolving a bridge into the neighbouring sector) and the nodes linked to it
    static bool nodeAt(Game& g, const NodeRef& n, int32_t out[3]);
    bool resolveBridge(Game& g, const NodeRef& bridge, NodeRef& out) const;   // cSectorNodeData::ResolveBridgeNode
    // cSectorNodeData::GetRandomNode: best = a Q12 direction to follow (nullptr: random)
    bool nextNode(Game& g, const NodeRef& cur, const int32_t from[2], const int32_t* best, NodeRef& out) const;
    friend struct PedsTestAccess;
};
