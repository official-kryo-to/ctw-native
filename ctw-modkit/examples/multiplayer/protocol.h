/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kryo.to
 * See LICENSE in the repository root.
 *
 * The multiplayer mod's messages. Every UDP datagram is one message: the header "CTWM", the protocol version, the
 * type and the sender's slot, then the type's fields, little-endian. The host is slot 0 and relays everything.
 *
 *   HELLO    client -> host   join: mod API version, name, looks
 *   WELCOME  host -> client   the client's slot
 *   REFUSE   host -> client   full or a different protocol
 *   STATE    both ways        a player's character and vehicle, about 15 times a second (the host relays them all)
 *   ROSTER   host -> all      who is playing, every second
 *   CHAT     both ways        a line of chat (the host relays it)
 *   HIT      both ways        damage one player did to another's character (the victim's game takes it)
 *   WORLD    host -> all      the time of day and the weather, every second
 *   WORLD_VEHICLE / WORLD_PED host -> all: stable entity IDs, poses, health and seats, in bounded batches
 *   WORLD_REMOVE host -> all: entity tombstone (rejects delayed snapshots)
 *   EXPLOSION / ENTITY_DAMAGE both ways: replay-protected events, relayed by the host
 *   VEHICLE_CREATE client -> host: a locally mod-spawned car; CREATED acknowledges its host ID
 *   BYE      both ways        leaving
 */
#ifndef MP_PROTOCOL_H
#define MP_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>
#include "../../include/ctw_types.h"

enum { MP_VERSION = 5, MP_MAX_PLAYERS = 32, MP_DEFAULT_PLAYERS = 16, MP_NAME = 16, MP_CHAT = 96, MP_PACKET = 1200, MP_SKID_POINTS = 24 };
enum MpType { MP_HELLO = 1, MP_WELCOME, MP_REFUSE, MP_STATE, MP_ROSTER, MP_CHAT_MSG, MP_HIT, MP_WORLD, MP_BYE,
              MP_WORLD_VEHICLE, MP_WORLD_PED, MP_EXPLOSION, MP_ENTITY_DAMAGE, MP_WORLD_REMOVE,
              MP_VEHICLE_CREATE, MP_VEHICLE_CREATED, MP_VEHICLE_DOOR, MP_SKID_BATCH,
              MP_PICKUP_STATE, MP_PICKUP_REMOVE, MP_PICKUP_CLAIM, MP_PICKUP_GRANT, MP_PICKUP_ACK, MP_WORLD_HIT, MP_SKID_ACK };
enum MpRefusal { MP_FULL = 1, MP_WRONG_VERSION = 2 };
enum MpStateFlags { MP_DEAD = 1, MP_IN_VEHICLE = 2, MP_FIRING = 4 };

typedef struct MpState {
    uint8_t slot;
    uint16_t seq;
    float pos[3], heading, vel[3];
    uint8_t move, flags, health, armour, body, upper, legs, weapon;
    int16_t model;               /* the vehicle's catalogue id, -1 on foot */
    uint8_t palette;
    uint32_t vehicle;            /* host world ID, shared by every occupant */
    int8_t seat;                 /* 0 driver, 1..3 passenger, -1 on foot */
    float vpos[3], vquat[4], vvel[3];
    CtwVehicleVisual visual;
    int32_t view_eye[3];         /* the camera the sender's on-screen tests use (CtwView); width 0 = none */
    uint16_t view_yaw, view_pitch, view_width, view_height;
} MpState;

typedef struct MpMessage {
    uint8_t type, sender;
    union {
        struct { uint16_t api; char name[MP_NAME]; uint8_t body, upper, legs; } hello;
        struct { uint8_t slot, max; uint32_t skid_baseline; } welcome;
        struct { uint8_t reason; } refuse;
        MpState state;
        struct { uint8_t count, slots[MP_MAX_PLAYERS]; char names[MP_MAX_PLAYERS][MP_NAME]; } roster;
        struct { uint8_t slot; char text[MP_CHAT]; } chat;
        struct { uint8_t attacker, victim; uint16_t damage; } hit;
        struct { float time; uint8_t weather; } world;
        struct { uint32_t seq; CtwWorldVehicle state; CtwVehicleVisual visual; } vehicle;
        struct { uint32_t seq; CtwWorldPed state; } ped;
        struct { uint32_t seq; CtwWorldExplosion state; } explosion;
        struct { uint32_t seq, id; uint16_t amount; uint8_t ped; } damage;
        struct { uint32_t seq, id; uint8_t ped; } remove;
        struct { uint32_t request, id; } created;
        struct { uint32_t seq, id; uint8_t seat, open; uint16_t angle; } door;
        struct { uint32_t seq; uint8_t count; CtwSkidPoint points[MP_SKID_POINTS]; } skid;
        struct { uint32_t seq; CtwWorldPickup state; } pickup;
        struct { uint32_t seq, id; } pickup_remove;
        struct { uint32_t id; } pickup_claim;
        struct { CtwWorldPickup state; } pickup_grant;
        struct { uint32_t id; } pickup_ack;
        struct { uint32_t seq; uint8_t victim; uint16_t damage; } world_hit;
        struct { uint32_t seq; } skid_ack;
    } u;
} MpMessage;

size_t mp_encode(const MpMessage* m, uint8_t* out, size_t size);   /* bytes written, 0 if it does not fit */
int mp_decode(const uint8_t* data, size_t size, MpMessage* out);   /* 1 = a whole, valid message */

#endif
