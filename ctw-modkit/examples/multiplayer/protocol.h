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
 *   BYE      both ways        leaving
 */
#ifndef MP_PROTOCOL_H
#define MP_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

enum { MP_VERSION = 1, MP_MAX_PLAYERS = 8, MP_NAME = 16, MP_CHAT = 96, MP_PACKET = 512 };
enum MpType { MP_HELLO = 1, MP_WELCOME, MP_REFUSE, MP_STATE, MP_ROSTER, MP_CHAT_MSG, MP_HIT, MP_WORLD, MP_BYE };
enum MpRefusal { MP_FULL = 1, MP_WRONG_VERSION = 2 };
enum MpStateFlags { MP_DEAD = 1, MP_IN_VEHICLE = 2, MP_FIRING = 4 };

typedef struct MpState {
    uint8_t slot;
    uint16_t seq;
    float pos[3], heading, vel[3];
    uint8_t move, flags, health, armour, body, upper, legs, weapon;
    int16_t model;               /* the vehicle's catalogue id, -1 on foot */
    uint8_t palette;
    float vpos[3], vquat[4], vvel[3];
} MpState;

typedef struct MpMessage {
    uint8_t type, sender;
    union {
        struct { uint16_t api; char name[MP_NAME]; uint8_t body, upper, legs; } hello;
        struct { uint8_t slot, max; } welcome;
        struct { uint8_t reason; } refuse;
        MpState state;
        struct { uint8_t count, slots[MP_MAX_PLAYERS]; char names[MP_MAX_PLAYERS][MP_NAME]; } roster;
        struct { uint8_t slot; char text[MP_CHAT]; } chat;
        struct { uint8_t attacker, victim; uint16_t damage; } hit;
        struct { float time; uint8_t weather; } world;
    } u;
} MpMessage;

size_t mp_encode(const MpMessage* m, uint8_t* out, size_t size);   /* bytes written, 0 if it does not fit */
int mp_decode(const uint8_t* data, size_t size, MpMessage* out);   /* 1 = a whole, valid message */

#endif
