/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kryo.to
 * See LICENSE in the repository root.
 */
#include "online.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32

int online_available(void) { return 0; }
void online_set_url(const char* base) { (void)base; }
void online_stop(void) {}
void online_leave(void) {}
void online_refresh(void) {}
int online_list(OnlineServer* out, int max) { (void)out; (void)max; return -2; }
void online_host(const char* name, int max, const char* version, int is_public) { (void)name; (void)max; (void)version; (void)is_public; }
void online_update(const char* name, int players) { (void)name; (void)players; }
int online_hosting(char* id_out) { (void)id_out; return -1; }
void online_join(const char* id) { (void)id; }
int online_joined(void) { return -1; }
int online_send(uint16_t player, const void* data, size_t size) { (void)player; (void)data; (void)size; return 0; }
int online_receive(uint16_t* player, void* data, size_t size) { (void)player; (void)data; (void)size; return 0; }

#else

#include <windows.h>
#include <winhttp.h>

enum { JOB_REFRESH = 1, JOB_HOST = 2, JOB_UPDATE = 4, JOB_JOIN = 8, JOB_REMOVE = 16, JOB_QUIT = 32 };
enum { QUEUE = 256, PACKET = 2050 };

static CRITICAL_SECTION lock;
static int lock_ready;
static HANDLE wake, worker, receiver;
static HINTERNET session, connection;
static wchar_t host_name[256];
static INTERNET_PORT host_port;
static int secure;
static unsigned jobs;

static OnlineServer servers[ONLINE_MAX_SERVERS];
static int server_count = -2;

static char game_id[8], game_token[32], my_name[41], my_version[17];
static int my_max, my_public, my_players;
static int hosting = -1, joined = -1;          /* 1 up, 0 connecting, -1 not */
static int as_host;
static HINTERNET socket_handle;
static CRITICAL_SECTION send_lock;

static struct { uint16_t player; uint16_t size; uint8_t data[PACKET]; } queue[QUEUE];
static int queue_head, queue_count;

static void ensure_lock(void) {
    if (lock_ready) return;
    InitializeCriticalSection(&lock);
    InitializeCriticalSection(&send_lock);
    wake = CreateEventW(NULL, FALSE, FALSE, NULL);
    lock_ready = 1;
}

/* ---- HTTP ----------------------------------------------------------------------------------------------------- */
static HINTERNET open_request(const wchar_t* verb, const char* path) {
    if (!session || !host_name[0]) return NULL;
    if (!connection) connection = WinHttpConnect(session, host_name, host_port, 0);   /* one, reused */
    if (!connection) return NULL;
    wchar_t wpath[512];
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, 512);
    HINTERNET request = WinHttpOpenRequest(connection, verb, wpath, NULL, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0);
    if (!request) return NULL;
    WinHttpSetTimeouts(request, 5000, 5000, 8000, 8000);
    return request;
}

/* a whole request: status code, and the body (cut to size - 1) */
static int http(const wchar_t* verb, const char* path, const char* body, char* out, size_t size) {
    HINTERNET request = open_request(verb, path);
    if (!request) return 0;
    const DWORD length = body ? (DWORD)strlen(body) : 0;
    DWORD status = 0, status_size = sizeof status;
    size_t got = 0;
    if (WinHttpSendRequest(request, L"Content-Type: application/json\r\n", (DWORD)-1L, (void*)body, length, length, 0) &&
        WinHttpReceiveResponse(request, NULL) &&
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                            &status, &status_size, WINHTTP_NO_HEADER_INDEX)) {
        DWORD read = 0;
        while (out && got + 1 < size && WinHttpReadData(request, out + got, (DWORD)(size - 1 - got), &read) && read) got += read;
    }
    if (out && size) out[got] = 0;
    WinHttpCloseHandle(request);
    return (int)status;
}

/* "key":"value" or "key":number from a flat JSON object */
static int json_text(const char* object, const char* key, char* out, size_t size) {
    char pattern[32];
    snprintf(pattern, sizeof pattern, "\"%s\":\"", key);
    const char* p = strstr(object, pattern);
    if (!p) return 0;
    p += strlen(pattern);
    size_t n = 0;
    for (; *p && *p != '"' && n + 1 < size; ++p) {
        if (*p == '\\' && p[1]) ++p;
        out[n++] = *p;
    }
    out[n] = 0;
    return 1;
}

static int json_number(const char* object, const char* key) {
    char pattern[32];
    snprintf(pattern, sizeof pattern, "\"%s\":", key);
    const char* p = strstr(object, pattern);
    return p ? atoi(p + strlen(pattern)) : 0;
}

static void json_escape(const char* in, char* out, size_t size) {
    size_t n = 0;
    for (; *in && n + 3 < size; ++in) {
        if (*in == '"' || *in == '\\') out[n++] = '\\';
        if ((unsigned char)*in >= 0x20) out[n++] = *in;
    }
    out[n] = 0;
}

static void do_refresh(void) {
    static char body[32768];
    OnlineServer found[ONLINE_MAX_SERVERS];
    int count = 0;
    if (http(L"GET", "/v1/servers", NULL, body, sizeof body) != 200) {
        EnterCriticalSection(&lock); server_count = -2; LeaveCriticalSection(&lock);
        return;
    }
    for (const char* p = strchr(body, '{'); p && count < ONLINE_MAX_SERVERS; p = strchr(p + 1, '{')) {
        const char* end = strchr(p, '}');
        if (!end) break;
        char object[512];
        const size_t n = (size_t)(end - p) < sizeof object - 1 ? (size_t)(end - p) : sizeof object - 1;
        memcpy(object, p, n);
        object[n] = 0;
        OnlineServer* s = &found[count];
        memset(s, 0, sizeof *s);
        if (!json_text(object, "id", s->id, sizeof s->id)) continue;
        json_text(object, "name", s->name, sizeof s->name);
        json_text(object, "version", s->version, sizeof s->version);
        s->players = json_number(object, "players");
        s->max = json_number(object, "max");
        ++count;
        p = end;
    }
    EnterCriticalSection(&lock);
    memcpy(servers, found, sizeof found[0] * (size_t)count);
    server_count = count;
    LeaveCriticalSection(&lock);
}

/* ---- the relay ------------------------------------------------------------------------------------------------ */
static DWORD WINAPI receive_loop(void* unused) {
    (void)unused;
    static uint8_t buf[PACKET + 16];
    for (;;) {
        DWORD read = 0, total = 0;
        WINHTTP_WEB_SOCKET_BUFFER_TYPE type;
        do {   /* a message can arrive in fragments */
            if (WinHttpWebSocketReceive(socket_handle, buf + total, (DWORD)sizeof buf - total, &read, &type) != NO_ERROR) {
                type = WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE;
                break;
            }
            total += read;
        } while (type == WINHTTP_WEB_SOCKET_BINARY_FRAGMENT_BUFFER_TYPE && total < sizeof buf);
        if (type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) break;
        if (type != WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE) continue;
        uint16_t player = 0;
        const uint8_t* data = buf;
        if (as_host) {
            if (total < 2) continue;
            player = (uint16_t)(buf[0] | buf[1] << 8);
            data += 2;
            total -= 2;
        }
        if (total > PACKET) continue;
        EnterCriticalSection(&lock);
        if (queue_count < QUEUE) {
            const int at = (queue_head + queue_count) % QUEUE;
            queue[at].player = player;
            queue[at].size = (uint16_t)total;
            memcpy(queue[at].data, data, total);
            ++queue_count;
        }
        LeaveCriticalSection(&lock);
    }
    EnterCriticalSection(&lock);
    if (as_host) hosting = -1; else joined = -1;
    LeaveCriticalSection(&lock);
    return 0;
}

static int connect_relay(const char* id, const char* token) {
    char path[128];
    if (token) snprintf(path, sizeof path, "/v1/rooms/%s?token=%s", id, token);
    else snprintf(path, sizeof path, "/v1/rooms/%s", id);
    HINTERNET request = open_request(L"GET", path);
    if (!request) return 0;
    DWORD status = 0, status_size = sizeof status;
    HINTERNET ws = NULL;
    if (WinHttpSetOption(request, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, NULL, 0) &&
        WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, NULL, 0, 0, 0) &&
        WinHttpReceiveResponse(request, NULL) &&
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                            &status, &status_size, WINHTTP_NO_HEADER_INDEX) && status == 101)
        ws = WinHttpWebSocketCompleteUpgrade(request, 0);
    WinHttpCloseHandle(request);
    if (!ws) return 0;
    socket_handle = ws;
    as_host = token != NULL;
    receiver = CreateThread(NULL, 0, receive_loop, NULL, 0, NULL);
    return 1;
}

static void close_relay(void) {
    if (socket_handle) {
        WinHttpWebSocketClose(socket_handle, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, NULL, 0);
        if (receiver) { WaitForSingleObject(receiver, 3000); CloseHandle(receiver); }
        EnterCriticalSection(&send_lock);
        WinHttpCloseHandle(socket_handle);
        socket_handle = NULL;
        LeaveCriticalSection(&send_lock);
    }
    receiver = NULL;
    queue_count = 0;
}

static void do_host(void) {
    char body[256], name[96], reply[256];
    json_escape(my_name, name, sizeof name);
    snprintf(body, sizeof body, "{\"name\":\"%s\",\"players\":1,\"max\":%d,\"version\":\"%s\",\"public\":%s}",
             name, my_max, my_version, my_public ? "true" : "false");
    char id[8] = "", token[32] = "";
    if (http(L"POST", "/v1/servers", body, reply, sizeof reply) == 200 && json_text(reply, "id", id, sizeof id) &&
        json_text(reply, "token", token, sizeof token) && connect_relay(id, token)) {
        EnterCriticalSection(&lock);
        memcpy(game_id, id, sizeof game_id);
        memcpy(game_token, token, sizeof game_token);
        hosting = 1;
        LeaveCriticalSection(&lock);
    } else {
        EnterCriticalSection(&lock); hosting = -1; LeaveCriticalSection(&lock);
    }
}

static void do_update(void) {
    char body[256], name[96], path[64];
    json_escape(my_name, name, sizeof name);
    snprintf(path, sizeof path, "/v1/servers/%s", game_id);
    snprintf(body, sizeof body, "{\"token\":\"%s\",\"players\":%d,\"name\":\"%s\"}", game_token, my_players, name);
    http(L"POST", path, body, NULL, 0);
}

static void do_remove(void) {
    char path[96];
    if (!game_id[0]) return;
    snprintf(path, sizeof path, "/v1/servers/%s?token=%s", game_id, game_token);
    http(L"DELETE", path, NULL, NULL, 0);
    game_id[0] = game_token[0] = 0;
}

static DWORD WINAPI work_loop(void* unused) {
    (void)unused;
    for (;;) {
        WaitForSingleObject(wake, INFINITE);
        EnterCriticalSection(&lock);
        const unsigned todo = jobs;
        jobs = 0;
        LeaveCriticalSection(&lock);
        if (todo & JOB_REMOVE) { close_relay(); do_remove(); }
        if (todo & JOB_QUIT) return 0;
        if (todo & JOB_REFRESH) do_refresh();
        if (todo & JOB_HOST) do_host();
        if (todo & JOB_JOIN) {
            const int ok = connect_relay(game_id, NULL);
            EnterCriticalSection(&lock); joined = ok ? 1 : -1; LeaveCriticalSection(&lock);
        }
        if ((todo & JOB_UPDATE) && hosting == 1) do_update();
    }
}

static void post(unsigned job) {
    ensure_lock();
    if (!session) {
        session = WinHttpOpen(L"CTW-Native Multiplayer/1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                              WINHTTP_NO_PROXY_BYPASS, 0);
        if (!session) session = WinHttpOpen(L"CTW-Native Multiplayer/1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    }
    if (!worker) worker = CreateThread(NULL, 0, work_loop, NULL, 0, NULL);
    EnterCriticalSection(&lock);
    jobs |= job;
    LeaveCriticalSection(&lock);
    SetEvent(wake);
}

/* ---- the game's side ------------------------------------------------------------------------------------------ */
int online_available(void) { return host_name[0] != 0; }

void online_set_url(const char* base) {
    wchar_t wide[512];
    URL_COMPONENTS parts;
    host_name[0] = 0;
    if (connection) { WinHttpCloseHandle(connection); connection = NULL; }
    if (!base || !*base) return;
    MultiByteToWideChar(CP_UTF8, 0, base, -1, wide, 512);
    memset(&parts, 0, sizeof parts);
    parts.dwStructSize = sizeof parts;
    parts.lpszHostName = host_name;
    parts.dwHostNameLength = 256;
    if (!WinHttpCrackUrl(wide, 0, 0, &parts)) { host_name[0] = 0; return; }
    host_port = parts.nPort;
    secure = parts.nScheme == INTERNET_SCHEME_HTTPS;
}

void online_stop(void) {
    if (!worker) return;
    post(JOB_REMOVE | JOB_QUIT);
    WaitForSingleObject(worker, 5000);
    CloseHandle(worker);
    worker = NULL;
    hosting = joined = -1;
}

void online_leave(void) {
    if (!worker) return;
    EnterCriticalSection(&lock); hosting = joined = -1; LeaveCriticalSection(&lock);
    post(JOB_REMOVE);
}

void online_refresh(void) {
    ensure_lock();
    EnterCriticalSection(&lock); server_count = -1; LeaveCriticalSection(&lock);
    post(JOB_REFRESH);
}

int online_list(OnlineServer* out, int max) {
    ensure_lock();
    EnterCriticalSection(&lock);
    const int n = server_count < 0 ? server_count : (server_count < max ? server_count : max);
    if (n > 0) memcpy(out, servers, sizeof servers[0] * (size_t)n);
    LeaveCriticalSection(&lock);
    return n;
}

void online_host(const char* name, int max, const char* version, int is_public) {
    ensure_lock();
    EnterCriticalSection(&lock);
    snprintf(my_name, sizeof my_name, "%s", name);
    snprintf(my_version, sizeof my_version, "%s", version);
    my_max = max;
    my_public = is_public;
    hosting = 0;
    LeaveCriticalSection(&lock);
    post(JOB_HOST);
}

void online_update(const char* name, int players) {
    ensure_lock();
    EnterCriticalSection(&lock);
    snprintf(my_name, sizeof my_name, "%s", name);
    my_players = players;
    LeaveCriticalSection(&lock);
    post(JOB_UPDATE);
}

int online_hosting(char* id_out) {
    ensure_lock();
    EnterCriticalSection(&lock);
    const int state = hosting;
    if (id_out) memcpy(id_out, game_id, sizeof game_id);
    LeaveCriticalSection(&lock);
    return state;
}

void online_join(const char* id) {
    ensure_lock();
    EnterCriticalSection(&lock);
    size_t n = 0;
    for (; id[n] && n + 1 < sizeof game_id; ++n) game_id[n] = (char)(id[n] >= 'a' && id[n] <= 'z' ? id[n] - 32 : id[n]);
    game_id[n] = 0;
    game_token[0] = 0;
    joined = 0;
    LeaveCriticalSection(&lock);
    post(JOB_JOIN);
}

int online_joined(void) {
    ensure_lock();
    EnterCriticalSection(&lock);
    const int state = joined;
    LeaveCriticalSection(&lock);
    return state;
}

int online_send(uint16_t player, const void* data, size_t size) {
    uint8_t frame[PACKET + 2];
    if (!socket_handle || size > PACKET) return 0;
    DWORD length = (DWORD)size;
    const uint8_t* send = data;
    if (as_host) {
        frame[0] = (uint8_t)player;
        frame[1] = (uint8_t)(player >> 8);
        memcpy(frame + 2, data, size);
        send = frame;
        length += 2;
    }
    EnterCriticalSection(&send_lock);
    const int ok = socket_handle && WinHttpWebSocketSend(socket_handle, WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE,
                                                         (void*)send, length) == NO_ERROR;
    LeaveCriticalSection(&send_lock);
    return ok;
}

int online_receive(uint16_t* player, void* data, size_t size) {
    if (!lock_ready) return 0;
    int n = 0;
    EnterCriticalSection(&lock);
    while (queue_count) {
        const int at = queue_head;
        queue_head = (queue_head + 1) % QUEUE;
        --queue_count;
        if (queue[at].size > size) continue;
        *player = queue[at].player;
        memcpy(data, queue[at].data, queue[at].size);
        n = queue[at].size ? queue[at].size : -1;
        break;
    }
    LeaveCriticalSection(&lock);
    return n;
}

#endif
