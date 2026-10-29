/* SPDX-License-Identifier: GPL-3.0-only
 * Host authority for city entities. State is disposable/repeated; effects and damage have replay windows.
 * Every car has one host ID, independent of the number of players in it.
 */
#include "worldsync.h"
#include <math.h>
#include <string.h>

enum { VEHICLE_LIMIT = 1024, PED_LIMIT = 2048, EVENT_WINDOW = 64 };
typedef struct Replica { uint32_t id, seq; double heard; int health, removed; } Replica;
typedef struct Replay { uint32_t newest; uint64_t bits; } Replay;
static const CtwApi* game;
static MpWorldSend send_message;
static int role;
static Replica vehicles[VEHICLE_LIMIT], pedestrians[PED_LIMIT];
static MpState occupants[MP_MAX_PLAYERS];
static double player_heard[MP_MAX_PLAYERS];
static MpState skid_driver[MP_MAX_PLAYERS];
static double skid_driver_time[MP_MAX_PLAYERS];
static Replay explosions[MP_MAX_PLAYERS], damages[MP_MAX_PLAYERS];
static Replay doors[MP_MAX_PLAYERS];
static uint32_t skid_owner[MP_MAX_PLAYERS], skid_epoch;
enum { SKID_OUT = 128, SKID_WINDOW = 32 };
typedef struct SkidOut { MpMessage message; uint32_t waiting; double sent; int used, except; } SkidOut;
typedef struct SkidIn { MpMessage message; int used; } SkidIn;
static SkidOut skid_out[SKID_OUT];
static SkidIn skid_in[MP_MAX_PLAYERS][SKID_WINDOW];
static uint32_t skid_next[MP_MAX_PLAYERS], skid_send_sequence, skid_peers;
static CtwVehicle last_door_car;
static int last_door_seat;
static double last_door_sent;
static uint32_t event_seq;
static double last_snapshot;
static uint32_t snapshot_seq;
static int vehicle_cursor, ped_cursor;
static uint32_t vehicle_sweep, ped_sweep;
static MpState requests[64];
static uint16_t newest_request;
static int has_request;
static uint16_t newest_ack;
static int has_ack;
static int own_slot;
typedef struct Created { uint32_t request, id; } Created;
static Created created[MP_MAX_PLAYERS][VEHICLE_LIMIT];
static uint32_t created_next[MP_MAX_PLAYERS];
static double last_create_scan;
static int create_cursor;

static int newer32(uint32_t a, uint32_t b) { return (int32_t)(a - b) > 0; }
static int accept(Replay* r, uint32_t seq) {
    if (!r->bits) { r->newest = seq; r->bits = 1; return 1; }
    if (newer32(seq, r->newest)) {
        uint32_t d = seq - r->newest;
        r->bits = d >= EVENT_WINDOW ? 1 : (r->bits << d) | 1;
        r->newest = seq; return 1;
    }
    uint32_t d = r->newest - seq;
    if (d >= EVENT_WINDOW || (r->bits & ((uint64_t)1 << d))) return 0;
    r->bits |= (uint64_t)1 << d; return 1;
}
static Replica* replica(Replica* list, int count, uint32_t id) {
    Replica* empty = NULL;
    for (int i = 0; i < count; ++i) {
        if (list[i].id == id) return &list[i];
        if (!list[i].id && !empty) empty = &list[i];
    }
    return empty;
}
static void send_event(MpMessage* m, int except) {
    /* Repeated datagrams tolerate occasional loss. Receivers apply each sequence once. */
    for (int i = 0; i < 3; ++i) send_message(m, -1, except);
}
int mp_world_start(const CtwApi* api, int next_role, MpWorldSend send) {
    mp_world_stop();
    if (!api || !send || !CTW_MOD_HAS(api, set_multiplayer_world) || !api->set_multiplayer_world(next_role)) return 0;
    game = api; role = next_role; send_message = send;
    memset(occupants, 0, sizeof occupants); memset(player_heard, 0, sizeof player_heard);
    memset(skid_driver,0,sizeof skid_driver); memset(skid_driver_time,0,sizeof skid_driver_time);
    memset(explosions, 0, sizeof explosions); memset(damages, 0, sizeof damages);
    memset(doors, 0, sizeof doors); last_door_car = 0; last_door_sent = -1.;
    memset(skid_owner,0,sizeof skid_owner); memset(skid_out,0,sizeof skid_out); memset(skid_in,0,sizeof skid_in);
    for(int p=0;p<MP_MAX_PLAYERS;++p) skid_next[p]=1;
    skid_send_sequence=0; skid_peers=0;
    event_seq = 0; last_snapshot = -1.;
    snapshot_seq = 0; vehicle_cursor = ped_cursor = 0;
    vehicle_sweep = ped_sweep = 0;
    memset(requests, 0, sizeof requests); has_request = 0;
    has_ack = 0; own_slot = 0;
    memset(created, 0, sizeof created); memset(created_next, 0, sizeof created_next);
    last_create_scan = -1.; create_cursor = 0;
    return 1;
}
void mp_world_stop(void) {
    if (game) game->set_multiplayer_world(CTW_MULTIPLAYER_OFF);
    game = NULL; role = 0; send_message = NULL;
    memset(vehicles, 0, sizeof vehicles); memset(pedestrians, 0, sizeof pedestrians);
}
void mp_world_forget(int slot) {
    if (slot < 0 || slot >= MP_MAX_PLAYERS) return;
    memset(&occupants[slot], 0, sizeof occupants[slot]); player_heard[slot] = 0.;
    memset(&skid_driver[slot],0,sizeof skid_driver[slot]); skid_driver_time[slot]=0.;
    memset(&explosions[slot], 0, sizeof explosions[slot]); memset(&damages[slot], 0, sizeof damages[slot]);
    memset(&doors[slot], 0, sizeof doors[slot]);
    memset(skid_in[slot],0,sizeof skid_in[slot]); skid_next[slot]=1; skid_owner[slot]=0;
    skid_peers&=~(1u<<slot);
    for(int p=0;p<SKID_OUT;++p) { skid_out[p].waiting&=~(1u<<slot); if(!skid_out[p].waiting) skid_out[p].used=0; }
    memset(created[slot], 0, sizeof created[slot]); created_next[slot] = 0;
}
void mp_world_player(int slot, MpState* s, double now) {
    if (!game || slot < 0 || slot >= MP_MAX_PLAYERS) return;
    player_heard[slot] = now;
    if (role == CTW_MULTIPLAYER_HOST && s->vehicle) {
        CtwVehicle handle = game->world_vehicle_handle(s->vehicle);
        CtwVehicleState car; memset(&car, 0, sizeof car); car.size = sizeof car;
        if (!handle || !game->get_vehicle_state(handle, &car) || (car.flags & CTW_VEHICLE_DEAD)) {
            s->vehicle = 0; s->seat = -1; s->model = -1; s->flags &= ~MP_IN_VEHICLE;
        } else {
            int used[4] = {0};
            for (int p = 0; p < MP_MAX_PLAYERS; ++p)
                if (p != slot && now - player_heard[p] < 10. && occupants[p].vehicle == s->vehicle &&
                    occupants[p].seat >= 0 && occupants[p].seat < 4) used[occupants[p].seat] = 1;
            const int seats = CTW_MOD_HAS(game, vehicle_seat_count) ? game->vehicle_seat_count(handle) :
                              (car.flags & CTW_VEHICLE_BIKE) ? 2 : 4;
            const int bounded_seats = seats < 1 ? 1 : seats > 4 ? 4 : seats;
            if (s->seat < 0 || s->seat >= bounded_seats || used[s->seat]) {
                s->seat = -1;
                for (int seat = 1; seat < bounded_seats; ++seat) if (!used[seat]) { s->seat = (int8_t)seat; break; }
            }
            if (s->seat < 0) { s->vehicle = 0; s->model = -1; s->flags &= ~MP_IN_VEHICLE; }
            else {
                s->model = (int16_t)car.model_id; s->palette = (uint8_t)car.palette;
                if (s->seat == 0 && slot != 0) {
                    CtwWorldVehicle v; memset(&v, 0, sizeof v); v.size = sizeof v;
                    v.id = s->vehicle; v.model = car.model_id; v.palette = car.palette; v.health = car.health; v.flags = car.flags;
                    memcpy(v.position, s->vpos, sizeof v.position); memcpy(v.quaternion, s->vquat, sizeof v.quaternion);
                    memcpy(v.velocity, s->vvel, sizeof v.velocity); game->set_world_vehicle(&v);
                    if (CTW_MOD_HAS(game, set_vehicle_visual)) game->set_vehicle_visual(handle, &s->visual);
                }
            }
        }
    }
    occupants[slot] = *s;
    if(s->vehicle && s->seat==0) { skid_driver[slot]=*s; skid_driver_time[slot]=now; }
}
void mp_world_correct_seat(const MpState* accepted) {
    if (!game) return;
    if (has_ack && (int16_t)(accepted->seq - newest_ack) <= 0) return;
    newest_ack = accepted->seq; has_ack = 1;
    CtwPlayerState p; memset(&p, 0, sizeof p); p.size = sizeof p;
    if (!game->get_player_state(&p)) return;
    /* A delayed acknowledgment must never undo an exit or entry into a different car. */
    const MpState* request = &requests[accepted->seq % 64];
    if (!has_request || request->seq != accepted->seq || !p.vehicle ||
        game->world_vehicle_id(p.vehicle) != request->vehicle || requests[newest_request % 64].vehicle != request->vehicle) return;
    if (!accepted->vehicle) game->set_player_seat(0, -1);
    else if (game->get_player_seat() != accepted->seat) game->set_player_seat(p.vehicle, accepted->seat);
}
void mp_world_request(const MpState* requested) {
    requests[requested->seq % 64] = *requested; newest_request = requested->seq; has_request = 1;
    own_slot = requested->slot;
}
static void entity_damage(uint32_t id, int ped, int amount, int slot);
uint32_t mp_world_skid_baseline(void) { return skid_send_sequence+1; }
void mp_world_set_skid_baseline(uint32_t sequence) { skid_next[0]=sequence ? sequence : 1; }
void mp_world_peer_join(int slot) { if(slot>0 && slot<MP_MAX_PLAYERS) skid_peers|=1u<<slot; }
static SkidOut* skid_free(void) {
    for(int i=0;i<SKID_OUT;++i) if(!skid_out[i].used) return &skid_out[i]; return NULL;
}
static int queue_skid(MpMessage* m, int except) {
    // The host stream is contiguous for every peer, including the point's producer. Producers ACK their own
    // batches without drawing them twice; excluding them would create a permanent hole in this sequence.
    if(role==CTW_MULTIPLAYER_HOST) except=-1;
    uint32_t recipients=role==CTW_MULTIPLAYER_HOST ? skid_peers : 1;
    if(except>=0) recipients&=~(1u<<except);
    SkidOut* out=skid_free(); if(recipients && !out) return 0;
    m->u.skid.seq=++skid_send_sequence; m->sender=(uint8_t)(role==CTW_MULTIPLAYER_HOST?0:own_slot);
    if(!recipients) return 1;
    out->message=*m; out->waiting=recipients; out->sent=-1; out->used=1; out->except=except; return 1;
}
static void skid_ack(int slot,uint32_t sequence) {
    MpMessage ack; memset(&ack,0,sizeof ack); ack.type=MP_SKID_ACK; ack.sender=(uint8_t)own_slot;
    ack.u.skid_ack.seq=sequence; send_message(&ack,role==CTW_MULTIPLAYER_HOST?slot:-1,-1);
}
static void flush_skids(int slot,double now) {
    for(int count=0;count<SKID_WINDOW;++count) {
        SkidIn* in=&skid_in[slot][skid_next[slot]%SKID_WINDOW];
        if(!in->used || in->message.u.skid.seq!=skid_next[slot]) return;
        if(role==CTW_MULTIPLAYER_HOST && !skid_free()) return;
        MpMessage relay; memset(&relay,0,sizeof relay); relay.type=MP_SKID_BATCH;
        for(int i=0;i<in->message.u.skid.count;++i) {
            CtwSkidPoint p=in->message.u.skid.points[i];
            if(role==CTW_MULTIPLAYER_CLIENT && p.owner && (p.owner&31u)==(uint32_t)own_slot) continue;
            if(game->add_world_skid(&p) && role==CTW_MULTIPLAYER_HOST) relay.u.skid.points[relay.u.skid.count++]=p;
        }
        if(role==CTW_MULTIPLAYER_HOST && relay.u.skid.count) queue_skid(&relay,slot);
        skid_ack(slot,skid_next[slot]); in->used=0; ++skid_next[slot];
    }
}
void mp_world_receive(int slot, const MpMessage* m, double now) {
    if (!game || slot < 0 || slot >= MP_MAX_PLAYERS) return;
    if (m->type == MP_SKID_BATCH && CTW_MOD_HAS(game,add_world_skid)) {
        if ((role==CTW_MULTIPLAYER_CLIENT && slot!=0) || !m->u.skid.count || m->u.skid.count>MP_SKID_POINTS) return;
        const int32_t ahead=(int32_t)(m->u.skid.seq-skid_next[slot]);
        if(ahead<0) { skid_ack(slot,m->u.skid.seq); return; }
        if(ahead>=SKID_WINDOW) return;
        SkidIn* in=&skid_in[slot][m->u.skid.seq%SKID_WINDOW];
        if(in->used && in->message.u.skid.seq==m->u.skid.seq) { flush_skids(slot,now); return; }
        in->message=*m; in->used=1;
        if(role==CTW_MULTIPLAYER_HOST) {
            in->message.u.skid.count=0;
            const MpState* driver=&skid_driver[slot];
            for(int i=0;i<m->u.skid.count;++i) {
                CtwSkidPoint p=m->u.skid.points[i];
                if(slot<=0 || now-player_heard[slot]>10. || now-skid_driver_time[slot]>2. || driver->vehicle!=p.vehicle) continue;
                float distance=0; for(int k=0;k<3;++k) { float d=p.position[k]/4096.f-driver->vpos[k]; distance+=d*d; }
                if(distance>32.f*32.f) continue;
                if(!skid_owner[slot]) skid_owner[slot]=((++skid_epoch)<<5)|(uint32_t)slot;
                p.owner=skid_owner[slot]; in->message.u.skid.points[in->message.u.skid.count++]=p;
            }
        }
        flush_skids(slot,now);
    } else if(m->type==MP_SKID_ACK) {
        if(role==CTW_MULTIPLAYER_CLIENT && slot!=0) return;
        for(int i=0;i<SKID_OUT;++i) if(skid_out[i].used && skid_out[i].message.u.skid.seq==m->u.skid_ack.seq) {
            skid_out[i].waiting&=~(1u<<slot); if(!skid_out[i].waiting) skid_out[i].used=0; break;
        }
    } else if (m->type == MP_VEHICLE_CREATE && role == CTW_MULTIPLAYER_HOST && slot > 0) {
        const CtwWorldVehicle* requested = &m->u.vehicle.state;
        uint32_t id = 0;
        for (int i = 0; i < VEHICLE_LIMIT; ++i) if (created[slot][i].request == requested->id) { id = created[slot][i].id; break; }
        if (!id && CTW_MOD_HAS(game, live_vehicle_count) && game->live_vehicle_count() < VEHICLE_LIMIT) {
            CtwVehicle h = game->spawn_vehicle_at(requested->model, requested->position, 0.f, requested->palette);
            if (h) {
                id = game->world_vehicle_id(h);
                CtwWorldVehicle v = *requested; v.id = id; game->set_world_vehicle(&v);
                Created* c = &created[slot][created_next[slot]++ % VEHICLE_LIMIT]; c->request = requested->id; c->id = id;
            }
        }
        if (id) {
            MpMessage reply; memset(&reply, 0, sizeof reply); reply.type = MP_VEHICLE_CREATED;
            reply.u.created.request = requested->id; reply.u.created.id = id; send_message(&reply, slot, -1);
        }
    } else if (m->type == MP_VEHICLE_CREATED && role == CTW_MULTIPLAYER_CLIENT && slot == 0) {
        if (CTW_MOD_HAS(game, adopt_world_vehicle)) game->adopt_world_vehicle(m->u.created.request, m->u.created.id);
    } else if (m->type == MP_WORLD_VEHICLE && role == CTW_MULTIPLAYER_CLIENT && slot == 0) {
        const CtwWorldVehicle* v = &m->u.vehicle.state; Replica* r = replica(vehicles, VEHICLE_LIMIT, v->id);
        if (!r || (r->id && !newer32(m->u.vehicle.seq, r->seq))) return;
        if (r->id) {
            CtwVehicleState local; memset(&local, 0, sizeof local); local.size = sizeof local;
            CtwVehicle h = game->world_vehicle_handle(v->id);
            if (h && game->get_vehicle_state(h, &local) && local.health < r->health)
                entity_damage(v->id, 0, r->health - local.health, own_slot);
        }
        if (game->set_world_vehicle(v)) {
            if (CTW_MOD_HAS(game, set_vehicle_visual)) game->set_vehicle_visual(game->world_vehicle_handle(v->id), &m->u.vehicle.visual);
            r->id = v->id; r->seq = m->u.vehicle.seq; r->heard = now; r->health = v->health; r->removed = 0;
        }
    } else if (m->type == MP_WORLD_PED && role == CTW_MULTIPLAYER_CLIENT && slot == 0) {
        const CtwWorldPed* p = &m->u.ped.state; Replica* r = replica(pedestrians, PED_LIMIT, p->id);
        if (!r || (r->id && !newer32(m->u.ped.seq, r->seq))) return;
        if (game->set_world_ped(p)) { r->id = p->id; r->seq = m->u.ped.seq; r->heard = now; r->removed = 0; }
    } else if (m->type == MP_WORLD_REMOVE && role == CTW_MULTIPLAYER_CLIENT && slot == 0) {
        Replica* r = m->u.remove.ped ? replica(pedestrians, PED_LIMIT, m->u.remove.id) :
                                     replica(vehicles, VEHICLE_LIMIT, m->u.remove.id);
        if (!r || (r->id && !newer32(m->u.remove.seq, r->seq))) return;
        if (m->u.remove.ped) game->remove_world_ped(m->u.remove.id); else game->remove_world_vehicle(m->u.remove.id);
        r->id = m->u.remove.id; r->seq = m->u.remove.seq; r->heard = now; r->removed = 1;
    } else if (m->type == MP_EXPLOSION && accept(&explosions[slot], m->u.explosion.seq)) {
        if (role == CTW_MULTIPLAYER_CLIENT && slot != 0) return;
        game->add_world_explosion(&m->u.explosion.state);
        if (role == CTW_MULTIPLAYER_HOST) {
            MpMessage copy = *m; copy.sender = 0; copy.u.explosion.seq = ++event_seq; send_event(&copy, slot);
        }
    } else if (m->type == MP_VEHICLE_DOOR && role == CTW_MULTIPLAYER_HOST &&
               (!doors[slot].bits || newer32(m->u.door.seq, doors[slot].newest)) && accept(&doors[slot], m->u.door.seq)) {
        CtwVehicle h = game->world_vehicle_handle(m->u.door.id);
        CtwVehicleState v; memset(&v, 0, sizeof v); v.size = sizeof v;
        if (!h || !CTW_MOD_HAS(game, get_vehicle_visual) || !game->get_vehicle_state(h, &v)) return;
        float d = 0.f; for (int k = 0; k < 3; ++k) { float a = occupants[slot].pos[k] - v.position[k]; d += a*a; }
        if (d > 144.f || now - player_heard[slot] > 10. || m->u.door.seat >= game->vehicle_seat_count(h)) return;
        CtwVehicleVisual visual; memset(&visual, 0, sizeof visual); visual.size = sizeof visual;
        if (game->get_vehicle_visual(h, &visual)) {
            uint32_t bit = 1u << m->u.door.seat;
            visual.doors_open = (visual.doors_open & ~bit) | (m->u.door.open ? bit : 0);
            visual.door_angles[m->u.door.seat] = m->u.door.angle; game->set_vehicle_visual(h, &visual);
        }
    } else if (m->type == MP_ENTITY_DAMAGE && role == CTW_MULTIPLAYER_HOST && accept(&damages[slot], m->u.damage.seq)) {
        if (m->u.damage.ped) {
            if (CTW_MOD_HAS(game, damage_world_ped)) game->damage_world_ped(m->u.damage.id, m->u.damage.amount);
        } else {
            CtwVehicle h = game->world_vehicle_handle(m->u.damage.id);
            if (h) game->damage_vehicle(h, m->u.damage.amount);
        }
    }
}
static void entity_damage(uint32_t id, int ped, int amount, int slot) {
    if (amount <= 0) return;
    MpMessage m; memset(&m, 0, sizeof m); m.type = MP_ENTITY_DAMAGE; m.sender = (uint8_t)slot;
    m.u.damage.id = id; m.u.damage.ped = (uint8_t)ped; m.u.damage.amount = (uint16_t)(amount > 255 ? 255 : amount);
    m.u.damage.seq = ++event_seq; send_event(&m, -1);
}
static int host_has_ped(uint32_t id) {
    for (int i = 0; i < PED_LIMIT; ++i) {
        CtwWorldPed p; memset(&p, 0, sizeof p); p.size = sizeof p;
        if (!game->get_world_ped(i, &p)) break;
        if (p.id == id) return 1;
    }
    return 0;
}
static void remove_missing(Replica* list, int count, uint32_t sweep, int ped) {
    int removed = 0;
    for (int i = 0; i < count && removed < 16; ++i) if (list[i].id && list[i].seq != sweep) {
        /* Enumeration indices can shift between batches. Check actual identity before deleting a missed ID. */
        if (ped ? host_has_ped(list[i].id) : game->world_vehicle_handle(list[i].id) != 0) continue;
        MpMessage m; memset(&m, 0, sizeof m); m.type = MP_WORLD_REMOVE;
        m.u.remove.id = list[i].id; m.u.remove.ped = (uint8_t)ped; m.u.remove.seq = snapshot_seq;
        send_event(&m, -1); memset(&list[i], 0, sizeof list[i]);
        ++removed;
    }
}
void mp_world_tick(double now, uint32_t tick, int my_slot) {
    (void)tick;
    if (!game || !send_message) return;
    own_slot = my_slot;
    for(int slot=0;slot<MP_MAX_PLAYERS;++slot) if(role==CTW_MULTIPLAYER_HOST || slot==0) flush_skids(slot,now);
    if(CTW_MOD_HAS(game,take_world_skid)) {
        for(int batch=0;batch<8;++batch) {
            if(!skid_free()) break;
            MpMessage m; memset(&m,0,sizeof m); m.type=MP_SKID_BATCH; m.sender=(uint8_t)my_slot;
            while(m.u.skid.count<MP_SKID_POINTS) {
                CtwSkidPoint p; memset(&p,0,sizeof p); p.size=sizeof p;
                if(!game->take_world_skid(&p)) break;
                p.owner=(uint32_t)my_slot; m.u.skid.points[m.u.skid.count++]=p;
            }
            if(!m.u.skid.count) break;
            queue_skid(&m,-1);
        }
    }
    for(int i=0,budget=16;i<SKID_OUT && budget>0;++i) if(skid_out[i].used &&
        (skid_out[i].sent<0 || now-skid_out[i].sent>=.15)) {
        send_message(&skid_out[i].message,-1,skid_out[i].except); skid_out[i].sent=now; --budget;
    }
    CtwWorldExplosion e; memset(&e, 0, sizeof e); e.size = sizeof e;
    for (int i = 0; i < 64 && game->take_world_explosion(&e); ++i) {
        MpMessage m; memset(&m, 0, sizeof m); m.type = MP_EXPLOSION; m.sender = (uint8_t)my_slot;
        m.u.explosion.seq = ++event_seq; m.u.explosion.state = e; send_event(&m, -1);
    }
    if (role == CTW_MULTIPLAYER_HOST) {
        if (now - last_snapshot < 0.1) return;
        last_snapshot = now;
        ++snapshot_seq;
        if (!vehicle_cursor) ++vehicle_sweep;
        if (!ped_cursor) ++ped_sweep;
        /* Bound each burst below the online receive queue. Large populations stream round-robin. */
        for (int i = 0; i < 48; ++i) {
            MpMessage m; memset(&m, 0, sizeof m); m.type = MP_WORLD_VEHICLE; m.u.vehicle.seq = snapshot_seq;
            m.u.vehicle.state.size = sizeof(CtwWorldVehicle);
            if (!game->get_world_vehicle(vehicle_cursor++, &m.u.vehicle.state)) {
                vehicle_cursor = 0; remove_missing(vehicles, VEHICLE_LIMIT, vehicle_sweep, 0); break;
            }
            Replica* r = replica(vehicles, VEHICLE_LIMIT, m.u.vehicle.state.id);
            if (CTW_MOD_HAS(game, get_vehicle_visual)) {
                m.u.vehicle.visual.size = sizeof(CtwVehicleVisual);
                game->get_vehicle_visual(game->world_vehicle_handle(m.u.vehicle.state.id), &m.u.vehicle.visual);
            }
            if (r) { r->id = m.u.vehicle.state.id; r->seq = vehicle_sweep; }
            send_message(&m, -1, -1);
        }
        for (int i = 0; i < 48; ++i) {
            MpMessage m; memset(&m, 0, sizeof m); m.type = MP_WORLD_PED; m.u.ped.seq = snapshot_seq;
            m.u.ped.state.size = sizeof(CtwWorldPed);
            if (!game->get_world_ped(ped_cursor++, &m.u.ped.state)) {
                ped_cursor = 0; remove_missing(pedestrians, PED_LIMIT, ped_sweep, 1); break;
            }
            Replica* r = replica(pedestrians, PED_LIMIT, m.u.ped.state.id);
            if (r) { r->id = m.u.ped.state.id; r->seq = ped_sweep; }
            send_message(&m, -1, -1);
        }
    } else {
        if (CTW_MOD_HAS(game, get_player_vehicle_task) && now - last_door_sent >= 1./30.) {
            int seat = -1; CtwVehicle h = game->get_player_vehicle_task(&seat);
            const int final = !h && last_door_car;
            int closed = 0;
            if (final) { h = last_door_car; seat = last_door_seat; }
            if (h && seat >= 0 && seat < 4) {
                CtwVehicleVisual v; memset(&v, 0, sizeof v); v.size = sizeof v;
                if (game->get_vehicle_visual(h, &v) && game->world_vehicle_id(h)) {
                    closed = !(v.doors_open & (1u << seat)) && !v.door_angles[seat];
                    MpMessage m; memset(&m, 0, sizeof m); m.type = MP_VEHICLE_DOOR; m.sender = (uint8_t)my_slot;
                    m.u.door.id = game->world_vehicle_id(h); m.u.door.seq = ++event_seq; m.u.door.seat = (uint8_t)seat;
                    m.u.door.open = (uint8_t)((v.doors_open >> seat) & 1); m.u.door.angle = (uint16_t)v.door_angles[seat];
                    send_event(&m, -1);
                }
            }
            last_door_car = final && closed ? 0 : h; last_door_seat = seat; last_door_sent = now;
        }
        /* A car spawned by another client mod is proposed once by its stable local handle. The host assigns
           its shared ID; retries get the same acknowledgment and adoption keeps the original local car. */
        if (now - last_create_scan >= 0.5 && CTW_MOD_HAS(game, adopt_world_vehicle)) {
            last_create_scan = now;
            for (int i = 0; i < 16; ++i) {
                CtwVehicle h = game->live_vehicle_at(create_cursor++);
                if (!h) { create_cursor = 0; break; }
                if (game->world_vehicle_id(h)) continue;
                CtwVehicleState s; memset(&s, 0, sizeof s); s.size = sizeof s;
                if (!game->get_vehicle_state(h, &s) || (s.flags & CTW_VEHICLE_DEAD)) continue;
                MpMessage m; memset(&m, 0, sizeof m); m.type = MP_VEHICLE_CREATE; m.sender = (uint8_t)my_slot;
                CtwWorldVehicle* v = &m.u.vehicle.state; v->size = sizeof *v; v->id = h;
                v->model = s.model_id; v->palette = s.palette; v->health = s.health; v->flags = s.flags;
                if (game->get_vehicle_pose(h, v->position, v->quaternion, v->velocity)) send_message(&m, -1, -1);
            }
        }
        for (int i = 0; i < VEHICLE_LIMIT; ++i) {
            Replica* v = &vehicles[i];
            if (v->id) {
                CtwVehicleState s; memset(&s, 0, sizeof s); s.size = sizeof s;
                CtwVehicle h = game->world_vehicle_handle(v->id);
                if (!v->removed && h && game->get_vehicle_state(h, &s) && s.health < v->health) {
                    entity_damage(v->id, 0, v->health - s.health, my_slot); v->health = s.health;
                }
                if (now - v->heard > 12. && game->remove_world_vehicle(v->id)) memset(v, 0, sizeof *v);
            }
        }
        for (int i = 0; i < PED_LIMIT; ++i) {
            Replica* p = &pedestrians[i];
            if (p->id) {
                if (!p->removed && CTW_MOD_HAS(game, take_world_ped_damage)) entity_damage(p->id, 1, game->take_world_ped_damage(p->id), my_slot);
                if (now - p->heard > 12. && game->remove_world_ped(p->id)) memset(p, 0, sizeof *p);
            }
        }
    }
}
