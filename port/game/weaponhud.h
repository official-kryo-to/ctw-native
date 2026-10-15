// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// The weapon HUD: the weapon button (cIPhonePad::UpdateWeaponSprite / UpdatePrimaryAmmo) and the weapon wheel
// (Gui::cHudWeaponSelect), in the original's 1024 x 768 HUD space (768 high; wider screens add (768 W / H - 1024) / 2
// on the right of x 512 and take it off on the left, as cIPhonePad::LoadInitialButtons does).
//
// Weapon button: the held weapon's icon from SS_AlwaysLoaded (GetSpriteFromWeaponType(type, 0): 53 fists .. 70) at
// button config 15 of the game's buttonConfig file (moved 25 left and up), scaled 60 / 42 x 0.8, with "clip/rest"
// (or the count without a clip, nothing when infinite) centred 10 below it at phone text scale 1.5.
// Weapon wheel (LoadWeaponPanel / PositionWeaponSprite): slots 0..6 around (512, 344) at the positions of the
// 0x4836F0 table, each with SS_Hud sprite 26 behind it (alpha 0.25 except for the held slot), the SS_Weapons icon
// (GetSpriteFromWeaponType(type, 1)) and "clip/rest" 23 below; the held weapon again in the middle (position 8) and the
// secondary (thrown) weapon at (376, 208) with its count. Pressing a slot lights its background; releasing on it
// selects the weapon (cPda::TsCommand) and closes the wheel. The game is paused while it is open.
// PC: Tab opens and closes it on foot (the phone game taps the weapon button), the mouse points and clicks, Esc
// closes it. Not decoded: the icons' hover render mode (cSprite +0x1F 7 / 8), the HUD sound effects and the rest
// of the pause panel the wheel shares (money, radar and radio buttons).
#pragma once
#include <string>

class Game;

bool WeaponHud_Init(const std::string& dataDir);
void WeaponHud_Shutdown();                             // before destroying the game or its OpenGL context
bool WeaponHud_Ok();
void WeaponHud_Render(Game& g, int width, int height);   // button and, when open, the wheel (in the HUD pass)
bool WeaponHud_Key(Game& g, int scancode);              // Tab / Esc; true if used
bool WeaponHud_WheelOpen();
void WeaponHud_SetWheel(Game& g, bool open);
// The big SS_Weapons icon of a weapon type, centred in the box (pixels; the mod API's draw_weapon_icon).
bool WeaponHud_DrawIcon(int type, float x, float y, float w, float h, float alpha);
