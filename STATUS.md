# Current state

A playable prototype, not the finished game. This lists what is there; it is not a claim of complete
compatibility with the original.

## What works

- **The city:** streets and buildings with their textures, water, animated signs, weather, day and night,
  street furniture, street lights, steam vents and fountains.
- **On foot:** the player character with movement, sprinting and collision.
- **Vehicles:** cars and bikes with the game's handling, getting in and out, damage, lights, skidmarks and sounds.
  Street furniture gets knocked over or swaps to its original broken model; anchored remnants stay at their placement.
- **Traffic:** parked cars and cars driving the roads.
- **Sound:** engines, horns, doors, impacts, object smashing, explosions, animation-timed player footsteps and music.
- **Radio:** the original station artwork, car-specific tuning and volume controls. Press **R** in a car;
  use Left/Right or the mouse wheel to tune, Up/Down for volume, and R/Esc to return to driving. Stations cannot
  be paused; select the radio-off station to switch it off.
- **Mods:** the mod menu (F4), texture mods and code mods. See the [modding guide](ctw-modkit/README.md).
- **Tools:** the Porter, which makes the game from your APK, and a showcase viewer for the game's assets.

The prototype HUD, control hints and collision diagnostics are hidden by default; **F3** toggles them.
The original gameplay HUD is not ported yet. Mods can still draw their own UI.

## Not there yet

- Missions, save games, combat, wanted levels and pedestrians.
- Traffic is cars only.
- Loose street furniture uses its rotated model geometry to stay above terrain, but its impact forces and tumbling
  still approximate the original rigid-body integrator. It does not collide with other moving objects. Debris,
  object-specific explosions and splash effects remain unported.

## Supported copy

Only GTA: Chinatown Wars for Android **4.4.243** (the arm64 build). A few lookup tables are read from its game
binary when the APK is ported; they are never part of this repository.

Object sounds need the updated sound table from your APK. Re-run setup or export with the updated Porter;
older sound tables still load, but do not contain the object sound mapping.
