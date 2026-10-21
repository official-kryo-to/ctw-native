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
// node plus up to +-0.75 units of random offset per axis (Rand16Critical(101)); within 1 unit the ped picks a
// random onward link, not going back unless it must. Bridge nodes on a sector edge continue in the neighbouring
// sector (cSectorNodeData::ResolveBridgeNodeId). The ped body is the ported on-foot controller (player.cpp) at
// walking speed.
//
// Speed (native wander processing): peds walk; joggers (civilian subtype 1) and everyone in rain (time cycle rain
// >= 0x800) run.
//
// Evasive rolls (cWanderPath::HandleEvasiveManeuvers): within 25 units of the player, a ped in the path of the
// player's car moving at 10 units/s or more rolls to a random side of it (cRollOutOfVehicle: cAnimation state 0xB,
// animations 0x3C / 0x3D, 20 units/s with the cAnimation::ApplyPhysics ground drag), then runs while the player's
// car stays within 25 units, and walks again after that. Nearby peds run even if no roll is needed: the
// handler skips the ordinary walk restriction, while Process's existing maximum of run cannot be raised.
// Lost node IDs rejoin the nearest cached pavement link before proceeding. A ped that requests movement but
// remains within one unit of its retained position for 46 ticks receives the native ten-unit, ten-tick goto
// detour. Eligible civilians pause at mooch nodes: a 75% roll every 150 ticks selects a 20..89-tick idle task
// with the original 172/173 animation pair. Talking and attractor tasks still need their world dependencies.
//
// Reactions (cPed::GetReactionHelper / HandleReaction): pedinfo.bin gives each ped type and subtype a list of
// {chance, reaction, action} records; the first record for the action (or for any action, 9) whose
// Rand32Critical(100) roll is within its chance decides. Women turn attacking and rolling into fleeing. On foot:
// 1 and 4 flee (cFleeOnFoot), 5 rolls away, 2 (attack) is not ported and flees, 3 needs a vehicle.
// Fleeing (cFleeOnFoot): sprint along the pavement network, at each node taking the link that leads most directly
// away from the threat (cSectorNodeData::sRandomNode::SetBestDir), reaching nodes within 2 units; off the network,
// straight away from it for 200 units. A ped flees until it is removed.
// Generic type-0/type-4 blasts apply distance-based damage and effect-2 knockdowns to on-foot peds;
// seated occupants are protected from direct blast damage and die through their vehicle instead.
//
// Not ported yet (approximations are marked in peds.cpp): attractors and doors, talking, road-link rejoin
// fallback, the roll's facing quadrants, target-blocked/contact detours and combat. Being run over knocks a ped
// down; central impacts above 25 units/s kill an NPC, while slower damaging hits allow recovery.
#pragma once
#include "player.h"
#include "traffic.h"
#include <cstdint>
#include <string>
#include <vector>
#include <array>

class Game;
class PedSprites;
struct PedLight;

class Pedestrians {
public:
    bool init(const std::string& dataDir);   // pedinfo.bin
    bool ok() const { return !records_.empty(); }
    void update(Game& g);                     // once per game frame, after traffic
    void render(const PedSprites* sprites, const PedLight* light, float alpha = 1.f) const;
    void clear() { peds.clear(); }

    struct NodeRef {
        int16_t cx = -1, cy = -1;
        uint16_t index = 0;
        uint8_t network = 1; // 0: global road node; 1: a sector's pavement node
        bool valid() const { return cx >= 0; }
        bool operator==(const NodeRef& o) const { return network == o.network && cx == o.cx && cy == o.cy && index == o.index; }
    };
    struct Ped {
        Player body;
        uint32_t uid = 0;
        int type = 1, subtype = 0;
        bool male = true;
        uint32_t vehicle = 0;       // stable vehicle uid, never a cars[] index
        int seat = -1;
        bool exiting = false;
        int32_t seatPosition[3]{}, exitTarget[3]{};
        int16_t exitVelocity[3]{};
        NodeRef cur, prev;           // walking from prev towards cur
        int32_t target[2] = {0, 0};  // cWanderPath's cTarget
        int deadFrames = -1;         // >= 0: knocked down
        // The process manager's movement-intent flag and sStuckRecord, retained across AI subtasks.
        bool wantsMovement = false;
        struct Stuck {
            bool tracked = false, stuck = false;
            uint16_t frames = 0;
            int32_t origin[2]{};
        } stuck;
        struct Wander {
            enum Phase { Rejoin, Walk, Mooch } phase = Walk;
            bool initialized = false, joining = false, idleAnimation = false;
            uint16_t socialStart = 0;
            uint32_t idleFrames = 0;
            int32_t joinTarget[2]{};
        } wander;
        struct Goto {             // cGotoTargetOnFootStraightLine's temporary avoidance target
            uint8_t detourFrames = 0;
            int32_t target[2]{};
        } go;
        bool evading = false;        // cWanderPath +0x6A: already rolled away from the nearby player's car
        int rollFrames = -1;         // >= 0 while rolling (cRollOutOfVehicle)
        int32_t rollVel[2] = {0, 0}; // units/s
        bool movementPrepared = false;
        struct Flee {                // cFleeOnFoot
            bool on = false, straight = false;
            int32_t from[3] = {0, 0, 0};   // the threat
            uint32_t fromCar = 0;          // follows this vehicle while it exists (0: a fixed point)
            int32_t to[2] = {0, 0};        // straight-line target
        } flee;
        uint32_t socialFriend = 0;
        bool socialFriendVehicle = false;
        uint16_t talkingCooldown = 0;
        bool ambientDisabled = false;
        struct Talk {
            enum Role { None, Initiator, Responder } role = None;
            int state = 0; // native states: 1 waiting, 10 speaking, 11 listening, 12 finished, 20 approach, 21 ready
            uint32_t partner = 0;
            uint16_t timer = 0, idleTimer = 0;
            uint8_t exchanges = 0, limit = 0;
            int16_t heading = 0;
            int gesture = 0;
            bool approachStarted = false, idle = false, animation = false;
        } talk;
    };
    std::vector<Ped> peds;
    int maxPeds = 0;
    float densityScale = 1.f;        // PC mod API: scales the target population; 1 = the original

    int alive() const;
    static bool vehicleAbove(const Game& g, const Player& body); // cAnimation::NextState's radius-0.75 query

    // Record of pedinfo.bin for a type and subtype (nullptr if there is none).
    struct Reaction { uint8_t chance, reaction, action; };
    struct Record {
        uint8_t type, subtype, maleOnly, femaleOnly, legsPalette, upperPalette;
        std::vector<Reaction> reactions;
        std::array<uint8_t,7> enemies{}, friends{}; // cThreats: masks of native pedinfo record indices
    };
    const Record* record(int type, int subtype) const;
    void dress(Ped& p, int zoneSex) const;   // cPed::cPed + SetPedType + SetMale (looks only)
    enum ReactionKind { None = 0, Flee = 1, Attack = 2, VehicleReaction = 3, FleeOrGetIn = 4, Roll = 5, Idle = 6 };
    int reactionFor(const Ped& p, int action) const;   // cPed::GetReactionHelper
    // cPed::HandleReaction on foot: the threat is a point, or follows a vehicle (fromCar != 0)
    void react(Game& g, Ped& p, int action, const int32_t from[3], uint32_t fromCar);
    void explosion(Game& g, const struct ExplosionBlast& blast);
    // cPed::Damage for a bullet (sDamageInfo type 1): FixDamageForArmour (armour 100), the health, death, then
    // ReactToDamage (cSimpleMover::GetAction: action 0). from: the shooter's position.
    // Blood: type 1 (bullets) bleeds 15 drops when the armour-fixed damage is at least 5, type 2 (minigun) always.
    void bulletHit(Game& g, Ped& p, int damage, const int32_t dir[3], int effect, const int32_t from[3], int type = 1);
    // cPed::Damage for a melee hit (sDamageInfo type 0, or 12 for the chainsaw): the health and death, the hit
    // animation roll (Rand16Critical(2): +0xEB) and ReactToDamage (action 0).
    // Blood: type 0 bleeds four drops unless the weapon's effect is 0x1D, other types one (the chainsaw's
    // type-12 cParticleEmitterBloodSpray is not ported).
    void meleeHit(Game& g, Ped& p, int damage, int type, const int32_t from[3], int effect = 0);
    // cPed::Damage for burning (sDamageInfo type 5, effect 0x1D) and the taser (type 3, effect 0x13): the health and
    // death, no blood, then ReactToDamage (GetAction: types 3 and 5 are action 0). from: the attacker.
    void fireHit(Game& g, Ped& p, int damage, const int32_t from[3]);
    // cPopulationManager::GenerateRandomPed (from Game::updatePopulation): roll a type from the zone's profile and
    // create the ped at the next cLocalAreaKnowledge pavement crossing of the 60-unit circle
    void generate(Game& g, const Traffic::ZoneInfo& z);
    void collect(Game& g);   // cWorld::GarbageCollection: peds in unloaded sectors are deleted
    bool createDriver(Game& g, Vehicle& car, int type, const Traffic::ZoneInfo& zone);
    bool exitOccupant(Game& g, Vehicle& car, int seat); // true when the seat is free
    void releaseOccupants(Game& g, Vehicle& car, bool killed);
    void discardOccupants(Vehicle& car); // removal of an offscreen generated car

private:
    std::vector<Record> records_;
    std::vector<uint16_t> firstRecord_;      // per ped type
    uint32_t nextUid_ = 1;

    void spawn(Game& g, int type, const Traffic::ZoneInfo& z);
    bool appearance(Game& g, Ped& p, int type, const Traffic::ZoneInfo& z);
    void updateOccupant(Game& g, Ped& p);
    void wander(Game& g, Ped& p);
    bool rejoin(Game& g, Ped& p);
    bool idle(Game& g, Ped& p);
    void updateSocialSensor(Game& g, Ped& p);
    bool canTalk(const Ped& p) const;
    void startTalk(Ped& p);
    void finishTalk(Ped& p);
    bool talk(Game& g, Ped& p);
    void talkIdle(Game& g, Ped& p);
    void gotoTarget(Game& g, Ped& p, const int32_t target[2], int maxLevel, bool sprint = false);
    static void updateStuck(Ped& p, const int32_t before[3]);
    bool advance(Game& g, Ped& p);
    void setTarget(Game& g, Ped& p);
    void runOver(Game& g, Ped& p);
    void contacts(Game& g); // dynamic sphere contacts, after Act and before Integrate
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
    friend class LocalArea;   // AddPathingNodes resolves bridge nodes
};
