// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// The Porter's interface for the game's own PC screens (the developer menu): the same tokens and components as the
// mod kit's ctw_ui.h (Kryoto's OLED palette with pill radii, porter/src/styles.css; Label, Caption, Button, Check,
// the card, title bar and field of porter/src/ui and App.tsx), drawn with the HUD and the interface typeface. Sizes
// are CSS pixels. Call between Hud_Begin and Hud_End.
#pragma once
#include <cstdint>

namespace PorterUi {
constexpr uint32_t Background = 0x000000FFu, Foreground = 0xF8F8F8FFu, Card = 0x010101F2u, Primary = 0xF8F8F8FFu,
                   PrimaryFg = 0x000000FFu, Secondary = 0x060606FFu, MutedFg = 0x808080FFu, Border = 0x121212FFu,
                   Ring = 0x9E9E9EFFu, Destructive = 0xCF4040FFu, Warning = 0xE1A536FFu, Success = 0x4DBF74FFu,
                   Info = 0x52B5F4FFu;
constexpr float Radius = 16.f, TextXs = 12.f, TextSm = 14.f;
enum Style { Bold = 1, Upper = 2 };
enum Variant { Outline, PrimaryButton, Ghost, Danger };
enum Size { Sm, Md, Lg };

uint32_t alpha(uint32_t rgba, float a);
void roundRect(float x, float y, float w, float h, float r, uint32_t rgba);
void frame(float x, float y, float w, float h, float r, float t, uint32_t rgba);   // a border, t pixels
void line(float x0, float y0, float x1, float y1, float w, uint32_t rgba);
void iconX(float x, float y, float size, uint32_t rgba);
void iconCheck(float x, float y, float size, float stroke, uint32_t rgba);
void iconChevron(float x, float y, float size, bool right, uint32_t rgba);
void card(float x, float y, float w, float h);   // bg-card/95, border, radius, shadow-2xl black/70

void text(float x, float y, float px, float tracking, int style, uint32_t rgba, const char* s);   // y: line box top
float textWidth(float px, float tracking, int style, const char* s);
void textFit(float x, float y, float px, float tracking, int style, uint32_t rgba, const char* s, float maxW);
inline float typeY(float y, float h, float px) { return y + (h - px * 1.5f) * 0.5f; }

// The title bar (h-10, border-b): the title and a pill note. Returns the close button's rectangle in out[4].
void titleBar(float x, float y, float w, const char* title, const char* note, bool closeHot, float out[4]);
float buttonWidth(int size, const char* label);
void button(float x, float y, float w, float h, int variant, int size, bool hot, bool pressed, bool disabled, const char* label);
void check(float x, float y, float h, bool on, bool hot);          // the size-4 box, centred in a row of height h
void field(float x, float y, float w, float h, bool hot, bool focused);
}
