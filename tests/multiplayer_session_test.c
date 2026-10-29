/* SPDX-License-Identifier: GPL-3.0-only
 * Synthetic transport and game API: exercise the actual multiplayer session without game data or sockets. */
#include "../ctw-modkit/examples/multiplayer/multiplayer.c"

typedef struct TestCar { CtwWorldVehicle state; CtwVehicleVisual visual; } TestCar;
enum { TEST_CAR_LIMIT = 160 };
static TestCar test_cars[TEST_CAR_LIMIT];
static int test_failures, test_created, test_applied, test_removed, test_effects, test_world_role;
static int test_forbidden_spawn, test_forbidden_remove, test_puppets, test_puppet_removed;
static int test_send_counts[32], test_last_except, test_car_damage, test_ped_damage;
static CtwVehicle test_puppet_cars[64], test_local_car;
static int test_puppet_seats[64], test_local_seat = -1, test_seat_corrections;
static int test_seat_limit = 4, test_generate_cars;
static int test_allow_spawn, test_adoptions, test_last_target;
static CtwVehicle test_unmapped_car;
static uint32_t test_adopted_id, test_next_host_id = 80;
static CtwWorldVehicle test_unmapped_state;
static uint32_t test_snapshot_seq;
static MpMessage test_wire_message;
static MpMessage test_last_world_message;
static int test_wire_sends;
static NetAddr test_wire_destination;
static int test_world_hit_source, test_player_damage_total, test_player_damage_calls;
static char test_labels[MP_MAX_PLAYERS + 1][3];
static CtwApi test_api;
static CtwVehicle test_task_car;
static int test_task_seat;
static float test_projected_head[3];
static int test_capture_head(const float* xyz, float* xy) { (void)xy; memcpy(test_projected_head,xyz,sizeof test_projected_head); return 0; }
static int test_puppet_position(CtwPuppet h, float* xyz) { (void)h; xyz[0]=7; xyz[1]=8; xyz[2]=9; return 1; }
static void test_check(int ok, const char* why) {
    if (!ok) { fprintf(stderr, "FAIL: %s\n", why); ++test_failures; }
}
static TestCar* test_car(uint32_t id) {
    for (int i = 0; i < TEST_CAR_LIMIT; ++i) if (test_cars[i].state.id == id) return &test_cars[i];
    return NULL;
}
static int test_role(int next) {
    test_world_role = next;
    if (!next) memset(test_cars, 0, sizeof test_cars);
    return 1;
}
static int test_set_car(const CtwWorldVehicle* state) {
    if (test_unmapped_car && state->id == test_adopted_id) {
        test_unmapped_state = *state; ++test_applied; return 1;
    }
    TestCar* car = test_car(state->id);
    if (!car) for (int i = 0; i < TEST_CAR_LIMIT; ++i) if (!test_cars[i].state.id) {
        car = &test_cars[i]; ++test_created; break;
    }
    if (!car) return 0;
    car->state = *state; ++test_applied; return 1;
}
static CtwVehicle test_handle(uint32_t id) {
    if (id && test_unmapped_car && id == test_adopted_id) return test_unmapped_car;
    return id && test_car(id) ? id + 1000 : 0;
}
static uint32_t test_id(CtwVehicle handle) {
    if (handle && handle == test_unmapped_car) return test_adopted_id;
    return handle > 1000 && test_car(handle - 1000) ? handle - 1000 : 0;
}
static int test_get_visual(CtwVehicle h, CtwVehicleVisual* out) {
    TestCar* c=test_car(test_id(h)); if (!c) return 0; *out=c->visual; out->size=sizeof *out; return 1;
}
static int test_set_visual(CtwVehicle h, const CtwVehicleVisual* s) {
    TestCar* c=test_car(test_id(h)); if (!c) return 0; c->visual=*s; return 1;
}
static CtwVehicle test_vehicle_task(int* seat) { *seat=test_task_seat; return test_task_car; }
static int test_get_car(CtwVehicle handle, CtwVehicleState* out) {
    TestCar* car = test_car(test_id(handle));
    const CtwWorldVehicle* state = handle && handle == test_unmapped_car ? &test_unmapped_state : car ? &car->state : NULL;
    if (!state) return 0;
    out->vehicle = handle; out->model_id = state->model; out->palette = state->palette;
    out->health = state->health; out->flags = state->flags;
    memcpy(out->position, state->position, sizeof out->position); return 1;
}
static int test_live_count(void) {
    int count = test_unmapped_car ? 1 : 0;
    for (int i = 0; i < TEST_CAR_LIMIT; ++i) count += test_cars[i].state.id != 0;
    return count;
}
static CtwVehicle test_live_at(int index) {
    for (int i = 0; i < TEST_CAR_LIMIT; ++i) if (test_cars[i].state.id && index-- == 0) return test_cars[i].state.id + 1000;
    return index == 0 ? test_unmapped_car : 0;
}
static int test_vehicle_pose(CtwVehicle handle, float* position, float* quaternion, float* velocity) {
    TestCar* car = test_car(test_id(handle));
    const CtwWorldVehicle* state = handle == test_unmapped_car ? &test_unmapped_state : car ? &car->state : NULL;
    if (!state) return 0;
    memcpy(position, state->position, sizeof state->position); memcpy(quaternion, state->quaternion, sizeof state->quaternion);
    memcpy(velocity, state->velocity, sizeof state->velocity); return 1;
}
static int test_adopt(CtwVehicle local, uint32_t host_id) {
    if (!local || local != test_unmapped_car || !host_id) return 0;
    TestCar* duplicate = test_car(host_id);
    if (duplicate) { memset(duplicate, 0, sizeof *duplicate); ++test_removed; }
    test_adopted_id = host_id; test_unmapped_state.id = host_id; ++test_adoptions; return 1;
}
static int test_get_world_car(int index, CtwWorldVehicle* out) {
    if (test_generate_cars) {
        if (index < 0 || index >= test_generate_cars) return 0;
        memset(out, 0, sizeof *out); out->size = sizeof *out; out->id = (uint32_t)index + 100;
        out->model = 4; out->health = 127; out->quaternion[3] = 1; return 1;
    }
    for (int i = 0; i < TEST_CAR_LIMIT; ++i) if (test_cars[i].state.id && index-- == 0) { *out = test_cars[i].state; return 1; }
    return 0;
}
static int test_remove_car(uint32_t id) {
    TestCar* car = test_car(id);
    if (!car) return 0;
    memset(car, 0, sizeof *car); ++test_removed; return 1;
}
static int test_no_ped(int index, CtwWorldPed* out) { (void)index; (void)out; return 0; }
static int test_set_ped(const CtwWorldPed* state) { (void)state; return 1; }
static int test_remove_ped(uint32_t id) { (void)id; return 1; }
static int test_no_explosion(CtwWorldExplosion* out) { (void)out; return 0; }
static int test_explosion(const CtwWorldExplosion* state) { (void)state; ++test_effects; return 1; }
static int test_damage_car(CtwVehicle car, int amount) { (void)car; test_car_damage += amount; return 1; }
static int test_damage_ped(uint32_t id, int amount) { (void)id; test_ped_damage += amount; return 1; }
static int test_no_ped_damage(uint32_t id) { (void)id; return 0; }
static int test_take_world_hit(CtwPuppet p) { (void)p; int n = test_world_hit_source; test_world_hit_source = 0; return n; }
static int test_take_pvp_hit(CtwPuppet p) { (void)p; return 0; }
static int test_damage_player(int amount) { test_player_damage_total += amount; ++test_player_damage_calls; return 1; }
static int test_player(CtwPlayerState* out) { out->vehicle = test_local_car; return 1; }
static int test_seat(void) { return test_local_seat; }
static int test_vehicle_seats(CtwVehicle car) { return test_id(car) ? test_seat_limit : 0; }
static int test_set_seat(CtwVehicle car, int seat) {
    test_local_car = car; test_local_seat = car ? seat : -1; ++test_seat_corrections; return 1;
}
static CtwPuppet test_spawn_puppet(const float* pos, float heading, int body, int upper, int legs) {
    (void)pos; (void)heading; (void)body; (void)upper; (void)legs; return (CtwPuppet)++test_puppets;
}
static int test_set_puppet(CtwPuppet puppet, const CtwPuppetState* state) { test_puppet_cars[puppet] = state->vehicle; return 1; }
static int test_set_puppet_seat(CtwPuppet puppet, int seat) { test_puppet_seats[puppet] = seat; return 1; }
static int test_remove_puppet(CtwPuppet puppet) { test_puppet_cars[puppet] = 0; ++test_puppet_removed; return 1; }
static CtwVehicle test_spawn_vehicle(int model, const float* pos, float heading, int palette) {
    (void)heading; ++test_forbidden_spawn;
    if (!test_allow_spawn) return 9999;
    CtwWorldVehicle state; memset(&state, 0, sizeof state); state.size = sizeof state; state.id = test_next_host_id++;
    state.model = model; state.palette = palette; state.health = 127; state.quaternion[3] = 1;
    memcpy(state.position, pos, sizeof state.position); return test_set_car(&state) ? test_handle(state.id) : 0;
}
static int test_remove_vehicle(CtwVehicle car) { (void)car; ++test_forbidden_remove; return 1; }
static void test_blip(CtwMod* mod, int id, const float* pos, uint32_t colour) { (void)mod; (void)id; (void)pos; (void)colour; }
static void test_blip_remove(CtwMod* mod, int id) { (void)mod; memset(test_labels[id], 0, sizeof test_labels[id]); }
static void test_blip_label(CtwMod* mod, int id, const char* label) { (void)mod; snprintf(test_labels[id], 3, "%s", label); }
static void test_send(const MpMessage* message, int target, int except) {
    test_last_target = target; ++test_send_counts[message->type]; test_last_except = except;
    test_last_world_message = *message;
    if (message->type == MP_WORLD_VEHICLE) test_snapshot_seq = message->u.vehicle.seq;
}
static void test_log(CtwMod* mod, const char* text) { (void)mod; (void)text; }
static void test_reset(int role) {
    test_task_car=0; test_task_seat=0;
    mp_world_stop(); memset(test_cars, 0, sizeof test_cars); memset(players, 0, sizeof players);
    memset(test_puppet_cars, 0, sizeof test_puppet_cars); memset(test_labels, 0, sizeof test_labels);
    memset(test_send_counts, 0, sizeof test_send_counts);
    test_created = test_applied = test_removed = test_effects = test_forbidden_spawn = test_forbidden_remove = 0;
    test_puppets = test_puppet_removed = test_car_damage = test_ped_damage = test_seat_corrections = 0;
    test_local_car = 0; test_local_seat = -1; my_slot = 0; ticks = 0;
    test_seat_limit = 4; test_generate_cars = 0; test_snapshot_seq = 0;
    test_allow_spawn = test_adoptions = 0; test_unmapped_car = 0; test_adopted_id = 0; test_next_host_id = 80;
    memset(&test_unmapped_state, 0, sizeof test_unmapped_state);
    test_check(mp_world_start(&test_api, role, test_send), "shared world starts with the synthetic API");
}
static MpMessage test_vehicle(uint32_t id, uint32_t seq, float x) {
    MpMessage message; memset(&message, 0, sizeof message); message.type = MP_WORLD_VEHICLE;
    message.u.vehicle.seq = seq; message.u.vehicle.state.size = sizeof(CtwWorldVehicle);
    message.u.vehicle.state.id = id; message.u.vehicle.state.model = 4; message.u.vehicle.state.palette = 2;
    message.u.vehicle.state.health = 127; message.u.vehicle.state.position[0] = x;
    message.u.vehicle.state.quaternion[3] = 1; return message;
}
static MpState test_occupant(uint32_t vehicle, int seat) {
    MpState state; memset(&state, 0, sizeof state); state.vehicle = vehicle; state.seat = (int8_t)seat;
    state.model = 4; state.flags = MP_IN_VEHICLE; state.vquat[3] = 1; return state;
}
static void test_replication(void) {
    test_reset(CTW_MULTIPLAYER_CLIENT);
    MpMessage message = test_vehicle(41, 10, 10);
    mp_world_receive(1, &message, 1);
    test_check(!test_created, "clients ignore vehicle snapshots from anyone except the host");
    mp_world_receive(0, &message, 1); message.u.vehicle.seq = 11; message.u.vehicle.state.position[0] = 20;
    mp_world_receive(0, &message, 1.1); message.u.vehicle.seq = 10; message.u.vehicle.state.position[0] = 30;
    mp_world_receive(0, &message, 1.2);
    test_check(test_created == 1 && test_applied == 2 && test_car(41)->state.position[0] == 20,
               "repeated host IDs update one car and late packets cannot rewind it");
    message = test_vehicle(42, 2, 40); mp_world_receive(0, &message, 1.3);
    test_check(test_created == 2, "sequence ordering is per entity, and same-model cars retain distinct IDs");
    message = test_vehicle(43, 0xFFFFFFFFu, 50); mp_world_receive(0, &message, 1.4);
    message.u.vehicle.seq = 0; message.u.vehicle.state.position[0] = 60; mp_world_receive(0, &message, 1.5);
    test_check(test_car(43)->state.position[0] == 60, "entity sequences continue correctly across wraparound");
    for (int slot = 1; slot <= 2; ++slot) {
        players[slot].used = players[slot].has_state = 1; players[slot].state = test_occupant(41, slot - 1);
        update_remote(slot);
    }
    test_check(test_created == 3 && !test_forbidden_spawn && players[1].car == players[2].car &&
               test_puppet_cars[players[1].puppet] == test_puppet_cars[players[2].puppet] &&
               test_puppet_seats[players[1].puppet] == 0 && test_puppet_seats[players[2].puppet] == 1,
               "two players in one car resolve one shared vehicle with distinct seats");
    test_check(!strcmp(test_labels[2], "B") && !strcmp(test_labels[3], "C"), "player letters match their radar markers");
    players[31].used = players[31].has_state = 1; players[31].state.model = -1; players[31].state.seat = -1;
    update_remote(31); test_check(!strcmp(test_labels[32], "AF"), "player letters remain distinct beyond Z");
    forget_player(1);
    test_check(!test_forbidden_remove && test_handle(41) && test_puppet_removed == 1 && !test_labels[2][0],
               "a departing occupant removes its puppet and marker without deleting the shared car");
    mp_world_tick(14, 420, 0);
    test_check(test_removed == 3 && !test_handle(41), "entities absent from host snapshots expire on clients");
    mp_world_stop(); test_check(test_world_role == CTW_MULTIPLAYER_OFF, "leaving releases client replication mode");
    test_check(mp_world_start(&test_api, CTW_MULTIPLAYER_CLIENT, test_send), "a second session starts");
    message = test_vehicle(41, 1, 1); mp_world_receive(0, &message, 0);
    test_check(test_handle(41) && test_car(41)->state.position[0] == 1, "a new session accepts fresh IDs and sequence numbers");
}
static void test_seats(void) {
    test_reset(CTW_MULTIPLAYER_HOST);
    MpMessage message = test_vehicle(70, 1, 0); test_set_car(&message.u.vehicle.state);
    MpState driver = test_occupant(70, 0); mp_world_player(0, &driver, 1);
    MpState rider = test_occupant(70, 0); rider.vpos[0] = 999; mp_world_player(1, &rider, 1);
    test_check(driver.seat == 0 && rider.seat == 1 && test_car(70)->state.position[0] == 0,
               "the host gives a contested driver seat to a passenger and rejects its driving pose");
    MpState third = test_occupant(70, 0), fourth = third, fifth = third;
    mp_world_player(2, &third, 1); mp_world_player(3, &fourth, 1); mp_world_player(4, &fifth, 1);
    test_check(third.seat == 2 && fourth.seat == 3 && !fifth.vehicle && fifth.seat == -1,
               "seat arbitration fills each seat once and rejects a full vehicle");
    mp_world_forget(0); MpState next = test_occupant(70, 0); next.vpos[0] = 25;
    mp_world_player(5, &next, 2);
    test_check(next.seat == 0 && test_car(70)->state.position[0] == 25 && test_created == 1,
               "a departing driver releases its seat and the next driver moves the existing car");
    MpState invalid = test_occupant(1234, 0); mp_world_player(6, &invalid, 2);
    test_check(!invalid.vehicle && invalid.model == -1 && !(invalid.flags & MP_IN_VEHICLE),
               "unknown host vehicle IDs cannot create cars through player state");
    test_seat_limit = 2; message = test_vehicle(71, 1, 0); test_set_car(&message.u.vehicle.state);
    driver = test_occupant(71, 0); rider = driver; third = driver;
    mp_world_player(0, &driver, 3); mp_world_player(1, &rider, 3); mp_world_player(2, &third, 3);
    test_check(rider.seat == 1 && !third.vehicle, "two-seat vehicles cannot be assigned nonexistent rear seats");
    message = test_vehicle(70, 1, 0);
    test_reset(CTW_MULTIPLAYER_CLIENT); test_set_car(&message.u.vehicle.state);
    test_local_car = test_handle(70); test_local_seat = 0;
    MpState request = test_occupant(70, 0); request.seq = 22; mp_world_request(&request);
    MpState accepted = request; accepted.seat = 1; mp_world_correct_seat(&accepted);
    test_check(test_local_seat == 1 && test_seat_corrections == 1, "host acceptance corrects a contested local seat");
    MpState delayed = request; delayed.vehicle = 0; delayed.seat = -1; delayed.flags &= ~MP_IN_VEHICLE; delayed.model = -1;
    request.seq = 23; mp_world_request(&request); accepted = request; accepted.vehicle = 0; accepted.seat = -1;
    accepted.model = -1; accepted.flags &= ~MP_IN_VEHICLE; mp_world_correct_seat(&accepted);
    test_check(!test_local_car && test_local_seat == -1, "a full-car rejection ejects the rejected local entrant");
    message = test_vehicle(71, 1, 0); test_set_car(&message.u.vehicle.state); test_local_car = test_handle(71); test_local_seat = 0;
    const int corrections = test_seat_corrections; mp_world_correct_seat(&accepted);
    test_check(test_local_car == test_handle(71) && test_seat_corrections == corrections,
               "a delayed rejection cannot eject the player from a different car");
    test_local_car = test_handle(70); test_local_seat = 0;
    request.seq = 24; mp_world_request(&request); accepted = request; accepted.seat = 0;
    mp_world_correct_seat(&accepted); mp_world_correct_seat(&delayed);
    test_check(test_local_car == test_handle(70), "an older same-car rejection cannot override a newer seat acknowledgment");
}
static void test_snapshot_rate(void) {
    test_reset(CTW_MULTIPLAYER_HOST);
    MpMessage message = test_vehicle(3, 1, 0); test_set_car(&message.u.vehicle.state);
    mp_world_tick(1, 77, 0); const uint32_t first_seq = test_snapshot_seq;
    mp_world_tick(1.05, 77, 0);
    test_check(test_send_counts[MP_WORLD_VEHICLE] == 1, "host city snapshots respect the real-time send interval");
    mp_world_tick(1.11, 77, 0);
    test_check(test_send_counts[MP_WORLD_VEHICLE] == 2 && test_snapshot_seq != first_seq,
               "paused game ticks still send fresh monotonically ordered world snapshots");
    test_reset(CTW_MULTIPLAYER_HOST); test_generate_cars = 1000;
    mp_world_tick(1, 1, 0);
    test_check(test_send_counts[MP_WORLD_VEHICLE] == 48, "large worlds bound each vehicle snapshot burst");
    mp_world_tick(1.11, 1, 0);
    test_check(test_send_counts[MP_WORLD_VEHICLE] == 96, "large-world streaming resumes from the next entity batch");
}
static void test_removal_and_damage(void) {
    test_reset(CTW_MULTIPLAYER_CLIENT);
    MpMessage message = test_vehicle(41, 10, 1); mp_world_receive(0, &message, 1);
    test_car(41)->state.health = 115; message.u.vehicle.seq = 11;
    mp_world_receive(0, &message, 1.1);
    test_check(test_send_counts[MP_ENTITY_DAMAGE] == 3 && test_last_world_message.u.damage.amount == 12,
               "an incoming snapshot cannot erase local vehicle damage before it is sent to the host");
    mp_world_tick(1.2, 36, 1);
    test_check(test_send_counts[MP_ENTITY_DAMAGE] == 3, "reported vehicle damage is not sent again after snapshot reconciliation");
    MpMessage removed; memset(&removed, 0, sizeof removed); removed.type = MP_WORLD_REMOVE;
    removed.u.remove.id = 41; removed.u.remove.seq = 12;
    mp_world_receive(0, &removed, 1.3); mp_world_receive(0, &message, 1.4);
    test_check(test_removed == 1 && !test_handle(41) && test_created == 1,
               "a host removal prevents delayed snapshots from resurrecting the deleted car");
    test_reset(CTW_MULTIPLAYER_HOST); message = test_vehicle(70, 1, 1); test_set_car(&message.u.vehicle.state);
    mp_world_tick(1, 30, 0); test_remove_car(70); mp_world_tick(1.2, 36, 0);
    test_check(test_send_counts[MP_WORLD_REMOVE] == 3 && test_last_world_message.u.remove.id == 70,
               "the host promptly replicates entity deletion with retry datagrams");
}
static void test_presentation(void) {
    test_reset(CTW_MULTIPLAYER_CLIENT);
    MpMessage message=test_vehicle(71,1,0); message.u.vehicle.visual.size=sizeof(CtwVehicleVisual);
    message.u.vehicle.visual.flags=63; message.u.vehicle.visual.controls=0x106;
    message.u.vehicle.visual.tyres=63; message.u.vehicle.visual.burnout=3;
    message.u.vehicle.visual.doors_open=2; message.u.vehicle.visual.door_angles[1]=80;
    mp_world_receive(0,&message,1);
    test_check(test_car(71)->visual.tyres==63 && test_car(71)->visual.flags==63 && test_car(71)->visual.door_angles[1]==80,
               "host snapshots carry lights, skid triggers, bike particles and doors into the actual replica");
    test_task_car=test_handle(71); test_task_seat=1; mp_world_tick(1.1,33,1);
    test_check(test_send_counts[MP_VEHICLE_DOOR]==3 && test_last_world_message.u.door.open,
               "entry door updates travel to the host before the player is seated");
    test_task_car=0; mp_world_tick(1.2,36,1);
    test_check(test_send_counts[MP_VEHICLE_DOOR]==6,"door ownership persists while the closing animation is unfinished");
    test_car(71)->visual.doors_open=0; test_car(71)->visual.door_angles[1]=0;
    mp_world_tick(1.3,39,1); mp_world_tick(1.4,42,1);
    test_check(test_send_counts[MP_VEHICLE_DOOR]==9,"the final closed door is retried once and finishes transmission");
    test_reset(CTW_MULTIPLAYER_HOST); message=test_vehicle(71,1,0); test_set_car(&message.u.vehicle.state);
    MpState occupant=test_occupant(0,-1); memset(occupant.pos,0,sizeof occupant.pos); mp_world_player(1,&occupant,1);
    memset(&message,0,sizeof message); message.type=MP_VEHICLE_DOOR; message.u.door.seq=1;
    message.u.door.id=71; message.u.door.seat=1; message.u.door.open=1; message.u.door.angle=70;
    mp_world_receive(1,&message,1.1);
    test_check(test_car(71)->visual.doors_open==2 && test_car(71)->visual.door_angles[1]==70,
               "host accepts an entry door for a nearby player without changing driving controls");
    message.u.door.seq=0; message.u.door.open=0; mp_world_receive(1,&message,1.2);
    test_check(test_car(71)->visual.doors_open==2,"out-of-order entry packets cannot close a newly opened door");
    occupant.pos[0]=1000; mp_world_player(1,&occupant,1.3); message.u.door.seq=2;
    mp_world_receive(1,&message,1.4);
    test_check(test_car(71)->visual.doors_open==2,"distant players cannot manipulate vehicle doors");
    test_reset(CTW_MULTIPLAYER_CLIENT); message=test_vehicle(71,1,5); test_set_car(&message.u.vehicle.state);
    players[0].used=players[0].has_state=1; players[0].car=test_handle(71); players[0].state.vpos[0]=999;
    my_slot=1; show_names=1; test_api.world_to_screen=test_capture_head; test_api.get_puppet_position=test_puppet_position;
    draw_names();
    test_check(test_projected_head[0]==5,"moving car labels anchor to the rendered vehicle rather than the last player packet");
    players[0].car=0; players[0].puppet=1; players[0].state.pos[0]=999; draw_names();
    test_check(test_projected_head[0]==7 && test_projected_head[1]==8,
               "walking labels anchor to the rendered character rather than the last player packet");
}
static void test_host_forwarding(void) {
    test_reset(CTW_MULTIPLAYER_HOST); mode = HOSTING; my_slot = 0; now = 1;
    players[1].used = players[2].used = 1;
    players[1].addr.ip = 1; players[1].addr.port = 101;
    players[2].addr.ip = 2; players[2].addr.port = 102;
    MpMessage message; memset(&message, 0, sizeof message);
    message.type = MP_CHAT_MSG; message.sender = 1; message.u.chat.slot = 1;
    strcpy(message.u.chat.text, "hello"); host_receive(&players[1].addr, &message);
    test_check(test_wire_message.type == MP_CHAT_MSG && test_wire_message.sender == 0 &&
               test_wire_message.u.chat.slot == 1, "the host relays chat using its own authenticated sender field");
    message.type = MP_HIT; message.u.hit.attacker = 1; message.u.hit.victim = 2; message.u.hit.damage = 12;
    host_receive(&players[1].addr, &message);
    test_check(test_wire_message.type == MP_HIT && test_wire_message.sender == 0 &&
               test_wire_message.u.hit.attacker == 1, "host-relayed damage preserves the attacker and uses host authority");
    const int sends = test_wire_sends; message.sender = 2; host_receive(&players[1].addr, &message);
    test_check(test_wire_sends == sends, "a joined address cannot send messages on behalf of another slot");
    mode = JOINED; my_slot = 2; host_addr.ip = 99; host_addr.port = 199;
    memset(pending_damage, 0, sizeof pending_damage); client_receive(&host_addr, &test_wire_message);
    test_check(pending_damage[1] == 12, "a client accepts host-relayed damage for its own player");
}
static void test_world_hits(void) {
    test_reset(CTW_MULTIPLAYER_HOST); mode = HOSTING; my_slot = 0;
    players[2].used = 1; players[2].puppet = 3; players[2].addr.ip = 2; players[2].addr.port = 102;
    test_world_hit_source = 15; test_wire_sends = 0;
    report_hits();
    test_check(test_wire_sends == 3 && net_same(&test_wire_destination, &players[2].addr) &&
               test_wire_message.type == MP_WORLD_HIT && test_wire_message.u.world_hit.victim == 2 &&
               test_wire_message.u.world_hit.damage == 15,
               "host sends NPC damage retries only to the affected player even without a PvP hit");
    report_hits(); test_check(test_wire_sends == 3, "drained NPC damage is not sent again on the next report");

    mode = JOINED; my_slot = 2; host_addr.ip = 99; host_addr.port = 199;
    pending_world_damage = 0; memset(pending_damage, 0, sizeof pending_damage);
    MpMessage hit; memset(&hit, 0, sizeof hit); hit.type = MP_WORLD_HIT;
    hit.u.world_hit.victim = 2; hit.u.world_hit.seq = 100; hit.u.world_hit.damage = 10;
    NetAddr stranger = {0}; stranger.ip = 3; stranger.port = 103;
    client_receive(&stranger, &hit);
    test_check(!pending_world_damage, "a client rejects world damage from a non-host address");
    client_receive(&host_addr, &hit); client_receive(&host_addr, &hit);
    hit.u.world_hit.seq = 102; hit.u.world_hit.damage = 30; client_receive(&host_addr, &hit);
    hit.u.world_hit.seq = 101; hit.u.world_hit.damage = 20; client_receive(&host_addr, &hit); client_receive(&host_addr, &hit);
    hit.u.world_hit.victim = 1; hit.u.world_hit.seq = 103; client_receive(&host_addr, &hit);
    test_check(pending_world_damage == 60 && !pending_damage[0],
               "distinct reordered host NPC hits queue once while duplicates and other victims are ignored");
    test_player_damage_total = test_player_damage_calls = 0; last_attacker = 1; last_hit_tick = ticks;
    apply_hits(); apply_hits();
    test_check(test_player_damage_total == 60 && test_player_damage_calls == 1 && last_attacker == -1,
               "queued NPC damage applies once and cannot credit an earlier PvP attacker");
}
static void test_client_created_vehicle(void) {
    test_reset(CTW_MULTIPLAYER_CLIENT);
    MpMessage message = test_vehicle(9001, 1, 12);
    test_unmapped_car = 9001; test_unmapped_state = message.u.vehicle.state;
    mp_world_tick(1, 30, 1);
    test_check(test_send_counts[MP_VEHICLE_CREATE] == 1 && test_last_world_message.sender == 1 &&
               test_last_world_message.u.vehicle.state.id == 9001 && test_last_world_message.u.vehicle.state.position[0] == 12,
               "a client proposes its mod-spawned car by stable local handle and full pose");
    mp_world_tick(1.2, 36, 1);
    test_check(test_send_counts[MP_VEHICLE_CREATE] == 1, "client vehicle creation retries are rate bounded");
    mp_world_tick(1.6, 48, 1);
    test_check(test_send_counts[MP_VEHICLE_CREATE] == 2, "an unacknowledged local car is retried");
    memset(&message, 0, sizeof message); message.type = MP_VEHICLE_CREATED;
    message.u.created.request = 9001; message.u.created.id = 200;
    mp_world_receive(2, &message, 1.7);
    test_check(!test_adoptions, "clients reject vehicle adoption messages from another client");
    mp_world_receive(0, &message, 1.7); mp_world_receive(0, &message, 1.8);
    test_check(test_id(9001) == 200 && test_handle(200) == 9001 && !test_created && !test_forbidden_spawn,
               "host acknowledgment adopts the original local car without creating an occupant copy");
    mp_world_tick(2.2, 66, 1);
    test_check(test_send_counts[MP_VEHICLE_CREATE] == 2, "adopted cars stop sending creation requests");
    message = test_vehicle(200, 1, 15); mp_world_receive(0, &message, 2.3);
    test_check(test_handle(200) == 9001 && !test_created && test_unmapped_state.position[0] == 15,
               "world snapshots update the adopted client car through its shared host ID");
    test_reset(CTW_MULTIPLAYER_CLIENT); test_unmapped_car = 9001; test_unmapped_state = test_vehicle(9001, 1, 12).u.vehicle.state;
    message = test_vehicle(200, 1, 15); mp_world_receive(0, &message, 1);
    memset(&message, 0, sizeof message); message.type = MP_VEHICLE_CREATED;
    message.u.created.request = 9001; message.u.created.id = 200; mp_world_receive(0, &message, 1.1);
    test_check(test_live_count() == 1 && test_handle(200) == 9001 && test_removed == 1,
               "a creation acknowledgment arriving after its snapshot merges the replica into the original car");
    test_reset(CTW_MULTIPLAYER_HOST); test_allow_spawn = 1;
    message = test_vehicle(9001, 1, 12); message.type = MP_VEHICLE_CREATE; message.sender = 1;
    mp_world_receive(1, &message, 1); const uint32_t host_id = test_last_world_message.u.created.id;
    mp_world_receive(1, &message, 1.5);
    test_check(test_created == 1 && test_forbidden_spawn == 1 && host_id && test_last_target == 1 &&
               test_last_world_message.type == MP_VEHICLE_CREATED && test_last_world_message.u.created.id == host_id &&
               test_last_world_message.u.created.request == 9001,
               "replayed creation requests get the same host car and acknowledgment");
    mp_world_receive(2, &message, 2);
    test_check(test_created == 2 && test_last_target == 2 && test_last_world_message.u.created.id != host_id,
               "different clients can use the same local handle without sharing unrelated cars");
    test_reset(CTW_MULTIPLAYER_HOST); test_allow_spawn = 1;
    for (int i = 0; i < 130; ++i) {
        message = test_vehicle((uint32_t)(9001 + i), 1, 12); message.type = MP_VEHICLE_CREATE;
        mp_world_receive(1, &message, 1);
    }
    message = test_vehicle(9001, 1, 12); message.type = MP_VEHICLE_CREATE; mp_world_receive(1, &message, 2);
    test_check(test_created == 130 && test_forbidden_spawn == 130 && test_last_world_message.u.created.id == 80,
               "large mod-spawn batches retain earlier request identities so delayed retries cannot duplicate cars");
}
static void test_events(void) {
    test_reset(CTW_MULTIPLAYER_CLIENT);
    MpMessage message; memset(&message, 0, sizeof message); message.type = MP_EXPLOSION;
    message.u.explosion.seq = 100; message.u.explosion.state.scale = 1;
    mp_world_receive(1, &message, 1); test_check(!test_effects, "clients ignore explosions that bypass host authority");
    mp_world_receive(0, &message, 1); mp_world_receive(0, &message, 1);
    message.u.explosion.seq = 102; mp_world_receive(0, &message, 1);
    message.u.explosion.seq = 101; mp_world_receive(0, &message, 1); mp_world_receive(0, &message, 1);
    message.u.explosion.seq = 1; mp_world_receive(0, &message, 1);
    test_check(test_effects == 3, "explosions tolerate reordering while duplicates and expired replay IDs are discarded");
    test_reset(CTW_MULTIPLAYER_HOST); message.u.explosion.seq = 1;
    mp_world_receive(2, &message, 1); mp_world_receive(2, &message, 1);
    test_check(test_effects == 1 && test_send_counts[MP_EXPLOSION] == 3 && test_last_except == 2,
               "the host applies a client explosion once and relays retries without echoing to its origin");
    message = test_vehicle(5, 1, 0); test_set_car(&message.u.vehicle.state);
    memset(&message, 0, sizeof message); message.type = MP_ENTITY_DAMAGE; message.u.damage.seq = 1;
    message.u.damage.id = 5; message.u.damage.amount = 12;
    mp_world_receive(2, &message, 1); mp_world_receive(2, &message, 1);
    message.u.damage.ped = 1; message.u.damage.seq = 2; mp_world_receive(2, &message, 1);
    test_check(test_car_damage == 12 && test_ped_damage == 12, "replayed vehicle and NPC damage requests apply once");
    mp_world_forget(2); mp_world_receive(2, &message, 2);
    test_check(test_ped_damage == 24, "reused player slots start a new replay window");
}

/* No network I/O: including multiplayer.c exposes its actual remote-player update and cleanup paths. */
int net_start(void) { return 1; } void net_stop(void) {} int net_open(uint16_t port) { (void)port; return 1; }
void net_close(void) {} int net_is_open(void) { return 0; }
int net_resolve(const char* host, uint16_t port, NetAddr* out) { (void)host; (void)port; (void)out; return 0; }
int net_send(const NetAddr* to, const void* data, size_t size) {
    test_wire_destination = *to; ++test_wire_sends; return mp_decode(data, size, &test_wire_message);
}
int net_receive(NetAddr* from, void* data, size_t size) { (void)from; (void)data; (void)size; return 0; }
int net_same(const NetAddr* a, const NetAddr* b) { return a->ip == b->ip && a->port == b->port; }
void net_format(const NetAddr* addr, char* out, size_t size) { (void)addr; snprintf(out, size, "synthetic"); }
int net_beacon_open(uint16_t port) { (void)port; return 1; } void net_beacon_close(void) {}
int net_beacon_send(uint16_t port, const void* data, size_t size) { (void)port; (void)data; (void)size; return 1; }
int net_beacon_receive(NetAddr* from, void* data, size_t size) { (void)from; (void)data; (void)size; return 0; }
int online_available(void) { return 0; } void online_set_url(const char* base) { (void)base; }
void online_stop(void) {} void online_leave(void) {} void online_refresh(void) {}
int online_list(OnlineServer* out, int max) { (void)out; (void)max; return 0; }
void online_host(const char* name, int max, const char* version, int public_game) { (void)name; (void)max; (void)version; (void)public_game; }
void online_update(const char* name, int players_count) { (void)name; (void)players_count; }
int online_hosting(char* id_out) { (void)id_out; return 0; } void online_join(const char* id) { (void)id; }
int online_joined(void) { return 0; }
int online_send(uint16_t player, const void* data, size_t size) { (void)player; (void)data; (void)size; return 1; }
int online_receive(uint16_t* player, void* data, size_t size) { (void)player; (void)data; (void)size; return 0; }

int main(void) {
    test_api.version = CTW_MOD_API_VERSION; test_api.size = sizeof test_api;
    test_api.set_multiplayer_world = test_role; test_api.get_world_vehicle = test_get_world_car;
    test_api.set_world_vehicle = test_set_car; test_api.world_vehicle_handle = test_handle; test_api.world_vehicle_id = test_id;
    test_api.get_vehicle_state = test_get_car; test_api.remove_world_vehicle = test_remove_car;
    test_api.live_vehicle_count = test_live_count; test_api.live_vehicle_at = test_live_at;
    test_api.get_vehicle_pose = test_vehicle_pose; test_api.adopt_world_vehicle = test_adopt;
    test_api.get_world_ped = test_no_ped; test_api.set_world_ped = test_set_ped; test_api.remove_world_ped = test_remove_ped;
    test_api.take_world_explosion = test_no_explosion; test_api.add_world_explosion = test_explosion;
    test_api.get_player_state = test_player; test_api.get_player_seat = test_seat; test_api.set_player_seat = test_set_seat;
    test_api.vehicle_seat_count = test_vehicle_seats;
    test_api.damage_vehicle = test_damage_car; test_api.damage_world_ped = test_damage_ped;
    test_api.take_world_ped_damage = test_no_ped_damage;
    test_api.spawn_puppet = test_spawn_puppet; test_api.set_puppet_state = test_set_puppet;
    test_api.set_puppet_seat = test_set_puppet_seat; test_api.remove_puppet = test_remove_puppet;
    test_api.spawn_vehicle_at = test_spawn_vehicle; test_api.remove_vehicle = test_remove_vehicle;
    test_api.set_radar_blip = test_blip; test_api.remove_radar_blip = test_blip_remove;
    test_api.set_radar_blip_label = test_blip_label; test_api.log = test_log; api = &test_api;
    test_api.get_vehicle_visual=test_get_visual; test_api.set_vehicle_visual=test_set_visual;
    test_api.get_player_vehicle_task=test_vehicle_task;
    test_api.take_puppet_world_damage=test_take_world_hit; test_api.take_puppet_damage=test_take_pvp_hit;
    test_api.damage_player=test_damage_player;
    test_replication(); test_seats(); test_events(); test_snapshot_rate(); test_removal_and_damage();
    test_host_forwarding(); test_world_hits(); test_client_created_vehicle(); test_presentation(); mp_world_stop();
    if (!test_failures) puts("PASS: shared multiplayer session");
    return test_failures ? 1 : 0;
}
