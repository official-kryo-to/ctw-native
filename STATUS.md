# Current state

A playable prototype, not the finished game. This lists what is there; it is not a claim of complete
compatibility with the original.

## What works

- **The city:** streets and buildings with their textures, water, animated signs, weather, day and night,
  street furniture, street lights, steam vents and fountains.
- **On foot:** the player character with movement, sprinting and collision. The follow camera counts consecutive
  blocked views and uses the original roof-edge escape and safe above-target recovery.
- **Vehicles:** cars and bikes with the game's handling, getting in and out, damage, lights, skidmarks and sounds.
- **Street furniture:** the original mass, inertia, force thresholds and rigid-body integration. Loose props tumble,
  collide with the world, cars and other props, and settle; anchored broken remnants keep their placement. Paper,
  wood, glass and garbage use the original emitters; a knocked-over bin spills rubbish while it rolls.
- **Traffic:** parked cars and cars driving the roads, stopping for pedestrians in their path.
- **Pedestrians:** civilians and cops walk the pavement network of each world sector, using the original zone
  population, spawn schedule, spawn ring, cleanup distance and `pedinfo.bin` looks (gender, body, colours).
  Joggers run, and everyone hurries in the rain. They roll out of the way of the player's speeding car and sprint
  while it is near; a fast car knocks them down, a slow one pushes them aside. Near an explosion they react from
  their `pedinfo.bin` reaction table, mostly by fleeing along the pavement away from it.
- **Sound:** engines, horns, doors, impacts, object smashing, explosions, the player's footsteps, music, the city,
  rain and airport ambience, and pedestrian screams and falls.
- **Random numbers:** the original four-stream generator and seed.
- **Radio:** the original radio app (**Tab** in a car): the station carousel, the equaliser driven by the music,
  favourite stars and the volume meter, with the game paused. That is the only way to change the radio.
- **Smooth motion:** the game runs at the original 30 ticks a second; vehicles, people, loose props and the camera
  are drawn between ticks, so motion is smooth at any refresh rate.
- **Mods:** the mod menu (**F4**). A mod is one folder with any mix of texture replacements and a C or C++ DLL.
  The API covers vehicles, the player, pedestrians, the world, traffic, radio, custom cameras, game events, saved
  settings and menu items. The bundled **Enhancements** mod adds an optional third-person camera (**V**) and
  PSP-style lighting. See the [modding guide](ctw-modkit/README.md).
- **Tools:** the Porter, which makes the game from your APK, and a showcase viewer for the game's assets.

**F3** shows the debug overlay and collision diagnostics. The keys are in [CONTROLS.md](CONTROLS.md).

## Not there yet

- Missions, save games, combat, wanted levels and the original gameplay HUD.
- Pedestrians do not talk, fight, use doors or attractors, or bump into each other. Explosions do not damage them.
  Traffic is cars only.
- Slats, other debris, object-specific explosions, buoyancy and splashes.
- Camera shake, scripted, aiming and specialised cameras, and the hidden-player visibility aid.
- Boats, the jet ski, helicopters and the tank.
- The SDK covers what is ported; weapons, wanted levels, missions, saves, custom models and audio come with those
  systems. Rendering hooks are limited to the render style (light pools and colour grade).
- The third-person camera is a PC addition. The game's people and some effects are flat artwork made for the
  overhead view, so they look thin from low angles.

## Supported copy

Only GTA: Chinatown Wars for Android **4.4.243** (the arm64 build). A few lookup tables are read from its game
binary when the APK is ported; they are never part of this repository.
