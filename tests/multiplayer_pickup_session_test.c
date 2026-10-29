/* SPDX-License-Identifier: GPL-3.0-only */
#include "pickupsync.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static CtwWorldPickup active;
static int exists, grants, removes;
static uint32_t touch;
static MpMessage messages[2048];
static int targets[2048], count;
static void send_msg(const MpMessage* m, int target, int except) {
    (void)except; assert(count < 2048); messages[count] = *m; targets[count++] = target;
}
static int get(int i, CtwWorldPickup* out) {
    if (i || !exists) return 0;
    *out = active; return 1;
}
static int set(const CtwWorldPickup* s) { active = *s; exists = 1; return 1; }
static int remove_pickup(uint32_t id) { if (exists && active.id == id) exists = 0; ++removes; return 1; }
static uint32_t take(void) { return touch; }
static int claim(uint32_t id, const int32_t pos[3], int in_vehicle, CtwWorldPickup* out) {
    (void)in_vehicle;
    if (!exists || id != active.id || pos[0] > 8192 || pos[0] < -8192) return 0;
    *out = active; exists = 0; return 1;
}
static int grant(const CtwWorldPickup* s) { assert(s->amount == 60 && s->id == 42); ++grants; return 1; }
static CtwApi api;
static void setup(void) {
    memset(&api, 0, sizeof api); api.size = sizeof api;
    api.get_world_pickup = get; api.set_world_pickup = set; api.remove_world_pickup = remove_pickup;
    api.take_world_pickup_claim = take; api.claim_world_pickup = claim; api.grant_world_pickup = grant;
    memset(&active, 0, sizeof active); active.size = sizeof active; active.id = 42; active.amount = 60;
    exists = 1; grants = removes = 0; touch = 0; count = 0;
}
static MpMessage find_type(int type) {
    for (int i = 0; i < count; ++i) if (messages[i].type == type) return messages[i];
    assert(!"expected message"); return messages[0];
}
int main(void) {
    setup(); assert(mp_pickups_start(&api, CTW_MULTIPLAYER_HOST, send_msg));
    MpState peer = {0}; peer.slot = 1; mp_pickups_player(1, &peer, 1.);
    peer.slot = 2; mp_pickups_player(2, &peer, 1.);
    mp_pickups_tick(1., 0); MpMessage snapshot = find_type(MP_PICKUP_STATE);
    MpMessage request = {0}; request.type = MP_PICKUP_CLAIM; request.u.pickup_claim.id = 42;
    mp_pickups_receive(1, &request, 1.); assert(!exists);
    mp_pickups_receive(2, &request, 1.); mp_pickups_receive(1, &request, 1.);
    count = 0; mp_pickups_tick(1.3, 0);
    MpMessage award = find_type(MP_PICKUP_GRANT), tombstone = find_type(MP_PICKUP_REMOVE);
    int awards = 0; for (int i = 0; i < count; ++i) if (messages[i].type == MP_PICKUP_GRANT) { assert(targets[i] == 1); ++awards; }
    assert(awards == 1); count = 0; mp_pickups_tick(1.6, 0); assert(find_type(MP_PICKUP_GRANT).u.pickup_grant.state.id == 42);
    MpMessage ack = {0}; ack.type = MP_PICKUP_ACK; ack.u.pickup_ack.id = 42; mp_pickups_receive(1, &ack, 1.6);
    count = 0; mp_pickups_tick(1.9, 0); for (int i = 0; i < count; ++i) assert(messages[i].type != MP_PICKUP_GRANT);
    mp_pickups_stop();
    /* A late client starts with no pickup; a tombstone rejects delayed state. */
    exists = 0; count = 0; assert(mp_pickups_start(&api, CTW_MULTIPLAYER_CLIENT, send_msg));
    mp_pickups_tick(2., 1); mp_pickups_receive(0, &tombstone, 2.); mp_pickups_receive(0, &snapshot, 2.); assert(!exists);
    mp_pickups_receive(0, &award, 2.); mp_pickups_receive(0, &award, 2.); assert(grants == 1);
    int acks = 0; for (int i = 0; i < count; ++i) if (messages[i].type == MP_PICKUP_ACK) { assert(messages[i].sender == 1); ++acks; }
    assert(acks == 2); mp_pickups_stop();
    /* Host local collection is reflected by its next snapshot. */
    setup(); assert(mp_pickups_start(&api, CTW_MULTIPLAYER_HOST, send_msg)); mp_pickups_tick(1., 0);
    exists = 0; count = 0; mp_pickups_tick(1.3, 0); assert(find_type(MP_PICKUP_REMOVE).u.pickup_remove.id == 42);
    mp_pickups_stop(); puts("pickup session ok"); return 0;
}
