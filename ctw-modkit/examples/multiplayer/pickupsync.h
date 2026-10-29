/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MP_PICKUPSYNC_H
#define MP_PICKUPSYNC_H
#include "worldsync.h"
int mp_pickups_start(const CtwApi* api, int role, MpWorldSend send);
void mp_pickups_stop(void);
void mp_pickups_tick(double now, int my_slot);
void mp_pickups_player(int slot, const MpState* state, double now);
void mp_pickups_forget(int slot);
void mp_pickups_receive(int slot, const MpMessage* message, double now);
#endif
