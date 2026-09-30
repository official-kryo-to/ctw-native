// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// Bitmap font as used by the game: <name>.bin (glyph table) + <name>.png (glyph atlas).
// .bin = u16 count, u16 cellHeight, then count * { u16 width, u16 x, u16 y } (pixels in the PNG).
// Glyph for character code c is entry (c - 0x20).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

class BitmapFont {
public:
    bool load(const std::vector<uint8_t>& bin, const std::string& pngPath);   // needs a GL context
    // Japanese font (GTACTWJapanese): full-width advance, no '>0xAD -> -' replacement, per-glyph line wrapping.
    void setJapanese(bool v) { japanese_ = v; }
    int lineHeight() const { return height_; }
    int glyphWidth(char16_t c) const;
    int textWidth(const std::u16string& s) const;
    // Draws with the top-left of the first glyph at (x,y); returns the pen x after the text.
    // `colour` is the active text-colour index (0 = caller's glColor); colour tags in the string update it.
    float draw(const std::u16string& s, float x, float y, float scale, int* colour = nullptr) const;
    // Word-wraps to maxWidth pixels (at the given scale) and draws; returns total height.
    float drawWrapped(const std::u16string& s, float x, float y, float maxWidth, float scale) const;
private:
    struct Glyph { uint16_t w, x, y; };
    std::vector<Glyph> glyphs_;
    int height_ = 0;
    bool japanese_ = false;
    int adv(int w) const { return japanese_ ? w : w - 1; }   // advance and sampled width
    unsigned tex_ = 0;
    int texW_ = 1, texH_ = 1;
};
