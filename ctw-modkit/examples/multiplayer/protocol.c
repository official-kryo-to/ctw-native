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
static int vector_valid(const float* xyz, float bound) {
    for (int k = 0; k < 3; ++k) if (fabsf(xyz[k]) > bound) return 0;
    return 1;
}
static int pose_valid(const float* pos, const float* quat, const float* vel) {
    float norm = 0.f;
    for (int k = 0; k < 4; ++k) norm += quat[k] * quat[k];
    return vector_valid(pos, 32768.f) && vector_valid(vel, 512.f) && norm >= 0.5f && norm <= 1.5f;
}

static void putvisual(Writer* w, const CtwVehicleVisual* s) {
    put32(w, s->flags); put32(w, s->controls); put32(w, s->tyres); put32(w, s->burnout);
    put32(w, (uint32_t)s->steer); put32(w, (uint32_t)s->bike_lean); put32(w, (uint32_t)s->bike_pitch); put32(w, (uint32_t)s->bike_kick);
    put32(w, s->doors_open); for (int i = 0; i < 5; ++i) put16(w, (uint16_t)s->door_angles[i]); put32(w, s->frame); put32(w,s->source);
}
static void getvisual(Reader* r, CtwVehicleVisual* s) {
    s->size = sizeof *s;
    s->flags = get32(r); s->controls = get32(r); s->tyres = get32(r); s->burnout = get32(r);
    s->steer = (int32_t)get32(r); s->bike_lean = (int32_t)get32(r); s->bike_pitch = (int32_t)get32(r); s->bike_kick = (int32_t)get32(r);
    s->doors_open = get32(r); for (int i = 0; i < 5; ++i) s->door_angles[i] = get16(r); s->frame = get32(r); s->source = get32(r);
    if (s->flags > 63 || (s->controls & ~0x106u) || s->tyres > 63 || s->burnout > 3 || s->doors_open > 31 ||
        s->steer < -65536 || s->steer > 65536 || s->bike_lean < -65536 || s->bike_lean > 65536 ||
        s->bike_pitch < -65536 || s->bike_pitch > 65536 || s->bike_kick < -65536 || s->bike_kick > 65536) r->ok = 0;
}

static void putpickup(Writer* w, const CtwWorldPickup* s) {
    put32(w,s->id); put8(w,(uint8_t)s->type); put32(w,(uint32_t)s->amount);
    for(int k=0;k<3;++k) put32(w,(uint32_t)s->position[k]); put8(w,(uint8_t)s->in_vehicle);
}
static void getpickup(Reader* r, CtwWorldPickup* s) {
    s->size=sizeof *s; s->id=get32(r); s->type=get8(r); s->amount=(int32_t)get32(r);
    for(int k=0;k<3;++k) { s->position[k]=(int32_t)get32(r); if(s->position[k]<-134217727 || s->position[k]>134217727) r->ok=0; }
    s->in_vehicle=get8(r);
    if(!s->id || s->type>33 || s->amount<0 || s->in_vehicle>1) r->ok=0;
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
    case MP_WELCOME: put8(&w, m->u.welcome.slot); put8(&w, m->u.welcome.max); put32(&w,m->u.welcome.skid_baseline); break;
    case MP_REFUSE: put8(&w, m->u.refuse.reason); break;
    case MP_STATE: {
        const MpState* s = &m->u.state;
        put8(&w, s->slot); put16(&w, s->seq);
        putfs(&w, s->pos, 3); putf(&w, s->heading); putfs(&w, s->vel, 3);
        put8(&w, s->move); put8(&w, s->flags); put8(&w, s->health); put8(&w, s->armour);
        put8(&w, s->body); put8(&w, s->upper); put8(&w, s->legs); put8(&w, s->weapon);
        put16(&w, (uint16_t)s->model); put8(&w, s->palette);
        put32(&w, s->vehicle); put8(&w, (uint8_t)s->seat);
        if (s->model >= 0) { putfs(&w, s->vpos, 3); putfs(&w, s->vquat, 4); putfs(&w, s->vvel, 3); putvisual(&w, &s->visual); }
        for (int k = 0; k < 3; ++k) put32(&w, (uint32_t)s->view_eye[k]);
        put16(&w, s->view_yaw); put16(&w, s->view_pitch); put16(&w, s->view_width); put16(&w, s->view_height);
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
    case MP_WORLD_VEHICLE: case MP_VEHICLE_CREATE: {
        const CtwWorldVehicle* v = &m->u.vehicle.state;
        put32(&w, m->u.vehicle.seq); put32(&w, v->id);
        put16(&w, (uint16_t)v->model); put8(&w, (uint8_t)v->palette); put8(&w, (uint8_t)v->health);
        put32(&w, v->flags); putfs(&w, v->position, 3); putfs(&w, v->quaternion, 4); putfs(&w, v->velocity, 3);
        putvisual(&w, &m->u.vehicle.visual);
        break;
    }
    case MP_WORLD_PED: {
        const CtwWorldPed* p = &m->u.ped.state;
        put32(&w, m->u.ped.seq); put32(&w, p->id);
        put8(&w, (uint8_t)p->type); put8(&w, (uint8_t)p->subtype); put8(&w, (uint8_t)p->body);
        put8(&w, (uint8_t)p->upper); put8(&w, (uint8_t)p->legs); put8(&w, (uint8_t)p->move);
        put16(&w, (uint16_t)p->health); put8(&w, (uint8_t)p->weapon); put32(&w, p->flags);
        putfs(&w, p->position, 3); putf(&w, p->heading); putfs(&w, p->velocity, 3);
        put32(&w, p->vehicle); put8(&w, (uint8_t)p->seat);
        break;
    }
    case MP_EXPLOSION: {
        const CtwWorldExplosion* e = &m->u.explosion.state;
        put32(&w, m->u.explosion.seq); put8(&w, (uint8_t)e->type);
        putfs(&w, e->position, 3); putfs(&w, e->velocity, 3); putf(&w, e->scale); break;
    }
    case MP_ENTITY_DAMAGE:
        put32(&w, m->u.damage.seq); put32(&w, m->u.damage.id);
        put16(&w, m->u.damage.amount); put8(&w, m->u.damage.ped); break;
    case MP_WORLD_REMOVE:
        put32(&w, m->u.remove.seq); put32(&w, m->u.remove.id); put8(&w, m->u.remove.ped); break;
    case MP_VEHICLE_CREATED: put32(&w, m->u.created.request); put32(&w, m->u.created.id); break;
    case MP_VEHICLE_DOOR:
        put32(&w, m->u.door.seq); put32(&w, m->u.door.id); put8(&w, m->u.door.seat); put8(&w, m->u.door.open); put16(&w, m->u.door.angle); break;
    case MP_SKID_BATCH:
        if(!m->u.skid.count || m->u.skid.count>MP_SKID_POINTS) return 0;
        put32(&w,m->u.skid.seq); put8(&w,m->u.skid.count);
        for(int i=0;i<m->u.skid.count;++i) {
            const CtwSkidPoint* p=&m->u.skid.points[i];
            put32(&w,p->vehicle); put8(&w,(uint8_t)p->wheel); put32(&w,p->strip); put32(&w,p->sequence);
            put8(&w,(uint8_t)p->hold); put32(&w,p->owner);
            for(int k=0;k<3;++k) put32(&w,(uint32_t)p->position[k]);
            for(int k=0;k<3;++k) put16(&w,(uint16_t)p->direction[k]);
        }
        break;
    case MP_PICKUP_STATE: put32(&w,m->u.pickup.seq); putpickup(&w,&m->u.pickup.state); break;
    case MP_PICKUP_REMOVE: put32(&w,m->u.pickup_remove.seq); put32(&w,m->u.pickup_remove.id); break;
    case MP_PICKUP_CLAIM: put32(&w,m->u.pickup_claim.id); break;
    case MP_PICKUP_GRANT: putpickup(&w,&m->u.pickup_grant.state); break;
    case MP_PICKUP_ACK: put32(&w,m->u.pickup_ack.id); break;
    case MP_WORLD_HIT: put32(&w,m->u.world_hit.seq); put8(&w,m->u.world_hit.victim); put16(&w,m->u.world_hit.damage); break;
    case MP_SKID_ACK: put32(&w,m->u.skid_ack.seq); break;
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
    case MP_WELCOME:
        m->u.welcome.slot = get8(&r); m->u.welcome.max = get8(&r);
        m->u.welcome.skid_baseline=get32(&r);
        if (m->u.welcome.max < 2 || m->u.welcome.max > MP_MAX_PLAYERS || !m->u.welcome.slot ||
            m->u.welcome.slot >= m->u.welcome.max) return 0;
        break;
    case MP_REFUSE: m->u.refuse.reason = get8(&r); break;
    case MP_STATE: {
        MpState* s = &m->u.state;
        s->slot = get8(&r); s->seq = get16(&r);
        getfs(&r, s->pos, 3); s->heading = getf(&r); getfs(&r, s->vel, 3);
        s->move = get8(&r); s->flags = get8(&r); s->health = get8(&r); s->armour = get8(&r);
        s->body = get8(&r); s->upper = get8(&r); s->legs = get8(&r); s->weapon = get8(&r);
        s->model = (int16_t)get16(&r); s->palette = get8(&r);
        s->vehicle = get32(&r); s->seat = (int8_t)get8(&r);
        if (s->model >= 0) { getfs(&r, s->vpos, 3); getfs(&r, s->vquat, 4); getfs(&r, s->vvel, 3); getvisual(&r, &s->visual); }
        for (int k = 0; k < 3; ++k) s->view_eye[k] = (int32_t)get32(&r);
        s->view_yaw = get16(&r); s->view_pitch = get16(&r); s->view_width = get16(&r); s->view_height = get16(&r);
        if (s->view_width > 16384 || s->view_height > 16384 || (s->view_width && !s->view_height)) return 0;
        for (int k = 0; k < 3; ++k) if (s->view_eye[k] < -134217727 || s->view_eye[k] > 134217727) return 0;
        if (s->slot >= MP_MAX_PLAYERS || s->seat < -1 || s->seat > 3) return 0;
        if (!vector_valid(s->pos, 32768.f) || !vector_valid(s->vel, 512.f) || fabsf(s->heading) > 36000.f ||
            s->body > 1 || s->upper >= 64 || s->legs >= 64 || s->weapon > 24 || s->move > 3 ||
            (s->model >= 0 && (!pose_valid(s->vpos, s->vquat, s->vvel) || s->palette > 26)) ||
            (s->vehicle && (s->model < 0 || s->seat < 0 || !(s->flags & MP_IN_VEHICLE)))) return 0;
        break;
    }
    case MP_ROSTER:
        m->u.roster.count = get8(&r);
        if (m->u.roster.count > MP_MAX_PLAYERS) return 0;
        for (int i = 0; i < m->u.roster.count; ++i) {
            m->u.roster.slots[i] = get8(&r);
            getname(&r, m->u.roster.names[i], MP_NAME);
            if (m->u.roster.slots[i] >= MP_MAX_PLAYERS) return 0;
            for (int j = 0; j < i; ++j) if (m->u.roster.slots[j] == m->u.roster.slots[i]) return 0;
        }
        break;
    case MP_CHAT_MSG:
        m->u.chat.slot = get8(&r); getname(&r, m->u.chat.text, MP_CHAT);
        if (m->u.chat.slot >= MP_MAX_PLAYERS) return 0;
        break;
    case MP_HIT:
        m->u.hit.attacker = get8(&r); m->u.hit.victim = get8(&r); m->u.hit.damage = get16(&r);
        if (m->u.hit.victim >= MP_MAX_PLAYERS || m->u.hit.attacker >= MP_MAX_PLAYERS) return 0;
        break;
    case MP_WORLD:
        m->u.world.time = getf(&r); m->u.world.weather = get8(&r);
        if (m->u.world.time < 0.f || m->u.world.time > 24.f || m->u.world.weather > 7) return 0;
        break;
    case MP_WORLD_VEHICLE: case MP_VEHICLE_CREATE: {
        CtwWorldVehicle* v = &m->u.vehicle.state; v->size = sizeof *v;
        m->u.vehicle.seq = get32(&r); v->id = get32(&r);
        v->model = (int16_t)get16(&r); v->palette = get8(&r); v->health = get8(&r);
        v->flags = get32(&r); getfs(&r, v->position, 3); getfs(&r, v->quaternion, 4); getfs(&r, v->velocity, 3);
        getvisual(&r, &m->u.vehicle.visual);
        if (!v->id || v->model < 0 || v->palette > 26 || !pose_valid(v->position, v->quaternion, v->velocity)) return 0;
        break;
    }
    case MP_WORLD_PED: {
        CtwWorldPed* p = &m->u.ped.state; p->size = sizeof *p;
        m->u.ped.seq = get32(&r); p->id = get32(&r);
        p->type = get8(&r); p->subtype = get8(&r); p->body = get8(&r); p->upper = get8(&r);
        p->legs = get8(&r); p->move = get8(&r); p->health = get16(&r); p->weapon = get8(&r); p->flags = get32(&r);
        getfs(&r, p->position, 3); p->heading = getf(&r); getfs(&r, p->velocity, 3);
        p->vehicle = get32(&r); p->seat = (int8_t)get8(&r);
        if (!p->id || p->type > 19 || p->body > 1 || p->upper >= 64 || p->legs >= 64 || p->move > 3 ||
            p->weapon > 24 || p->seat < -1 || p->seat > 3 || !vector_valid(p->position, 32768.f) ||
            !vector_valid(p->velocity, 512.f) || fabsf(p->heading) > 36000.f) return 0;
        break;
    }
    case MP_EXPLOSION: {
        CtwWorldExplosion* e = &m->u.explosion.state; e->size = sizeof *e;
        m->u.explosion.seq = get32(&r); e->type = get8(&r);
        getfs(&r, e->position, 3); getfs(&r, e->velocity, 3); e->scale = getf(&r);
        if (e->type > 11 || e->scale <= 0.f || e->scale > 7.f || !vector_valid(e->position, 32768.f) ||
            !vector_valid(e->velocity, 512.f)) return 0;
        break;
    }
    case MP_ENTITY_DAMAGE:
        m->u.damage.seq = get32(&r); m->u.damage.id = get32(&r);
        m->u.damage.amount = get16(&r); m->u.damage.ped = get8(&r);
        if (!m->u.damage.id || !m->u.damage.amount || m->u.damage.amount > 255 || m->u.damage.ped > 1) return 0;
        break;
    case MP_WORLD_REMOVE:
        m->u.remove.seq = get32(&r); m->u.remove.id = get32(&r); m->u.remove.ped = get8(&r);
        if (!m->u.remove.id || m->u.remove.ped > 1) return 0;
        break;
    case MP_VEHICLE_CREATED:
        m->u.created.request = get32(&r); m->u.created.id = get32(&r);
        if (!m->u.created.request || !m->u.created.id) return 0;
        break;
    case MP_VEHICLE_DOOR:
        m->u.door.seq = get32(&r); m->u.door.id = get32(&r); m->u.door.seat = get8(&r);
        m->u.door.open = get8(&r); m->u.door.angle = get16(&r);
        if (!m->u.door.id || m->u.door.seat > 3 || m->u.door.open > 1) return 0;
        break;
    case MP_SKID_BATCH:
        m->u.skid.seq=get32(&r); m->u.skid.count=get8(&r);
        if(!m->u.skid.count || m->u.skid.count>MP_SKID_POINTS) return 0;
        for(int i=0;i<m->u.skid.count;++i) {
            CtwSkidPoint* p=&m->u.skid.points[i]; p->size=sizeof *p;
            p->vehicle=get32(&r); p->wheel=get8(&r); p->strip=get32(&r); p->sequence=get32(&r);
            p->hold=get8(&r); p->owner=get32(&r);
            for(int k=0;k<3;++k) { p->position[k]=(int32_t)get32(&r); if(p->position[k]<-134217727 || p->position[k]>134217727) r.ok=0; }
            for(int k=0;k<3;++k) { p->direction[k]=(int16_t)get16(&r); if(p->direction[k]<-8192 || p->direction[k]>8192) r.ok=0; }
            if(!p->vehicle || p->wheel>3 || !p->strip || !p->sequence || (p->hold!=0 && p->hold!=1 && p->hold!=4)) r.ok=0;
        }
        break;
    case MP_PICKUP_STATE: m->u.pickup.seq=get32(&r); getpickup(&r,&m->u.pickup.state); break;
    case MP_PICKUP_REMOVE: m->u.pickup_remove.seq=get32(&r); m->u.pickup_remove.id=get32(&r); if(!m->u.pickup_remove.id) return 0; break;
    case MP_PICKUP_CLAIM: m->u.pickup_claim.id=get32(&r); if(!m->u.pickup_claim.id) return 0; break;
    case MP_PICKUP_GRANT: getpickup(&r,&m->u.pickup_grant.state); break;
    case MP_PICKUP_ACK: m->u.pickup_ack.id=get32(&r); if(!m->u.pickup_ack.id) return 0; break;
    case MP_WORLD_HIT:
        m->u.world_hit.seq=get32(&r); m->u.world_hit.victim=get8(&r); m->u.world_hit.damage=get16(&r);
        if(m->u.world_hit.victim>=MP_MAX_PLAYERS || !m->u.world_hit.damage) return 0; break;
    case MP_SKID_ACK: m->u.skid_ack.seq=get32(&r); break;
    case MP_BYE: break;
    default: return 0;
    }
    return r.ok && r.at == size;
}
