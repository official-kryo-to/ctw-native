/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kryo.to
 * See LICENSE in the repository root.
 *
 * A non-blocking UDP socket (IPv4) for the multiplayer mod: Winsock on Windows, BSD sockets elsewhere.
 */
#ifndef MP_NET_H
#define MP_NET_H

#include <stddef.h>
#include <stdint.h>

typedef struct NetAddr { uint32_t ip; uint16_t port; } NetAddr;   /* host byte order */

int net_start(void);                                 /* 1 = ok (WSAStartup on Windows) */
void net_stop(void);
int net_open(uint16_t port);                         /* bind to the port (0: any); 1 = ok */
void net_close(void);
int net_is_open(void);
int net_resolve(const char* host, uint16_t port, NetAddr* out);   /* a name or a dotted address; 1 = ok */
int net_send(const NetAddr* to, const void* data, size_t size);  /* 1 = sent */
int net_receive(NetAddr* from, void* data, size_t size);         /* bytes, 0 when nothing is waiting */
int net_same(const NetAddr* a, const NetAddr* b);
void net_format(const NetAddr* a, char* out, size_t size);        /* "1.2.3.4:7777" */

#endif
