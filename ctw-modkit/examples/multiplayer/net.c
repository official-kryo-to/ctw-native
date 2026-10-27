/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kryo.to
 * See LICENSE in the repository root.
 */
#include "net.h"
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET Socket;
#define BAD_SOCKET INVALID_SOCKET
#define close_socket closesocket
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int Socket;
#define BAD_SOCKET (-1)
#define close_socket close
#endif

static Socket sock = BAD_SOCKET;
static int started;

int net_start(void) {
#ifdef _WIN32
    if (!started) {
        WSADATA data;
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return 0;
    }
#endif
    started = 1;
    return 1;
}

void net_stop(void) {
    net_close();
#ifdef _WIN32
    if (started) WSACleanup();
#endif
    started = 0;
}

int net_open(uint16_t port) {
    net_close();
    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == BAD_SOCKET) return 0;
#ifdef _WIN32
    u_long nonblocking = 1;
    ioctlsocket(sock, FIONBIO, &nonblocking);
#else
    fcntl(sock, F_SETFL, fcntl(sock, F_GETFL, 0) | O_NONBLOCK);
#endif
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons(port);
    if (bind(sock, (struct sockaddr*)&a, sizeof a) != 0) { net_close(); return 0; }
    return 1;
}

void net_close(void) {
    if (sock != BAD_SOCKET) close_socket(sock);
    sock = BAD_SOCKET;
}

int net_is_open(void) { return sock != BAD_SOCKET; }

int net_resolve(const char* host, uint16_t port, NetAddr* out) {
    struct addrinfo hints, *found = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    if (!host || !*host || getaddrinfo(host, NULL, &hints, &found) != 0 || !found) return 0;
    out->ip = ntohl(((struct sockaddr_in*)found->ai_addr)->sin_addr.s_addr);
    out->port = port;
    freeaddrinfo(found);
    return 1;
}

int net_send(const NetAddr* to, const void* data, size_t size) {
    if (sock == BAD_SOCKET) return 0;
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(to->ip);
    a.sin_port = htons(to->port);
    return sendto(sock, (const char*)data, (int)size, 0, (struct sockaddr*)&a, sizeof a) == (int)size;
}

int net_receive(NetAddr* from, void* data, size_t size) {
    if (sock == BAD_SOCKET) return 0;
    struct sockaddr_in a;
#ifdef _WIN32
    int len = sizeof a;
#else
    socklen_t len = sizeof a;
#endif
    for (;;) {
        const int n = (int)recvfrom(sock, (char*)data, (int)size, 0, (struct sockaddr*)&a, &len);
        if (n > 0) {
            from->ip = ntohl(a.sin_addr.s_addr);
            from->port = ntohs(a.sin_port);
            return n;
        }
#ifdef _WIN32
        /* a port that is not listening (yet) reports itself here: skip it and read on */
        if (n < 0 && WSAGetLastError() == WSAECONNRESET) continue;
#endif
        return 0;
    }
}

int net_same(const NetAddr* a, const NetAddr* b) { return a->ip == b->ip && a->port == b->port; }

void net_format(const NetAddr* a, char* out, size_t size) {
    snprintf(out, size, "%u.%u.%u.%u:%u", (unsigned)(a->ip >> 24), (unsigned)(a->ip >> 16 & 255),
             (unsigned)(a->ip >> 8 & 255), (unsigned)(a->ip & 255), (unsigned)a->port);
}
