/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MP_WORLDSYNC_H
#define MP_WORLDSYNC_H
#include "ctw_mod.h"
#include "protocol.h"

/* target -1 broadcasts (host) or sends to the host (client). */
typedef void (*MpWorldSend)(const MpMessage* message, int target, int except);
int mp_world_start(const CtwApi* api, int role, MpWorldSend send);
void mp_world_stop(void);
void mp_world_tick(double now, uint32_t tick, int my_slot);
void mp_world_receive(int slot, const MpMessage* message, double now);
void mp_world_player(int slot, MpState* state, double now); /* host validates seats and driver poses */
void mp_world_forget(int slot);
void mp_world_request(const MpState* requested); /* remember entry requests for authoritative rejection */
void mp_world_correct_seat(const MpState* accepted);
uint32_t mp_world_skid_baseline(void);
void mp_world_set_skid_baseline(uint32_t sequence);
void mp_world_peer_join(int slot);
#endif
