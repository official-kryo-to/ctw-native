/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kryo.to
 * See LICENSE in the repository root.
 *
 * Multiplayer: up to eight players in one city over UDP. One player hosts (F9, Host), the others join with the
 * host's address. Everyone sees the others walk, drive, ride, fight and die; hits on another player are taken by
 * that player's own game; there is chat (T) and the host's time of day and weather. Traffic, pedestrians, police
 * and pickups stay each player's own.
 *
 * Needs a version 13 game (puppets: characters a mod moves). The host must be reachable on its UDP port (7777 by
 * default): on the same network that just works; over the internet forward the port, or use a virtual network
 * such as Tailscale, ZeroTier or Radmin VPN.
 *
 * A shortcut can start a game straight away: set CTW_MULTIPLAYER to "host" or "join <address>" (and optionally
 * CTW_MULTIPLAYER_NAME) before starting the game.
 */
#include "ctw_mod.h"
#include "ctw_ui.h"
#include "net.h"
#include "protocol.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { SC_T = 23, SC_RETURN = 40, SC_ESCAPE = 41, SC_F1 = 58, SC_F9 = 66, SC_F12 = 69, SC_KP_ENTER = 88 };
enum { TICKS = 30, CHAT_LINES = 8 };
/* real time, in seconds: the network keeps going while the game is paused (pumped from every drawn frame) */
#define TIMEOUT 10.0
#define STATE_EVERY (1.0 / 15.0)
#define CHAT_SHOW 12.0
enum Mode { OFF, HOSTING, JOINING, JOINED };

static const CtwApi* api;
static CtwMod* self;

/* ---- settings (multiplayer.ini in the mod's folder) -------------------------------------------------------- */
static char my_name[MP_NAME] = "Player";
static char address[64] = "127.0.0.1";
static char port_text[8] = "7777";
static int show_names = 1, sync_world = 1;

/* ---- the session --------------------------------------------------------------------------------------------- */
typedef struct Player {
    int used;
    char name[MP_NAME];
    NetAddr addr;               /* host: where the client is */
    double heard;               /* when something last came from it */
    MpState state;
    int has_state;
    uint32_t state_tick;
    CtwPuppet puppet;
    CtwVehicle car;
    int car_model, car_leaving; /* car_leaving: removed once the puppet has got out */
} Player;

static int mode = OFF, my_slot = -1;
static Player players[MP_MAX_PLAYERS];
static NetAddr host_addr;
static uint32_t ticks;
static double now, host_heard, hello_time, join_started, last_state_sent, last_world_sent;
static int pending_damage[MP_MAX_PLAYERS];   /* hits on the player, by attacker, taken at the next game frame */
static int world_pending;
static float world_time;
static int world_weather;
static uint16_t my_seq;
static char status[128];
static int last_attacker = -1;
static uint32_t last_hit_tick;
static int was_dead;

static struct { char text[MP_CHAT + MP_NAME + 4]; double time; uint32_t rgba; } chat_log[CHAT_LINES];
static int chat_count;

/* ---- the windows ---------------------------------------------------------------------------------------------- */
static CtwUi ui;
static int panel_open, name_focused, address_focused, port_focused, autostart;
static int chat_open;
static char chat_text[MP_CHAT];

static void set_status(const char* text) {
    snprintf(status, sizeof status, "%s", text);
    if (text[0]) api->log(self, text);
}

static void add_chat(uint32_t rgba, const char* text) {
    if (chat_count == CHAT_LINES) {
        memmove(chat_log, chat_log + 1, sizeof chat_log[0] * (CHAT_LINES - 1));
        --chat_count;
    }
    snprintf(chat_log[chat_count].text, sizeof chat_log[0].text, "%s", text);
    chat_log[chat_count].time = now;
    chat_log[chat_count].rgba = rgba;
    ++chat_count;
    api->log(self, text);
}

static const char* player_name(int slot) {
    return slot >= 0 && slot < MP_MAX_PLAYERS && players[slot].used ? players[slot].name : "someone";
}

static void show_line(int slot, const char* text) {   /* "* ..." is something that happened, otherwise speech */
    char line[MP_CHAT + MP_NAME + 4];
    if (text[0] == '*' && text[1] == ' ') snprintf(line, sizeof line, "%s %s", player_name(slot), text + 2);
    else snprintf(line, sizeof line, "%s: %s", player_name(slot), text);
    add_chat(text[0] == '*' ? CTW_UI_ACCENT : CTW_UI_TEXT, line);
}

/* ---- settings file ---------------------------------------------------------------------------------------------- */
static void settings_path(char* out, size_t size) { snprintf(out, size, "%smultiplayer.ini", api->mod_dir(self)); }

static void load_settings(void) {
    char path[512], line[128];
    settings_path(path, sizeof path);
    FILE* f = fopen(path, "r");
    if (!f) return;
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        char* value = strchr(line, '=');
        if (!value) continue;
        *value++ = 0;
        if (!strcmp(line, "name") && *value) snprintf(my_name, sizeof my_name, "%s", value);
        else if (!strcmp(line, "address")) snprintf(address, sizeof address, "%s", value);
        else if (!strcmp(line, "port")) snprintf(port_text, sizeof port_text, "%s", value);
    }
    fclose(f);
}

static void save_settings(void) {
    char path[512];
    settings_path(path, sizeof path);
    FILE* f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "name=%s\naddress=%s\nport=%s\n", my_name, address, port_text);
    fclose(f);
}

static uint16_t port_number(void) {
    const long p = strtol(port_text, NULL, 10);
    return p > 0 && p < 65536 ? (uint16_t)p : 7777;
}

/* ---- sending -------------------------------------------------------------------------------------------------- */
static void send_to(const NetAddr* to, const MpMessage* m) {
    uint8_t buf[MP_PACKET];
    const size_t n = mp_encode(m, buf, sizeof buf);
    if (n) net_send(to, buf, n);
}

static void send_to_clients(const MpMessage* m, int except) {   /* host */
    for (int s = 1; s < MP_MAX_PLAYERS; ++s)
        if (players[s].used && s != except) send_to(&players[s].addr, m);
}

static void send_roster(void) {   /* host */
    MpMessage m;
    memset(&m, 0, sizeof m);
    m.type = MP_ROSTER;
    m.sender = 0;
    for (int s = 0; s < MP_MAX_PLAYERS; ++s) {
        if (!players[s].used) continue;
        m.u.roster.slots[m.u.roster.count] = (uint8_t)s;
        memcpy(m.u.roster.names[m.u.roster.count], players[s].name, MP_NAME);
        ++m.u.roster.count;
    }
    send_to_clients(&m, -1);
}

static void send_chat(int slot, const char* text) {   /* says it, relayed by the host */
    MpMessage m;
    memset(&m, 0, sizeof m);
    m.type = MP_CHAT_MSG;
    m.sender = (uint8_t)my_slot;
    m.u.chat.slot = (uint8_t)slot;
    snprintf(m.u.chat.text, sizeof m.u.chat.text, "%s", text);
    if (mode == HOSTING) {
        send_to_clients(&m, -1);
        show_line(slot, m.u.chat.text);
    } else if (mode == JOINED) {
        send_to(&host_addr, &m);   /* the host sends it back to everyone, us included */
    }
}

/* ---- remote players in the game --------------------------------------------------------------------------------- */
static void forget_player(int slot) {
    Player* p = &players[slot];
    if (p->puppet) api->remove_puppet(p->puppet);   /* (frees the driver's seat) */
    if (p->car) api->remove_vehicle(p->car);
    memset(p, 0, sizeof *p);
}

static void forget_everyone(void) {
    for (int s = 0; s < MP_MAX_PLAYERS; ++s) forget_player(s);
}

static int valid_palette(int p) { return p >= 0 && p < 64; }

static void update_remote(int slot) {
    Player* p = &players[slot];
    if (!p->used || !p->has_state || slot == my_slot) return;
    const MpState* s = &p->state;
    float age = (float)(ticks - p->state_tick) / TICKS;
    if (age > 0.5f) age = 0.5f;
    if (!p->puppet) {
        int body = s->body <= 1 ? s->body : 1, upper = valid_palette(s->upper) ? s->upper : 4,
            legs = valid_palette(s->legs) ? s->legs : 5;
        p->puppet = api->spawn_puppet(s->pos, s->heading, body, upper, legs);
        if (!p->puppet) p->puppet = api->spawn_puppet(s->pos, s->heading, 1, 4, 5);
        if (!p->puppet) return;
    }
    /* the vehicle: one of the right model, kept where their game has it */
    const int in_vehicle = s->model >= 0 && (s->flags & MP_IN_VEHICLE);
    if (p->car) {
        CtwVehicleState v;
        memset(&v, 0, sizeof v);
        v.size = sizeof v;
        const int alive = api->get_vehicle_state(p->car, &v) && !(v.flags & CTW_VEHICLE_DEAD);
        if (!in_vehicle || p->car_model != s->model || !alive) p->car_leaving = 1;
    }
    if (p->car_leaving && p->car) {
        if (api->remove_vehicle(p->car)) { p->car = 0; p->car_leaving = 0; }   /* (once the puppet is out) */
    }
    if (in_vehicle && !p->car && !p->car_leaving) {
        p->car = api->spawn_vehicle_at(s->model, s->vpos, s->heading, s->palette);
        p->car_model = s->model;
        if (p->car) {
            api->set_vehicle_persistent(p->car, 1);
            api->set_vehicle_engine(p->car, 1);
        }
    }
    if (p->car && !p->car_leaving) {
        float at[3];
        for (int k = 0; k < 3; ++k) at[k] = s->vpos[k] + s->vvel[k] * age;
        api->set_vehicle_pose(p->car, at, s->vquat, s->vvel);
    }
    CtwPuppetState ps;
    memset(&ps, 0, sizeof ps);
    ps.size = sizeof ps;
    for (int k = 0; k < 3; ++k) {
        ps.position[k] = s->pos[k] + s->vel[k] * age;
        ps.velocity[k] = s->vel[k];
    }
    ps.heading = s->heading;
    ps.move = s->move;
    ps.flags = s->flags & MP_DEAD ? CTW_PUPPET_DEAD : 0;
    ps.vehicle = p->car && !p->car_leaving ? p->car : 0;
    api->set_puppet_state(p->puppet, &ps);
}

static void report_hits(void) {   /* what the player did to the others' characters goes to their games */
    for (int s = 0; s < MP_MAX_PLAYERS; ++s) {
        Player* p = &players[s];
        if (!p->used || !p->puppet || s == my_slot) continue;
        const int damage = api->take_puppet_damage(p->puppet);
        if (damage <= 0) continue;
        MpMessage m;
        memset(&m, 0, sizeof m);
        m.type = MP_HIT;
        m.sender = (uint8_t)my_slot;
        m.u.hit.attacker = (uint8_t)my_slot;
        m.u.hit.victim = (uint8_t)s;
        m.u.hit.damage = (uint16_t)(damage > 65535 ? 65535 : damage);
        if (mode == HOSTING) send_to(&p->addr, &m);
        else send_to(&host_addr, &m);
    }
}

static void take_hit(int attacker, int damage) {   /* (from the network: applied at the next game frame) */
    if (attacker >= 0 && attacker < MP_MAX_PLAYERS) pending_damage[attacker] += damage;
}

static void apply_hits(void) {
    for (int s = 0; s < MP_MAX_PLAYERS; ++s) {
        if (pending_damage[s] > 0 && api->damage_player(pending_damage[s])) {
            last_attacker = s;
            last_hit_tick = ticks;
        }
        pending_damage[s] = 0;
    }
}

/* ---- the player's own state ----------------------------------------------------------------------------------- */
static void own_state(MpState* s) {
    memset(s, 0, sizeof *s);
    CtwPlayerState ps;
    memset(&ps, 0, sizeof ps);
    ps.size = sizeof ps;
    api->get_player_state(&ps);
    int health = 0, armour = 0, move = 0, weapon = 0;
    api->get_player_vitals(&health, &armour, &move);
    if (CTW_MOD_HAS(api, get_player_weapon)) api->get_player_weapon(&weapon, NULL, NULL);
    s->slot = (uint8_t)my_slot;
    s->seq = ++my_seq;
    for (int k = 0; k < 3; ++k) { s->pos[k] = ps.position[k]; s->vel[k] = ps.velocity[k]; }
    s->heading = ps.heading;
    s->move = (uint8_t)move;
    s->flags = (uint8_t)((ps.flags & CTW_PLAYER_DEAD ? MP_DEAD : 0) | (ps.flags & CTW_PLAYER_IN_VEHICLE ? MP_IN_VEHICLE : 0));
    s->health = (uint8_t)health;
    s->armour = (uint8_t)armour;
    s->body = (uint8_t)ps.body_set;
    s->upper = (uint8_t)ps.palette_upper;
    s->legs = (uint8_t)ps.palette_legs;
    s->weapon = (uint8_t)weapon;
    s->model = -1;
    if (ps.vehicle) {
        CtwVehicleState v;
        memset(&v, 0, sizeof v);
        v.size = sizeof v;
        if (api->get_vehicle_state(ps.vehicle, &v) && api->get_vehicle_pose(ps.vehicle, s->vpos, s->vquat, s->vvel)) {
            s->model = (int16_t)v.model_id;
            s->palette = (uint8_t)v.palette;
        }
    }
}

static void watch_own_death(void) {
    CtwPlayerState ps;
    memset(&ps, 0, sizeof ps);
    ps.size = sizeof ps;
    api->get_player_state(&ps);
    const int dead = (ps.flags & CTW_PLAYER_DEAD) != 0;
    if (dead && !was_dead) {
        char text[MP_CHAT];
        if (last_attacker >= 0 && ticks - last_hit_tick < 5 * TICKS)
            snprintf(text, sizeof text, "* was killed by %s", player_name(last_attacker));
        else snprintf(text, sizeof text, "* died");
        send_chat(my_slot, text);
        last_attacker = -1;
    }
    was_dead = dead;
}

/* ---- starting and stopping -------------------------------------------------------------------------------------- */
static void leave(const char* why) {
    if (mode != OFF) {
        MpMessage m;
        memset(&m, 0, sizeof m);
        m.type = MP_BYE;
        m.sender = (uint8_t)(my_slot < 0 ? 0xFF : my_slot);
        if (mode == HOSTING) send_to_clients(&m, -1);
        else send_to(&host_addr, &m);
    }
    forget_everyone();
    net_close();
    mode = OFF;
    my_slot = -1;
    set_status(why);
}

static void host_game(void* user) {
    (void)user;
    leave("");
    save_settings();
    if (!net_start() || !net_open(port_number())) {
        set_status("Could not open the UDP port (is something else using it?)");
        return;
    }
    mode = HOSTING;
    my_slot = 0;
    players[0].used = 1;
    snprintf(players[0].name, MP_NAME, "%s", my_name);
    char text[128];
    snprintf(text, sizeof text, "Hosting on UDP port %u. Others join with your address.", (unsigned)port_number());
    set_status(text);
    add_chat(CTW_UI_OK, text);
}

static void join_game(void* user) {
    (void)user;
    leave("");
    save_settings();
    if (!net_start() || !net_resolve(address, port_number(), &host_addr)) {
        set_status("Could not find that address");
        return;
    }
    if (!net_open(0)) { set_status("Could not open a UDP socket"); return; }
    mode = JOINING;
    join_started = now;
    hello_time = 0;
    set_status("Joining...");
}

static void leave_button(void* user) { (void)user; leave("Left the game"); }
static void panel_button(void* user) { (void)user; panel_open = !panel_open; }

/* ---- the host --------------------------------------------------------------------------------------------------- */
static int client_slot(const NetAddr* from) {
    for (int s = 1; s < MP_MAX_PLAYERS; ++s)
        if (players[s].used && net_same(&players[s].addr, from)) return s;
    return -1;
}

static int newer(uint16_t a, uint16_t b) { return (int16_t)(a - b) > 0; }

static void host_receive(const NetAddr* from, const MpMessage* m) {
    int slot = client_slot(from);
    if (m->type == MP_HELLO) {
        MpMessage r;
        memset(&r, 0, sizeof r);
        r.sender = 0;
        if (m->u.hello.api == 0) { r.type = MP_REFUSE; r.u.refuse.reason = MP_WRONG_VERSION; send_to(from, &r); return; }
        if (slot < 0) {
            for (int s = 1; s < MP_MAX_PLAYERS && slot < 0; ++s) if (!players[s].used) slot = s;
            if (slot < 0) { r.type = MP_REFUSE; r.u.refuse.reason = MP_FULL; send_to(from, &r); return; }
            memset(&players[slot], 0, sizeof players[slot]);
            players[slot].used = 1;
            players[slot].addr = *from;
            snprintf(players[slot].name, MP_NAME, "%s", m->u.hello.name[0] ? m->u.hello.name : "Player");
            char text[64];
            snprintf(text, sizeof text, "* joined");
            players[slot].heard = now;
            send_roster();
            send_chat(slot, text);
        }
        players[slot].heard = now;
        r.type = MP_WELCOME;
        r.u.welcome.slot = (uint8_t)slot;
        r.u.welcome.max = MP_MAX_PLAYERS;
        send_to(from, &r);
        return;
    }
    if (slot < 0 || m->sender != slot) return;   /* only players who joined, speaking for themselves */
    Player* p = &players[slot];
    p->heard = now;
    switch (m->type) {
    case MP_STATE:
        if (m->u.state.slot == slot && (!p->has_state || newer(m->u.state.seq, p->state.seq))) {
            p->state = m->u.state;
            p->has_state = 1;
            p->state_tick = ticks;
        }
        break;
    case MP_CHAT_MSG: {
        MpMessage c = *m;
        c.u.chat.slot = (uint8_t)slot;
        send_to_clients(&c, -1);
        show_line(slot, c.u.chat.text);
        break;
    }
    case MP_HIT:
        if (m->u.hit.attacker != slot) break;
        if (m->u.hit.victim == 0) take_hit(slot, m->u.hit.damage);
        else if (players[m->u.hit.victim].used) send_to(&players[m->u.hit.victim].addr, m);
        break;
    case MP_BYE: {
        char text[MP_CHAT];
        snprintf(text, sizeof text, "%s left", p->name);
        add_chat(CTW_UI_DIM, text);
        MpMessage c;
        memset(&c, 0, sizeof c);
        c.type = MP_CHAT_MSG;
        c.u.chat.slot = (uint8_t)slot;
        snprintf(c.u.chat.text, sizeof c.u.chat.text, "* left");
        send_to_clients(&c, slot);
        forget_player(slot);
        send_roster();
        break;
    }
    default:
        break;
    }
}

static void host_send(void) {
    for (int s = 1; s < MP_MAX_PLAYERS; ++s) {   /* clients that went quiet */
        if (!players[s].used || now - players[s].heard < TIMEOUT) continue;
        char text[MP_CHAT];
        snprintf(text, sizeof text, "%s lost connection", players[s].name);
        add_chat(CTW_UI_DIM, text);
        forget_player(s);
        send_roster();
    }
    if (now - last_state_sent >= STATE_EVERY) {   /* everyone's state to everyone */
        last_state_sent = now;
        own_state(&players[0].state);
        players[0].has_state = 1;
        players[0].state_tick = ticks;
        for (int s = 0; s < MP_MAX_PLAYERS; ++s) {
            if (!players[s].used || !players[s].has_state) continue;
            MpMessage m;
            memset(&m, 0, sizeof m);
            m.type = MP_STATE;
            m.sender = 0;
            m.u.state = players[s].state;
            send_to_clients(&m, s);
        }
    }
    if (now - last_world_sent >= 1.0) {
        last_world_sent = now;
        send_roster();
        MpMessage w;
        memset(&w, 0, sizeof w);
        w.type = MP_WORLD;
        w.u.world.time = api->get_time_of_day();
        w.u.world.weather = (uint8_t)api->get_weather();
        send_to_clients(&w, -1);
    }
}

/* ---- a client ------------------------------------------------------------------------------------------------- */
static void client_receive(const NetAddr* from, const MpMessage* m) {
    if (!net_same(from, &host_addr) || m->sender != 0) return;
    host_heard = now;
    switch (m->type) {
    case MP_WELCOME:
        if (mode == JOINING || my_slot != m->u.welcome.slot) {
            my_slot = m->u.welcome.slot;
            mode = JOINED;
            players[my_slot].used = 1;
            snprintf(players[my_slot].name, MP_NAME, "%s", my_name);
            set_status("Joined");
            add_chat(CTW_UI_OK, "Joined the game");
        }
        break;
    case MP_REFUSE:
        leave(m->u.refuse.reason == MP_FULL ? "The game is full" : "The host plays a different version");
        break;
    case MP_STATE: {
        const int s = m->u.state.slot;
        if (mode != JOINED || s == my_slot) break;
        Player* p = &players[s];
        if (!p->has_state || newer(m->u.state.seq, p->state.seq)) {
            p->used = 1;
            p->state = m->u.state;
            p->has_state = 1;
            p->state_tick = ticks;
        }
        break;
    }
    case MP_ROSTER: {
        int listed[MP_MAX_PLAYERS] = {0};
        for (int i = 0; i < m->u.roster.count; ++i) {
            const int s = m->u.roster.slots[i];
            listed[s] = 1;
            players[s].used = 1;
            memcpy(players[s].name, m->u.roster.names[i], MP_NAME);
        }
        for (int s = 0; s < MP_MAX_PLAYERS; ++s)
            if (players[s].used && !listed[s] && s != my_slot) forget_player(s);
        break;
    }
    case MP_CHAT_MSG: show_line(m->u.chat.slot, m->u.chat.text); break;
    case MP_HIT:
        if (mode == JOINED && m->u.hit.victim == my_slot) take_hit(m->u.hit.attacker, m->u.hit.damage);
        break;
    case MP_WORLD:
        world_pending = sync_world;
        world_time = m->u.world.time;
        world_weather = m->u.world.weather;
        break;
    case MP_BYE:
        leave("The host left");
        add_chat(CTW_UI_DIM, "The host left the game");
        break;
    default:
        break;
    }
}

static void client_send(void) {
    if (mode == JOINING) {
        if (now - join_started > TIMEOUT) { leave("No answer from that address"); return; }
        if (!hello_time || now - hello_time >= 1.0) {
            MpMessage m;
            memset(&m, 0, sizeof m);
            m.type = MP_HELLO;
            m.sender = 0xFF;
            m.u.hello.api = (uint16_t)api->version;
            snprintf(m.u.hello.name, MP_NAME, "%s", my_name);
            CtwPlayerState ps;
            memset(&ps, 0, sizeof ps);
            ps.size = sizeof ps;
            api->get_player_state(&ps);
            m.u.hello.body = (uint8_t)ps.body_set;
            m.u.hello.upper = (uint8_t)ps.palette_upper;
            m.u.hello.legs = (uint8_t)ps.palette_legs;
            send_to(&host_addr, &m);
            hello_time = now;
        }
        return;
    }
    if (now - host_heard > TIMEOUT) { leave("Lost the connection to the host"); return; }
    if (now - last_state_sent >= STATE_EVERY) {
        last_state_sent = now;
        MpMessage m;
        memset(&m, 0, sizeof m);
        m.type = MP_STATE;
        m.sender = (uint8_t)my_slot;
        own_state(&m.u.state);
        send_to(&host_addr, &m);
    }
}

/* ---- every game frame ------------------------------------------------------------------------------------------- */
/* the network: what came in, timeouts and what is due to go out (every drawn frame, also while paused) */
static void pump(void) {
    now = api->time_seconds();
    if (mode == OFF) return;
    uint8_t buf[MP_PACKET];
    NetAddr from;
    int n, budget = 512;   /* (never stall a frame on a flood) */
    while (mode != OFF && budget-- > 0 && (n = net_receive(&from, buf, sizeof buf)) > 0) {
        MpMessage m;
        if (!mp_decode(buf, (size_t)n, &m)) continue;
        if (mode == HOSTING) host_receive(&from, &m);
        else client_receive(&from, &m);
    }
    if (mode == HOSTING) host_send();
    else if (mode != OFF) client_send();
}

static void tick(void* user) {
    (void)user;
    ++ticks;
    now = api->time_seconds();
    if (autostart) {   /* CTW_MULTIPLAYER, once the game runs */
        if (autostart == 1) host_game(0);
        else join_game(0);
        autostart = 0;
    }
    pump();
    if (world_pending) {
        world_pending = 0;
        if (fabsf(api->get_time_of_day() - world_time) > 0.1f) api->set_time_of_day(world_time);
        if (api->get_weather() != world_weather) api->set_weather(world_weather);
    }
    if (mode == HOSTING || mode == JOINED) {
        apply_hits();
        for (int s = 0; s < MP_MAX_PLAYERS; ++s) update_remote(s);
        report_hits();
        watch_own_death();
    }
}

/* ---- the HUD ---------------------------------------------------------------------------------------------------- */
static void draw_names(void) {
    if (!show_names) return;
    for (int s = 0; s < MP_MAX_PLAYERS; ++s) {
        const Player* p = &players[s];
        if (!p->used || !p->has_state || s == my_slot) continue;
        const float* at = p->car ? p->state.vpos : p->state.pos;
        const float head[3] = {at[0], at[1], at[2] + (p->car ? 3.f : 2.6f)};
        float xy[2];
        if (!api->world_to_screen(head, xy)) continue;
        const float w = api->text_width(1.f, p->name), lh = api->line_height(1.f);
        api->draw_rect(xy[0] - w * 0.5f - 6.f, xy[1] - lh - 8.f, w + 12.f, lh + 10.f, 0x00000090u);
        api->draw_text(xy[0] - w * 0.5f, xy[1] - lh - 4.f, 1.f, p->state.flags & MP_DEAD ? CTW_UI_DIM : CTW_UI_TEXT, p->name);
        const float bar = w + 12.f, health = p->state.health / 127.f;
        api->draw_rect(xy[0] - bar * 0.5f, xy[1] + 2.f, bar, 4.f, 0x000000A0u);
        api->draw_rect(xy[0] - bar * 0.5f, xy[1] + 2.f, bar * (health > 1.f ? 1.f : health), 4.f, 0x5BD48AFFu);
        if (p->state.armour)
            api->draw_rect(xy[0] - bar * 0.5f, xy[1] + 7.f, bar * p->state.armour / 127.f, 3.f, 0x4C8FD6FFu);
    }
}

static void draw_chat(void) {
    const float x = CTW_UI_GAP + 8.f, lh = api->line_height(1.f), bottom = ctw_ui_screen_bottom(api) - (chat_open ? 54.f : 8.f);
    float y = bottom;
    for (int i = chat_count - 1; i >= 0; --i) {
        if (!chat_open && now - chat_log[i].time > CHAT_SHOW) continue;
        y -= lh + 4.f;
        const float w = api->text_width(1.f, chat_log[i].text);
        api->draw_rect(x - 6.f, y - 2.f, w + 12.f, lh + 4.f, 0x00000080u);
        api->draw_text(x, y, 1.f, chat_log[i].rgba, chat_log[i].text);
    }
    if (!chat_open) return;
    const float row = ctw_ui_row_h(&ui), w = (float)api->screen_width() * 0.45f;
    int focused = 1;
    ctw_ui_round_rect(api, x - 8.f, bottom + 6.f, w + 16.f, row + 12.f, 10.f, CTW_UI_PANEL);
    ctw_ui_textbox(&ui, x, bottom + 12.f, w, row, chat_text, sizeof chat_text, &focused, "Say something (Enter to send, Esc to close)");
}

static void draw_panel(void) {
    const CtwUiRect r = ctw_ui_side_area(api, 520.f);
    const float row = ctw_ui_row_h(&ui), pad = 16.f, x = r.x + pad, w = r.w - 2.f * pad;
    const char* modes[] = {"Not connected", "Hosting", "Joining", "Connected"};
    if (ctw_ui_panel(&ui, r.x, r.y, r.w, r.h, "MULTIPLAYER", modes[mode])) { panel_open = 0; name_focused = address_focused = port_focused = 0; return; }
    float y = r.y + ctw_ui_title_h(&ui) + 12.f;
    const float label = api->line_height(0.9f) + 4.f;
    api->draw_text(x, y, 0.9f, CTW_UI_DIM, "Your name");
    y += label;
    if (ctw_ui_textbox(&ui, x, y, w, row, my_name, sizeof my_name, &name_focused, "Player")) {}
    y += row + 10.f;
    api->draw_text(x, y, 0.9f, CTW_UI_DIM, "Host address (to join)");
    api->draw_text(x + w * 0.72f, y, 0.9f, CTW_UI_DIM, "UDP port");
    y += label;
    ctw_ui_textbox(&ui, x, y, w * 0.7f, row, address, sizeof address, &address_focused, "192.168.1.10");
    ctw_ui_textbox(&ui, x + w * 0.72f, y, w * 0.28f, row, port_text, sizeof port_text, &port_focused, "7777");
    y += row + 14.f;
    const float bw = (w - 20.f) / 3.f;
    if (ctw_ui_button(&ui, x, y, bw, row, "Host")) host_game(0);
    if (ctw_ui_button(&ui, x + bw + 10.f, y, bw, row, "Join")) join_game(0);
    if (ctw_ui_button(&ui, x + 2.f * (bw + 10.f), y, bw, row, "Leave")) leave("Left the game");
    y += row + 12.f;
    if (status[0]) y += ctw_ui_text_wrap(&ui, x, y, 0.9f, mode == OFF ? CTW_UI_ERROR : CTW_UI_OK, status, w, 1) + 8.f;
    api->draw_text(x, y, 1.f, CTW_UI_TEXT, "Players");
    y += api->line_height(1.f) + 6.f;
    for (int s = 0; s < MP_MAX_PLAYERS; ++s) {
        if (!players[s].used) continue;
        char line[96];
        snprintf(line, sizeof line, "%s%s%s", players[s].name, s == 0 ? "  (host)" : "", s == my_slot ? "  (you)" : "");
        api->draw_rect(x, y, w, row - 6.f, CTW_UI_ROW);
        ctw_ui_text_fit(&ui, x + 10.f, ctw_ui_text_y(&ui, y, row - 6.f, 1.f), 1.f, s == my_slot ? CTW_UI_ACCENT : CTW_UI_TEXT, line, w - 20.f);
        y += row - 2.f;
    }
    ctw_ui_footer(&ui, r.x, r.y, r.w, r.h, CTW_UI_DIM, "T: chat.   F9: close.   Others need your address and the UDP port open.");
}

static void update_game_input(void) {
    api->set_game_input(!(chat_open || (panel_open && (name_focused || address_focused || port_focused))));
}

static void hud(void* user) {
    (void)user;
    pump();
    ctw_ui_begin(&ui, api);
    draw_names();
    draw_chat();
    if (panel_open) draw_panel();
    update_game_input();
}

static int on_key(int key, void* user) {
    (void)user;
    if (chat_open) {
        if (key == SC_RETURN || key == SC_KP_ENTER) {
            if (chat_text[0]) send_chat(my_slot, chat_text);
            chat_text[0] = 0;
            chat_open = 0;
        } else if (key == SC_ESCAPE) {
            chat_open = 0;
        }
        update_game_input();
        return key < SC_F1 || key > SC_F12;   /* typing stays out of the game */
    }
    if (key == SC_F9) { panel_open = !panel_open; name_focused = address_focused = port_focused = 0; update_game_input(); return 1; }
    if (panel_open && (name_focused || address_focused || port_focused)) {
        if (key == SC_RETURN || key == SC_ESCAPE) { name_focused = address_focused = port_focused = 0; save_settings(); }
        update_game_input();
        return key < SC_F1 || key > SC_F12;
    }
    if (panel_open && key == SC_ESCAPE) { panel_open = 0; return 1; }
    if (key == SC_T && (mode == HOSTING || mode == JOINED)) {
        chat_open = 1;
        chat_text[0] = 0;
        update_game_input();
        return 1;
    }
    return 0;
}

static void enabled_changed(int enabled, void* user) {
    (void)user;
    if (!enabled) { leave(""); panel_open = chat_open = 0; api->set_game_input(1); }
}

CTW_MOD_EXPORT int ctw_mod_init(CtwMod* mod, const CtwApi* a) {
    api = a;
    self = mod;
    if (!CTW_MOD_HAS(api, set_vehicle_pose) || !CTW_MOD_HAS(api, spawn_puppet)) {
        api->log(mod, "Multiplayer needs a newer game (mod API version 13)");
        if (CTW_MOD_HAS(api, menu_add_label)) api->menu_add_label(mod, "Needs a newer game");
        return 0;
    }
    load_settings();
    const char* start = getenv("CTW_MULTIPLAYER");
    const char* name = getenv("CTW_MULTIPLAYER_NAME");
    if (name && *name) snprintf(my_name, sizeof my_name, "%s", name);
    if (start && !strcmp(start, "host")) autostart = 1;
    else if (start && !strncmp(start, "join", 4)) {
        autostart = 2;
        if (start[4] == ' ' && start[5]) snprintf(address, sizeof address, "%s", start + 5);
    }
    api->menu_add_button(mod, "Host or join a game (F9)", panel_button, 0);
    api->menu_add_button(mod, "Leave the game", leave_button, 0);
    api->menu_add_toggle(mod, "Show players' names", &show_names);
    api->menu_add_toggle(mod, "Use the host's time and weather", &sync_world);
    api->on_tick(mod, tick, 0);
    api->on_draw_hud(mod, hud, 0);
    api->on_key(mod, on_key, 0);
    api->on_enabled_changed(mod, enabled_changed, 0);
    return 0;
}

CTW_MOD_EXPORT void ctw_mod_shutdown(void) {
    if (!api || !CTW_MOD_HAS(api, set_vehicle_pose)) return;
    leave("");
    save_settings();
    net_stop();
    api->set_game_input(1);
}
