# Current state

A native Windows gameplay prototype plus a separate showcase viewer. This lists what is implemented in source;
it is not a claim of complete compatibility with the original.

## World

- City geometry and textures stream around the camera.
- Water, animated signs, weather colours and the day/night cycle.
- Street furniture (props): lamp posts, bins, benches, hydrants, signs, with their collision shapes.
- Night lights: fixed world lights and the lights of lamp-post props.
- Steam from the city's vents and water jets from its fountains (city emitters, CWaterCannon).
- Vehicles knock street furniture over: lamp posts fall, bins and cones fly off, using the game's per-kind
  thresholds (the force estimate and the tumbling are simplified, see `port/game/propdynamics.h`).
- Collision queries against ground, boxes, cylinders and triangle meshes.

## Gameplay

- The player: layered character sprites, movement animations, acceleration, turning and swept collision.
- Follow camera that turns toward the movement and checks buildings for obstruction.
- Cars and bikes: vehicle data, models, paint variants, driving physics, doors, entering and leaving.
- Parked-car generators and road traffic (cars only).
- Collision damage, smoke, head/brake/indicator lights, skidmarks, engine, horn and impact sounds, music.

## Assets and tools

- Setup and export read a user-supplied Android 4.4.243 APK. `scripts/export_pc.py` writes a clean game folder
  (exe, data, empty mods). `GTACTW.exe --export-textures DIR` writes every texture as a PNG for modders.
- Readers for archives, textures, models, sprites, fonts and text in the six game languages.
- The showcase has texture, text, model, audio and world views.
- `--shot` and `--trace` options for local verification.

## Mods

Mods use a public C interface (`ctw-modkit/include/ctw_mod.h`) and are managed by the mouse-driven mod menu
(`ModMenu.dll`, F4), a plugin of its own. Texture mods are PNGs named after the texture: opaque ones replace it,
ones with transparency are layers over it. Code mods can change time and weather, the camera (including a free
camera and render distance), spawn vehicles and draw their pictures, change speeds, and use the mouse, typed text
and a small UI helper (`ctw_ui.h`). The kit includes two examples, *Cheat Example* (vehicle spawner, free camera,
speed, time speed, render distance) and *Trumpify* (texture layers that cover the city's adverts). The modding
guide is `ctw-modkit/README.md`.

## Limitations

Missions, save-game compatibility, combat, wanted levels and pedestrian AI are not implemented. Knocked-over props
do not collide with anything, and the debris particles and water splashes of the originals are not ported yet. Traffic spawns cars only;
rail drivers that lose their vehicle do not rejoin the network. Building targets the Windows MinGW-w64 toolchain.

Lookup tables (population, sound, rendering, vehicle/traffic) are read from the supported ARM64 binary in the APK
during setup and loaded at runtime. Missing rendering or gameplay tables prevent startup; missing population or
sound tables disable that subsystem with a message. Extracted tables are never part of the source tree.
