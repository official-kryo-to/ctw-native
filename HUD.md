# Gameplay HUD coverage

The normal north-up radar and health display are ported from the supported Android 4.4.243 ARM64 build.
The map resource and sprite artwork are read from the user's files at runtime. This repository contains no
map tiles, sprite sheets or extracted map grid.

## Evidence

These are observations from static inspection of build ID `a4c441f4943abbcc72e8270ec18248e4358a89e2`.
Addresses identify routines in that local binary; they are not executable code included in the port.

| Reference routine | Observed behavior used by the port |
| --- | --- |
| `cRadar::Init`, `0x847330` | The radar description comes from game-directory entry 22. |
| `cRadar::RenderMapTexturedAndGPS`, `0x849b60` | A header supplies the map origin, rectangular tile grid and fallback texture. The grid follows three overview texture IDs; tiles are addressed by row. |
| `cRadar::RenderRadar`, `0x84921c` | World coordinates use the original origin and Q12 map scale, truncated to whole map pixels. The circular mask and 32-step health semicircle use distinct radii. |
| `cRadar::GetScreenSpaceFromRadarSpace`, `0x848220` | The normal radar sits near the top-left edge in the 768-high HUD space. |
| `cRadar::StartupFromPDA`, `0x847380` | The frame uses the always-loaded sheet. The frame's scale follows the selected radar diameter. |
| `cRadar::Render`, `0x8486d8` | The player arrow follows the player's or occupied vehicle's heading; north is an upright marker at the radar edge in north-up mode. |
| `cSprite::CalculateVerts`, instructions `0x6c3284` to `0x6c33c8` | Stored sprite dimensions include a final pixel; sprite anchors and the native Y conversion determine the rendered placement. |

The normal radar supports both phone and tablet sizes in the reference. Choosing the phone size on PC is an
explicit adaptation. North-up is the implemented mode; the reference also has a saved rotation setting.
The port uses a stencil mask for the circle instead of the reference's depth mask, preserving world depth for
subsequent rendering. Screenshots establish that the port draws these elements; comparison against an original
Android runtime capture is still needed to claim pixel-for-pixel output.

## Other fixes

- Opening the weapon wheel pauses sound effects and simulation. Closing it restores the prior sound pause state.
- Clicks completed between display frames select a weapon. Releasing a fire-button press that predates the wheel
  does not dismiss it. Escape closes an open wheel even when a mod has disabled game input.
- Reloading or failing to initialize the weapon HUD releases its textures and owned pause. Default button
  coordinates reset before applying the native offset, preventing drift after repeated initialization.
- Font measurements return the widest explicit line, and drawing advances to the next line at a newline.
  This follows `cFontManager::TextSize` (`0x6cd3b0` and `0x6cd3d0`). Failed atlas replacement preserves the previous
  font; successful replacement releases the old texture. Native unsupported-character rules remain unchanged.
- The mod-menu layout test builds its C examples with a C compiler, avoiding mixed C/C++ linkage with GCC 16.

## Validation and remaining work

`radar_map` tests synthetic rectangular grids, missing tiles, malformed reloads and fixed-point coordinates.
`gameplay_hud_rendering` tests circular clipping, health sides, zero health, scaling and GL state restoration at
several viewport sizes. `weapon_hud_rendering` and `font_rendering` use synthetic sprite/font atlases and actual
OpenGL rendering, without requiring game files. The existing gameplay and mod-menu tests still apply.

Local screenshots cover the game, weapon wheel and showcase. The private test data must include the current
weapon and weather tables generated from the supported APK; older data folders lack those tables.

GPS, location and mission blips, map rotation controls, armour ownership, HUD fades, cash and the rest of the
pause/PDA interface remain unported. The port's existing weapon and gameplay limitations still apply.
