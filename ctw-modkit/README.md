# Modding guide

Everything you need to make mods for the Chinatown Wars PC port: texture mods (no programming) and code mods
(a small C or C++ DLL). The mod menu itself, `ModMenu.dll`, is also a mod: without it the game runs plain.

- [1. Set up the mods folder](#1-set-up-the-mods-folder)
- [2. Your first texture mod](#2-your-first-texture-mod)
- [3. Texture mods in detail](#3-texture-mods-in-detail)
- [4. Your first code mod](#4-your-first-code-mod)
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
  Trumpify/        mod.ini, textures/*.png
```

Start the game and press **F4**. Click a mod to see its options; the switch on the right turns it on or off (remembered in
`mods/enabled.ini`).

`ModMenu.dll` and the two example mods come from building this kit (see [section 4](#4-your-first-code-mod)), or
from `python scripts/export_pc.py --with-modkit`.

## 2. Your first texture mod

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

## 3. Texture mods in detail

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
- Switching a texture mod on or off in the menu takes effect right away.
- Two mods that change the same texture: the one loaded last (alphabetical folder order) wins.

## 4. Your first code mod

Code mods are Windows DLLs. You need the same tools as the game (see the main README): CMake, Ninja and a
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
ctw_code_mod(hello Hello)      # builds examples/hello/hello.c into build/mods/Hello/hello.dll
```

Build again and copy `build/mods/Hello/` into the game's `mods/` folder. A mod may also export
`ctw_mod_shutdown()`, called before the game exits. Switching a code mod off stops its callbacks at once; the DLL
itself is unloaded at the next start.

## 5. The mod API

Everything is in `include/ctw_mod.h`; the table below groups it. Positions are world units (z up), angles degrees,
colours `0xRRGGBBAA`, screen coordinates pixels from the top left.

| Group | Functions |
| --- | --- |
| General | `log`, `mod_dir`, `frame_count`, `time_seconds` |
| Mod menu | `menu_add_toggle`, `menu_add_slider`, `menu_add_button`, `menu_is_open` |
| Events | `on_tick` (30 per second), `on_draw_hud` (every drawn frame), `on_key` (return 1 to take the key) |
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

Keys are SDL scancodes (<https://wiki.libsdl.org/SDL2/SDL_Scancode>). Read the mouse and typed text inside
`on_draw_hud`. `examples/cheat_example` uses most of this: study it next.

**Versions.** `api->version` is 3 for this game. Functions are only ever added at the end of `CtwApi`, so a mod built
for an older version keeps working; if your mod needs a newer function, check `api->version` or `api->size` in
`ctw_mod_init` and return non-zero to refuse loading on an older game.

## 6. Windows and menus with ctw_ui.h

`include/ctw_ui.h` is optional, plain C and header-only: windows, buttons, switches, sliders, scrolling lists and
text fields, drawn with the mod API and used with the mouse. The mod menu and the Cheat Example's vehicle spawner
are built with it.

```c
#include "ctw_ui.h"
static const CtwApi* api;
static CtwMod* self;         /* both kept from ctw_mod_init */
static CtwUi ui;
static CtwUiScroll list;

static void hud(void* user) {
    ctw_ui_begin(&ui, api);
    if (ctw_ui_panel(&ui, 20, 20, 300, 400, "MY WINDOW", NULL)) { /* close clicked */ }
    if (ctw_ui_button(&ui, 34, 70, 120, 30, "Click me")) api->log(self, "clicked");
    ctw_ui_scroll_begin(&ui, 1, &list, 34, 110, 272, 290, content_height);
    /* draw rows at y - list.pos */
    ctw_ui_scroll_end(&ui);
}
```

## 7. Sharing a mod

- Zip the mod's folder (`mods/MyMod/`). Players unzip it into their `mods/` folder.
- **Never share files from the game.** Exported textures are the game's artwork: a replacement PNG made from one is
  not yours to share. Layers that only contain your own pixels are fine, and so are replacements you drew yourself.
- Only use artwork you have the right to distribute. Code mods are normal programs: players should only install
  code mods they trust.

## 8. Troubleshooting

- **F4 does nothing.** `ModMenu.dll` must be directly in `mods/`, not in a subfolder.
- **A mod is missing from the menu.** It needs its own folder with a `mod.ini`. `mods/log.txt` says what loaded and
  why something failed.
- **"needs a newer game" or ERROR in the menu.** The mod was built for a newer API; update the game.
- **A texture does not change.** Check the number (see section 2) and that the file is a real PNG.

## 9. How it fits together

```text
include/ctw_mod.h       the mod interface (all a mod needs)
include/ctw_ui.h        optional UI helpers
include/ctw_plugin.h    the game's plugin interface, used by ModMenu.dll only
modmenu/                source of ModMenu.dll
examples/               Cheat Example (code) and Trumpify (textures)
```

The game loads every DLL directly in `mods/` as a *plugin* (`ctw_plugin_init`, see `ctw_plugin.h`). `ModMenu.dll`
is that plugin: it reads `mods/*/mod.ini`, applies textures, loads code mods and gives them the mod API on top of
the plugin interface. Keep `include/ctw_plugin.h` identical to `port/game/ctw_plugin.h`.
