// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "assets.h"
#include "font.h"
#include "os/datafile.h"
#include <glad/gl.h>
#include <stb_image.h>
#include <cstring>

bool BitmapFont::load(const std::vector<uint8_t>& bin, const std::string& pngPath) {
    if (bin.size() < 4) return false;
    uint16_t n, h;
    memcpy(&n, &bin[0], 2);
    memcpy(&h, &bin[2], 2);
    if (bin.size() != 4 + (size_t)n * 6) return false;
    glyphs_.resize(n);
    for (uint16_t i = 0; i < n; ++i) memcpy(&glyphs_[i], &bin[4 + i * 6], 6);
    height_ = h;

    int w, hh, comp;
    std::vector<uint8_t> png;
    if (!Data_ReadAll(pngPath, png)) return false;
    unsigned char* px = stbi_load_from_memory(png.data(), (int)png.size(), &w, &hh, &comp, 4);
    if (!px) return false;
    texW_ = w; texH_ = hh;
    glGenTextures(1, &tex_);
    glBindTexture(GL_TEXTURE_2D, tex_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, hh, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    stbi_image_free(px);
    return true;
}

// 0xFE00..0xFFFF are in-band tags (colour changes 0xFF00.., script argument inserts 0xFEFE/0xFEFF, ...).
static bool isTag(char16_t c) { return c >= 0xFE00; }
// Codes with the 0x4000 / 0x8000 flag bits set are ordinary glyphs; the original masks them with 0x3FFF (CharWid).
static char16_t glyphCode(char16_t c) { return (c >= 0x4000 && c < 0xFE00) ? (char16_t)(c & 0x3FFF) : c; }

// Port of cFontManager::ReplaceUnsupportedCharacters for non-Japanese languages:
// codes outside the font (or above 0xAD, or stray control codes) become '-', and 0xAD becomes a flagged space.
static char16_t replaceUnsupported(char16_t c, size_t glyphCount, bool japanese) {
    if (c >= 0xFEF0) return c;                      // tags are left alone
    unsigned m = c & 0x3FFF, orig = m;
    if ((m - 9 > 1 && (unsigned)(m - 0x20) > glyphCount) || (!japanese && m > 0xAD)) c = 0x2D;
    if (orig == 0xAD) c = 0x8020;
    return c;
}

// cFontManager::HandleTextTag: which colour index a tag selects. Returns -1 if the tag doesn't change colour.
static int tagColour(char16_t t) {
    switch (t) {
        case 0xFF00: return 0;
        case 0xFF03: return 1;
        case 0xFF06: return 2;
        case 0xFF09: return 3;
        case 0xFF0C: return 4;
        case 0xFF0F: return 7;
        default: return -1;
    }
}

int BitmapFont::glyphWidth(char16_t c) const {
    c = glyphCode(replaceUnsupported(c, glyphs_.size(), japanese_));
    if (c < 0x20 || isTag(c) || (size_t)(c - 0x20) >= glyphs_.size()) return 0;
    return adv(glyphs_[c - 0x20].w);   // the original advances/samples width-1 (see CharWid, cFontStripBuilder::Add)
}

int BitmapFont::textWidth(const std::u16string& s) const {
    int w = 0;
    for (char16_t c : s) w += glyphWidth(c);
    return w;
}

float BitmapFont::draw(const std::u16string& s, float x, float y, float scale, int* colour) const {
    float base[4];
    glGetFloatv(GL_CURRENT_COLOR, base);
    int cur = colour ? *colour : 0;
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, tex_);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBegin(GL_QUADS);
    for (char16_t c : s) {
        int tc = tagColour(c);
        if (tc >= 0) cur = tc;
        c = glyphCode(replaceUnsupported(c, glyphs_.size(), japanese_));
        if (c < 0x20 || isTag(c) || (size_t)(c - 0x20) >= glyphs_.size()) continue;
        const Glyph& g = glyphs_[c - 0x20];
        if (cur == 0) glColor4f(base[0], base[1], base[2], base[3]);
        else glColor4ub(Assets_RenderTables().textColour[cur][0], Assets_RenderTables().textColour[cur][1], Assets_RenderTables().textColour[cur][2], 255);
        float hx = 0.5f / texW_, hy = 0.5f / texH_;   // half-texel inset: keep bilinear filtering inside the glyph cell
        float u0 = (float)g.x / texW_ + hx, v0 = (float)g.y / texH_ + hy;
        float u1 = (float)(g.x + adv(g.w)) / texW_ - hx, v1 = (float)(g.y + height_) / texH_ - hy;
        float gw = adv(g.w) * scale, gh = height_ * scale;
        glTexCoord2f(u0, v0); glVertex2f(x, y);
        glTexCoord2f(u1, v0); glVertex2f(x + gw, y);
        glTexCoord2f(u1, v1); glVertex2f(x + gw, y + gh);
        glTexCoord2f(u0, v1); glVertex2f(x, y + gh);
        x += gw;
    }
    glEnd();
    glDisable(GL_TEXTURE_2D);
    glColor4f(base[0], base[1], base[2], base[3]);
    if (colour) *colour = cur;
    return x;
}

float BitmapFont::drawWrapped(const std::u16string& s, float x, float y, float maxWidth, float scale) const {
    float cy = y, lineH = height_ * scale;
    int colour = 0;
    std::u16string line, word;
    auto flushWord = [&]() {
        if (word.empty()) return;
        if (!line.empty() && (textWidth(line + word) * scale) > maxWidth) {
            draw(line, x, cy, scale, &colour);
            cy += lineH;
            line.clear();
        }
        line += word;
        word.clear();
    };
    for (size_t i = 0; i < s.size(); ++i) {
        char16_t c = s[i];
        if (c == 0x0A) { flushWord(); draw(line, x, cy, scale, &colour); cy += lineH; line.clear(); continue; }
        if (c == u' ') { word += c; flushWord(); }
        else { word += c; if (japanese_) flushWord(); }   // no spaces in Japanese: any glyph may end a line
    }
    flushWord();
    if (!line.empty()) { draw(line, x, cy, scale, &colour); cy += lineH; }
    return cy - y;
}
