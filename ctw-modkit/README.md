# Modding guide

Make mods for the Chinatown Wars PC port. Each mod is one folder and can contain textures,
a C or C++ DLL, or both. The mod menu itself, `ModMenu.dll`, is also a mod: without it the game runs plain.

- [1. Set up the mods folder](#1-set-up-the-mods-folder)
- [2. Adding textures to a mod](#2-adding-textures-to-a-mod)
- [3. Texture replacements and layers](#3-texture-replacements-and-layers)
- [4. Adding code to a mod](#4-adding-code-to-a-mod)
- [5. The mod API](#5-the-mod-api)
- [6. Windows and menus with ctw_ui.h](#6-windows-and-menus-with-ctw_uih)
- [7. Sharing a mod](#7-sharing-a-mod)
- [8. Troubleshooting](#8-troubleshooting)
- [9. How it fits together](#9-how-it-fits-together)

## 1. Set up the mods folder

The game folder looks like this:

```text
GTACTW.exe
data/            the game's files (from your own copy)
mods/            mods go here
```

Mods need the mod menu. Put `ModMenu.dll` directly in `mods/`, then each mod in its own folder:

```text
mods/
  ModMenu.dll
  CheatExample/    mod.ini, cheat_example.dll
  Enhancements/    mod.ini, enhancements.dll
  Trumpify/        mod.ini, textures/*.png
```

Start the game and press **F4** for the mod menu. To combine artwork and gameplay changes, put both
`textures/*.png` and the DLL in the same folder and set `dll = your_mod.dll` in its `mod.ini`.
They share one enable switch. The menu lists your mods on the left (with a search box) and shows the selected mod's
switch, description and options on the right.

| Example | What it shows |
| --- | --- |
| `Enhancements` | PC extras, one feature per source file: a third-person camera (**V**) and PSP-style lighting. Remembers its options. |
| `CheatExample` | Cheats, a vehicle spawner (F7) and a free camera (F8). |
| `Trumpify` | Transparent texture layers over the city's adverts. |

The game folder the Porter makes already has `ModMenu.dll` and the example mods. They also come from building
this kit (see [section 4](#4-adding-code-to-a-mod)).

## 2. Adding textures to a mod

**Step 1 - get the textures.** Run the game once with:

```text
GTACTW.exe --export-textures textures
```

This writes every texture of the game as `textures/<number>.png` (about 1,800 files, 320 MB) and quits. The number
is the texture's id; mods use it to say which texture they change. The exported files are for looking at and
editing on your own PC - see [section 7](#7-sharing-a-mod) before sharing anything.

**Step 2 - find the texture you want.** Open the folder with an image viewer that shows thumbnails. Most city
textures are *atlases*: one big image holding many walls, windows and signs. The water is `4180.png`.

**Step 3 - make the mod folder.**

```text
mods/MyFirstMod/mod.ini
mods/MyFirstMod/textures/4180.png
```

`mod.ini`:

```text
name = My First Mod
author = your name
version = 1.0
description = Pink water.
```

**Step 4 - paint.** Open `4180.png` in any image editor, change it and save it into `mods/MyFirstMod/textures/`.
Start the game: the water is yours. Switch the mod off in the F4 menu to compare.

## 3. Texture replacements and layers

A file `textures/<id>.png` does one of two things, depending on whether it has transparency:

| PNG | What happens |
| --- | --- |
| **Fully opaque** | It *replaces* the texture. Any size works (UVs are relative), so you can make it sharper than the original. |
| **Has transparent pixels** | It is a *layer* drawn over the game's texture. Where it is transparent, the game's artwork shows. It is stretched to the texture's size, so make it the same size as the exported texture. |

Layers are the easy way to change part of an atlas: open the exported atlas in your editor, add a new layer, paint
on it (for example a new picture over one billboard), hide or delete the original layer and save as PNG with
transparency. Only your own pixels end up in the file. `examples/trumpify` is made exactly like this: 61 layers
that each put one picture on the adverts of an atlas.

Tips:

- Keep the exported texture's width and height for layers; for replacements, keep its aspect ratio.
- Two mods that change the same texture: the one loaded last (alphabetical folder order) wins.

## 4. Adding code to a mod

A mod can include a Windows DLL alongside its textures. You need the same tools as the game (see the main README): CMake, Ninja and a
MinGW-w64 compiler. Build the kit to check your setup:

```text
cmake -S ctw-modkit -B ctw-modkit/build -G Ninja
cmake --build ctw-modkit/build          # ModMenu.dll and the examples in ctw-modkit/build/mods/
```

Make a folder `examples/hello` with `hello.c`:

```c
#include "ctw_mod.h"

static const CtwApi* api;
static int enabled = 1;

static void hud(void* user) {
    (void)user;
    if (enabled) api->draw_text(20, 60, 1.0f, 0xFFFFFFFF, "Hello from my mod!");
}

CTW_MOD_EXPORT int ctw_mod_init(CtwMod* mod, const CtwApi* a) {
    api = a;
    api->menu_add_toggle(mod, "Show greeting", &enabled);   // F4 -> Hello
    api->on_draw_hud(mod, hud, 0);
    return 0;                                                // anything else: do not load
}
```

and `mod.ini` with `dll = hello.dll`. Add one line to `CMakeLists.txt`:

```cmake
ctw_mod(hello Hello)      # packages mod.ini, textures/*.png and every .c/.cpp file in examples/hello
```

A bigger mod can split its code across several files in the same folder; `examples/enhancements` keeps each
feature in its own file and lists them in `enhancements.c`.

Build again and copy `build/mods/Hello/` into the game's `mods/` folder. A mod may also export
`ctw_mod_shutdown()`, called before the game exits. A mod switched off in the menu loses its texture changes and gets no more code callbacks; its DLL
is unloaded at the next start.

## 5. The mod API

Everything is in `include/ctw_mod.h`; the table below groups it. Positions are world units (z up), angles degrees,
colours `0xRRGGBBAA`, screen coordinates pixels from the top left.

| Group | Functions |
| --- | --- |
| General | `log`, `mod_dir`, `frame_count`, `time_seconds` |
| Mod menu | `menu_add_toggle`, `menu_add_slider`, `menu_add_button`, `menu_add_choice`, `menu_add_label` (v5), `menu_is_open` |
| Saved settings (v5) | `get_setting`, `set_setting`: numbers kept in the mod's own `settings.ini` between sessions |
| Events | `on_tick` (30 per second), `on_frame_begin` (every drawn frame, before the world: cameras), `on_draw_hud` (every drawn frame), `on_key` (return 1 to take the key) |
| Drawing | `draw_text`, `draw_rect`, `text_width`, `line_height`, `screen_width`, `screen_height`, `set_clip`, `draw_vehicle` |
| Keyboard | `key_down`, `key_pressed`, `set_game_input` (0: the player ignores the keyboard, for your own text fields) |
| Mouse | `get_mouse`, `mouse_down`, `mouse_clicked`, `mouse_wheel`, `set_mouse_captured`, `mouse_delta` |
| Text | `text_input` (typed text this frame, `\b` = backspace) |
| Time, weather | `get/set_time_of_day`, `get/set_clock_running`, `get/set_weather` |
| Player | `get/set_player_position`, `get_player_heading`, `get/set_speed_scale` |
| Camera | `get/set_camera_height`, `get_camera`, `set_free_camera`, `get/set_render_distance` |
| Vehicles | `vehicle_count`, `vehicle_name`, `vehicle_spawnable`, `spawn_vehicle`, `draw_vehicle` (a picture) |
| Simulation | `get/set_game_speed` (0 pauses; HUD, mouse and `time_seconds` keep running) |
| Textures | `override_texture_png` (replace or layer, like `textures/`), `patch_texture_png` (a picture over part of a texture) |
| Live vehicles (v4) | `live_vehicle_count`, `live_vehicle_at`, `get_vehicle_state`, `spawn_vehicle_at`, `remove_vehicle` |
| Vehicle editing (v4) | `set_vehicle_transform`, `set_vehicle_velocity`, `set_vehicle_palette`, `set_vehicle_persistent`, `set_vehicle_engine`, `set_vehicle_door`, `damage_vehicle`, `repair_vehicle` |
| Player (v4) | `get_player_state`, `set_player_heading`, `set_player_appearance`, `player_enter_vehicle`, `player_exit_vehicle` |
| World / traffic (v4) | `get_ground`, `line_hits_world_boxes`, `get/set_traffic_density` |
| Radio (v4) | `radio_station_count`, `radio_station_name`, `radio_station_available`, `get/set_radio_station`, `get/set_radio_volume` |
| Events / lifecycle (v4) | `on_game_event`, `on_enabled_changed`, `consume_mouse_wheel`, `get_capabilities` |
| Pedestrians (v5) | `ped_count`, `get_ped_state` (`CtwPedState`: id, type, position, heading, knocked down, male), `get/set_ped_density`; event `CTW_EVENT_PED_KNOCKED_DOWN` |
| Custom cameras (v5) | `set_control_yaw` (on-foot movement relative to your camera), `set_camera_fov`, `world_line` (nearest wall/ground hit with its distance) |
| Look (v5) | `set_render_style` (`CtwRenderStyle`: street light and headlight pools at night, tint, sepia, saturation, contrast, brightness, vignette; `NULL` = the game's own) |

Keys are SDL scancodes (<https://wiki.libsdl.org/SDL2/SDL_Scancode>). Read the mouse and typed text inside
`on_draw_hud`. `examples/cheat_example` uses most of this: study it next.

**Versions.** `api->version` is 5 for this game. Functions are only ever added at the end of `CtwApi`, so a mod built
for an older version keeps working; if your mod needs a newer function, check `api->version` or `api->size` in
`ctw_mod_init` and return non-zero to refuse loading on an older game.

Use `CTW_MOD_HAS(api, function_name)` to check both table size and a non-null function pointer. The mod menu also
runs on older games: on a v4 game the v5 game functions are NULL (settings and menu items still work), and on a v3
game it advertises a v3 table. No-op functions
are not advertised for unported systems. `get_capabilities()` reports which gameplay systems have loaded data.

**Live handles.** A model id (from `vehicle_count`) selects a vehicle type. A `CtwVehicle` identifies one actual
vehicle: it is never an array index, 0 is invalid, and removed handles are never reused in the same session.
Enumeration indices can change when cars are removed. Store the handle, then check each getter's result:

```c
CtwVehicle car = api->live_vehicle_at(0);
CtwVehicleState state = {0};
state.size = sizeof state;
if (api->get_vehicle_state(car, &state)) {
    state.position[2] += 2.f;
    api->set_vehicle_transform(car, state.position, state.heading);
}
```

Set `size` on `CtwVehicleState`, `CtwPlayerState` and `CtwGround` before reading them. Their shared definitions
are in `ctw_types.h`. Positions are world units (z up), velocity is units/second, and headings are degrees
counter-clockwise from north. Positions must be finite and within +/-32768 units; velocity components must be
within +/-512 units/second. Mutations return 1 on success or 0 on invalid/unavailable input; spawn returns a
handle or 0. `line_hits_world_boxes` returns -1 for invalid/missing data, 0 clear or 1 hit, and tests world boxes
only (not triangles, cylinders or moving entities).

Spawn places vehicles on the ground near the supplied z; transform uses the exact position, makes the vehicle
upright and stops it. Transform/velocity edits detach a traffic car from its rail controller. Occupied cars and
active enter/exit targets cannot be removed. Repair restores a living car's health and removes smoke/fire; it
does not resurrect a wreck. Enter/exit requests use the existing animations and distance/speed restrictions.
Turning the engine off suppresses propulsion while preserving steering and braking, and stops engine audio/lights.
Traffic density is 0..4 (1 normal); 0 stops new moving traffic while existing and parked cars remain. Radio
volume is 0..10, and tuning requires a living player in a car with a radio (bikes have none).

**Events and cleanup.** `on_game_event` reports spawn, removal, destruction, player enter/exit and tuning.
Callbacks run on a game tick; events created inside a callback wait until the following tick, so callbacks may
spawn/remove entities safely. Event data lasts for the callback only, and a removal event's handle has expired.
Disabled or failed mods receive no gameplay events. `on_enabled_changed` runs when F4 toggles your mod,
including on disable: use it to release input, restore settings or remove entities you created. World edits
are not automatically undone. `examples/cheat_example` uses handles, repair and cleanup.

Read mouse input during `on_draw_hud`. Call `consume_mouse_wheel()` when your UI uses scrolling.
`ctw_ui.h` sliders, choices and scroll areas do this automatically.

**Custom cameras.** `set_free_camera` replaces the view; call it in `on_frame_begin` every drawn frame with the pose
you want. Positions read there are the interpolated ones that frame shows, so the camera never trails the target. For a
camera the player walks with (third person, over the shoulder), also call `set_control_yaw(1, yaw)` so WASD moves
relative to it, and `world_line(target, eye, &fraction)` to pull the camera in front of walls. Undo all three, the
field of view and mouse capture when your camera stops and in `on_enabled_changed`; when the plugin system shuts
down the game resets them itself. Hand the view back while the player is dead so the game's death camera runs.
Two mods driving the free camera at once fight over it: the one later in folder order wins.

**Saved settings.** `get_setting(mod, "key", default)` returns the saved number or the default;
`set_setting(mod, "key", value)` stores one. The menu writes them to `mods/<YourMod>/settings.ini` about once a
second, when the menu closes and when the game exits. Keys may contain letters, digits, `_` and `.`.

**Remaining limits.** The SDK exposes the playable prototype, but full original-game modding
still depends on unfinished ports. Pedestrians, weapons/combat, wanted levels, missions and save/load are not
implemented. Custom models/animations, object spawning and custom audio also lack public interfaces. These are
tracked in [STATUS.md](../STATUS.md); access to the source does not make those missing systems complete.
Rendering hooks (custom lighting or shaders, such as a PSP-style lighting mode) are not in the API yet either.

## 6. Windows and menus with ctw_ui.h

`include/ctw_ui.h` is optional, plain C and header-only: windows, buttons, switches, sliders, option pickers,
tags, scrolling lists, text fields and wrapped text, drawn with the mod API. The mod menu and the Cheat Example's vehicle spawner are built with
it.

Two things keep windows tidy:

- **Sizes follow the game font.** Use `ctw_ui_row_h`, `ctw_ui_slider_h`, `ctw_ui_title_h` and `ctw_ui_footer_h`
  for heights and `ctw_ui_lh` for lines of text, not fixed pixel numbers.
- **Windows have a place.** `ctw_ui_side_area` gives a window a rectangle on the right that stays clear of the
  game's clock, its help line and the open mod menu.

```c
#include "ctw_ui.h"
static const CtwApi* api;
static CtwMod* self;         /* both kept from ctw_mod_init */
static CtwUi ui;
static CtwUiScroll list;

static void hud(void* user) {
    ctw_ui_begin(&ui, api);
    CtwUiRect r = ctw_ui_side_area(api, 420.f);                      /* at most 420 pixels wide */
    if (ctw_ui_panel(&ui, r.x, r.y, r.w, r.h, "MY WINDOW", NULL)) { /* close clicked */ }
    float y = r.y + ctw_ui_title_h(&ui) + 10.f;
    if (ctw_ui_button(&ui, r.x + 14.f, y, 160.f, ctw_ui_row_h(&ui), "Click me")) api->log(self, "clicked");
    y += ctw_ui_row_h(&ui) + 10.f;
    ctw_ui_scroll_begin(&ui, 1, &list, r.x + 14.f, y, r.w - 28.f, r.y + r.h - ctw_ui_footer_h(&ui) - y, content_height);
    /* draw rows at y - list.pos */
    ctw_ui_scroll_end(&ui);
    ctw_ui_footer(&ui, r.x, r.y, r.w, r.h, CTW_UI_DIM, "F9: close");
}
```

## 7. Sharing a mod

- Zip the mod's folder (`mods/MyMod/`). Players unzip it into their `mods/` folder.
- **Never share files from the game.** Exported textures are the game's artwork: a replacement PNG made from one is
  not yours to share. Layers that only contain your own pixels are fine, and so are replacements you drew yourself.
- Only use artwork you have the right to distribute. Mod DLLs are normal programs: players should only install
  mods they trust.

## 8. Troubleshooting

- **F4 does nothing.** `ModMenu.dll` must be directly in `mods/`, not in a subfolder.
- **A mod is missing from the menu.** It needs its own folder with a `mod.ini`. `mods/log.txt` says what loaded and
  why something failed.
- **"needs a newer game" or ERROR in the menu.** The mod was built for a newer API; update the game.
- **A texture does not change.** Check the number (see section 2) and that the file is a real PNG.

## 9. How it fits together

```text
include/ctw_mod.h       the mod interface (all a mod needs)
include/ctw_types.h     shared gameplay handles, state structs, capabilities and event types
include/ctw_ui.h        optional UI helpers
include/ctw_plugin.h    the game's plugin interface, used by ModMenu.dll only
modmenu/                source of ModMenu.dll
examples/               Enhancements, Cheat Example and Trumpify
```

The game loads every DLL directly in `mods/` as a *plugin* (`ctw_plugin_init`, see `ctw_plugin.h`). `ModMenu.dll`
is that plugin: it reads `mods/*/mod.ini`, applies each folder's textures, loads its optional DLL and gives it the mod API on top of
the plugin interface. Keep `include/ctw_plugin.h` identical to `port/game/ctw_plugin.h`.
