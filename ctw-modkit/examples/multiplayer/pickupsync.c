/* SPDX-License-Identifier: GPL-3.0-only */
/* Host-created pickups and reliable, single-recipient grants. */
#include "pickupsync.h"
#include <math.h>
#include <string.h>

enum { LIMIT = 4096 };
typedef struct Entry { uint32_t id, seq; int seen, removed, granted; double removed_at; } Entry;
typedef struct Peer { MpState state; double heard, sent; int valid, pending; CtwWorldPickup grant; } Peer;
static const CtwApi* game;
static MpWorldSend send_message;
static int role, own_slot;
static Entry entries[LIMIT];
static Peer peers[MP_MAX_PLAYERS];
static uint32_t sequence;
static int snapshot_cursor, removal_cursor;
static double last_snapshot, last_claim;
static Entry* entry(uint32_t id) {
    Entry* free_entry = NULL;
    if (!id) return NULL;
    for (int i = 0; i < LIMIT; ++i) {
        if (entries[i].id == id) return &entries[i];
        if (!entries[i].id && !free_entry) free_entry = &entries[i];
    }
    if (free_entry) free_entry->id = id;
    return free_entry;
}
int mp_pickups_start(const CtwApi* api, int next_role, MpWorldSend send) {
    mp_pickups_stop();
    if (!api || !send || !CTW_MOD_HAS(api, grant_world_pickup) || !api->get_world_pickup ||
        !api->set_world_pickup || !api->remove_world_pickup || !api->take_world_pickup_claim ||
        !api->claim_world_pickup || !api->grant_world_pickup) return 0;
    game = api; role = next_role; send_message = send; last_snapshot = last_claim = -1.; return 1;
}
void mp_pickups_stop(void) {
    game = NULL; send_message = NULL; role = 0; sequence = 0; snapshot_cursor = removal_cursor = 0;
    memset(entries, 0, sizeof entries); memset(peers, 0, sizeof peers);
}
void mp_pickups_player(int slot, const MpState* state, double now) {
    if (!game || slot <= 0 || slot >= MP_MAX_PLAYERS || !state) return;
    peers[slot].state = *state; peers[slot].heard = now; peers[slot].valid = 1;
}
void mp_pickups_forget(int slot) {
    if (slot > 0 && slot < MP_MAX_PLAYERS) memset(&peers[slot], 0, sizeof peers[slot]);
}
void mp_pickups_tick(double now, int my_slot) {
    if (!game) return;
    own_slot = my_slot;
    if (role == CTW_MULTIPLAYER_CLIENT) {
        if (last_claim < 0 || now - last_claim >= .15) {
            uint32_t id = game->take_world_pickup_claim();
            if (id) { MpMessage m = {0}; m.type = MP_PICKUP_CLAIM; m.sender = (uint8_t)my_slot;
                m.u.pickup_claim.id = id; send_message(&m, -1, -1); }
            last_claim = now;
        }
        return;
    }
    if (role != CTW_MULTIPLAYER_HOST) return;
    for (int i = 1; i < MP_MAX_PLAYERS; ++i) if (peers[i].pending &&
        (peers[i].sent < 0 || now - peers[i].sent >= .2)) {
        MpMessage m = {0}; m.type = MP_PICKUP_GRANT; m.u.pickup_grant.state = peers[i].grant;
        send_message(&m, i, -1); peers[i].sent = now;
    }
    if (last_snapshot >= 0 && now - last_snapshot < .25) return;
    last_snapshot = now; ++sequence; if (!sequence) ++sequence;
    for (int i = 0; i < LIMIT; ++i) entries[i].seen = 0;
    CtwWorldPickup states[256]; int state_count = 0;
    for (int i = 0; i < 256; ++i) {
        CtwWorldPickup s = {0}; s.size = sizeof s;
        if (!game->get_world_pickup(i, &s)) break;
        Entry* e = entry(s.id); if (!e || e->removed) continue;
        e->seen = 1; states[state_count++] = s;
    }
    if (state_count) {
        snapshot_cursor %= state_count;
        const int budget = state_count < 24 ? state_count : 24;
        for (int i = 0; i < budget; ++i) {
            CtwWorldPickup* s = &states[(snapshot_cursor + i) % state_count];
            Entry* e = entry(s->id); e->seq = sequence;
            MpMessage m = {0}; m.type = MP_PICKUP_STATE; m.u.pickup.seq = sequence; m.u.pickup.state = *s;
            send_message(&m, -1, -1);
        }
        snapshot_cursor = (snapshot_cursor + budget) % state_count;
    } else snapshot_cursor = 0;
    /* Repeat tombstones across transient packet loss; receivers retain their IDs for the session. */
    int sent = 0; const int removal_start = removal_cursor;
    for (int offset = 0; offset < LIMIT; ++offset) {
        Entry* e = &entries[(removal_start + offset) % LIMIT];
        if (!e->id || e->seen) continue;
        if (!e->removed) { e->removed = 1; e->seq = sequence; e->removed_at = now; }
        if (now - e->removed_at > 10.) { memset(e, 0, sizeof *e); continue; }
        if (sent >= 24) continue;
        MpMessage m = {0}; m.type = MP_PICKUP_REMOVE; m.u.pickup_remove.id = e->id; m.u.pickup_remove.seq = e->seq;
        send_message(&m, -1, -1); ++sent;
        removal_cursor = (int)(e - entries + 1) % LIMIT;
    }
}
void mp_pickups_receive(int slot, const MpMessage* m, double now) {
    if (!game || !m) return;
    if (role == CTW_MULTIPLAYER_CLIENT && slot == 0) {
        if (m->type == MP_PICKUP_STATE) {
            Entry* e = entry(m->u.pickup.state.id);
            if (!e || e->removed || (e->seq && (int32_t)(m->u.pickup.seq - e->seq) <= 0)) return;
            e->seq = m->u.pickup.seq; game->set_world_pickup(&m->u.pickup.state);
        } else if (m->type == MP_PICKUP_REMOVE) {
            Entry* e = entry(m->u.pickup_remove.id);
            if (!e || (e->seq && (int32_t)(m->u.pickup_remove.seq - e->seq) < 0)) return;
            e->seq = m->u.pickup_remove.seq; e->removed = 1; game->remove_world_pickup(e->id);
        } else if (m->type == MP_PICKUP_GRANT) {
            Entry* e = entry(m->u.pickup_grant.state.id); if (!e) return;
            if (!e->granted) {
                if (!game->grant_world_pickup(&m->u.pickup_grant.state)) return;
                e->granted = 1;
            }
            MpMessage ack = {0}; ack.type = MP_PICKUP_ACK; ack.sender = (uint8_t)own_slot; ack.u.pickup_ack.id = e->id;
            send_message(&ack, -1, -1);
        }
    } else if (role == CTW_MULTIPLAYER_HOST && slot > 0 && slot < MP_MAX_PLAYERS) {
        Peer* p = &peers[slot];
        if (m->type == MP_PICKUP_ACK && p->pending && p->grant.id == m->u.pickup_ack.id) p->pending = 0;
        if (m->type != MP_PICKUP_CLAIM || p->pending || !p->valid || now - p->heard > 10. ||
            (p->state.flags & MP_DEAD)) return;
        int32_t pos[3];
        for (int i = 0; i < 3; ++i) {
            float f = p->state.pos[i]; if (!isfinite(f) || fabsf(f) > 32768.f) return;
            pos[i] = (int32_t)lroundf(f * 4096.f);
        }
        CtwWorldPickup out = {0}; out.size = sizeof out;
        if (game->claim_world_pickup(m->u.pickup_claim.id, pos, !!(p->state.flags & MP_IN_VEHICLE), &out)) {
            p->grant = out; p->pending = 1; p->sent = -1.;
        }
    }
}
