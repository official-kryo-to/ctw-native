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
// Game strings (UTF-16 with the game's colour tags): the line breaks for maxWidth pixels, and drawing
std::u16string Hud_Wrap16(const std::u16string& s, float maxWidth, float scale);
void Hud_Text16(float x, float y, float scale, uint32_t rgba, const std::u16string& s);
float Hud_TextWidth(const std::string& utf8, float scale);
float Hud_LineHeight(float scale);
void Hud_Rect(float x, float y, float w, float h, uint32_t rgba);
std::u16string Hud_Utf8To16(const std::string& s);
// The interface typeface (the Porter's, gfx/uifont.h): CSS pixels, tracking in em, UiFont::Style bits. y is the top
// of a line box 1.5 px high. Without the typeface, the game's font at a matching size.
void Hud_UiText(float x, float y, float px, float trackingEm, int style, uint32_t rgba, const char* utf8);
float Hud_UiTextWidth(float px, float trackingEm, int style, const char* utf8);
