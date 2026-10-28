/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kryo.to
 * See LICENSE in the repository root.
 *
 * CTW Online for the multiplayer mod: the public game list and the relay (see online/ in the repository). Requests
 * run on a worker thread and the relay's WebSocket on its own, so the game never waits on the internet. Windows
 * (WinHTTP); elsewhere online_available() is 0 and the mod offers LAN and direct games only.
 */
#ifndef MP_ONLINE_H
#define MP_ONLINE_H

#include <stddef.h>
#include <stdint.h>

#define ONLINE_MAX_SERVERS 64

typedef struct OnlineServer { char id[8], name[41], version[17]; int players, max; } OnlineServer;

int online_available(void);
void online_set_url(const char* base);   /* e.g. "https://ctw-online.example.workers.dev" */
void online_stop(void);                  /* ends the relay and any registration; waits for the threads */
void online_leave(void);                 /* the same without waiting: the game carries on */

/* The public list: refresh starts a fetch; list copies the last result (-1 while fetching, -2 on an error). */
void online_refresh(void);
int online_list(OnlineServer* out, int max);

/* Hosting: register lists the game (or keeps it private: joinable by its code) and connects the relay as its
   host. id_out receives the game's code once registered (online_hosting() > 0). update keeps it listed. */
void online_host(const char* name, int max, const char* version, int is_public);
void online_update(const char* name, int players);
int online_hosting(char* id_out);        /* 1 connected, 0 working on it, -1 failed */

/* Joining a game by its code: connects the relay as a player (online_joined() > 0 when it is up). */
void online_join(const char* id);
int online_joined(void);                 /* 1 connected, 0 working on it, -1 failed or closed */

/* The relay: the host sends to a player (0xFFFF: all) and hears players by number; a player sends to the host.
   receive returns the bytes, 0 when nothing is waiting, or -1 when *player has gone (host side). */
int online_send(uint16_t player, const void* data, size_t size);
int online_receive(uint16_t* player, void* data, size_t size);

#endif
