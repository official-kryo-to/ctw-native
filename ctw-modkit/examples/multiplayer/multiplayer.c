/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Kryo.to
 * See LICENSE in the repository root.
 *
 * Multiplayer: up to 32 players in one city over UDP. One player hosts (F9, Host), the others join with the
 * host's address. Everyone sees the others walk, drive, ride, fight and die; hits on another player are taken by
 * that player's own game; there is chat (T) and the host's time of day and weather. Traffic, pedestrians, police
 * explosions and pickups belong to the host's shared world; cash and inventories are individual.
 *
 * Needs a version 16 game (shared city, pickups and contact geometry). The host must be reachable on its UDP port (7777 by
 * default): on the same network that just works; over the internet forward the port, or use a virtual network
 * such as Tailscale, ZeroTier or Radmin VPN.
 *
 * A shortcut can start a game straight away: set CTW_MULTIPLAYER to "host", "join <address>" or "online <code>"
 * (and optionally CTW_MULTIPLAYER_NAME, and CTW_ONLINE_URL for another CTW Online) before starting the game.
 */
#include "ctw_mod.h"
#include "ctw_ui.h"
#include "net.h"
#include "online.h"
#include "protocol.h"
#include "worldsync.h"
#include "pickupsync.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { SC_T = 23, SC_RETURN = 40, SC_ESCAPE = 41, SC_F1 = 58, SC_F9 = 66, SC_F12 = 69, SC_KP_ENTER = 88 };
/* CTW Online, the public list and relay (online/ in the repository); multiplayer.ini's online= overrides it */
#define ONLINE_DEFAULT_URL "https://ctw-online.kryotooooo.workers.dev"
#define LAN_PORT 7778
#define ONLINE_VERSION "mp4"
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
static char game_name[41] = "";
static char online_url[160] = ONLINE_DEFAULT_URL;
static char join_code[8] = "";
static int show_names = 1, sync_world = 1, list_public = 1;
static int max_players = MP_DEFAULT_PLAYERS, session_max = MP_DEFAULT_PLAYERS;
/* what the settings file says, kept when a shortcut's CTW_MULTIPLAYER_NAME / CTW_ONLINE_URL stand in for them */
static char saved_name[MP_NAME], saved_url[160];
static int name_from_env, url_from_env;
static int max_from_env, saved_max = MP_DEFAULT_PLAYERS;

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
} Player;

static int mode = OFF, my_slot = -1;
static int via_relay;           /* a player in a CTW Online game: everything goes through the relay */
static int relay_hosting;       /* the host also takes players through the relay (it was offered online) */
static double last_beacon, last_heartbeat;
/* games on the local network, from their beacons */
typedef struct LanGame { NetAddr addr; char name[41]; int players, max; double seen; } LanGame;
static LanGame lan_games[16];
static int lan_count, lan_listening;
static OnlineServer online_games[ONLINE_MAX_SERVERS];
static int online_count = -2;
static Player players[MP_MAX_PLAYERS];
static NetAddr host_addr;
static uint32_t ticks;
static double now, host_heard, hello_time, join_started, last_state_sent, last_world_sent;
static int pending_damage[MP_MAX_PLAYERS];   /* hits on the player, by attacker, taken at the next game frame */
static int world_pending;
static float world_time;
static int world_weather;
static uint16_t my_seq;
static char status[192];
static int last_attacker = -1;
static uint32_t last_hit_tick;
static int was_dead;
static uint32_t world_hit_seq, world_hit_received;
static int has_world_hit, pending_world_damage;
static uint64_t world_hit_bits;

static struct { char text[MP_CHAT + MP_NAME + 4]; double time; uint32_t rgba; } chat_log[CHAT_LINES];
static int chat_count;

/* ---- the windows ---------------------------------------------------------------------------------------------- */
static CtwUi ui;
static int panel_open, name_focused, address_focused, port_focused, game_focused, code_focused, autostart;
static int chat_open;
enum Tab { TAB_ONLINE, TAB_NEARBY, TAB_ADDRESS, TAB_HOST };
static int tab = TAB_ONLINE;
static CtwUiScroll list_scroll, player_scroll;
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

static int player_limit(const char* text) {
    char* end;
    const long n = strtol(text, &end, 10);
    if (end == text || *end) return MP_DEFAULT_PLAYERS;
    return n < 2 ? 2 : n > MP_MAX_PLAYERS ? MP_MAX_PLAYERS : (int)n;
}

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
        else if (!strcmp(line, "game")) snprintf(game_name, sizeof game_name, "%s", value);
        else if (!strcmp(line, "online")) snprintf(online_url, sizeof online_url, "%s", value);
        else if (!strcmp(line, "public")) list_public = atoi(value) != 0;
        else if (!strcmp(line, "max_players")) max_players = player_limit(value);
    }
    fclose(f);
}

static void save_settings(void) {
    char path[512];
    settings_path(path, sizeof path);
    FILE* f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "name=%s\naddress=%s\nport=%s\ngame=%s\npublic=%d\n", name_from_env ? saved_name : my_name, address,
            port_text, game_name, list_public);
    const char* url = url_from_env ? saved_url : online_url;
    fprintf(f, "max_players=%d\n", max_from_env ? saved_max : max_players);
    if (strcmp(url, ONLINE_DEFAULT_URL) != 0) fprintf(f, "online=%s\n", url);   /* (empty: turned off) */
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
    if (!n) return;
    if (to->ip == 0) online_send(to->port, buf, n);   /* a relay address: the player's number (or the host, 0) */
    else net_send(to, buf, n);
}

static void send_to_clients(const MpMessage* m, int except) {   /* host */
    uint8_t buf[MP_PACKET];
    const size_t n = mp_encode(m, buf, sizeof buf);
    if (!n) return;
    int broadcast_relay = 0;
    for (int s = 1; s < MP_MAX_PLAYERS; ++s) {
        if (!players[s].used || s == except) continue;
        if (players[s].addr.ip) net_send(&players[s].addr, buf, n);
        else if (except < 0) broadcast_relay = 1;
        else online_send(players[s].addr.port, buf, n);
    }
    if (broadcast_relay) online_send(0xFFFF, buf, n);
}

static void world_send(const MpMessage* m, int target, int except) {
    if (mode == HOSTING) {
        if (target > 0 && target < MP_MAX_PLAYERS && players[target].used) send_to(&players[target].addr, m);
        else if (target < 0) send_to_clients(m, except);
    } else if (mode == JOINED) { MpMessage client=*m; client.sender=(uint8_t)my_slot; send_to(&host_addr, &client); }
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
/* each player's colour: their dot on the radar and the swatch on their name tag */
static uint32_t player_colour(int slot) {
    static const uint32_t colours[] = {0xF2C230FFu, 0x4FB3FFFFu, 0xFF5A5AFFu, 0x5BD48AFFu,
                                                     0xC77DFFFFu, 0xFF9A3CFFu, 0x3CE0E0FFu, 0xFF7FD0FFu};
    return colours[(unsigned)slot % (sizeof colours / sizeof colours[0])];
}

static void forget_player(int slot) {
    Player* p = &players[slot];
    if (CTW_MOD_HAS(api, remove_radar_blip)) api->remove_radar_blip(self, slot + 1);
    if (p->puppet) api->remove_puppet(p->puppet);   /* (frees the driver's seat) */
    mp_world_forget(slot);
    mp_pickups_forget(slot);
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
    /* Resolve the host ID; every occupant references this same car. World snapshots create it once. */
    p->car = s->vehicle && (s->flags & MP_IN_VEHICLE) ? api->world_vehicle_handle(s->vehicle) : 0;
    CtwPuppetState ps;
    memset(&ps, 0, sizeof ps);
    ps.size = sizeof ps;
    for (int k = 0; k < 3; ++k) {
        ps.position[k] = s->pos[k] + s->vel[k] * age;
        ps.velocity[k] = s->vel[k];
    }
    ps.heading = s->heading;
    ps.move = s->move;
    ps.flags = (s->flags & MP_DEAD ? CTW_PUPPET_DEAD : 0) | (s->flags & MP_FIRING ? CTW_PUPPET_FIRING : 0);
    ps.vehicle = p->car;
    ps.weapon = s->weapon;
    api->set_puppet_state(p->puppet, &ps);
    api->set_puppet_seat(p->puppet, s->seat);
    if (mode == HOSTING && CTW_MOD_HAS(api, set_puppet_view)) {   /* what they see counts as on screen here */
        CtwView view;
        memset(&view, 0, sizeof view);
        view.size = sizeof view;
        for (int k = 0; k < 3; ++k) view.eye[k] = s->view_eye[k];
        view.yaw = s->view_yaw; view.pitch = s->view_pitch; view.width = s->view_width; view.height = s->view_height;
        api->set_puppet_view(p->puppet, s->view_width ? &view : NULL);
    }
    if (CTW_MOD_HAS(api, set_radar_blip)) {   /* where they are, on the radar */
        api->set_radar_blip(self, slot + 1, ps.vehicle ? s->vpos : ps.position, player_colour(slot));
        if (CTW_MOD_HAS(api, set_radar_blip_label)) {
            char label[3];
            if (slot < 26) { label[0] = (char)('A' + slot); label[1] = 0; }
            else { label[0] = 'A'; label[1] = (char)('A' + slot - 26); label[2] = 0; }
            api->set_radar_blip_label(self, slot + 1, label);
        }
    }
}

static void report_hits(void) {   /* what the player did to the others' characters goes to their games */
    for (int s = 0; s < MP_MAX_PLAYERS; ++s) {
        Player* p = &players[s];
        if (!p->used || !p->puppet || s == my_slot) continue;
        if(mode==HOSTING && CTW_MOD_HAS(api,take_puppet_world_damage)) {
            const int worldDamage=api->take_puppet_world_damage(p->puppet);
            if(worldDamage>0) {
                MpMessage hit; memset(&hit,0,sizeof hit); hit.type=MP_WORLD_HIT;
                hit.u.world_hit.seq=++world_hit_seq; hit.u.world_hit.victim=(uint8_t)s;
                hit.u.world_hit.damage=(uint16_t)(worldDamage>65535?65535:worldDamage);
                for(int retry=0;retry<3;++retry) send_to(&p->addr,&hit);
            }
        }
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
    if(pending_world_damage) { api->damage_player(pending_world_damage); pending_world_damage=0; last_attacker=-1; }
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
    int health = 0, armour = 0, move = 0, firing = 0, weapon = 0;
    api->get_player_vitals(&health, &armour, &move, &firing);
    if (CTW_MOD_HAS(api, get_player_weapon)) api->get_player_weapon(&weapon, NULL, NULL);
    s->slot = (uint8_t)my_slot;
    s->seq = ++my_seq;
    for (int k = 0; k < 3; ++k) { s->pos[k] = ps.position[k]; s->vel[k] = ps.velocity[k]; }
    s->heading = ps.heading;
    s->move = (uint8_t)move;
    s->flags = (uint8_t)((ps.flags & CTW_PLAYER_DEAD ? MP_DEAD : 0) | (ps.flags & CTW_PLAYER_IN_VEHICLE ? MP_IN_VEHICLE : 0) |
                         (firing ? MP_FIRING : 0));
    s->health = (uint8_t)health;
    s->armour = (uint8_t)armour;
    s->body = (uint8_t)ps.body_set;
    s->upper = (uint8_t)ps.palette_upper;
    s->legs = (uint8_t)ps.palette_legs;
    s->weapon = (uint8_t)weapon;
    s->model = -1;
    s->seat = -1;
    if (CTW_MOD_HAS(api, get_view)) {
        CtwView view;
        memset(&view, 0, sizeof view);
        view.size = sizeof view;
        if (api->get_view(&view)) {
            for (int k = 0; k < 3; ++k) s->view_eye[k] = view.eye[k];
            s->view_yaw = (uint16_t)view.yaw; s->view_pitch = (uint16_t)view.pitch;
            s->view_width = (uint16_t)view.width; s->view_height = (uint16_t)view.height;
        }
    }
    if (ps.vehicle) {
        CtwVehicleState v;
        memset(&v, 0, sizeof v);
        v.size = sizeof v;
        if (api->get_vehicle_state(ps.vehicle, &v) && api->get_vehicle_pose(ps.vehicle, s->vpos, s->vquat, s->vvel)) {
            s->model = (int16_t)v.model_id;
            s->palette = (uint8_t)v.palette;
            s->vehicle = api->world_vehicle_id(ps.vehicle);
            s->seat = (int8_t)api->get_player_seat();
            if (CTW_MOD_HAS(api, get_vehicle_visual)) { s->visual.size = sizeof s->visual; api->get_vehicle_visual(ps.vehicle, &s->visual); }
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
    mp_world_stop();
    mp_pickups_stop(); has_world_hit=0; world_hit_bits=0; pending_world_damage=0;
    net_close();
    if (via_relay || relay_hosting) online_leave();
    via_relay = relay_hosting = 0;
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
    if (!mp_world_start(api, CTW_MULTIPLAYER_HOST, world_send)) { leave("Could not start the shared world"); return; }
    if (!mp_pickups_start(api,CTW_MULTIPLAYER_HOST,world_send)) { leave("Could not start shared pickups"); return; }
    session_max = max_players;
    my_slot = 0;
    players[0].used = 1;
    snprintf(players[0].name, MP_NAME, "%s", my_name);
    if (!game_name[0]) snprintf(game_name, sizeof game_name, "%s's game", my_name);
    char text[160];
    if (online_available()) {   /* also through CTW Online: listed publicly, or private and joined by its code */
        online_host(game_name, session_max, ONLINE_VERSION, list_public);
        relay_hosting = 1;
        last_heartbeat = now;
        snprintf(text, sizeof text, "Hosting \"%s\" on your network (UDP port %u) and online: getting its code...",
                 game_name, (unsigned)port_number());
    } else {
        snprintf(text, sizeof text, "Hosting \"%s\" on UDP port %u. Others join with your address.", game_name,
                 (unsigned)port_number());
    }
    set_status(text);
    add_chat(CTW_UI_OK, "Hosting a game");
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

static void join_online(const char* code) {
    leave("");
    save_settings();
    if (!online_available()) { set_status("CTW Online is not set up in this game"); return; }
    online_join(code);
    via_relay = 1;
    memset(&host_addr, 0, sizeof host_addr);   /* the relay's host */
    mode = JOINING;
    join_started = now;
    hello_time = 0;
    char text[64];
    snprintf(text, sizeof text, "Joining game %s...", code);
    set_status(text);
}

static void join_lan(const NetAddr* at) {
    char text[32];
    net_format(at, text, sizeof text);
    *strrchr(text, ':') = 0;
    snprintf(address, sizeof address, "%s", text);
    snprintf(port_text, sizeof port_text, "%u", (unsigned)at->port);
    join_game(0);
}

static void leave_button(void* user) { (void)user; leave("Left the game"); }
static void open_panel(int open) {
    panel_open = open;
    name_focused = address_focused = port_focused = game_focused = code_focused = 0;
    if (open && tab == TAB_ONLINE && online_available()) online_refresh();
    api->set_game_input(1);
}

static void panel_button(void* user) { (void)user; open_panel(!panel_open); }
static void pause_button(void* user) { (void)user; open_panel(1); }

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
        if (m->u.hello.api < 16) { r.type = MP_REFUSE; r.u.refuse.reason = MP_WRONG_VERSION; send_to(from, &r); return; }
        if (slot < 0) {
            for (int s = 1; s < session_max && slot < 0; ++s) if (!players[s].used) slot = s;
            if (slot < 0) { r.type = MP_REFUSE; r.u.refuse.reason = MP_FULL; send_to(from, &r); return; }
            memset(&players[slot], 0, sizeof players[slot]);
            players[slot].used = 1;
            mp_world_peer_join(slot);
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
        r.u.welcome.max = (uint8_t)session_max;
        r.u.welcome.skid_baseline=mp_world_skid_baseline();
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
            mp_world_player(slot, &p->state, now);
            mp_pickups_player(slot,&p->state,now);
            p->has_state = 1;
            p->state_tick = ticks;
        }
        break;
    case MP_CHAT_MSG: {
        MpMessage c = *m;
        c.sender = 0;
        c.u.chat.slot = (uint8_t)slot;
        send_to_clients(&c, -1);
        show_line(slot, c.u.chat.text);
        break;
    }
    case MP_HIT:
        if (m->u.hit.attacker != slot) break;
        if (m->u.hit.victim == 0) take_hit(slot, m->u.hit.damage);
        else if (players[m->u.hit.victim].used) {
            MpMessage hit = *m; hit.sender = 0;
            send_to(&players[m->u.hit.victim].addr, &hit);
        }
        break;
    case MP_EXPLOSION: case MP_ENTITY_DAMAGE: case MP_VEHICLE_CREATE: case MP_VEHICLE_DOOR: case MP_SKID_BATCH: case MP_SKID_ACK:
        mp_world_receive(slot, m, now);
        break;
    case MP_PICKUP_CLAIM: case MP_PICKUP_ACK:
        mp_pickups_receive(slot,m,now); break;
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
    if (relay_hosting) {
        static int announced;
        char code[8];
        const int state = online_hosting(code);
        if (state == 1 && !announced) {
            char text[160];
            snprintf(text, sizeof text, "Online game code: %s (%s). Friends join it from the Online tab.", code,
                     list_public ? "listed publicly" : "private");
            set_status(text);
            add_chat(CTW_UI_OK, text);
            announced = 1;
        } else if (state == -1) {
            set_status("Could not reach CTW Online: the game is on your network only");
            relay_hosting = 0;
        }
        if (state != 1) announced = 0;
        if (state == 1 && now - last_heartbeat >= 20.0) {   /* keeps it listed, with the number of players */
            int count = 0;
            for (int s = 0; s < MP_MAX_PLAYERS; ++s) count += players[s].used;
            online_update(game_name, count);
            last_heartbeat = now;
        }
    }
    if (now - last_beacon >= 1.0) {   /* the local network sees the game: "CTWL", version, port, players, max, name */
        uint8_t beacon[50];
        int count = 0;
        for (int s = 0; s < MP_MAX_PLAYERS; ++s) count += players[s].used;
        memset(beacon, 0, sizeof beacon);
        memcpy(beacon, "CTWL", 4);
        beacon[4] = MP_VERSION;
        beacon[5] = (uint8_t)(port_number() & 255);
        beacon[6] = (uint8_t)(port_number() >> 8);
        beacon[7] = (uint8_t)count;
        beacon[8] = (uint8_t)session_max;
        memcpy(beacon + 9, game_name, strlen(game_name) < 40 ? strlen(game_name) : 40);
        net_beacon_send(LAN_PORT, beacon, sizeof beacon);
        last_beacon = now;
    }
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
        mp_world_request(&players[0].state);
        mp_world_player(0, &players[0].state, now);
        mp_world_correct_seat(&players[0].state);
        players[0].has_state = 1;
        players[0].state_tick = ticks;
        for (int s = 0; s < MP_MAX_PLAYERS; ++s) {
            if (!players[s].used || !players[s].has_state) continue;
            MpMessage m;
            memset(&m, 0, sizeof m);
            m.type = MP_STATE;
            m.sender = 0;
            m.u.state = players[s].state;
            send_to_clients(&m, -1); /* includes the driver's authoritative seat acknowledgment */
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
        if (m->u.welcome.max < 2 || m->u.welcome.max > MP_MAX_PLAYERS || !m->u.welcome.slot ||
            m->u.welcome.slot >= m->u.welcome.max) break;
        session_max = m->u.welcome.max;
        if (mode == JOINING || my_slot != m->u.welcome.slot) {
            my_slot = m->u.welcome.slot;
            mode = JOINED;
            if (!mp_world_start(api, CTW_MULTIPLAYER_CLIENT, world_send)) { leave("Could not join the shared world"); return; }
            mp_world_set_skid_baseline(m->u.welcome.skid_baseline);
            if (!mp_pickups_start(api,CTW_MULTIPLAYER_CLIENT,world_send)) { leave("Could not join shared pickups"); return; }
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
        if (mode != JOINED) break;
        if (s == my_slot) { mp_world_correct_seat(&m->u.state); break; }
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
    case MP_WORLD_VEHICLE: case MP_WORLD_PED: case MP_EXPLOSION: case MP_WORLD_REMOVE: case MP_VEHICLE_CREATED: case MP_SKID_BATCH: case MP_SKID_ACK:
        if (mode == JOINED) mp_world_receive(0, m, now);
        break;
    case MP_PICKUP_STATE: case MP_PICKUP_REMOVE: case MP_PICKUP_GRANT:
        if(mode==JOINED) mp_pickups_receive(0,m,now); break;
    case MP_WORLD_HIT:
        if(mode==JOINED && m->u.world_hit.victim==my_slot) {
            const uint32_t sequence=m->u.world_hit.seq;
            if(!has_world_hit) { has_world_hit=1; world_hit_received=sequence; world_hit_bits=1; }
            else if((int32_t)(sequence-world_hit_received)>0) {
                uint32_t distance=sequence-world_hit_received;
                world_hit_bits=distance>=64 ? 1 : (world_hit_bits<<distance)|1; world_hit_received=sequence;
            } else {
                uint32_t distance=world_hit_received-sequence;
                if(distance>=64 || (world_hit_bits&((uint64_t)1<<distance))) break;
                world_hit_bits|=(uint64_t)1<<distance;
            }
            pending_world_damage+=m->u.world_hit.damage;
            if(pending_world_damage>65535) pending_world_damage=65535;
        }
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
    if (via_relay && online_joined() < 0) {
        leave(mode == JOINING ? "Could not join that game (is the code right?)" : "Lost the connection to the game");
        return;
    }
    if (mode == JOINING) {
        if (now - join_started > TIMEOUT) { leave("No answer from that address"); return; }
        if (via_relay && online_joined() != 1) return;   /* the relay is still connecting */
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
        mp_world_request(&m.u.state);
        send_to(&host_addr, &m);
    }
}

/* ---- every game frame ------------------------------------------------------------------------------------------- */
/* the network: what came in, timeouts and what is due to go out (every drawn frame, also while paused) */
static void listen_lan(void) {   /* the browser's Nearby list */
    if (!lan_listening) lan_listening = net_start() && net_beacon_open(LAN_PORT) ? 1 : -1;
    uint8_t buf[64];
    NetAddr from;
    int n;
    while ((n = net_beacon_receive(&from, buf, sizeof buf)) > 0) {
        if (n != 50 || memcmp(buf, "CTWL", 4) || buf[4] != MP_VERSION) continue;
        from.port = (uint16_t)(buf[5] | buf[6] << 8);
        LanGame* g = NULL;
        for (int i = 0; i < lan_count; ++i) if (net_same(&lan_games[i].addr, &from)) g = &lan_games[i];
        if (!g && lan_count < 16) g = &lan_games[lan_count++];
        if (!g) continue;
        g->addr = from;
        g->players = buf[7];
        g->max = buf[8];
        memcpy(g->name, buf + 9, 40);
        g->name[40] = 0;
        for (char* c = g->name; *c; ++c) if ((unsigned char)*c < 0x20) *c = ' ';
        g->seen = now;
    }
    for (int i = 0; i < lan_count;)   /* gone quiet: gone */
        if (now - lan_games[i].seen > 4.0) lan_games[i] = lan_games[--lan_count]; else ++i;
}

static void pump(void) {
    now = api->time_seconds();
    if (panel_open) listen_lan();
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
    uint16_t relay_player;
    while ((via_relay || relay_hosting) && mode != OFF && budget-- > 0 &&
           (n = online_receive(&relay_player, buf, sizeof buf)) != 0) {
        memset(&from, 0, sizeof from);
        from.port = mode == HOSTING ? relay_player : 0;
        if (n < 0) {   /* a relay player went away without a word */
            const int slot = mode == HOSTING ? client_slot(&from) : -1;
            if (slot > 0) {
                MpMessage bye;
                memset(&bye, 0, sizeof bye);
                bye.type = MP_BYE;
                bye.sender = (uint8_t)slot;
                host_receive(&from, &bye);
            }
            continue;
        }
        MpMessage m;
        if (!mp_decode(buf, (size_t)n, &m)) continue;
        if (mode == HOSTING) host_receive(&from, &m);
        else client_receive(&from, &m);
    }
    if (mode == HOSTING) host_send();
    else if (mode != OFF) client_send();
    if (mode == HOSTING || mode == JOINED) mp_world_tick(now, ticks, my_slot);
    if (mode == HOSTING || mode == JOINED) mp_pickups_tick(now,my_slot);
}

static void tick(void* user) {
    (void)user;
    ++ticks;
    now = api->time_seconds();
    if (autostart) {   /* CTW_MULTIPLAYER, once the game runs */
        if (autostart == 1) host_game(0);
        else if (autostart == 3) join_online(join_code);
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
/* Everything here is drawn with the Porter's components (ctw_ui.h). */

/* Over another player: a pill (bg-background/80, border) with their colour's dot and their name in text-[11px] bold
 * uppercase tracking-wider, and a health line under it (armour beside it in info blue). */
static void draw_names(void) {
    if (!show_names) return;
    const int style = CTW_UI_BOLD | CTW_UI_UPPER;
    for (int s = 0; s < MP_MAX_PLAYERS; ++s) {
        const Player* p = &players[s];
        if (!p->used || !p->has_state || s == my_slot) continue;
        float rendered[3];
        const float* at = p->car ? p->state.vpos : p->state.pos;
        CtwVehicleState vehicle; memset(&vehicle, 0, sizeof vehicle); vehicle.size = sizeof vehicle;
        if (p->car && api->get_vehicle_state(p->car, &vehicle)) at = vehicle.position;
        else if (p->puppet && CTW_MOD_HAS(api, get_puppet_position) && api->get_puppet_position(p->puppet, rendered)) at = rendered;
        const float head[3] = {at[0], at[1], at[2] + (p->car ? 3.f : 2.6f)};
        float xy[2];
        if (!api->world_to_screen(head, xy)) continue;
        const float tw = ctw_ui_type_w(&ui, 11.f, 0.05f, style, p->name), w = 8.f + 6.f + 8.f + tw + 10.f, h = 24.f;
        const float x = xy[0] - w * 0.5f, y = xy[1] - h - 8.f;
        ctw_ui_round_rect(api, x, y, w, h, h * 0.5f, ctw_ui_alpha(CTW_UI_BACKGROUND, 0.8f));
        ctw_ui_round_frame(api, x, y, w, h, h * 0.5f, 1.f, CTW_UI_BORDER);
        ctw_ui_round_rect(api, x + 10.f, y + 9.f, 6.f, 6.f, 3.f, player_colour(s));   /* = radar dot */
        ctw_ui_type(&ui, x + 24.f, ctw_ui_type_y(y, h, 11.f), 11.f, 0.05f, style,
                    p->state.flags & MP_DEAD ? CTW_UI_MUTED_FG : CTW_UI_FOREGROUND, p->name);
        const float health = p->state.health / 127.f, bar = w - 16.f;
        ctw_ui_round_rect(api, x + 8.f, xy[1] - 4.f, bar, 3.f, 1.5f, ctw_ui_alpha(CTW_UI_BACKGROUND, 0.8f));
        ctw_ui_round_rect(api, x + 8.f, xy[1] - 4.f, bar * (health > 1.f ? 1.f : health), 3.f, 1.5f, CTW_UI_SUCCESS);
        if (p->state.armour)
            ctw_ui_round_rect(api, x + 8.f, xy[1], bar * p->state.armour / 127.f, 2.f, 1.f, CTW_UI_INFO);
    }
}

/* Chat: each line text-xs on a bg-background/70 pill; typing, the Porter's field on a card. */
static void draw_chat(void) {
    const float x = CTW_UI_GAP + 8.f, lh = 18.f, bottom = ctw_ui_screen_bottom(api) - (chat_open ? 64.f : 8.f);
    float y = bottom;
    for (int i = chat_count - 1; i >= 0; --i) {
        if (!chat_open && now - chat_log[i].time > CHAT_SHOW) continue;
        y -= lh + 8.f + 4.f;
        const float w = ctw_ui_type_w(&ui, CTW_UI_TEXT_XS, 0.f, 0, chat_log[i].text);
        ctw_ui_round_rect(api, x - 10.f, y - 4.f, w + 20.f, lh + 8.f, (lh + 8.f) * 0.5f, ctw_ui_alpha(CTW_UI_BACKGROUND, 0.7f));
        ctw_ui_type(&ui, x, y, CTW_UI_TEXT_XS, 0.f, 0, chat_log[i].rgba, chat_log[i].text);
    }
    if (!chat_open) return;
    const float row = ctw_ui_row_h(&ui), w = (float)api->screen_width() * 0.45f;
    int focused = 1;
    ctw_ui_card(api, x - 12.f, bottom + 4.f, w + 24.f, row + 24.f);
    ctw_ui_textbox(&ui, x, bottom + 16.f, w, row, chat_text, sizeof chat_text, &focused, "Say something (Enter to send, Esc to close)");
}

/* The multiplayer window: find a game (online, on your network or by address) or host one. */

static int any_field_focused(void) {
    return name_focused || address_focused || port_focused || game_focused || code_focused;
}

static void unfocus(void) { name_focused = address_focused = port_focused = game_focused = code_focused = 0; }

/* A field's caption, as the Porter's "Install to": text-[10px] bold uppercase tracking-wider text-foreground/80, gap-2
 * above the field. Returns the height it takes. */
static float field_caption(float x, float y, const char* text) {
    ctw_ui_type(&ui, x, y, 10.f, 0.05f, CTW_UI_BOLD | CTW_UI_UPPER, ctw_ui_alpha(CTW_UI_FOREGROUND, 0.8f), text);
    return 15.f + 8.f;
}

/* A game to join: a card as the Porter lists its mods (kryo-radius border p-3, border-foreground/40 under the
 * pointer), the name in text-sm and the details as a caption, a primary Join button. */
static float game_row(float x, float y, float w, float h, const char* name, const char* detail, int* join) {
    const int hot = ctw_ui_in(&ui, x, y, w, h);
    ctw_ui_round_frame(api, x, y, w, h, CTW_UI_RADIUS, 1.f, hot ? ctw_ui_alpha(CTW_UI_FOREGROUND, 0.4f) : CTW_UI_BORDER);
    const float bw = ctw_ui_button_w(&ui, CTW_UI_SM, "Join"), room = w - 24.f - bw - 12.f;
    ctw_ui_type_fit(&ui, x + 12.f, y + 12.f + (20.f - 21.f) * 0.5f, CTW_UI_TEXT_SM, 0.f, 0, CTW_UI_FOREGROUND, name, room);
    ctw_ui_type_fit(&ui, x + 12.f, y + 12.f + 20.f + 4.f, 10.f, 0.05f, CTW_UI_UPPER, CTW_UI_MUTED_FG, detail, room);
    *join = ctw_ui_button_ex(&ui, x + w - 12.f - bw, y + (h - 28.f) * 0.5f, bw, 28.f, CTW_UI_PRIMARY_BUTTON, CTW_UI_SM, 0, "Join");
    return h + 8.f;
}

static void draw_panel(void) {
    const CtwUiRect r = ctw_ui_side_area(api, 620.f);
    const float row = ctw_ui_row_h(&ui), pad = 24.f, x = r.x + pad, w = r.w - 2.f * pad;
    const char* modes[] = {"Not connected", "Hosting", "Joining\xE2\x80\xA6", "Playing"};
    if (ctw_ui_panel(&ui, r.x, r.y, r.w, r.h, "Multiplayer", modes[mode])) { panel_open = 0; unfocus(); return; }
    float y = r.y + ctw_ui_title_h(&ui) + pad;

    /* who you are, and what is going on */
    y += field_caption(x, y, "Your name");
    const float leave_w = ctw_ui_button_w(&ui, CTW_UI_MD, "Leave the game");
    ctw_ui_textbox(&ui, x, y, mode != OFF ? w - leave_w - 8.f : w, row, my_name, sizeof my_name, &name_focused, "Player");
    if (mode != OFF && ctw_ui_button_ex(&ui, x + w - leave_w, y, leave_w, row, CTW_UI_DANGER, CTW_UI_MD, 0, "Leave the game"))
        leave("Left the game");
    y += row + 16.f;
    if (status[0]) y += ctw_ui_notice(&ui, x, y, w, mode == OFF ? CTW_UI_DESTRUCTIVE : CTW_UI_SUCCESS, status, 1) + 16.f;
    if (mode != OFF) {   /* who is playing: the Porter's numbered steps (a size-6 pill, the text-[11px] line) */
        int count = 0;
        for (int s = 0; s < MP_MAX_PLAYERS; ++s) count += players[s].used;
        const float entry_h = 24.f + 8.f;
        const float roster_h = entry_h * (count < 5 ? count : 5);
        ctw_ui_scroll_begin(&ui, 33, &player_scroll, x, y, w, roster_h, entry_h * count);
        float player_y = y - player_scroll.pos;
        for (int s = 0; s < MP_MAX_PLAYERS; ++s) {
            if (!players[s].used) continue;
            char marker[3];
            if (s < 26) { marker[0] = (char)('A' + s); marker[1] = 0; }
            else { marker[0] = 'A'; marker[1] = (char)('A' + s - 26); marker[2] = 0; }
            ctw_ui_round_frame(api, x, player_y, 24.f, 24.f, 12.f, 1.f, CTW_UI_BORDER);
            ctw_ui_round_rect(api, x + 17.f, player_y + 1.f, 6.f, 6.f, 3.f, player_colour(s));
            const float mw = ctw_ui_type_w(&ui, 10.f, 0.f, CTW_UI_BOLD, marker);
            ctw_ui_type(&ui, x + (24.f - mw) * 0.5f, ctw_ui_type_y(player_y, 24.f, 10.f), 10.f, 0.f, CTW_UI_BOLD, CTW_UI_FOREGROUND, marker);
            const int style = CTW_UI_UPPER | (s == my_slot ? CTW_UI_BOLD : 0);
            const float nx = x + 24.f + 12.f, ny = ctw_ui_type_y(player_y, 24.f, 11.f);
            ctw_ui_type_fit(&ui, nx, ny, 11.f, 0.05f, style, s == my_slot ? CTW_UI_FOREGROUND : CTW_UI_MUTED_FG,
                            players[s].name, w - 160.f);
            const char* role = s == 0 && s == my_slot ? "host, you" : s == 0 ? "host" : s == my_slot ? "you" : NULL;
            if (role) ctw_ui_tag(&ui, nx + ctw_ui_type_w(&ui, 11.f, 0.05f, style, players[s].name) + 8.f, player_y + 1.5f, role);
            player_y += entry_h;
        }
        ctw_ui_scroll_end(&ui);
        y += roster_h + 8.f;
    }

    /* the ways in: small buttons, the chosen one the white pill */
    const char* tabs[] = {"Online", "Nearby", "Address", "Host"};
    float tx = x;
    for (int i = 0; i < 4; ++i) {
        const float bw = ctw_ui_button_w(&ui, CTW_UI_SM, tabs[i]);
        if (ctw_ui_button_ex(&ui, tx, y, bw, 28.f, i == tab ? CTW_UI_PRIMARY_BUTTON : CTW_UI_GHOST, CTW_UI_SM, 0, tabs[i]) && tab != i) {
            tab = i;
            unfocus();
            if (tab == TAB_ONLINE && online_available()) online_refresh();
        }
        tx += bw + 8.f;
    }
    y += 28.f + 20.f;
    const float bottom = r.y + r.h - ctw_ui_footer_h(&ui) - 16.f;
    const float para = CTW_UI_TEXT_XS;   /* text-xs leading-relaxed text-muted-foreground */
    int join = 0;

    if (tab == TAB_ONLINE) {
        if (!online_available()) {
            ctw_ui_type_wrap(&ui, x, y, para, 1.625f, CTW_UI_MUTED_FG,
                             "CTW Online is not set up in this game yet. Put its address in mods/Multiplayer/multiplayer.ini "
                             "(online=https://...), or play on your network or by address.", w, 1);
        } else {
            const int n = online_list(online_games, ONLINE_MAX_SERVERS);
            if (n >= 0) online_count = n;
            const float rw = ctw_ui_button_w(&ui, CTW_UI_MD, "Refresh"), jw = ctw_ui_button_w(&ui, CTW_UI_MD, "Join code");
            if (ctw_ui_button(&ui, x, y, rw, row, "Refresh")) online_refresh();
            ctw_ui_textbox(&ui, x + rw + 8.f, y, w - rw - jw - 16.f, row, join_code, sizeof join_code, &code_focused, "Game code");
            if (ctw_ui_button_ex(&ui, x + w - jw, y, jw, row, CTW_UI_PRIMARY_BUTTON, CTW_UI_MD, !join_code[0], "Join code") && join_code[0])
                join_online(join_code);
            y += row + 16.f;
            if (n == -1) ctw_ui_caption(&ui, x, y, "Looking for games\xE2\x80\xA6");
            else if (n == -2) ctw_ui_type(&ui, x, y, para, 0.f, 0, CTW_UI_DESTRUCTIVE, "Could not reach CTW Online. Check the connection.");
            else if (online_count == 0) ctw_ui_caption(&ui, x, y, "No public games right now. Host one!");
            const float h = 12.f + 20.f + 4.f + 15.f + 12.f;
            ctw_ui_scroll_begin(&ui, 31, &list_scroll, x, y, w, bottom - y, online_count > 0 ? online_count * (h + 8.f) : 0.f);
            float ly = y - list_scroll.pos;
            for (int i = 0; i < online_count; ++i) {
                char detail[96];
                snprintf(detail, sizeof detail, "%d / %d players  \xC2\xB7  code %.7s%s", online_games[i].players, online_games[i].max,
                         online_games[i].id, strcmp(online_games[i].version, ONLINE_VERSION) ? "  \xC2\xB7  other version" : "");
                ly += game_row(x, ly, w - 14.f, h, online_games[i].name, detail, &join);
                if (join) join_online(online_games[i].id);
            }
            ctw_ui_scroll_end(&ui);
        }
    } else if (tab == TAB_NEARBY) {
        if (lan_listening < 0) ctw_ui_type(&ui, x, y, para, 0.f, 0, CTW_UI_DESTRUCTIVE, "Could not listen on the local network.");
        else if (!lan_count) ctw_ui_type_wrap(&ui, x, y, para, 1.625f, CTW_UI_MUTED_FG, "Looking for games on your network\xE2\x80\xA6 "
                                              "They show up here a second after someone hosts.", w, 1);
        const float h = 12.f + 20.f + 4.f + 15.f + 12.f;
        for (int i = 0; i < lan_count && y + h < bottom; ++i) {
            char detail[96], at[32];
            net_format(&lan_games[i].addr, at, sizeof at);
            snprintf(detail, sizeof detail, "%d / %d players  \xC2\xB7  %s", lan_games[i].players, lan_games[i].max, at);
            y += game_row(x, y, w, h, lan_games[i].name, detail, &join);
            if (join) { join_lan(&lan_games[i].addr); break; }
        }
    } else if (tab == TAB_ADDRESS) {
        field_caption(x, y, "Host address");
        y += field_caption(x + w * 0.72f, y, "UDP port");
        ctw_ui_textbox(&ui, x, y, w * 0.72f - 8.f, row, address, sizeof address, &address_focused, "192.168.1.10");
        ctw_ui_textbox(&ui, x + w * 0.72f, y, w * 0.28f, row, port_text, sizeof port_text, &port_focused, "7777");
        y += row + 16.f;
        if (ctw_ui_button_ex(&ui, x, y, ctw_ui_button_w(&ui, CTW_UI_MD, "Join"), row, CTW_UI_PRIMARY_BUTTON, CTW_UI_MD, 0, "Join"))
            join_game(0);
        y += row + 16.f;
        ctw_ui_type_wrap(&ui, x, y, para, 1.625f, CTW_UI_MUTED_FG, "For a host on a virtual network (Tailscale, ZeroTier, Radmin VPN) "
                         "or one whose UDP port is forwarded on the router.", w, 1);
    } else {
        y += field_caption(x, y, "Game name");
        ctw_ui_textbox(&ui, x, y, w, row, game_name, sizeof game_name, &game_focused, "My game");
        y += row + 16.f;
        float limit = (float)max_players;
        const float limit_h = ctw_ui_slider_h(&ui);
        if (ctw_ui_slider(&ui, 32, x - 8.f, y, w + 16.f, limit_h, "Maximum players (after restart)", &limit, 2.f, MP_MAX_PLAYERS, 1.f)) {
            max_players = (int)limit;
            max_from_env = 0;
            save_settings();
        }
        y += limit_h + 8.f;
        if (online_available()) {
            ctw_ui_check(&ui, x, y, w, 20.f, "List it publicly on CTW Online", &list_public, 0);
            y += 20.f + 16.f;
        }
        y += field_caption(x, y, "UDP port (your network and direct joins)");
        ctw_ui_textbox(&ui, x, y, 140.f, row, port_text, sizeof port_text, &port_focused, "7777");
        y += row + 20.f;
        const char* go = mode == HOSTING ? "Restart" : "Host";
        if (ctw_ui_button_ex(&ui, x, y, ctw_ui_button_w(&ui, CTW_UI_LG, go), 48.f, CTW_UI_PRIMARY_BUTTON, CTW_UI_LG, 0, go)) host_game(0);
        y += 48.f + 20.f;
        ctw_ui_type_wrap(&ui, x, y, para, 1.625f, CTW_UI_MUTED_FG, online_available()
                             ? "Players on your network find the game under Nearby. Online, anyone can join through "
                               "CTW Online with no ports to open: publicly listed, or private with the game's code."
                             : "Players on your network find the game under Nearby; others need your address and the UDP "
                               "port forwarded.", w, 1);
    }
    ctw_ui_footer(&ui, r.x, r.y, r.w, r.h, CTW_UI_MUTED_FG, "T: chat   \xC2\xB7   F9 or Esc: close");
}

static void update_game_input(void) {
    api->set_game_input(!(chat_open || (panel_open && any_field_focused())));
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
    if (key == SC_F9) { open_panel(!panel_open); return 1; }
    if (panel_open && any_field_focused()) {
        if (key == SC_RETURN && code_focused && join_code[0]) join_online(join_code);
        if (key == SC_RETURN || key == SC_ESCAPE) { unfocus(); save_settings(); }
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
    if (!CTW_MOD_HAS(api, take_puppet_world_damage) || !CTW_MOD_HAS(api, spawn_puppet)) {
        api->log(mod, "Multiplayer needs a newer game (mod API version 16)");
        if (CTW_MOD_HAS(api, menu_add_label)) api->menu_add_label(mod, "Needs a newer game");
        return 0;
    }
    load_settings();
    memcpy(saved_name, my_name, sizeof saved_name);
    memcpy(saved_url, online_url, sizeof saved_url);
    saved_max = max_players;
    const char* limit = getenv("CTW_MULTIPLAYER_MAX_PLAYERS");
    if (limit && *limit) { max_players = player_limit(limit); max_from_env = 1; }
    const char* start = getenv("CTW_MULTIPLAYER");
    const char* name = getenv("CTW_MULTIPLAYER_NAME");
    if (name && *name) { snprintf(my_name, sizeof my_name, "%s", name); name_from_env = 1; }
    const char* url = getenv("CTW_ONLINE_URL");
    if (url && *url) { snprintf(online_url, sizeof online_url, "%s", url); url_from_env = 1; }
    if (start && !strcmp(start, "host")) autostart = 1;
    else if (start && !strncmp(start, "online ", 7)) {
        autostart = 3;
        snprintf(join_code, sizeof join_code, "%s", start + 7);
    }
    else if (start && !strncmp(start, "join", 4)) {
        autostart = 2;
        if (start[4] == ' ' && start[5]) snprintf(address, sizeof address, "%s", start + 5);
    }
    online_set_url(online_url);
    if (CTW_MOD_HAS(api, add_pause_button)) api->add_pause_button(mod, "MULTIPLAYER", pause_button, 0);
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
    online_stop();
    net_stop();
    api->set_game_input(1);
}
