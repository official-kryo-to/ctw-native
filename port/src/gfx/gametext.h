// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// The game's text: cFontManager (gFonts, the metrics), cTextSprite::Print (cFontStripBuilder's glyph strips) and
// RenderStrip, as the shipped English build has them. That build is language 1 with UseOldFonts, UseNewJpnFonts and
// UseNewWWFont all set, so fonts 0..2 are IPhone_Hel_20x20_lrg at fTextScale x 0.3 / 0.25 / 0.35 (GetFontTextScale).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace GameText {

enum { kFonts = 6, kIconFont = 4, kRuns = 0x78 };

struct Vert { int16_t x, y, u, v; uint32_t colour; };   // cGlVtx2dPTC: u, v in 1/2048ths of the texture width

// cFontStrip: the glyph quads as one triangle strip, split into runs of one colour pair (top, bottom)
struct Strip {
    uint8_t run = 0;
    uint16_t runVerts[kRuns]{};
    uint8_t top[kRuns]{}, bottom[kRuns]{};
    std::vector<Vert> verts;
};

// cFontManager and gFonts
struct FontManager {
    uint8_t font = 0;              // +0: font 8's
    uint8_t colourTop = 0, colourBottom = 0;   // +1, +2: the colour the next glyphs take (TextColours indices)
    uint32_t justification = 0;    // +4: 0x4000 centred, 0x8000 right (cTextSprite::Alignment)
    bool printing = false;         // +8: ReplaceUnsupportedCharacters may ask for cPrintingText
    bool keepColour = false;       // +9: Print leaves the colour set
    bool iconKeepsColour = true;   // +0xA
    float textScale = 1.f;         // fTextScale (SetPhoneTextScale / RestorePhoneTextScale)

    // cGame::Init's LoadFont table and cAssetManager::LoadAssets' font textures. Needs a GL context.
    bool load(const std::string& dataDir);
    bool ok() const { return loaded_; }

    float fontTextScale(int font) const;                     // GetFontTextScale
    int fontHeight(int font) const;                          // cFontManager::FontHeight
    float lineHeight(int font) const;                        // GetLineHeight
    int charWid(unsigned c, int font) const;                 // cFontManager::CharWid
    // GetLineEnd: the width of the line from s within maxWidth and where it ends
    int lineEnd(const char16_t* s, int maxWidth, int font, const char16_t** end) const;
    // TextSize: (width + 1) & ~1 | height << 16 | (wrap & 0xC000)
    uint32_t textSize(const char16_t* s, int wrap, int font, uint16_t firstLine, uint16_t maxLines) const;
    int usableFont(const char16_t* s, int font) const;       // GetUsableFont
    bool replaceUnsupported(char16_t* s, int font) const;    // ReplaceUnsupportedCharacters (in place)

    int glyphCount(int font) const;
    const uint8_t* glyph(int font, int index) const;         // {u16 w, x, y}
    int binHeight(int font) const;                           // the .bin's own cell height
    int heightByte(int font) const { return fonts_[font].height; }   // gFonts +0x10 (LoadFont's height)
    int offset(int font) const { return fonts_[font].offset; }       // gFonts +0x11
    unsigned texture(int font) const { return font >= 0 && font < kFonts ? fonts_[font].gl : 0; }
    int widthShift(int font) const { return font >= 0 && font < kFonts ? fonts_[font].shift : 0; }   // 11 - WidthLog2
    const uint8_t* textColour(int i) const;                  // TextColours[i]: r, g, b, a

private:
    struct Font {
        std::vector<uint8_t> bin;
        uint8_t height = 0;
        int8_t offset = 0;
        unsigned gl = 0;
        int shift = 0;
    };
    Font fonts_[kFonts];
    uint8_t colours_[23][4]{};
    bool loaded_ = false;
};

FontManager& Fonts();   // gFontManager / gFonts

void MakeUpperCase(char16_t* dst, const char16_t* src, unsigned max);   // UnicodeMakeUpperCase(.., true)

// cTextSprite: a printed string as glyph strips, its origin at the sprite's position (y up, as the strips store it)
class TextSprite {
public:
    // cTextSprite::cTextSprite(width, maxY, font, glyphs, 0): the width carries the justification bits. (Its last
    // argument, RenderStrip's ortho mode, is 0 for every sprite the HUD makes: the cSprite sets the projection.)
    TextSprite(uint16_t width, uint16_t maxY, int font);
    void alignment(uint32_t justification) { width_ = (uint16_t)((width_ & 0x3FFF) | justification); }
    void print(const char16_t* s, uint16_t firstLine, uint16_t maxLines);
    // cTextSprite::Render: the strips under the current matrices; black draws the shadow pass (no icons)
    void render(bool black, float alpha) const;
    int font() const { return font_; }
    int16_t endX() const { return x_; }
    int16_t endY() const { return y_; }
private:
    uint8_t font_;
    uint16_t width_, maxY_;
    int16_t x_ = 0, y_ = 0;
    Strip glyphs_, icons_;
};

}
