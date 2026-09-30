/* Example code mod: time and weather controls in the mod menu (F4 -> Time Control). */
#include "ctw_mod.h"
#include <stdio.h>

static const CtwApi* api;
static CtwMod* self;

static float time_of_day = 12.f;
static int clock_running = 1;
static int show_clock = 1;
static int last_clock_running = 1;

static void next_weather(void* user) {
    (void)user;
    api->set_weather((api->get_weather() + 1) % 8);
}

static void set_midnight(void* user) {
    (void)user;
    time_of_day = 0.f;
    api->set_time_of_day(0.f);
}

static void tick(void* user) {
    (void)user;
    /* the slider drives the clock while it is stopped; otherwise it follows the game clock */
    if (clock_running != last_clock_running) {
        api->set_clock_running(clock_running);
        last_clock_running = clock_running;
    }
    if (clock_running) time_of_day = api->get_time_of_day();
    else api->set_time_of_day(time_of_day);
}

static void hud(void* user) {
    (void)user;
    if (!show_clock) return;
    char text[64];
    float t = api->get_time_of_day();
    snprintf(text, sizeof text, "Weather %d   %s", api->get_weather(), clock_running ? "" : "(clock stopped)");
    api->draw_text(20.f, 20.f, 1.f, 0xFFFFFFC0u, text);
    (void)t;
}

CTW_MOD_EXPORT int ctw_mod_init(CtwMod* mod, const CtwApi* a) {
    api = a;
    self = mod;
    if (api->version < 1) return 1;
    time_of_day = api->get_time_of_day();
    clock_running = last_clock_running = api->get_clock_running();
    api->menu_add_toggle(mod, "Clock running", &clock_running);
    api->menu_add_slider(mod, "Time of day (clock stopped)", &time_of_day, 0.f, 23.99f, 0.25f);
    api->menu_add_button(mod, "Next weather", next_weather, 0);
    api->menu_add_button(mod, "Jump to midnight", set_midnight, 0);
    api->menu_add_toggle(mod, "Show weather on screen", &show_clock);
    api->on_tick(mod, tick, 0);
    api->on_draw_hud(mod, hud, 0);
    api->log(mod, "time control ready");
    return 0;
}

CTW_MOD_EXPORT void ctw_mod_shutdown(void) {}
