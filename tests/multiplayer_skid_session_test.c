/* SPDX-License-Identifier: GPL-3.0-only */
/* Exercise skid transport using a synthetic game API and no sockets or game assets. */
#include "../ctw-modkit/examples/multiplayer/worldsync.h"
#include <stdio.h>
#include <string.h>

static int failures, applied, sent, queued, next_point, last_except, acks;
static CtwSkidPoint last_applied;
static MpMessage sent_messages[32];
static void check(int value, const char* message) { if (!value) { fprintf(stderr, "FAIL: %s\n", message); ++failures; } }
static int set_role(int role) { (void)role; return 1; }
static CtwVehicle handle(uint32_t id) { return id == 40 ? 40 : 0; }
static int car_state(CtwVehicle h, CtwVehicleState* out) { if (h != 40) return 0; out->model_id=1; out->health=127; return 1; }
static int seats(CtwVehicle h) { (void)h; return 4; }
static int set_car(const CtwWorldVehicle* car) { (void)car; return 1; }
static int add_skid(const CtwSkidPoint* p) { last_applied=*p; ++applied; return 1; }
static int take_skid(CtwSkidPoint* p) {
    if (next_point >= queued) return 0;
    memset(p,0,sizeof *p); p->size=sizeof *p; p->vehicle=40; p->strip=7; p->sequence=(uint32_t)++next_point; p->hold=1; p->direction[0]=4096;
    return 1;
}
static int no_explosion(CtwWorldExplosion* p) { (void)p; return 0; }
static int no_world_car(int i, CtwWorldVehicle* p) { (void)i; (void)p; return 0; }
static int no_world_ped(int i, CtwWorldPed* p) { (void)i; (void)p; return 0; }
static void send_message(const MpMessage* m, int target, int except) {
    (void)target; last_except=except; if (m->type == MP_SKID_ACK) ++acks; if (m->type == MP_SKID_BATCH && sent < 32) sent_messages[sent++]=*m;
}
static CtwApi api(void) {
    CtwApi a; memset(&a,0,sizeof a); a.size=sizeof a; a.version=CTW_MOD_API_VERSION;
    a.set_multiplayer_world=set_role; a.world_vehicle_handle=handle; a.get_vehicle_state=car_state;
    a.vehicle_seat_count=seats; a.set_world_vehicle=set_car; a.add_world_skid=add_skid;
    a.take_world_skid=take_skid; a.take_world_explosion=no_explosion; a.get_world_vehicle=no_world_car; a.get_world_ped=no_world_ped; return a;
}
static MpMessage point(uint32_t seq) {
    MpMessage m; memset(&m,0,sizeof m); m.type=MP_SKID_BATCH; m.u.skid.seq=seq; m.u.skid.count=1;
    CtwSkidPoint* p=&m.u.skid.points[0]; p->size=sizeof *p; p->vehicle=40; p->strip=7;
    p->sequence=seq; p->hold=1; p->direction[0]=4096; p->owner=999; return m;
}
int main(void) {
    CtwApi a=api(); check(mp_world_start(&a,CTW_MULTIPLAYER_HOST,send_message),"host starts");
    mp_world_peer_join(1); mp_world_peer_join(2);
    MpState passenger; memset(&passenger,0,sizeof passenger); passenger.vehicle=40; passenger.seat=1; passenger.flags=MP_IN_VEHICLE;
    mp_world_player(1,&passenger,1.0); MpMessage m=point(1); mp_world_receive(1,&m,1.1);
    check(!applied && acks==1,"passenger geometry is rejected but its batch is acknowledged");
    MpState driver=passenger; driver.seat=0; mp_world_player(1,&driver,1.2);
    m=point(3); mp_world_receive(1,&m,1.3);
    check(!applied,"future batch waits for missing predecessor");
    MpState exited; memset(&exited,0,sizeof exited); exited.seat=-1; mp_world_player(1,&exited,1.35);
    m=point(2); mp_world_receive(1,&m,1.4);
    check(applied==2 && acks==3,"authorized future batch drains after exit and missing predecessor within grace is accepted");
    uint32_t owner=last_applied.owner;
    check(owner != 999 && (owner & 31)==1 && owner>31,"host replaces spoofed owner with canonical slot and epoch");
    mp_world_receive(1,&m,1.5);
    check(applied==2 && acks==4,"retry after lost acknowledgment is acknowledged without applying twice");
    mp_world_tick(1.55,1,0);
    check(sent==2 && last_except==-1 && sent_messages[0].u.skid.seq+1==sent_messages[1].u.skid.seq,
          "host sends contiguous relays to every peer including their source");
    MpMessage relay_ack; memset(&relay_ack,0,sizeof relay_ack); relay_ack.type=MP_SKID_ACK;
    relay_ack.u.skid_ack.seq=sent_messages[0].u.skid.seq; mp_world_receive(2,&relay_ack,1.56);
    relay_ack.u.skid_ack.seq=sent_messages[1].u.skid.seq; mp_world_receive(2,&relay_ack,1.56);
    mp_world_tick(1.8,2,0); check(sent==4,"source acknowledgment is required even when other peers acknowledged");
    relay_ack.u.skid_ack.seq=sent_messages[0].u.skid.seq; mp_world_receive(1,&relay_ack,1.81);
    relay_ack.u.skid_ack.seq=sent_messages[1].u.skid.seq; mp_world_receive(1,&relay_ack,1.81);
    mp_world_tick(2.0,3,0); check(sent==4,"host stops retrying after the source acknowledges");
    mp_world_forget(1); driver.seat=0; driver.vehicle=40; mp_world_player(1,&driver,2.0); mp_world_peer_join(1);
    m=point(1); mp_world_receive(1,&m,2.1);
    check(applied==3 && last_applied.owner!=owner && (last_applied.owner & 31)==1,"reused slot obtains a new owner epoch");
    mp_world_stop(); applied=sent=queued=next_point=acks=0;
    check(mp_world_start(&a,CTW_MULTIPLAYER_CLIENT,send_message),"client starts");
    mp_world_set_skid_baseline(3); mp_world_tick(2.9,0,1);
    m=point(2); m.u.skid.points[0].owner=owner; mp_world_receive(0,&m,2.95);
    check(!applied && acks==1,"late join ignores old resend before welcome baseline");
    m=point(3); m.u.skid.points[0].owner=owner; mp_world_receive(0,&m,3.0);
    check(!applied && sent==0 && acks==2,"source acknowledges its own relay without drawing twice");
    m=point(4); m.u.skid.points[0].owner=owner+1; mp_world_receive(0,&m,3.05);
    check(applied==1 && acks==3,"source advances through its own sequence and draws the next remote batch");
    mp_world_tick(3.1,1,1); check(sent==0,"client tick does not retransmit replayed geometry");
    queued=25; mp_world_tick(3.2,2,1);
    check(sent==2 && sent_messages[0].u.skid.count==24 && sent_messages[1].u.skid.count==1,"25 points split into bounded batches of 24 and 1");
    uint8_t packet[MP_PACKET]; MpMessage decoded;
    size_t bytes=mp_encode(&sent_messages[0],packet,sizeof packet);
    check(bytes>0 && bytes<=MP_PACKET && mp_decode(packet,bytes,&decoded) && decoded.u.skid.count==24,"maximum batch fits and decodes within MTU");
    mp_world_tick(3.4,3,1); check(sent==4,"unacknowledged batches are retried");
    MpMessage ack; memset(&ack,0,sizeof ack); ack.type=MP_SKID_ACK;
    ack.u.skid_ack.seq=sent_messages[0].u.skid.seq; mp_world_receive(0,&ack,3.41);
    ack.u.skid_ack.seq=sent_messages[1].u.skid.seq; mp_world_receive(0,&ack,3.41);
    mp_world_tick(3.6,4,1); check(sent==4,"acknowledged batches stop retrying");
    mp_world_stop(); return failures ? 1 : 0;
}
