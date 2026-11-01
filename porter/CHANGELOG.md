# Changelog

What changed in each release of CTW-Native. A release is the Porter exe with the game inside, so every entry
covers both. The newest is on top; the release workflow publishes the entry of the version it builds.

## 0.5.0

0.4.0 was never published; its changes are in this release too (the second half of the list).

- Saving: after the first mission the apartment is yours. Walk into the green marker at its door to go in and save;
  you come out six hours later, healed and no longer wanted. The game loads your newest save when it starts. Saves
  use the original's slot files (`saves/savegame0`, `savegame1`). The PDA's save screen is not there yet.
- Linux: a Linux download (x86-64) next to the Porter. Unpack it, run `./port-apk.sh your.apk`, then `./GTACTW`.
  It also fixes a Linux build break.
- The first mission plays as the original from start to finish: the prologue on the plane (with its sound), the
  landing, the ambush at the port with the cars' own stops and handbrake turn, the intro movie, the escape off the
  pier with the sinking car and the swim to shore, hotwiring, the police chase, Kenny's restaurant, the apartment,
  its garage, and the safehouse tour.
- Cutscenes: the game's sequences with their camera paths, comic panels, typed subtitles and widescreen bars.
- The HUD's text as the game draws it: the objective line, help boxes (tap them to dismiss), MISSION PASSED, and
  WASTED / BUSTED with the original's timing; radar blips, chevrons and mission markers as the original's sprites.
- Play together: the new Multiplayer mod puts up to eight players in one city (F9 to host or join, T to chat).
  Players see each other walk, drive, ride and shoot, with names and health over them; hits are taken by the
  victim's own game, and joiners share the host's time and weather. CTW Online lists public games; LAN games show
  in the pause screen's browser. Mod API version 13 adds the puppets it uses and pause-screen buttons.
- Money: the HUD's cash counter, the hospital's $200 and the police's $100 when you are reborn.
- Pickups: the map's health, armour and weapon pickups with their five-minute respawns, and dead pedestrians drop
  their money or their gun.
- Police: cop cars chase and the cops get out to beat, shoot or arrest you, ending in the Busted screen.
- Start-up as on Android: the legal screen, the loading ring and the intro movie (hold any key for a second to skip).
- The radar flashes blue and red while you are wanted; bikes are mounted from the nearer side; Enhancements can make
  hit street furniture come loose and tumble.
- Fix the Linux checks, which failed on every run since the mouse-look tests were added.

## 0.3.0

- Add wanted levels, crime reporting, police pedestrians and vehicles, and HUD stars.
- Add the pause screen and the death screen with its shattering effect, Wasted jingle and text.
- Improve traffic reactions, obstacle avoidance, lane rejoining and horns; enter vehicles through the nearer door.
- Add pedestrian social behaviour, correct random pedestrian outfits, and improve crash and landing reactions.
- Add skid marks, tyre smoke, rim sparks, burnouts, damaged-car effects and riders thrown from bikes in crashes.
- Expand fire, explosion and water effects, including hydrants, fountains, sinking props and debris smoke trails.
- Add vehicle sirens, melee and weapon sounds, fight grunts and landing sounds; refine the radio screen.
- Update the Porter's table extraction to include the new game data, matching the Python setup.

## 0.2.0

- Streets feel alive: pedestrians walk the sidewalks in their original outfits, dive clear of speeding cars, and traffic stops for them.
- Smashed bins burst rubbish, and a knocked-over bin spills it as it rolls.
- The city has sound: street ambience, rain and airport hum, plus the missing smash sounds.
- The original radio: press Tab in a car for the station carousel, live equaliser, favourites and volume. The game pauses, like the original.
- Optional on-foot third-person view in the bundled Enhancements mod (V), plus a tidier mod menu (F4) with search and a bigger Cheat Example.
= PSP lighting in the bundled mod menu

## 0.1.1

- Include the current game changes: vehicle physics, radio and audio fixes, and updated game tables.
- Extract the radio mappings and hospital restart points at runtime, matching the Python setup and current game.
- Rebuild the game and mod kit before packaging the Porter, so an older local executable cannot silently ship.
- Stop release builds with an incomplete game payload.
- Fix the Rust 1.98 Clippy failures and show the Windows checks as separate steps. Cache Rust dependencies
  after failed checks so reruns do not start from scratch.

## 0.1.0

- The Porter: one exe that turns your GTA: Chinatown Wars APK (Android 4.4.243) into the PC game. Drop the APK,
  press Port it, play. No Python, no build tools.
- The mod menu (F4) and the example mods come with it, and it can put a shortcut on the desktop.
- Mod menu: nothing overlaps any more - not the game's clock, not the help line, not the vehicle spawner.
