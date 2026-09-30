/* Example code mod: camera zoom slider, a coordinates display and a "teleport home" hotkey (F6). */
#include "ctw_mod.h"
#include <stdio.h>

#define SCANCODE_F6 63   /* SDL_SCANCODE_F6 */

static const CtwApi* api;
static CtwMod* self;
static float zoom = 1.f;          /* multiplier on the game's camera height */
static float base_height = 0.f;
static int show_coords = 1;
static float home[3];

static void set_home(void* user) {
    (void)user;
    api->get_player_position(home);
    api->log(self, "home set");
}

static void tick(void* user) {
    (void)user;
    api->set_camera_height(base_height * zoom);
    if (api->key_pressed(SCANCODE_F6)) api->set_player_position(home);
}

static void hud(void* user) {
    (void)user;
    if (!show_coords) return;
    float p[3];
    char text[96];
    api->get_player_position(p);
    snprintf(text, sizeof text, "x %.1f  y %.1f  z %.1f  heading %.0f", p[0], p[1], p[2], api->get_player_heading());
    float y = (float)api->screen_height() - 60.f;
    api->draw_rect(14.f, y - 4.f, 360.f, 26.f, 0x00000080u);
    api->draw_text(20.f, y, 1.f, 0xE0B020FFu, text);
}

CTW_MOD_EXPORT int ctw_mod_init(CtwMod* mod, const CtwApi* a) {
    api = a;
    self = mod;
    base_height = api->get_camera_height();
    api->get_player_position(home);
    api->menu_add_slider(mod, "Camera zoom", &zoom, 0.5f, 3.f, 0.1f);
    api->menu_add_toggle(mod, "Show coordinates", &show_coords);
    api->menu_add_button(mod, "Set home here (F6 teleports back)", set_home, 0);
    api->on_tick(mod, tick, 0);
    api->on_draw_hud(mod, hud, 0);
    return 0;
}
