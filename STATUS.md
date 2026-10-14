# Current state

A playable prototype, not the finished game. This lists what is there; it is not a claim of complete
compatibility with the original.

## What works

- **The city:** streets and buildings with their textures, water, animated signs, weather, day and night,
  street furniture, street lights, steam vents and fountains.
- **On foot:** the player character with movement, sprinting and collision. The follow camera counts consecutive
  blocked views and uses the original roof-edge escape and safe above-target recovery.
- **Vehicles:** cars and bikes with the game's handling, getting in and out, damage, lights, skidmarks and sounds.
  Damaged doors and hatches swing with acceleration, turning and gravity; detached panels have independent
  motion and the burnt palette. Explosive destruction can tear off the five supported panels.
- **Explosion effects:** the reference flash's two counter-rotating layers and the generic/ordinary-vehicle
  cloud's birth, motion, darkening, frame-dependent fade and secondary smoke. Generic prop smash effect 28
  queues a unit-scale blast at its jittered centre; hotdog-cart effects 23 and 30 struck by vehicles queue a
  smaller blast. Generic type-0/type-4 flash, sound and cloud start on the third Process call and retain the
  eleven-call final state. Type-4 debris uses the reference five horizontal fragments, launch, colour, gravity,
  damping and lifetime; its trails are still missing. Nearby explosions shake the active on-foot or driving
  camera with the original random stream, amplitude and decay. Fire, flash and explosion clouds use the
  original additive pass before ordinary alpha-blended smoke; the yellow flash no longer darkens the street.
  Generic type-0/type-4 blasts now damage and knock down on-foot people, damage and push vehicles, and apply
  forces and health damage to street furniture. Vehicle and explosive-prop chain reactions queue delayed
  secondary blasts. The optional generic stationary-blast duplicate check follows the reference's cell and
  3D distance gates; it stays off by default, matching Reset.
- **Street furniture:** the original mass, inertia, force thresholds and rigid-body integration. Loose props tumble,
  collide with the world, cars and other props, and settle; anchored broken remnants keep their placement. Paper,
  wood, glass, garbage and gate slats use the original emitters; a knocked-over bin spills rubbish while it rolls.
  Gate and barrier smash effects 26 and 27 emit six rotating strips with the reference launch, damping,
  floor-stop and fade. Reactive props no longer also block vehicles as immovable scenery. Hydrants tilt about
  their fixed base and emit a water stream, nozzle spray and positional looping water sound. Partial blast
  damage retains standing models and collisions; lethal health can smash anchored props without uprooting them.
- **Traffic:** parked cars and cars driving the roads, stopping for pedestrians in their path. Generated traffic
  owns pedestrian drivers; they animate out when the player takes the seat and are released on destruction.
  Closed-car interiors still hide the original pedestrian sprites.
- **Pedestrians:** civilians and cops walk the pavement network of each world sector, using the original zone
  population, spawn schedule, spawn ring, cleanup distance and `pedinfo.bin` looks (gender, body, colours).
  Joggers run, and everyone hurries in the rain. They roll out of the way of the player's speeding car and sprint
  while it is near. Dynamic pedestrian/player/vehicle contacts change velocity before movement, using the original
  1.25-radius body spheres and 0.875-radius static sweeps. Vehicle run-over damage, fatal-task ordering,
  impact drag, grounded sliding and living knockdown/getup transitions follow the Android routines. A body
  stays down while a vehicle is above it. Generic explosions apply distance-based health damage and high
  knockdowns, including zero-damage fringe hits. Seated occupants are protected from direct blasts.
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
  PSP-style lighting. API v6 exposes interpolated explosion shake; Enhancements applies it to third person.
  API v7 adds skinned character resources and interpolated player/pedestrian render overrides. The Porter offers
  optional user-supplied LCS Android 2.4.379 character import and lets users choose premade mods.
  API v8 exposes skinned clip bounds and absolute animation time. Imported knockdowns advance on the simulation
  clock and settle on their animated body bounds; donor size is fitted from standing width rather than a guessed
  multiplier. API v9 adds full vehicle attachment frames, the owning vehicle handle, animated-joint drawing and
  continuous impact/recovery clocks. Imported riders follow bike lean and pitch; the mod selects chopper,
  dirt-bike or sport-bike donor poses for the corresponding CTW bikes instead of using scooter poses everywhere.
  See the [modding guide](ctw-modkit/README.md).
- **Tools:** the Porter, which makes the game from your APK, and a showcase viewer for the game's assets.

**F3** shows the debug overlay and collision diagnostics. The keys are in [CONTROLS.md](CONTROLS.md).

## Not there yet

- Missions, save games, combat, wanted levels and the original gameplay HUD.
- Pedestrians do not talk, fight, or use building doors or attractors.
  Traffic is cars only.
- Pedestrian physics still needs the full collision-cache ordering/limits, collision flags that stop AI movement,
  pinned-body crush-damage behavior, sprite pose-height interpolation and other combat damage/reaction types.
  The implemented vehicle impact paths do not constitute complete death physics.
- Full explosion entity ordering/limits, immunity and source-reaction flags, prop weapon/fire damage integration
  and health ownership, specialised duplicate-check exclusions and explosion pool limits; the vehicle
  death-cause and chain scheduling still need full parity, and debris trails are missing.
  The generic vehicle impulse is consumed after integration to avoid losing it to parked-car settling, an
  adaptation pending the native entity scheduler. Prop health resets at the port's existing cleanup distance,
  not the native sector/entity lifetime. Big-vehicle multi-stage clouds, hotdog-cart gas jets for nonvehicle
  damage, other specialised explosions, and the original shared particle-pool capacity/update ordering remain
  incomplete. Secondary smoke currently uses one emitter per burst.
- Other debris, buoyancy and splashes. Gate slats retain the original zero-height
  floor test; this does not fix the remaining waterfront mesh/collision clipping reports.
  Specialised prop smash branches, water-stream contact/extinguishing, the shared vehicle-fragment pool,
  complete carjacking reactions and traffic returning to its route after an impact remain incomplete.
  See the [destruction and traffic audit](DESTRUCTION.md) for the measured coverage and remaining differences.
- Scripted, aiming and specialised cameras, shake through camera-mode/death transitions, and the hidden-player
  visibility aid.
- Boats, the jet ski, helicopters and the tank.
- The SDK covers what is ported (v10 adds giving the player weapons, v11 switching them); wanted levels, missions, saves and custom audio remain unavailable.
- Weapons: the weapon table, cWeaponManager (slots, switching, ammo and clips, reloads), the weapon wheel and HUD button, and firing for the pistols, SMGs, shotguns, rifles and the minigun (accuracy spread, wall hits, peds and vehicles hit, damage and deaths, bullet traces, fire sounds); fists, nightstick, bat and chainsaw hits; the original auto-aim (cPlayerSensorCone: target priorities and distance bands, the 45-degree fire cone, the turning fire direction and the target marker); the RPG, grenades, molotovs, flash bangs and proximity mines (cBaseProjectile physics, bounces, fuses, the throw power and its arc, explosion types 0 to 3). Not yet: the flame thrower, sniper rifle, taser and sword, molotov fires, the flash bang's daze, water splashes, the gamepad lock-on and target cycling, muzzle flashes, blood, hit reactions, AI peds with guns and pickups.
  Character model overrides work for mobile DFF/ANPK/PVR assets; general model/material/shader interfaces and
  rigid object spawning are still missing.
- The third-person camera and imported LCS people are PC additions. The donor roster uses Toni and LCS civilian
  appearances, with approximate clip mapping and joint fitting; it does not reproduce Huang or every CTW
  pose. Some effects remain overhead artwork and look thin from low angles. See
  [third-person character support](ctw-modkit/THIRD_PERSON_ASSETS.md).

## Supported copy

Only GTA: Chinatown Wars for Android **4.4.243** (the arm64 build). A few lookup tables are read from its game
binary when the APK is ported; they are never part of this repository.
