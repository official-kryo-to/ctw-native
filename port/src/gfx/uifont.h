// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// The interface typeface: an OpenType font in a WOFF2 file (brotli, untransformed tables - CFF fonts such as Kryoto's
// Teletext), drawn with stb_truetype. Sizes are CSS pixels: px is the font size, tracking the letter spacing in em,
// and a line box is 1.5 px high (the CSS default the Porter's interface inherits).
#pragma once
#include <cstddef>
#include <cstdint>

namespace UiFont {
enum Style { Bold = 1, Upper = 2 };   // Upper: CSS text-transform: uppercase

// face 0 regular, 1 bold. Needs a GL context to draw; loading does not.
bool load(int face, const uint8_t* woff2, size_t size);
bool ok();
// Text with its line box's top-left at (x, y). Letter spacing follows every character (as in CSS), and a space
// also gets the face's word spacing (0.1 em). Newlines start another line box.
void draw(float x, float y, float px, float trackingEm, int style, uint32_t rgba, const char* utf8);
float width(float px, float trackingEm, int style, const char* utf8);   // the widest line
inline float lineHeight(float px) { return px * 1.5f; }
}
