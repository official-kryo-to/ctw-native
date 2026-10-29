// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "uifont.h"
#include <brotli/decode.h>
#include <glad/gl.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <vector>
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include <stb_truetype.h>

namespace {
uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
uint16_t be16(const uint8_t* p) { return (uint16_t)(p[0] << 8 | p[1]); }
void put32(std::vector<uint8_t>& v, size_t at, uint32_t x) { for (int k = 0; k < 4; ++k) v[at + k] = (uint8_t)(x >> (24 - 8 * k)); }
void put16(std::vector<uint8_t>& v, size_t at, uint16_t x) { v[at] = (uint8_t)(x >> 8); v[at + 1] = (uint8_t)x; }

// WOFF2 (W3C): the header, the table directory, then one brotli stream of every table. The sfnt is rebuilt from
// tables that were stored as they are; transformed glyf/loca/hmtx (TrueType outlines) are not supported.
bool woff2ToSfnt(const uint8_t* d, size_t n, std::vector<uint8_t>& out) {
    static const char* const kKnown[63] = {
        "cmap", "head", "hhea", "hmtx", "maxp", "name", "OS/2", "post", "cvt ", "fpgm", "glyf", "loca", "prep", "CFF ",
        "VORG", "EBDT", "EBLC", "gasp", "hdmx", "kern", "LTSH", "PCLT", "VDMX", "vhea", "vmtx", "BASE", "GDEF", "GPOS",
        "GSUB", "EBSC", "JSTF", "MATH", "CBDT", "CBLC", "COLR", "CPAL", "SVG ", "sbix", "acnt", "avar", "bdat", "bloc",
        "bsln", "cvar", "fdsc", "feat", "fmtx", "fvar", "gvar", "hsty", "just", "lcar", "mort", "morx", "opbd", "prop",
        "trak", "Zapf", "Silf", "Glat", "Gloc", "Feat", "Sill"};
    if (n < 48 || be32(d) != 0x774F4632u) return false;   // 'wOF2'
    const uint32_t flavor = be32(d + 4), compressed = be32(d + 20);
    const int count = be16(d + 12);
    if (flavor == 0x74746366u || !count) return false;     // a collection ('ttcf')
    struct Table { uint32_t tag, length; };
    std::vector<Table> tables;
    size_t at = 48, total = 0;
    auto base128 = [&](uint32_t& v) {
        v = 0;
        for (int i = 0; i < 5; ++i) {
            if (at >= n) return false;
            const uint8_t c = d[at++];
            if (i == 0 && c == 0x80) return false;
            if (v & 0xFE000000u) return false;
            v = v << 7 | (c & 0x7F);
            if (!(c & 0x80)) return true;
        }
        return false;
    };
    for (int i = 0; i < count; ++i) {
        if (at >= n) return false;
        const uint8_t flags = d[at++];
        uint32_t tag;
        if ((flags & 63) == 63) { if (at + 4 > n) return false; tag = be32(d + at); at += 4; }
        else { const char* s = kKnown[flags & 63]; tag = be32((const uint8_t*)s); }
        const int version = flags >> 6;
        const bool glyfLoca = tag == 0x676C7966u || tag == 0x6C6F6361u;
        uint32_t length;
        if (!base128(length)) return false;
        if (glyfLoca ? version != 3 : version != 0) return false;   // transformed
        tables.push_back({tag, length});
        total += length;
    }
    if (at + compressed > n || total > (64u << 20)) return false;
    std::vector<uint8_t> raw(total);
    size_t got = total;
    if (BrotliDecoderDecompress(compressed, d + at, &got, raw.data()) != BROTLI_DECODER_RESULT_SUCCESS || got != total)
        return false;
    // the sfnt: the offset table, records sorted by tag, each table 4-byte aligned
    std::vector<size_t> order(tables.size()), from(tables.size());
    for (size_t i = 0, o = 0; i < tables.size(); ++i) { order[i] = i; from[i] = o; o += tables[i].length; }
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return tables[a].tag < tables[b].tag; });
    size_t size = 12 + 16 * tables.size();
    for (const Table& t : tables) size += (t.length + 3) & ~3u;
    out.assign(size, 0);
    put32(out, 0, flavor);
    put16(out, 4, (uint16_t)count);
    int sel = 0;
    while ((2 << sel) <= count) ++sel;
    put16(out, 6, (uint16_t)(16 << sel));
    put16(out, 8, (uint16_t)sel);
    put16(out, 10, (uint16_t)(count * 16 - (16 << sel)));
    size_t data = 12 + 16 * tables.size();
    for (size_t r = 0; r < order.size(); ++r) {
        const Table& t = tables[order[r]];
        std::memcpy(out.data() + data, raw.data() + from[order[r]], t.length);
        uint32_t sum = 0;
        for (size_t k = 0; k < ((t.length + 3) & ~3u); k += 4) sum += be32(out.data() + data + k);
        const size_t rec = 12 + 16 * r;
        put32(out, rec, t.tag);
        put32(out, rec + 4, sum);
        put32(out, rec + 8, (uint32_t)data);
        put32(out, rec + 12, t.length);
        data += (t.length + 3) & ~3u;
    }
    return true;
}

struct Glyph { float u0, v0, u1, v1; int x0, y0, w, h; unsigned page; };

struct Face {
    std::vector<uint8_t> sfnt;
    stbtt_fontinfo info{};
    float ascent = 0, descent = 0, unitsPerEm = 1000;   // font units
    bool ok = false;
};
Face g_faces[2];

// One glyph atlas per face and size, in 512 x 512 alpha pages filled shelf by shelf.
struct Atlas {
    std::vector<unsigned> pages;
    int x = 0, y = 0, shelf = 0;
    std::map<uint32_t, Glyph> glyphs;
};
std::map<std::pair<int, int>, std::unique_ptr<Atlas>> g_atlases;
constexpr int kPage = 512;

const Glyph& glyph(int face, float scale, int key, Atlas& a, uint32_t cp) {
    auto it = a.glyphs.find(cp);
    if (it != a.glyphs.end()) return it->second;
    Glyph g{};
    const stbtt_fontinfo& info = g_faces[face].info;
    int x0, y0, x1, y1;
    stbtt_GetCodepointBitmapBox(&info, (int)cp, scale, scale, &x0, &y0, &x1, &y1);
    g.x0 = x0; g.y0 = y0; g.w = x1 - x0; g.h = y1 - y0;
    if (g.w > 0 && g.h > 0 && g.w < kPage - 2 && g.h < kPage - 2) {
        if (a.pages.empty() || a.x + g.w + 1 > kPage) { a.x = 0; a.y += a.shelf + 1; a.shelf = 0; }
        if (a.pages.empty() || a.y + g.h + 1 > kPage) {
            unsigned tex = 0;
            glGenTextures(1, &tex);
            glBindTexture(GL_TEXTURE_2D, tex);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            std::vector<uint8_t> clear((size_t)kPage * kPage, 0);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, kPage, kPage, 0, GL_ALPHA, GL_UNSIGNED_BYTE, clear.data());
            a.pages.push_back(tex);
            a.x = a.y = a.shelf = 0;
        }
        std::vector<uint8_t> px((size_t)g.w * g.h);
        stbtt_MakeCodepointBitmap(&info, px.data(), g.w, g.h, g.w, scale, scale, (int)cp);
        g.page = a.pages.back();
        glBindTexture(GL_TEXTURE_2D, g.page);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexSubImage2D(GL_TEXTURE_2D, 0, a.x, a.y, g.w, g.h, GL_ALPHA, GL_UNSIGNED_BYTE, px.data());
        g.u0 = (float)a.x / kPage; g.v0 = (float)a.y / kPage;
        g.u1 = (float)(a.x + g.w) / kPage; g.v1 = (float)(a.y + g.h) / kPage;
        a.x += g.w + 1;
        a.shelf = std::max(a.shelf, g.h);
    } else g.w = g.h = 0;
    (void)key;
    return a.glyphs.emplace(cp, g).first->second;
}

uint32_t next(const char*& s) {   // UTF-8
    const unsigned char c = (unsigned char)*s++;
    if (c < 0x80) return c;
    int n = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
    uint32_t cp = n == 3 ? c & 7 : n == 2 ? c & 15 : c & 31;
    for (; n > 0 && ((unsigned char)*s & 0xC0) == 0x80; --n) cp = cp << 6 | ((unsigned char)*s++ & 0x3F);
    return cp;
}
uint32_t upper(uint32_t c) {   // text-transform: uppercase, Latin-1 and the common Latin Extended-A pairs
    if ((c >= 'a' && c <= 'z') || (c >= 0xE0 && c <= 0xFE && c != 0xF7)) return c - 32;
    if (c == 0xFF) return 0x178;
    if (c >= 0x100 && c <= 0x17F && (c & 1) && c != 0x131 && c != 0x138 && c != 0x149 && c != 0x17F) return c - 1;
    return c;
}

int faceFor(int style) { return (style & UiFont::Bold) && g_faces[1].ok ? 1 : 0; }

// Walks the text as CSS lays it out: advance + kerning, then the letter spacing; a space adds the word spacing.
template<class F> float layout(float px, float trackingEm, int style, const char* s, F&& each) {
    const int face = faceFor(style);
    const Face& f = g_faces[face];
    const float scale = px / f.unitsPerEm, tracking = trackingEm * px, wordSpacing = 0.1f * px;
    float x = 0, widest = 0;
    int line = 0;
    uint32_t prev = 0;
    while (*s) {
        uint32_t cp = next(s);
        if (cp == '\n') { widest = std::max(widest, x); x = 0; ++line; prev = 0; continue; }
        if (style & UiFont::Upper) cp = upper(cp);
        if (prev) x += stbtt_GetCodepointKernAdvance(&f.info, (int)prev, (int)cp) * scale;
        int advance, lsb;
        stbtt_GetCodepointHMetrics(&f.info, (int)cp, &advance, &lsb);
        each(face, scale, cp, x, line);
        x += advance * scale + tracking + (cp == ' ' ? wordSpacing : 0.f);
        prev = cp;
    }
    return std::max(widest, x);
}
}   // namespace

namespace UiFont {
bool load(int face, const uint8_t* woff2, size_t size) {
    if (face < 0 || face > 1) return false;
    Face& f = g_faces[face];
    f.ok = false;
    if (!woff2ToSfnt(woff2, size, f.sfnt) || !stbtt_InitFont(&f.info, f.sfnt.data(), 0)) return false;
    int ascent, descent, gap;
    stbtt_GetFontVMetrics(&f.info, &ascent, &descent, &gap);
    const int head = stbtt__find_table(f.sfnt.data(), 0, "head");
    if (!head) return false;
    f.unitsPerEm = be16(f.sfnt.data() + head + 18);
    f.ascent = (float)ascent;
    f.descent = (float)-descent;
    f.ok = f.unitsPerEm > 0;
    for (auto it = g_atlases.begin(); it != g_atlases.end();)   // a reloaded face draws its glyphs again
        if (it->first.first == face) { glDeleteTextures((int)it->second->pages.size(), it->second->pages.data()); it = g_atlases.erase(it); }
        else ++it;
    return f.ok;
}

bool ok() { return g_faces[0].ok; }

float width(float px, float trackingEm, int style, const char* utf8) {
    if (!ok() || !utf8 || px <= 0.f) return 0.f;
    return layout(px, trackingEm, style, utf8, [](int, float, uint32_t, float, int) {});
}

void draw(float x, float y, float px, float trackingEm, int style, uint32_t rgba, const char* utf8) {
    if (!ok() || !utf8 || px <= 0.f) return;
    const Face& f = g_faces[faceFor(style)];
    // the baseline: the font's content area (ascent + descent) centred in the line box
    const float scale = px / f.unitsPerEm, lh = lineHeight(px);
    const float baseline = std::round(y + (lh - (f.ascent + f.descent) * scale) * 0.5f + f.ascent * scale);
    const int key = (int)std::lround(px * 16.f);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glColor4ub((GLubyte)(rgba >> 24), (GLubyte)(rgba >> 16), (GLubyte)(rgba >> 8), (GLubyte)rgba);
    auto& slot = g_atlases[{faceFor(style), key}];
    if (!slot) slot = std::make_unique<Atlas>();
    Atlas& atlas = *slot;
    // rasterise first: textures cannot be updated between glBegin and glEnd
    layout(px, trackingEm, style, utf8, [&](int face, float s, uint32_t cp, float, int) { glyph(face, s, key, atlas, cp); });
    unsigned bound = 0;
    layout(px, trackingEm, style, utf8, [&](int face, float s, uint32_t cp, float pen, int line) {
        const Glyph& g = glyph(face, s, key, atlas, cp);
        if (!g.w) return;
        if (g.page != bound) {
            if (bound) glEnd();
            glBindTexture(GL_TEXTURE_2D, g.page);
            glBegin(GL_QUADS);
            bound = g.page;
        }
        const float gx = std::round(x + pen) + g.x0, gy = baseline + line * lh + g.y0;
        glTexCoord2f(g.u0, g.v0); glVertex2f(gx, gy);
        glTexCoord2f(g.u1, g.v0); glVertex2f(gx + g.w, gy);
        glTexCoord2f(g.u1, g.v1); glVertex2f(gx + g.w, gy + g.h);
        glTexCoord2f(g.u0, g.v1); glVertex2f(gx, gy + g.h);
    });
    if (bound) glEnd();
    glDisable(GL_TEXTURE_2D);
}
}   // namespace UiFont
