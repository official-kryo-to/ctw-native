# Current state

A playable prototype, not the finished game. This lists what is there; it is not a claim of complete
compatibility with the original.

## What works

- **The city:** streets and buildings with their textures, water, animated signs, weather, day and night,
  street furniture, street lights, steam vents and fountains.
- **On foot:** the player character with movement, sprinting and collision. The follow camera counts consecutive
  blocked views and uses the original roof-edge escape and safe above-target recovery.
  Movement now integrates once per tick; weapon-dependent footsteps use the correct upper/leg animation cycle.
- **Gameplay HUD:** the original north-up radar tiles, frame, north marker, rotating player/vehicle arrow and
  health semicircle. The map follows the controlled character or vehicle and uses the original map coordinates,
  clipping and colours. The PC uses the phone radar size.
  The weapon wheel pauses world sounds as well as simulation, accepts clicks completed between display frames,
  and releases its pause and textures when closed, reloaded or shut down. Multiline font drawing and measurement
  now agree; a failed font replacement preserves the loaded font.
- **Vehicles:** cars and bikes with the game's handling, getting in and out, damage, lights, skidmarks and sounds.
  Skid marks follow cWheeledVehicle / cBike::AddSkidmarks: both back wheels of a car (one track for a bike),
  front marks only while those wheels spin, traffic marking on alternate frames, tyre smoke
  (cParticleEmitterSkidSmoke) and sparks from a burst tyre's rim above 10 units a second. Burnouts work as in the
  original (accelerate + brake when stopped, on the vehicles whose data allows them): the car holds first gear and
  spins its back wheels in dark smoke, steering swings it round on the spot, and letting go of the brake launches it
  with a forward kick, up to ten times the engine power and flaming tyres while the build-up lasts. A damaged
  engine has half its power from health 79 and a fifth once the car burns, and a car at health 159 or below
  rattles its body in contact (cWheeledVehicle::DamageWobble). Bike and scooter riders are thrown off
  in a hard crash (cBike::OnCollision: a collision force above 400, 600 against moving things, 141 for other riders,
  or any knock while the bike lies tipped over) or when the vehicle tips past 60 degrees
  (cVehicle::CheckForDriverFallingOut): they leave the seat at once, lose a point of health and fly off along the
  impact in the original's state-6 tumble before lying down and getting up.
  Damaged doors and hatches swing with acceleration, turning and gravity; detached panels have independent
  motion and the burnt palette. Explosive destruction can tear off the five supported panels.
- **Explosion effects:** the reference flash's two counter-rotating layers and the generic/ordinary-vehicle
  cloud's birth, motion, darkening, frame-dependent fade and secondary smoke. Generic prop smash effect 28
  queues a unit-scale blast at its jittered centre. Hotdog carts (effects 23 and 30) catch fire; a vehicle or a
  hard hit bursts them at once with a smaller blast, while a soft finishing hit opens a gas jet (cExplosionHotDog:
  a flame and a hiss for 63 frames) that then bursts with a push that does no damage. Molotov and hotdog blasts
  set people, vehicles and inflammable props within seven units alight (AffectEntities' fire radius,
  cDynamicProp::SetOnFire). Generic type-0/type-4 flash, sound and cloud start on the third Process call and retain the
  eleven-call final state. Type-4 debris uses the reference five horizontal fragments, launch, colour, gravity,
  damping and lifetime, each with its smoke trail (cTrailManager / cSimpleTrail: a tapering strip of up to
  twelve points in the fragment's colour). Nearby explosions shake the active on-foot or driving
  camera with the original random stream, amplitude and decay. Fire, flash and explosion clouds use the
  original additive pass before ordinary alpha-blended smoke; the yellow flash no longer darkens the street.
  Generic type-0/type-4 blasts now damage and knock down on-foot people, damage and push vehicles, and apply
  forces and health damage to street furniture. Vehicle and explosive-prop chain reactions queue delayed
  secondary blasts. The optional generic stationary-blast duplicate check follows the reference's cell and
  3D distance gates; it stays off by default, matching Reset.
  Tankers, ammo trucks (and the Rhino, once tanks exist) and smashed petrol pumps blow up as cExplosionBigVehicle:
  a wider blast (15-unit damage, 22-unit force), yellow twirls, then five to ten frames later the explosion
  sound, debris, a ring of cloud puffs that drifts along the pump's impact and the main cloud. A petrol pump also
  leaves a 900-frame petrol fire. Only one explosion-debris emitter exists at a time, as in the original, and
  molotov blasts now throw their debris. Explosion damage kinds and the x1.25 vehicle damage follow the
  reference tables for every type.
- **Street furniture:** the original mass, inertia, force thresholds and rigid-body integration. Loose props tumble,
  collide with the world, cars and other props, and settle; anchored broken remnants keep their placement. Paper,
  wood, glass, garbage and gate slats use the original emitters; a knocked-over bin spills rubbish while it rolls.
  Gate and barrier smash effects 26 and 27 emit six rotating strips with the reference launch, damping,
  floor-stop and fade. Reactive props no longer also block vehicles as immovable scenery. Hydrants tilt about
  their fixed base and emit a water stream, nozzle spray and positional looping water sound. The jet is a solid
  1 x 1 x 10 column (cFountainStream::OnCollision): people in it are knocked down and shoved, vehicles are lifted
  by eight times its velocity and loose props by four, and it puts out fires it reaches (CWaterCannon::PutOutFires,
  saving a burning car); park fountains behave the same way. Partial blast
  damage retains standing models and collisions; lethal health can smash anchored props without uprooting them.
- **Traffic:** parked cars and cars driving the roads, stopping for pedestrians in their path. Generated traffic
  owns pedestrian drivers; they animate out when the player takes the seat and are released on destruction.
  Closed-car interiors still hide the original pedestrian sprites. Turn signals follow cAISpline::ShouldIndicate /
  InaugerateYoke (the last 10 units before a junction and through the turn) and stay on a car knocked into physics.
  Ordinary drivers retain their task after an impact and steer back to the road, with gear-dependent controls,
  queued-vehicle prediction and timed reverse recovery. Seated drivers no longer block their own cars.
  Spawns use the loaded road-link cache and the original retry/fallback state. Dynamic/mission networks and
  the full task graph still need parity work.
- **Pedestrians:** civilians and cops walk the pavement network of each world sector, using the original zone
  population, spawn schedule, spawn ring, cleanup distance and `pedinfo.bin` looks (gender, body, colours).
  Joggers run, and everyone hurries in the rain. Nearby player vehicles retain the task's run-speed limit;
  pedestrians can roll out of a speeding car's path using scheduled interception checks and the original
  roll quadrants. Fleeing NPCs use their own sprint poses. Pavement/land-road rejoin, timed idle opportunities and
  collision-based stuck records replace the old timed path reversal. Dynamic contacts change velocity before movement, using the original
  1.25-radius body spheres and 0.875-radius static sweeps. Vehicle run-over damage, fatal-task ordering,
  impact drag, grounded sliding and living knockdown/getup transitions follow the Android routines. A body
  stays down while a vehicle is above it. Generic explosions apply distance-based health damage and high
  knockdowns, including zero-damage fringe hits. Seated occupants are protected from direct blasts.
- **Sound:** engines, horns, doors, impacts, object smashing, explosions, the player's footsteps, music, the city,
  rain and airport ambience, and pedestrian screams and falls. Footsteps use the legs while holding the sword
  or using unarmed fight idle; sword and fist swing cues follow the original animation timing. HUD sounds
  continue during pause while world sounds are suspended.
- **Random numbers:** the original four-stream generator and seed.
- **Radio:** the original radio app (**Tab** in a car): the station carousel (the original's quarter-step scrolling,
  swipe sound and "Music Player" heading), the equaliser driven by the music, favourite stars and the volume meter,
  with the game paused. Stations start at C rand() offsets like cWavStream::RandomiseRadioStart. The PDA frame
  around it is still drawn in a PC style.
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
- **Stats:** cStatsManager's counters (kills, explosions, playing time, days, distances, time in a vehicle and more)
  are kept as in the original save section; there is no stats screen yet.
- **Pause:** the original free-play HUD overlay, using local HUD artwork and GXT text, darkening, touch bounds
  and input delay. **Esc** / **Start** open it; mission/replay confirmations require the missing mission host.
- **PC extras:** game controllers (any SDL controller: sticks, triggers and buttons for every action) and Linux
  builds. Mod texture changes are composited on the GPU and spread over frames, so
  switching a texture mod on or off no longer stalls the game.
- **Tools:** the Porter, which makes the game from your APK and lets you pick which bundled mods to include, and a
  showcase viewer for the game's assets.

**F3** shows the debug overlay and collision diagnostics. The keys are in [CONTROLS.md](CONTROLS.md).

## Not there yet

- Missions, save games, full combat, wanted levels and the rest of the original gameplay HUD.
- Radar GPS routes, mission/location blips, a control for the rotating-map setting (the rotation itself is ported), armour pickups, HUD fades,
  cash and the full pause/PDA layout. The radar work is a subset of the HUD, not complete UI parity.
- Pedestrians do not talk, fight, or use building doors or attractors.
  Traffic is cars only.
- Pedestrian physics still needs the full collision-cache ordering/limits, collision flags that stop AI movement,
  pinned-body crush-damage behavior, sprite pose-height interpolation and other combat damage/reaction types.
  The implemented vehicle impact paths do not constitute complete death physics.
- Full explosion entity ordering/limits, immunity and source-reaction flags, prop weapon/fire damage integration
  and health ownership, specialised duplicate-check exclusions and explosion pool limits; the vehicle
  death-cause and chain scheduling still need full parity.
  The generic vehicle impulse is consumed after integration to avoid losing it to parked-car settling, an
  adaptation pending the native entity scheduler. Prop health resets at the port's existing cleanup distance,
  not the native sector/entity lifetime. Other specialised explosions, and the original shared particle-pool capacity/update ordering remain
  incomplete. Secondary smoke currently uses one emitter per burst.
- Other debris, buoyancy and splashes. Gate slats retain the original zero-height
  floor test; this does not fix the remaining waterfront mesh/collision clipping reports.
  Specialised prop smash branches, the fire truck's water cannon, the shared vehicle-fragment pool,
  complete carjacking reactions and specialised traffic/task branches remain incomplete.
- Scripted, aiming and specialised cameras, shake through camera-mode/death transitions, and the hidden-player
  visibility aid.
- Boats, the jet ski, helicopters and the tank.
- The SDK covers what is ported (v10 adds giving the player weapons, v11 switching them); wanted levels, missions, saves and custom audio remain unavailable.
- Weapons: the weapon table, cWeaponManager (slots, switching, ammo and clips, reloads), the weapon wheel and HUD button, and firing for the pistols, SMGs, shotguns, rifles and the minigun (accuracy spread, wall hits, peds and vehicles hit, damage and deaths, bullet traces, fire sounds); fists, nightstick, bat and chainsaw hits; the original auto-aim (cPlayerSensorCone: target priorities and distance bands, the 45-degree fire cone, the turning fire direction and the target marker); the RPG, grenades, molotovs, flash bangs and proximity mines (cBaseProjectile physics, bounces, fuses, the throw power and its arc, explosion types 0 to 3); the sword's delayed swing hit; cMuzzleFlashes (per-weapon flash type, size and jitter, drawn additively); wall ricochet sounds (AddRicochet); armour absorption and the radar's armour arc; blood drops (cParticleEmitterBlood) for bullet, minigun, melee and run-over damage; the flamethrower (its pilot light, flame particles, smoke and setting people and vehicles on fire); burning (cFire: burning people take damage until they die, vehicle fires, molotov ground fires and petrol flames that ignite whoever walks into them, sliding fires that stop at walls, and the looping fire sound); the taser (its dart, wire, electric arc, sparks and steam, shocks that end with the victim on fire, and its charge); the sniper rifle's scope (the top-down scope camera and model, zoom, and shots that hit whoever is under the crosshair). Not yet: the sword's prop hits, the chainsaw's blood spray, the flash bang's daze, water splashes, the gamepad lock-on and target cycling, hit reactions, AI peds with guns and pickups. Muzzle flashes stay up for one simulation tick (the original clears them after one 30 Hz draw).
  Character model overrides work for mobile DFF/ANPK/PVR assets; general model/material/shader interfaces and
  rigid object spawning are still missing.
- The third-person camera and imported LCS people are PC additions. The donor roster uses Toni and LCS civilian
  appearances, with approximate clip mapping and joint fitting; it does not reproduce Huang or every CTW
  pose. Some effects remain overhead artwork and look thin from low angles. See
  [third-person character support](ctw-modkit/THIRD_PERSON_ASSETS.md).

## Supported copy

Only GTA: Chinatown Wars for Android **4.4.243** (the arm64 build). A few lookup tables are read from its game
binary when the APK is ported; they are never part of this repository.
