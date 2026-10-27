/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kryo.to
 * See LICENSE in the repository root.
 */
#include "protocol.h"
#include <math.h>
#include <string.h>

typedef struct Writer { uint8_t* p; size_t size, at; int ok; } Writer;
typedef struct Reader { const uint8_t* p; size_t size, at; int ok; } Reader;

static void put(Writer* w, const void* v, size_t n) {
    if (!w->ok || w->at + n > w->size) { w->ok = 0; return; }
    memcpy(w->p + w->at, v, n);
    w->at += n;
}
static void put8(Writer* w, uint8_t v) { put(w, &v, 1); }
static void put16(Writer* w, uint16_t v) { const uint8_t b[2] = {(uint8_t)v, (uint8_t)(v >> 8)}; put(w, b, 2); }
static void put32(Writer* w, uint32_t v) {
    const uint8_t b[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
    put(w, b, 4);
}
static void putf(Writer* w, float f) { uint32_t v; memcpy(&v, &f, 4); put32(w, v); }
static void putfs(Writer* w, const float* f, int n) { for (int i = 0; i < n; ++i) putf(w, f[i]); }
static void putname(Writer* w, const char* s, size_t n) {
    char buf[MP_CHAT];
    memset(buf, 0, sizeof buf);
    strncpy(buf, s, n - 1);
    put(w, buf, n);
}

static void get(Reader* r, void* v, size_t n) {
    if (!r->ok || r->at + n > r->size) { r->ok = 0; memset(v, 0, n); return; }
    memcpy(v, r->p + r->at, n);
    r->at += n;
}
static uint8_t get8(Reader* r) { uint8_t v; get(r, &v, 1); return v; }
static uint16_t get16(Reader* r) { uint8_t b[2]; get(r, b, 2); return (uint16_t)(b[0] | b[1] << 8); }
static uint32_t get32(Reader* r) {
    uint8_t b[4];
    get(r, b, 4);
    return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
}
static float getf(Reader* r) {
    const uint32_t v = get32(r);
    float f;
    memcpy(&f, &v, 4);
    if (!isfinite(f)) { r->ok = 0; f = 0.f; }   /* nothing that is not a number gets into the game */
    return f;
}
static void getfs(Reader* r, float* f, int n) { for (int i = 0; i < n; ++i) f[i] = getf(r); }
static void getname(Reader* r, char* s, size_t n) {
    get(r, s, n);
    s[n - 1] = 0;
    for (size_t i = 0; s[i]; ++i) if ((unsigned char)s[i] < 0x20) s[i] = ' ';   /* no control characters */
}

size_t mp_encode(const MpMessage* m, uint8_t* out, size_t size) {
    Writer w = {out, size, 0, 1};
    put(&w, "CTWM", 4);
    put8(&w, MP_VERSION);
    put8(&w, m->type);
    put8(&w, m->sender);
    switch (m->type) {
    case MP_HELLO:
        put16(&w, m->u.hello.api);
        putname(&w, m->u.hello.name, MP_NAME);
        put8(&w, m->u.hello.body); put8(&w, m->u.hello.upper); put8(&w, m->u.hello.legs);
        break;
    case MP_WELCOME: put8(&w, m->u.welcome.slot); put8(&w, m->u.welcome.max); break;
    case MP_REFUSE: put8(&w, m->u.refuse.reason); break;
    case MP_STATE: {
        const MpState* s = &m->u.state;
        put8(&w, s->slot); put16(&w, s->seq);
        putfs(&w, s->pos, 3); putf(&w, s->heading); putfs(&w, s->vel, 3);
        put8(&w, s->move); put8(&w, s->flags); put8(&w, s->health); put8(&w, s->armour);
        put8(&w, s->body); put8(&w, s->upper); put8(&w, s->legs); put8(&w, s->weapon);
        put16(&w, (uint16_t)s->model); put8(&w, s->palette);
        if (s->model >= 0) { putfs(&w, s->vpos, 3); putfs(&w, s->vquat, 4); putfs(&w, s->vvel, 3); }
        break;
    }
    case MP_ROSTER:
        put8(&w, m->u.roster.count);
        for (int i = 0; i < m->u.roster.count && i < MP_MAX_PLAYERS; ++i) {
            put8(&w, m->u.roster.slots[i]);
            putname(&w, m->u.roster.names[i], MP_NAME);
        }
        break;
    case MP_CHAT_MSG: put8(&w, m->u.chat.slot); putname(&w, m->u.chat.text, MP_CHAT); break;
    case MP_HIT: put8(&w, m->u.hit.attacker); put8(&w, m->u.hit.victim); put16(&w, m->u.hit.damage); break;
    case MP_WORLD: putf(&w, m->u.world.time); put8(&w, m->u.world.weather); break;
    case MP_BYE: break;
    default: return 0;
    }
    return w.ok ? w.at : 0;
}

int mp_decode(const uint8_t* data, size_t size, MpMessage* m) {
    Reader r = {data, size, 0, 1};
    char magic[4];
    memset(m, 0, sizeof *m);
    get(&r, magic, 4);
    if (!r.ok || memcmp(magic, "CTWM", 4) != 0) return 0;
    const uint8_t version = get8(&r);
    m->type = get8(&r);
    m->sender = get8(&r);
    /* a different protocol can only say hello (and be refused) */
    if (!r.ok || (version != MP_VERSION && m->type != MP_HELLO)) return 0;
    switch (m->type) {
    case MP_HELLO:
        m->u.hello.api = get16(&r);
        getname(&r, m->u.hello.name, MP_NAME);
        m->u.hello.body = get8(&r); m->u.hello.upper = get8(&r); m->u.hello.legs = get8(&r);
        if (version != MP_VERSION) m->u.hello.api = 0;   /* (the host refuses it) */
        break;
    case MP_WELCOME: m->u.welcome.slot = get8(&r); m->u.welcome.max = get8(&r); break;
    case MP_REFUSE: m->u.refuse.reason = get8(&r); break;
    case MP_STATE: {
        MpState* s = &m->u.state;
        s->slot = get8(&r); s->seq = get16(&r);
        getfs(&r, s->pos, 3); s->heading = getf(&r); getfs(&r, s->vel, 3);
        s->move = get8(&r); s->flags = get8(&r); s->health = get8(&r); s->armour = get8(&r);
        s->body = get8(&r); s->upper = get8(&r); s->legs = get8(&r); s->weapon = get8(&r);
        s->model = (int16_t)get16(&r); s->palette = get8(&r);
        if (s->model >= 0) { getfs(&r, s->vpos, 3); getfs(&r, s->vquat, 4); getfs(&r, s->vvel, 3); }
        if (s->slot >= MP_MAX_PLAYERS) return 0;
        break;
    }
    case MP_ROSTER:
        m->u.roster.count = get8(&r);
        if (m->u.roster.count > MP_MAX_PLAYERS) return 0;
        for (int i = 0; i < m->u.roster.count; ++i) {
            m->u.roster.slots[i] = get8(&r);
            getname(&r, m->u.roster.names[i], MP_NAME);
            if (m->u.roster.slots[i] >= MP_MAX_PLAYERS) return 0;
        }
        break;
    case MP_CHAT_MSG: m->u.chat.slot = get8(&r); getname(&r, m->u.chat.text, MP_CHAT); break;
    case MP_HIT:
        m->u.hit.attacker = get8(&r); m->u.hit.victim = get8(&r); m->u.hit.damage = get16(&r);
        if (m->u.hit.victim >= MP_MAX_PLAYERS || m->u.hit.attacker >= MP_MAX_PLAYERS) return 0;
        break;
    case MP_WORLD: m->u.world.time = getf(&r); m->u.world.weather = get8(&r); break;
    case MP_BYE: break;
    default: return 0;
    }
    return r.ok && r.at == size;
}
