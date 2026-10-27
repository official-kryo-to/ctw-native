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
    m.u.state.vquat[2] = 0.7071f; m.u.state.vquat[3] = 0.7071f; m.u.state.vvel[0] = 20.f;
    check(round_trip(&m, &r) && !memcmp(&m.u.state, &r.u.state, sizeof m.u.state) && r.sender == 3,
          "a player's state in a vehicle survives the trip");
    m.u.state.model = -1;
    memset(m.u.state.vpos, 0, sizeof m.u.state.vpos);
    memset(m.u.state.vquat, 0, sizeof m.u.state.vquat);
    memset(m.u.state.vvel, 0, sizeof m.u.state.vvel);
    uint8_t buf[MP_PACKET], foot[MP_PACKET];
    const size_t driving = mp_encode(&r, buf, sizeof buf), walking = mp_encode(&m, foot, sizeof foot);
    check(walking + 40 == driving, "on foot the vehicle's pose is left out");

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
