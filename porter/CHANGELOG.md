# Changelog

What changed in each release of CTW-Native. A release is the Porter exe with the game inside, so every entry
covers both. The newest is on top; the release workflow publishes the entry of the version it builds.

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
