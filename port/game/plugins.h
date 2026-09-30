// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// Plugin host: loads the DLLs placed directly in the mods folder (e.g. the mod menu from the mod kit) and gives
// them the CtwHostApi (ctw_plugin.h). The game ships without any plugin; nothing here is needed to play.
#pragma once
#include <string>

void Plugins_Init(const std::string& modsDir);
void Plugins_Shutdown();
void Plugins_BeginFrameInput();          // snapshot the keyboard for key_pressed
void Plugins_Tick();                     // one 30 fps game frame
void Plugins_DrawHud(int width, int height);   // inside Hud_Begin/End
bool Plugins_Key(int scancode);          // true = a plugin swallowed the key
bool Plugins_GameInput();                // false while a plugin (a menu) has taken the keyboard
void Plugins_Log(const std::string& line);
