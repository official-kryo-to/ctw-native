# Current state

A playable prototype, not the finished game. This lists what is there; it is not a claim of complete
compatibility with the original.

## What works

- **Start-up:** as on Android: the English legal screen (LegalScreens_ENG.png, fitted and centred on black) for
  exactly five seconds with the game halted, then the loading app (cLoadingAppRing): background 87 and the ring,
  which makes its half-turn over eight frames and holds it until loading is done (at least nine frames). Then the
  new game's intro movie (kena01::cIntroDrive::StartMovie: CTW_iPhone_intro.mp4 with its soundtrack, stretched over
  the window like Android's movie view, the game's music paused) plays until its end, or until something is held for
  more than a second (UpdateMovieCallback). It is decoded by Windows (Media Foundation); other platforms skip it. The
  in-engine plane prologue before it and the rest of the first mission (KEN_A01) are not ported yet.
- **The city:** streets and buildings with their textures, water, animated signs, weather, day and night,
  street furniture, street lights, steam vents and fountains.
- **On foot:** the player character with movement, sprinting and collision. The follow camera counts consecutive
  blocked views and uses the original roof-edge escape and safe above-target recovery.
  Movement now integrates once per tick; weapon-dependent footsteps use the correct upper/leg animation cycle.
- **Gameplay HUD:** the original north-up radar tiles, frame, north marker, rotating player/vehicle arrow and
  health semicircle. The map follows the controlled character or vehicle and uses the original map coordinates,
  clipping and colours. The PC uses the phone radar size. While wanted the map flashes as cRadar does: steady blue
  with one star, blue and red every twelve frames once two stars are lit or a chasing cop sees the player, each colour
  fading in over a few frames, with the map premultiplied by its 0.75 alpha as RenderRadar draws it.
  The weapon wheel pauses world sounds as well as simulation, accepts clicks completed between display frames,
  and releases its pause and textures when closed, reloaded or shut down. Multiline font drawing and measurement
  now agree; a failed font replacement preserves the loaded font.
- **Vehicles:** cars and bikes with the game's handling, getting in and out, damage, lights, skidmarks and sounds.
  Bikes are mounted from the nearer side, as the seat plans' ops 9 and 10 do.
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
  saving a burning car); park fountains behave the same way. Street furniture knocked into the water splashes
  (cExplosionWaterSplashPed with all three cParticleEmitterWaterSplash types: droplets, a spreading ring, plumes
  and for big splashes a column) and sinks; the water cannon's nozzle spray no longer gets an invented upward push. Partial blast
  damage retains standing models and collisions; lethal health can smash anchored props without uprooting them.
- **Traffic:** parked cars and cars driving the roads, stopping for pedestrians in their path. Generated traffic
  owns pedestrian drivers; they animate out when the player takes the seat and are released on destruction.
  Closed-car interiors still hide the original pedestrian sprites. Turn signals follow cAISpline::ShouldIndicate /
  InaugerateYoke (the last 10 units before a junction and through the turn) and stay on a car knocked into physics.
  Ordinary drivers retain their task after an impact and steer back to the road, with gear-dependent controls,
  queued-vehicle prediction and timed reverse recovery. Seated drivers no longer block their own cars.
  Drivers react to damage as cVehicle::Damage / GetReaction / cPed::HandleReaction decide: a hard hit (collision
  damage of 12 or more), a bullet or the flamethrower rolls the driver's pedinfo reactions. Civilians mostly floor it
  and race off along the roads under physics (cWanderRoadsUnderPhysics at speed value 0x78) and sometimes jump out
  and run; drivers of slow vehicles always run. Attacking drivers (cKill) still flee instead.
  Spawns use the loaded road-link cache and the original retry/fallback state. Dynamic/mission networks and
  the full task graph still need parity work.
- **Wanted level:** cWantedRating / cWantedManager from the extracted wanted tables. Shooting, beating, tasering,
  burning, running over or killing peds and cops, damaging cop cars, jacking, and explosions from the player's
  weapons are crimes with the original points and instant-star rules. One lit star decays while no cop car is on
  screen (faster further from where a cop last saw him); two or more need cop cars taken down; a lost star stays
  dimmed through the 300-frame no-crime probation. The HUD shows the six stars (lit, half-alpha when taken down,
  probation sprite, flashing and popping in as cMiniHud does). The population answers it: WantedModifier puts
  more cop cars in the mix per star (capped per level, fewer while a star is dimmed), from two stars on every
  vehicle spawned is a cop (freeing the car off screen the longest when full), and the cop unit and vehicle come
  from GetCopSubType / GetCopsVehicleId. Cop cars give chase (cAggressiveDriving's target sides, rams and
  hold-offs, straight at the player: the road-network route search is not ported), stop within 20 units once the
  player is slow, wrecked or overturned, and the cops get out. On foot they close in, punch at one star and shoot
  pistols from two (cArrestOnFoot's AbortKillingOrder rule: an arrest needs the player below 21 health, and from two
  stars standing still or already held). The arresting cop's countdown ends in the "Busted" screen; a cop at the
  driver's door of a slow car (any speed at one star) makes the player get out. Wanted cops spawn at the usual places, not on the original's ring around the
  player; road blocks, helicopters and the higher levels' units are not ported.
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
  continue during pause while world sounds are suspended. Also: blocked traffic's horn patterns (cHorn), sirens
  (toggled with **Q** / **E** in a siren vehicle), weapon reloads and melee hits, the chainsaw's engine, fight
  grunts, pain comments for shot peds, the landing thud of a thrown player and the burnout launch.
- **Death and arrest:** the original death app: the frame breaks into one of the nine shard sets and flies apart with
  the "Wasted" jingle and the WASTED! text (or "Busted"), then the player is reborn six hours later at the nearest
  hospital for $200, or the nearest police station for $100 and without his weapons, and the game fades in.
- **Money and pickups:** cPlayer's money (up to $99,999,999) and the HUD's cash: the green amount slides in, counts
  towards the new total with the original's growing step and tick, shows what is left beside it and slides out
  again (cHudApp::UpdateCash, Gui::cCashMeter). The map's pickups from every sector (health, armour and weapons,
  with the collected-pickup store and five-minute respawns) are drawn from the game's pickup sheet, pulsing and
  fading as cBasePickup::ManagedRender does. A weapon pickup adds ammo, fills an empty slot or upgrades a lesser
  weapon of its kind (ShouldWeAutoPickupThisWeapon); otherwise it waits for the phone's swap box, which the port does
  not have. Peds carry money by the zone's affluence ($10 to $60) and gang members their pedinfo weapon
  (GetAssignedWeapon); a dead ped drops the weapon, or else the money, once the body is still, for about 17 seconds
  while five of the 30 pickup slots are free. Dumpsters, luck lions and mission pickups are not ported.
- **Random numbers:** the original four-stream generator and seed.
- **Radio:** the original radio app (**Tab** in a car): the station carousel (the original's quarter-step scrolling,
  swipe sound and "Music Player" heading), the equaliser driven by the music, favourite stars and the volume meter,
  with the game paused. Stations start at C rand() offsets like cWavStream::RandomiseRadioStart. The PDA frame
  around it is still drawn in a PC style.
- **Smooth motion:** the game runs at the original 30 ticks a second; vehicles, people, loose props and the camera
  are drawn between ticks, so motion is smooth at any refresh rate.
- **Multiplayer:** the bundled **Multiplayer** mod: up to eight players in one city over UDP (**F9** to host or
  join, **T** to chat). Everyone sees the others walk, drive and ride with their names and health over them; hits
  on another player are taken by that player's own game; joiners take the host's time and weather. Traffic,
  pedestrians, the police and pickups stay each player's own. See the [modding guide](ctw-modkit/README.md#playing-together).
- **Mods:** the mod menu (**F4**). A mod is one folder with any mix of texture replacements and a C or C++ DLL.
  The API covers vehicles, the player, pedestrians, the world, traffic, radio, custom cameras, game events, saved
  settings and menu items. The bundled **Enhancements** mod adds an option (on by default) that makes hit street furniture come loose and tumble (API v12 `set_prop_physics`) instead of the original's fixed tilts, and an optional third-person camera (**V**) and
  PSP-style lighting. API v6 exposes interpolated explosion shake; Enhancements applies it to third person.
  API v7 adds skinned character resources and interpolated player/pedestrian render overrides. The Porter offers
  optional user-supplied LCS Android 2.4.379 character import and lets users choose premade mods.
  API v8 exposes skinned clip bounds and absolute animation time. Imported knockdowns advance on the simulation
  clock and settle on their animated body bounds; donor size is fitted from standing width rather than a guessed
  multiplier. API v9 adds full vehicle attachment frames, the owning vehicle handle, animated-joint drawing and
  continuous impact/recovery clocks. Imported riders follow bike lean and pitch; the mod selects chopper,
  dirt-bike or sport-bike donor poses for the corresponding CTW bikes instead of using scooter poses everywhere.
  API v13 adds puppets (characters a mod moves, such as other players), screen projection, the player's vitals and
  damage, and full vehicle poses. See the [modding guide](ctw-modkit/README.md).
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

- Missions, save games, full combat, the rest of the police response (road blocks, helicopters, SWAT and the
  higher levels' units, route-finding pursuit) and the rest of the original gameplay HUD.
- Radar GPS routes, mission/location blips, a control for the rotating-map setting (the rotation itself is ported), HUD fades,
  the stats screen and the full pause/PDA layout. The radar work is a subset of the HUD, not complete UI parity.
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
- Buoyancy, people and vehicles going into water (drowning, sinking cars) and other debris. Gate slats retain the original zero-height
  floor test; this does not fix the remaining waterfront mesh/collision clipping reports.
  Specialised prop smash branches, the fire truck's water cannon, the shared vehicle-fragment pool,
  complete carjacking reactions and specialised traffic/task branches remain incomplete.
- Scripted, aiming and specialised cameras, shake through camera-mode/death transitions, and the hidden-player
  visibility aid.
- Boats, the jet ski, helicopters and the tank.
- The SDK covers what is ported (v10 adds giving the player weapons, v11 switching them, v13 multiplayer); wanted
  levels, money, missions, saves and custom audio remain unavailable to mods.
- Weapons: the weapon table, cWeaponManager (slots, switching, ammo and clips, reloads), the weapon wheel and HUD button, and firing for the pistols, SMGs, shotguns, rifles and the minigun (accuracy spread, wall hits, peds and vehicles hit, damage and deaths, bullet traces, fire sounds); fists, nightstick, bat and chainsaw hits; the original auto-aim (cPlayerSensorCone: target priorities and distance bands, the 45-degree fire cone, the turning fire direction and the target marker); the RPG, grenades, molotovs, flash bangs and proximity mines (cBaseProjectile physics, bounces, fuses, the throw power and its arc, explosion types 0 to 3); the sword's delayed swing hit; cMuzzleFlashes (per-weapon flash type, size and jitter, drawn additively); wall ricochet sounds (AddRicochet); armour absorption and the radar's armour arc; blood drops (cParticleEmitterBlood) for bullet, minigun, melee and run-over damage; the flamethrower (its pilot light, flame particles, smoke and setting people and vehicles on fire); burning (cFire: burning people take damage until they die, vehicle fires, molotov ground fires and petrol flames that ignite whoever walks into them, sliding fires that stop at walls, and the looping fire sound); the taser (its dart, wire, electric arc, sparks and steam, shocks that end with the victim on fire, and its charge); the sniper rifle's scope (the top-down scope camera and model, zoom, and shots that hit whoever is under the crosshair). Not yet: the sword's prop hits, the chainsaw's blood spray, the flash bang's daze, water splashes, the gamepad lock-on and target cycling, hit reactions and AI peds using their guns. Muzzle flashes stay up for one simulation tick (the original clears them after one 30 Hz draw).
  Character model overrides work for mobile DFF/ANPK/PVR assets; general model/material/shader interfaces and
  rigid object spawning are still missing.
- The third-person camera and imported LCS people are PC additions. The donor roster uses Toni and LCS civilian
  appearances, with approximate clip mapping and joint fitting; it does not reproduce Huang or every CTW
  pose. Some effects remain overhead artwork and look thin from low angles. See
  [third-person character support](ctw-modkit/THIRD_PERSON_ASSETS.md).

## Supported copy

Only GTA: Chinatown Wars for Android **4.4.243** (the arm64 build). A few lookup tables are read from its game
binary when the APK is ported; they are never part of this repository.
