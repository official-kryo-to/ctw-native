/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kryo.to
 * See LICENSE in the repository root.
 *
 * The multiplayer mod's messages: every type survives encoding, and nothing malformed is accepted.
 */
#include "../ctw-modkit/examples/multiplayer/protocol.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures;
static void check(int ok, const char* what) {
    if (!ok) { fprintf(stderr, "FAIL: %s\n", what); ++failures; }
}

static int round_trip(const MpMessage* in, MpMessage* out) {
    uint8_t buf[MP_PACKET];
    const size_t n = mp_encode(in, buf, sizeof buf);
    return n && mp_decode(buf, n, out);
}

int main(void) {
    MpMessage m, r;
    memset(&m, 0, sizeof m);
    m.type = MP_STATE;
    m.sender = 3;
    m.u.state.slot = 3; m.u.state.seq = 65535;
    m.u.state.pos[0] = -1000.5f; m.u.state.pos[1] = -1590.25f; m.u.state.pos[2] = 1.f;
    m.u.state.heading = 270.f; m.u.state.vel[1] = 6.5f;
    m.u.state.move = 2; m.u.state.flags = MP_IN_VEHICLE; m.u.state.health = 127; m.u.state.armour = 40;
    m.u.state.body = 1; m.u.state.upper = 4; m.u.state.legs = 5; m.u.state.weapon = 5;
    m.u.state.model = 12; m.u.state.palette = 9;
    m.u.state.vehicle = 923; m.u.state.seat = 1;
    m.u.state.visual.size = sizeof(CtwVehicleVisual); m.u.state.visual.flags = 63;
    m.u.state.visual.controls = 0x106; m.u.state.visual.tyres = 63; m.u.state.visual.burnout = 3;
    m.u.state.visual.door_angles[2] = 80; m.u.state.visual.doors_open = 4;
    m.u.state.vquat[2] = 0.7071f; m.u.state.vquat[3] = 0.7071f; m.u.state.vvel[0] = 20.f;
    m.u.state.view_eye[0] = -1000 * 4096; m.u.state.view_eye[2] = 12 * 4096;
    m.u.state.view_yaw = 0xC000; m.u.state.view_pitch = 0xF2AB; m.u.state.view_width = 1920; m.u.state.view_height = 1080;
    check(round_trip(&m, &r) && !memcmp(&m.u.state, &r.u.state, sizeof m.u.state) && r.sender == 3,
          "a player's state in a vehicle survives the trip");
    m.u.state.model = -1;
    m.u.state.vehicle = 0; m.u.state.seat = -1;
    memset(m.u.state.vpos, 0, sizeof m.u.state.vpos);
    memset(m.u.state.vquat, 0, sizeof m.u.state.vquat);
    memset(m.u.state.vvel, 0, sizeof m.u.state.vvel);
    uint8_t buf[MP_PACKET], foot[MP_PACKET];
    const size_t driving = mp_encode(&r, buf, sizeof buf), walking = mp_encode(&m, foot, sizeof foot);
    check(walking + 94 == driving, "on foot the vehicle's pose and effects are left out");

    memset(&m, 0, sizeof m);
    m.type = MP_HELLO; m.sender = 0xFF;
    m.u.hello.api = 13;
    memset(m.u.hello.name, 'x', MP_NAME);   /* a full field without its terminator */
    check(round_trip(&m, &r) && r.u.hello.api == 13 && strlen(r.u.hello.name) == MP_NAME - 1,
          "a name filling its field is cut to fit");

    memset(&m, 0, sizeof m);
    m.type = MP_ROSTER;
    m.u.roster.count = 2;
    m.u.roster.slots[0] = 0; strcpy(m.u.roster.names[0], "Host");
    m.u.roster.slots[1] = 7; strcpy(m.u.roster.names[1], "Seven");
    check(round_trip(&m, &r) && r.u.roster.count == 2 && r.u.roster.slots[1] == 7 && !strcmp(r.u.roster.names[1], "Seven"),
          "the roster survives the trip");
    m.u.roster.count = MP_MAX_PLAYERS;
    for (int i = 0; i < MP_MAX_PLAYERS; ++i) {
        m.u.roster.slots[i] = (uint8_t)i; snprintf(m.u.roster.names[i], MP_NAME, "Player %d", i + 1);
    }
    check(round_trip(&m, &r) && r.u.roster.count == 32 && r.u.roster.slots[31] == 31,
          "the complete 32-player roster fits a datagram");
    m.u.roster.slots[31] = 0;
    check(!round_trip(&m, &r), "duplicate roster slots are rejected");

    memset(&m, 0, sizeof m);
    m.type = MP_WORLD_VEHICLE; m.u.vehicle.seq = 123;
    CtwWorldVehicle* v = &m.u.vehicle.state;
    v->size = sizeof *v; v->id = 849; v->model = 12; v->palette = 26; v->health = 100;
    v->quaternion[3] = 1.f; v->velocity[1] = 32.f; v->position[0] = -1000.f;
    v->flags = CTW_VEHICLE_DEAD | (720u << 21) | (31u << 14) | (3u << 19);
    check(round_trip(&m, &r) && r.u.vehicle.seq == 123 && !memcmp(v, &r.u.vehicle.state, sizeof *v),
          "stable shared vehicle identity and state round trip");
    v->velocity[0] = 513.f;
    check(!round_trip(&m, &r), "excessive vehicle velocity rejected");
    v->velocity[0] = 0.f; v->quaternion[3] = 0.f;
    check(!round_trip(&m, &r), "invalid quaternion rejected");

    memset(&m, 0, sizeof m); m.type = MP_WORLD_PED; m.u.ped.seq = 43;
    CtwWorldPed* p = &m.u.ped.state;
    p->size = sizeof *p; p->id = 777; p->type = 10; p->body = 1; p->upper = 4; p->legs = 5;
    p->health = 127; p->weapon = 5; p->seat = -1; p->heading = 180.f; p->velocity[1] = 2.f;
    check(round_trip(&m, &r) && !memcmp(p, &r.u.ped.state, sizeof *p), "police pedestrian state round trips");

    memset(&m, 0, sizeof m); m.type = MP_EXPLOSION; m.u.explosion.seq = 65537;
    m.u.explosion.state.size = sizeof(CtwWorldExplosion); m.u.explosion.state.type = 6; m.u.explosion.state.scale = 1.f;
    m.u.explosion.state.position[0] = 55.f;
    check(round_trip(&m, &r) && r.u.explosion.seq == 65537 && r.u.explosion.state.position[0] == 55.f,
          "sequenced explosion round trips");
    memset(&m, 0, sizeof m); m.type = MP_ENTITY_DAMAGE; m.u.damage.seq = 72; m.u.damage.id = 777;
    m.u.damage.amount = 64; m.u.damage.ped = 1;
    check(round_trip(&m, &r) && r.u.damage.amount == 64 && r.u.damage.ped, "entity damage round trips");
    memset(&m, 0, sizeof m); m.type = MP_WORLD_REMOVE; m.u.remove.seq = 44; m.u.remove.id = 849;
    check(round_trip(&m, &r) && r.u.remove.seq == 44 && r.u.remove.id == 849, "entity tombstone round trips");
    memset(&m, 0, sizeof m); m.type = MP_VEHICLE_CREATE;
    m.u.vehicle.state.size = sizeof(CtwWorldVehicle); m.u.vehicle.state.id = 500;
    m.u.vehicle.state.model = 12; m.u.vehicle.state.health = 255; m.u.vehicle.state.quaternion[3] = 1.f;
    check(round_trip(&m, &r) && r.u.vehicle.state.id == 500, "client car creation request round trips");
    memset(&m, 0, sizeof m); m.type = MP_VEHICLE_CREATED; m.u.created.request = 500; m.u.created.id = 700;
    check(round_trip(&m, &r) && r.u.created.request == 500 && r.u.created.id == 700, "host car ID acknowledgment round trips");
    memset(&m,0,sizeof m); m.type=MP_PICKUP_STATE; m.u.pickup.seq=19;
    m.u.pickup.state.size=sizeof(CtwWorldPickup); m.u.pickup.state.id=88; m.u.pickup.state.amount=125;
    m.u.pickup.state.position[0]=-1000*4096; m.u.pickup.state.position[1]=-1590*4096;
    check(round_trip(&m,&r) && r.u.pickup.seq==19 && !memcmp(&m.u.pickup.state,&r.u.pickup.state,sizeof(CtwWorldPickup)),
          "shared NPC money amount, identity and fixed-point position round trip");
    m.type=MP_PICKUP_GRANT; m.u.pickup_grant.state=r.u.pickup.state;
    check(round_trip(&m,&r) && r.u.pickup_grant.state.amount==125,"exact collector reward round trips");
    memset(&m,0,sizeof m); m.type=MP_PICKUP_ACK; m.u.pickup_ack.id=88;
    check(round_trip(&m,&r) && r.u.pickup_ack.id==88,"pickup acknowledgment round trips");
    memset(&m,0,sizeof m); m.type=MP_WORLD_HIT; m.u.world_hit.seq=55; m.u.world_hit.victim=31; m.u.world_hit.damage=100;
    check(round_trip(&m,&r) && r.u.world_hit.seq==55 && r.u.world_hit.victim==31 && r.u.world_hit.damage==100,
          "sequenced NPC hit on the last player slot round trips");
    m.u.world_hit.victim=MP_MAX_PLAYERS; check(!round_trip(&m,&r),"invalid NPC hit victim rejected");

    memset(&m, 0, sizeof m);
    m.type = MP_CHAT_MSG; m.u.chat.slot = 2;
    strcpy(m.u.chat.text, "hello\tthere");
    check(round_trip(&m, &r) && !strcmp(r.u.chat.text, "hello there"), "control characters do not reach the screen");

    memset(&m, 0, sizeof m);
    m.type = MP_HIT; m.u.hit.attacker = 1; m.u.hit.victim = 0; m.u.hit.damage = 300;
    check(round_trip(&m, &r) && r.u.hit.damage == 300 && r.u.hit.victim == 0, "a hit survives the trip");
    m.u.hit.victim = MP_MAX_PLAYERS;
    check(!round_trip(&m, &r), "a hit on a slot that cannot exist is refused");

    memset(&m, 0, sizeof m);
    m.type = MP_WORLD; m.u.world.time = 13.5f; m.u.world.weather = 3;
    check(round_trip(&m, &r) && r.u.world.time == 13.5f && r.u.world.weather == 3, "the world survives the trip");

    /* malformed input */
    memset(&m, 0, sizeof m);
    m.type = MP_WELCOME; m.u.welcome.slot = 1; m.u.welcome.max = 8;
    size_t n = mp_encode(&m, buf, sizeof buf);
    check(n && mp_decode(buf, n, &r), "a welcome decodes");
    check(!mp_decode(buf, n - 1, &r), "a truncated message is refused");
    buf[n] = 0;
    check(!mp_decode(buf, n + 1, &r), "trailing bytes are refused");
    buf[0] = 'X';
    check(!mp_decode(buf, n, &r), "another program's datagram is refused");
    buf[0] = 'C'; buf[4] = MP_VERSION + 1;
    check(!mp_decode(buf, n, &r), "another protocol version is refused");
    memset(&m, 0, sizeof m);
    m.type = MP_HELLO; m.u.hello.api = 13;
    n = mp_encode(&m, buf, sizeof buf);
    buf[4] = MP_VERSION + 1;
    check(mp_decode(buf, n, &r) && r.u.hello.api == 0, "another version's hello is let through to be refused");
    memset(&m, 0, sizeof m);
    m.type = MP_STATE; m.u.state.model = -1;
    n = mp_encode(&m, buf, sizeof buf);
    const float nan_value = NAN;
    memcpy(buf + 10, &nan_value, 4);   /* pos[0] */
    check(!mp_decode(buf, n, &r), "a position that is not a number is refused");
    check(!mp_encode(&m, buf, 8), "a message too big for the buffer is not written");
    m.type = 99;
    check(!mp_encode(&m, buf, sizeof buf), "unknown types are not written");

    if (!failures) puts("PASS: multiplayer protocol");
    return failures ? 1 : 0;
}
