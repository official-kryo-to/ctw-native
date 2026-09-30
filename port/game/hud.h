// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// 2D drawing for the HUD and the mod menu, using the game's own font (IPhone_Hel_16x16).
#pragma once
#include <cstdint>
#include <string>

bool Hud_Init(const std::string& dataDir);
void Hud_Begin(int width, int height);   // ortho projection, pixels, origin top-left
void Hud_End();
void Hud_Text(float x, float y, float scale, uint32_t rgba, const std::string& utf8);
float Hud_TextWidth(const std::string& utf8, float scale);
float Hud_LineHeight(float scale);
void Hud_Rect(float x, float y, float w, float h, uint32_t rgba);
std::u16string Hud_Utf8To16(const std::string& s);
