// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "gametext.h"
#include "assets.h"
#include "os/datafile.h"
#include "os/gamefs.h"
#include "os/pak.h"
#include <glad/gl.h>
#include <stb_image.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace GameText {

namespace {
// The shipped build's language (gGame +0x20): 1, English. Its font switches are all set.
constexpr unsigned kLanguage = 1;

uint16_t u16(const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); }

int16_t clamp16(int v) { return (int16_t)std::min(std::max(v, -0x8000), 0x7FFF); }

// cTexture::OnLoad: the smallest power of two at least the width
int log2Up(int w) {
    int n = 0;
    while ((1 << n) < w) ++n;
    return n;
}

bool isColourTag(unsigned c) {   // 0xFF00, 03, 06, 09, 0C, 0F (the 0x9249 mask)
    const unsigned d = (c & 0xFFFF) - 0xFF00;
    return d <= 0xF && ((1u << d) & 0x9249);
}

unsigned uploadPng(const std::vector<uint8_t>& png, int* width) {
    int w, h, comp;
    unsigned char* px = stbi_load_from_memory(png.data(), (int)png.size(), &w, &h, &comp, 4);
    if (!px) return 0;
    unsigned tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);   // cTextureManager::Load(.., 0x812F)
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    stbi_image_free(px);
    *width = w;
    return tex;
}
}

FontManager& Fonts() {
    static FontManager m;
    return m;
}

bool FontManager::load(const std::string& dataDir) {
    if (loaded_) return true;
    GameFs fs;
    fs.setUseOldFonts(true);
    if (!fs.open(dataDir)) return false;
    // cGame::Init, language 1 with UsingNewFont: LoadFont(font, file, asset, height, offset); a height of 0 is the
    // .bin's own
    struct Entry { const char* file; int asset, height, offset; };
    static const Entry table[kFonts] = {
        {"IPhone_Hel_20x20_lrg.bin", 5, 0, 0},   {"IPhone_Hel_20x20_lrg.bin", 5, 0, 0},
        {"IPhone_Hel_20x20_lrg.bin", 5, 0, 0},   {"IPhone_Osaka_25x25.bin", 6, 0x19, -4},
        {"Icons_font.bin", 2, 0x10, -4},         {"Osaka_30x30.bin", 7, 0x23, -4},
    };
    // cAssetManager::LoadAssets: the font assets are gGameDir[10]'s textures (+0xD8 count, +0xDA ids); with
    // UseNewWWFont, assets 3..5 are the loose IPhone_Hel_20x20_lrg.png sheet (GetNewSpriteSheetId)
    std::vector<uint8_t> dir, assets;
    if (!Assets_Pak().read(0, dir) || dir.size() < 22 || !Assets_Pak().read(u16(&dir[20]), assets)) return false;
    std::vector<uint8_t> png;
    int lrgWidth = 0;
    const unsigned lrg = fs.read("IPhone_Hel_20x20_lrg.png", png) ? uploadPng(png, &lrgWidth) : 0;
    if (!lrg) return false;
    for (int f = 0; f < kFonts; ++f) {
        Font& font = fonts_[f];
        if (!fs.read(table[f].file, font.bin) || font.bin.size() < 4 || font.bin.size() < 4 + (size_t)u16(&font.bin[0]) * 6)
            return false;
        font.height = (uint8_t)(table[f].height > 0 ? table[f].height : u16(&font.bin[2]));
        font.offset = (int8_t)table[f].offset;
        const int asset = table[f].asset;
        int w = 0;
        if (asset >= 3 && asset <= 5) {
            font.gl = lrg;
            w = lrgWidth;
        } else {
            if (assets.size() < 4 + (size_t)asset * 2) return false;
            const int id = u16(&assets[2 + asset * 2]);
            int h;
            font.gl = Assets_Texture(id);
            if (!font.gl || !Assets_TextureSize(id, &w, &h)) return false;
        }
        font.shift = 11 - log2Up(w);   // cFontStripBuilder::cFontStripBuilder
    }
    std::vector<uint8_t> t;
    if (!Data_ReadAll(dataDir + "/font_tables.bin", t) || t.size() != 8 + sizeof colours_ ||
        std::memcmp(t.data(), "CTWFONT1", 8))
        return false;
    std::memcpy(colours_, &t[8], sizeof colours_);
    loaded_ = true;
    return true;
}

int FontManager::glyphCount(int font) const { return u16(&fonts_[font].bin[0]); }
int FontManager::binHeight(int font) const { return u16(&fonts_[font].bin[2]); }
const uint8_t* FontManager::glyph(int font, int index) const { return &fonts_[font].bin[4 + index * 6]; }
const uint8_t* FontManager::textColour(int i) const { return colours_[i >= 0 && i < 23 ? i : 0]; }

float FontManager::fontTextScale(int font) const {
    switch (font) {   // UseNewWWFont, language not 0
        case 0: return textScale * 0.3f;
        case 1: return textScale * 0.25f;
        case 2: return textScale * 0.35f;
        default: return textScale;
    }
}

int FontManager::fontHeight(int font) const {
    return (int)std::fmaf((float)fonts_[font].height, fontTextScale(font), 0.5f);
}

float FontManager::lineHeight(int font) const {
    const float h = (float)fonts_[font].height;
    return font <= 2 ? fontTextScale(font) * h : h;
}

int FontManager::charWid(unsigned c, int font) const {
    if (!isColourTag(c) && (c & 0xFFFF) >> 4 >= 0xFF1) {   // an icon: font 4's glyph, unrounded
        const int i = (int)(c & 0xFFFF) - 0xFF10;
        if (i >= glyphCount(kIconFont)) return 0;
        return (int)(textScale * (float)u16(glyph(kIconFont, i)));
    }
    const int i = (int)(c & 0x3FFF) - 0x20;
    if (i < 0 || i >= glyphCount(font)) return 0;
    float w = (float)u16(glyph(font, i));
    w = font <= 2 ? w + -2.f : w + -1.f;
    return (int)(w * fontTextScale(font) + 0.5f);
}

int FontManager::lineEnd(const char16_t* s, int maxWidth, int font, const char16_t** end) const {
    if (font == 8) font = this->font;
    const float max = (float)maxWidth;
    const unsigned limit = (unsigned)glyphCount(font) + 0x20;
    float w = 1.f;
    bool afterSpace = false;
    int breakWidth = 0;
    const char16_t* breakAt = s;
    const char16_t* p = s;
    unsigned c;
    const char16_t* at;
    for (;;) {
        at = p;
        c = *p++;
        bool measure = true;
        if (c >> 4 >= 0xFEF) {
            if (isColourTag(c) || c >> 4 <= 0xFF0) measure = false;   // tags take no room
            else afterSpace = false;                                 // an icon (0x4000 set): no break before it
        } else if ((c & 0x3FFF) >= limit) {
            afterSpace = false;
            c = '-';
        } else if (c == 0) {
            breakWidth = (int)w;
            breakAt = at;
            break;
        } else if (c <= 0x20) {
            if (!afterSpace) { breakAt = at; breakWidth = (int)w; }
            if (c != 0x20) break;
            afterSpace = true;
        } else if (c & 0x4000) {
            afterSpace = false;   // a flagged character can start the next line
            breakWidth = (int)w;
            breakAt = at;
        } else {
            afterSpace = false;
        }
        if (measure) w = w + (float)charWid(c, font);
        if (w > max || c <= 0x1F) break;
    }
    if (breakWidth == 0) {   // no break found: before this character
        breakWidth = (int)(w - (float)charWid(c, font));
        breakAt = at;
    }
    *end = breakAt;
    return breakWidth;
}

uint32_t FontManager::textSize(const char16_t* s, int wrap, int font, uint16_t firstLine, uint16_t maxLines) const {
    if (font == 8) font = this->font;
    char16_t upper[0x200];
    if (font != 1) {
        const int usable = usableFont(s, font);
        if (usable == font && font == 3) {
            MakeUpperCase(upper, s, 0x200);
            s = upper;
        }
        font = usable;
    } else {
        font = 1;
    }
    if (!*s) return (uint32_t)wrap & 0xC000;
    const float scale = fontTextScale(font);
    int widest = 0;
    int16_t height = 0;
    const char16_t* p = s;
    for (unsigned line = 0;; ++line) {
        const char16_t* end;
        const int w = lineEnd(p, wrap & 0x3FFF, font, &end);
        p = end;
        if (w > (int16_t)widest) widest = w;
        while (*p == ' ') ++p;
        if (*p == '\n') ++p;
        if (line == (unsigned)maxLines + 1) break;
        if (line >= firstLine) {
            const int h = (int)std::fmaf((float)fonts_[font].height, scale, 0.5f);
            height = clamp16((int)((float)h + (float)height));
        }
        if (!*p) break;
    }
    return (uint32_t)((widest + 1) & 0xFFFE) | (uint32_t)height << 16 | ((uint32_t)wrap & 0xC000);
}

int FontManager::usableFont(const char16_t* s, int font) const {
    if (font == 1 || font == 8) return 1;
    auto fits = [this, s](int f) {
        const unsigned count = (unsigned)glyphCount(f);
        for (const char16_t* p = s; *p; ++p) {
            if (*p >> 4 > 0xFEE) continue;
            const unsigned c = *p & 0x3FFF;
            if (c - 0xB < 0xFFFFFFFEu && c - 0x20 > count) return false;   // not in the font (tab and newline aside)
            if (kLanguage && c > 0xAD) return false;
        }
        return true;
    };
    if (fits(font)) return font;
    if (font == 3 || font == 5) return fits(0) ? 0 : 1;
    return 1;
}

bool FontManager::replaceUnsupported(char16_t* s, int font) const {
    const unsigned count = (unsigned)glyphCount(font);
    bool tags = false;
    for (char16_t* p = s; *p; ++p) {
        const unsigned c = *p;
        if (c >> 4 > 0xFEE) { tags |= c < 0xFEFE; continue; }
        const unsigned m = c & 0x3FFF;
        if ((m - 9 >= 2 && m - 0x20 > count) || (kLanguage && c >= 0xAE)) *p = '-';
        if (m == 0xAD) *p = 0x8020;
    }
    return printing && tags;
}

void MakeUpperCase(char16_t* dst, const char16_t* src, unsigned max) {
    unsigned i = 0;
    for (; i < max; ++i) {
        const unsigned c = src[i];
        if (!c) break;
        int d = 0;
        if (c - 'a' < 26) d = -0x20;
        else if (c >= 0x96 && c <= 0xAC) d = -0x18;
        dst[i] = (char16_t)(c + d);
    }
    dst[i == max ? i - 1 : i] = 0;
}

// ---- cFontStripBuilder ----

namespace {
struct Builder {
    Strip& strip;
    int font;
    int height, shift;   // +0x10 the .bin's cell height, +0x14
    Builder(Strip& s, int f) : strip(s), font(f), height(Fonts().binHeight(f)), shift(Fonts().widthShift(f)) {}

    // cFontStripBuilder::Add: the glyph's quad from x, y (top, y down) and the next x
    int add(int x, int y, int index) {
        FontManager& m = Fonts();
        const uint8_t* g = m.glyph(font, index);
        const int gw = u16(g), gx = u16(g + 2), gy = u16(g + 4);
        const bool small = font < 3;   // fonts 0..2 on the new world-wide font
        const int w = small ? gw - 2 : gw - 1;
        const int u0 = gx << shift, u1 = (gx + w) << shift;
        const int v0 = (small ? gy : gy + 2) << shift, v1 = (small ? height + gy : height + gy - 1) << shift;
        const int extra = small && index == 5 ? 3 << shift : 0;   // '%' samples three texels more
        const float scale = m.fontTextScale(font);
        const int16_t h = clamp16((int)std::fmaf((float)height, scale, 0.5f));
        const size_t n = strip.verts.size();
        if (n >= 4) {   // a new run unless this glyph carries on the strip in the same colours
            const Vert& last = strip.verts[n - 1];
            const bool apart = !(last.x == x && last.y + (y + h) == 0);
            const bool same = strip.top[strip.run] == m.colourTop && strip.bottom[strip.run] == m.colourBottom;
            if (!same || apart) {
                const unsigned next = strip.run + 1u;
                if ((next & 0xFF) <= 0x77) strip.run = (uint8_t)next;
                strip.runVerts[strip.run] = 0;
            }
        }
        strip.top[strip.run] = m.colourTop;
        strip.bottom[strip.run] = m.colourBottom;
        const int right = (int)std::fmaf((float)w, scale, 0.5f) + x;
        const int16_t top = (int16_t)-y, bottom = (int16_t)(-y - h);
        strip.verts.push_back({(int16_t)x, top, (int16_t)u0, (int16_t)v0, 0xFFFFFFFFu});
        strip.verts.push_back({(int16_t)x, bottom, (int16_t)u0, (int16_t)v1, 0xFFFFFFFFu});
        strip.verts.push_back({(int16_t)right, top, (int16_t)(u1 + extra), (int16_t)v0, 0xFFFFFFFFu});
        strip.verts.push_back({(int16_t)right, bottom, (int16_t)(u1 + extra), (int16_t)v1, 0xFFFFFFFFu});
        strip.runVerts[strip.run] = (uint16_t)(strip.runVerts[strip.run] + 4);
        return right;
    }
};

// RenderStrip: each run's quads in its colours, premultiplied by the alpha (top vertices the first colour)
void renderStrip(Strip& strip, int font, bool black, float alpha) {
    if (strip.verts.empty()) return;
    FontManager& m = Fonts();
    const unsigned scaled = (unsigned)(alpha * 255.f);
    const unsigned a = scaled < 0xFF ? scaled : 0xFF;
    auto premultiply = [a](const uint8_t* c) {
        auto k = [a](unsigned v) { return ((v * a & 0xFFFF) * 0x8081u) >> 23; };
        return k(c[0]) | k(c[1]) << 8 | k(c[2]) << 16 | a << 24;
    };
    static const uint8_t kBlack[4] = {0, 0, 0, 0};
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, m.texture(font));
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);   // cRenderer::BeginFrame's
    size_t start = 0;
    for (unsigned run = 0; run < kRuns && strip.runVerts[run]; ++run) {
        const unsigned n = strip.runVerts[run];
        unsigned c1 = strip.top[run], c2 = strip.bottom[run];
        if ((font == 5 || font == 3) && c1 == c2 && c1 <= 4) {   // the Osaka fonts' own pairs
            static const uint8_t kTop[5] = {0x0B, 0x0F, 0x13, 0x0D, 0x11}, kBottom[5] = {0x0C, 0x10, 0x14, 0x0E, 0x12};
            c1 = kTop[c1];
            c2 = kBottom[c2];
        }
        const uint32_t top = premultiply(black ? kBlack : m.textColour((int)c1)),
                       bottom = premultiply(black ? kBlack : m.textColour((int)c2));
        glBegin(GL_TRIANGLE_STRIP);   // cGlInterface::DrawTristrip
        for (size_t i = start; i < start + n && i < strip.verts.size(); ++i) {
            const Vert& v = strip.verts[i];
            const uint32_t c = (i - start) & 1 ? bottom : top;
            glColor4ub((GLubyte)c, (GLubyte)(c >> 8), (GLubyte)(c >> 16), (GLubyte)(c >> 24));
            glTexCoord2f(v.u / 2048.f, v.v / 2048.f);
            glVertex2f(v.x, v.y);
        }
        glEnd();
        start += n;
    }
    glColor4f(1, 1, 1, 1);
}
}

// ---- cTextSprite ----

TextSprite::TextSprite(uint16_t width, uint16_t maxY, int font) : font_((uint8_t)font), width_(width), maxY_(maxY) {}

void TextSprite::print(const char16_t* s, uint16_t firstLine, uint16_t maxLines) {
    FontManager& m = Fonts();
    const int font = font_;
    const float lineH = (float)(int)std::fmaf((float)m.heightByte(font), m.fontTextScale(font), 0.5f);
    Builder glyphs(glyphs_, font), icons(icons_, kIconFont);
    float x = (float)x_, y = (float)y_;
    if (*s) {
        const int width = width_ & 0x3FFF;
        const unsigned justify = width_ & 0xC000;
        const float full = (float)width, half = (float)(width >> 1);
        for (int line = 0;; ++line) {
            const float below = lineH + y;
            if (below > (float)maxY_) break;
            const char16_t* end = s;
            int w = 0, shift = 0;
            float left = x;
            bool beyond = false, measured = true;
            if (justify == 0x8000) {   // right: the line ends at x (the width when x is 0)
                left = x == 0.f ? full : x;
                w = m.lineEnd(s, (int)left, font, &end);
                if (end == s) { measured = false; x = left; }
                else shift = -w;
            } else {
                float room;
                if (justify == 0x4000) {   // centred on x (the middle when x is 0), as wide as both sides allow
                    const float c = x == 0.f ? half : x;
                    const float other = full - c;
                    x = c;
                    room = (c < other ? c : other) * 2.f;
                } else {
                    room = full - x;
                }
                w = m.lineEnd(s, (int)room, font, &end);
                if (end == s) measured = false;
                else {
                    shift = justify == 0x4000 ? -w >> 1 : 0;
                    left = x;
                }
            }
            if (measured) {
                beyond = line > (int)maxLines;
                if (!beyond && line >= (int)firstLine && w >= 2) {
                    const int gy = (int)(y + (float)m.offset(font));
                    float pen = left + (float)shift;
                    for (const char16_t* p = s; p != end; ++p) {
                        const unsigned c = *p;
                        const int index = (int)(c & 0x3FFF) - 0x20;
                        if (index >= 0 && index < m.glyphCount(font)) {
                            pen = (float)glyphs.add((int)pen, gy, index == 0x8D ? 0 : index);
                            continue;
                        }
                        switch (c) {   // cFontManager's colour tags
                            case 0xFF00: m.colourTop = m.colourBottom = 0; continue;
                            case 0xFF03: m.colourTop = m.colourBottom = 1; continue;
                            case 0xFF06: m.colourTop = m.colourBottom = 2; continue;
                            case 0xFF09: m.colourTop = m.colourBottom = 3; continue;
                            case 0xFF0C: m.colourTop = m.colourBottom = 4; continue;
                            case 0xFF0F: m.colourTop = m.colourBottom = 7; continue;
                            default: break;
                        }
                        if (c >> 4 < 0xFF1) continue;
                        const int icon = (int)c - 0xFF10;   // an icon on the icon strip, white unless kept
                        const uint8_t top = m.colourTop, bottom = m.colourBottom;
                        const bool white = !m.iconKeepsColour || icon == 0x11;
                        if (white) m.colourTop = m.colourBottom = 0;
                        if (icon < m.glyphCount(kIconFont)) {
                            pen = (float)icons.add((int)pen, (int)((float)gy + (font == 1 ? 0.f : 7.f)), icon);
                        }
                        if (white) { m.colourTop = top; m.colourBottom = bottom; }
                    }
                }
                x = left;
                s = end;
            }
            char16_t c;
            do c = *s++; while (c == ' ');
            if (!c) {   // the end: x after the last line's glyphs
                x = x + (float)(w - (w > 0) + shift);
                break;
            }
            if (c != 0x0A) --s;
            const char16_t next = c == 0x0A ? *s : c;
            const bool hidden = line < (int)firstLine || line > (int)maxLines;
            if (!hidden) y = below;
            x = 0.f;
            if (!next || beyond || line > (int)maxLines) break;
        }
    }
    x_ = clamp16((int)x);
    y_ = clamp16((int)y);
    if (!m.keepColour) m.colourTop = m.colourBottom = 0;
}

void TextSprite::reprint(const char16_t* s, uint16_t firstLine, uint16_t maxLines) {
    const uint32_t size = Fonts().textSize(s, (int16_t)width_, font_, firstLine, maxLines);
    maxY_ = (uint16_t)(size >> 16);
    glyphs_ = Strip{};
    icons_ = Strip{};
    x_ = y_ = 0;
    print(s, firstLine, maxLines);
}

void TextSprite::newLine() {
    const FontManager& m = Fonts();
    x_ = 0;
    const float lineH = (float)(int)std::fmaf((float)m.heightByte(font_), m.fontTextScale(font_), 0.5f);
    y_ = clamp16((int)(lineH + (float)y_));
}

void TextSprite::render(bool black, float alpha) const {
    renderStrip(const_cast<Strip&>(glyphs_), font_, black, alpha);
    if (!black) renderStrip(const_cast<Strip&>(icons_), kIconFont, false, alpha);
}

}
