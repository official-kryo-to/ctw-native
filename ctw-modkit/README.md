# GTA: Chinatown Wars PC port - Mod Menu & Mod SDK

The game itself ships without mod support switched on. This kit adds it:

```
include/ctw_mod.h       the mod interface (the only header a mod includes)
include/ctw_plugin.h    the game's plugin interface (only the mod menu uses it)
modmenu/                source of ModMenu.dll: finds, loads and switches mods, the in-game mod menu (F4)
examples/               example mods
```

We dont provide the source code of our port, sorry. You don't need it: everything here builds on its own.

## Installing (players)
1. Download the release of this kit.
2. Copy `ModMenu.dll` into the game's `mods` folder.
3. Put mods in their own folder inside `mods` (for example `mods/PinkWater/`).
4. Start the game and press **F4**.

```
GTA Chinatown Wars/
  GTACTW.exe
  mods/
    ModMenu.dll
    PinkWater/mod.ini
    PinkWater/textures/4180.png
```

## Building
```
cmake -S . -B build
cmake --build build
```
Everything ends up in `build/mods/` - copy its contents into the game's `mods` folder.

## Kinds of mods

**Asset mods** (no programming): a folder with a `mod.ini` and replacement files.

```
mods/MyTextures/mod.ini
mods/MyTextures/textures/4180.png      replaces game texture resource 4180 (the water)
```
Texture files are named after the game's texture resource number. PNG, any size (higher resolution is fine).
Asset mods switch on and off right away from the menu.

**Code mods**: a DLL that the mod menu loads. It can add options to the mod menu, react every game frame, draw on
the screen and change things in the game through the functions in `ctw_mod.h`. Switching a code mod off stops
its callbacks right away; it is fully unloaded after a restart.

## mod.ini
```
name = My Mod
author = you
version = 6.7 ## haha get it 67 *okay ill stfu*
description = One line shown in the mod menu.
dll = my_mod.dll        ; only for code mods
```

## The smallest code mod
```c
#include "ctw_mod.h"

static const CtwApi* api;
static int enabled = 1;

static void hud(void* user) {
    if (enabled) api->draw_text(20, 60, 1.0f, 0xFFFFFFFF, "Hello from my mod!");
}

CTW_MOD_EXPORT int ctw_mod_init(CtwMod* mod, const CtwApi* a) {
    api = a;
    api->menu_add_toggle(mod, "Show greeting", &enabled);   // appears in F4 -> My Mod
    api->on_draw_hud(mod, hud, 0);
    return 0;
}
```
Build it as a DLL (see `CMakeLists.txt`), put it next to your `mod.ini` in `mods/MyMod/`, start the game and
press **F4**.

## Examples
| Folder | What it shows |
|---|---|
| `examples/time_control` | Menu toggles, sliders and buttons; stopping the clock; changing the time and weather |
| `examples/free_camera` | Changing the camera height, drawing a HUD box, a hotkey (F6) that teleports the player |
| `examples/pink_water` | An asset mod: replaces the water texture, no code |

## How it fits together
The game loads every DLL placed directly in its `mods` folder as a *plugin* (`ctw_plugin_init`, see
`include/ctw_plugin.h`). `ModMenu.dll` is that plugin: it reads `mods/*/mod.ini`, applies textures, loads code
mods and hands them the mod API (`ctw_mod.h`), which it implements on top of the plugin interface.
`include/ctw_plugin.h` must stay identical to the copy in the game's source (`port/game/ctw_plugin.h`).

## Rules of the road
- The API only grows: new functions are added at the end of `CtwApi` / `CtwHostApi`, so your mod keeps working
  with newer versions of the game. `api->version` tells you which version you're running on.
- Everything happens on the game's thread; callbacks run 30 times per second (`on_tick`) or once per drawn frame
  (`on_draw_hud`). Keep them quick.
- The mod menu writes `mods/log.txt`; `api->log` adds your own lines.
- Code mods are normal programs: only install mods you trust.
- Never include files from the game itself in a mod you share. Be creative and make your own art. DONT USE GENERATIVE AI
