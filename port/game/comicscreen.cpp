// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "comicscreen.h"
#include "hud.h"
#include "gfx/assets.h"
#include "os/pak.h"
#include "text/gxt.h"
#include "world/timecycle.h"
#include <glad/gl.h>
#include <algorithm>
#include <cctype>
#include <cstring>

namespace {
// the virtual screen: 768 high, as wide as the display's aspect makes it, 0..1024 centred (y up, as the game's
// ortho matrix has it)
struct Virtual {
    float scale, left;   // pixels per unit; the virtual x at the window's left edge
    Virtual(int W, int H) {
        scale = H / 768.f;
        const float width = H > 0 ? W * 768.f / H : 1024.f;
        left = -(width - 1024.f) * 0.5f;
    }
};

void vertex(float x, float y, float u, float v, uint32_t abgr) {
    glColor4ub((GLubyte)abgr, (GLubyte)(abgr >> 8), (GLubyte)(abgr >> 16), (GLubyte)(abgr >> 24));
    glTexCoord2f(u, v);
    glVertex2f(x, y);
}

uint32_t scaled(uint32_t abgr, float k) {   // the colour times the overlay's fade, alpha included
    uint32_t out = 0;
    for (int i = 0; i < 32; i += 8) out |= (uint32_t)std::min(255.f, ((abgr >> i) & 0xFF) * k) << i;
    return out;
}
}

bool ComicScreen::load(const std::string& dataDir) {
    dataDir_ = dataDir;
    std::vector<uint8_t> directory;
    if (!Assets_Pak().read(0, directory) || directory.size() < 52) return false;
    uint16_t dir[26];
    std::memcpy(dir, directory.data(), sizeof dir);
    auto table = [&](int id, std::vector<Image>& out) {
        std::vector<uint8_t> d;
        if (!Assets_Pak().read(id, d)) return;
        out.resize(d.size() / sizeof(Image));
        std::memcpy(out.data(), d.data(), out.size() * sizeof(Image));
    };
    table(dir[24], bgImages_);       // cTwoDSequenceApp::Init: gGameDir +0x30
    table(dir[25], spriteImages_);   // +0x32
    return !bgImages_.empty() && !spriteImages_.empty();
}

void ComicScreen::begin(const Sequence& seq, const TimeCycle& tc) {
    for (Quad& q : bg_) q = Quad{};
    for (Quad& q : sprite_) q = Quad{};
    bgVisible_ = spriteVisible_ = 0;
    mainBg_ = -1;
    secondBg_ = false;
    marker_ = 0xFFFF;
    for (auto& r : rects_) { r[0] = 0; r[1] = 0; r[2] = 1024; r[3] = 768; }
    // the constructor's cTimeCycle::ValueAsRGBA8888ScaleToBlack(0x20 / 0x23, 0)
    top_ = tc.colour(0x20);
    bottom_ = tc.colour(0x23);
    fade_ = 0.f;
    text_.clear();
    textShown_ = false;
    printed_ = printDelay_ = 0;
    active_ = true;
    hidesWorld_ = false;
    // the subtitles: InitMovieText(the header's table) unless it is "cut"
    std::string name = seq.textTable();
    for (char& c : name) c = (char)std::tolower((unsigned char)c);
    if (name != table_) {
        table_ = name;
        strings_.clear();
        GxtFile gxt;
        if (!name.empty() && name != "cut" && gxt.load(dataDir_ + "/e_" + name + ".gxt"))
            for (size_t i = 0; i < gxt.count(); ++i) strings_.push_back(gxt.get(i));
    }
}

void ComicScreen::stop() {}   // (the app is not processed any more, but it still renders as it was)

void ComicScreen::end() {
    active_ = false;
    hidesWorld_ = false;
    textShown_ = false;
}

// cTwoDSequenceApp::Process builds the printing text: a speaker's line (it starts with a name colour, 0xFF03..0xFF0C)
// is kept whole, with speed 1 (0xFEF1) and an 8-frame pause (0xFEF9) put in before the colour reset (0xFF00) that
// ends the name, so the name shows at once; anything else starts with speed 1 and loses the line before its first
// newline. (A cPrintingText starts at speed 0: all at once.)
void ComicScreen::buildText(int string) {
    text_.clear();
    printed_ = printDelay_ = printSpeed_ = 0;
    if (string < 0 || (size_t)string >= strings_.size()) return;
    const std::u16string& s = strings_[string];
    const char16_t c0 = s.empty() ? 0 : s[0];
    if (c0 >= 0xFF03 && c0 <= 0xFF0C) {
        bool tagged = false;
        for (size_t i = 0; i < s.size() && s[i]; ++i) {
            if (s[i] == 0xFF00 && !tagged) { text_ += (char16_t)0xFEF1; text_ += (char16_t)0xFEF9; }
            if (s[i] >= 0xFEF0 && s[i] <= 0xFF00) tagged = true;
            text_ += s[i];
        }
        return;
    }
    text_ += (char16_t)0xFEF1;
    size_t from = 0;
    const size_t nl = s.find(u'\n');
    if (nl != std::u16string::npos) from = nl + 1;
    for (size_t i = from; i < s.size() && s[i]; ++i) text_ += s[i];
}

// Gfx2d::cPrintingText::Update: after a pause (+9) it takes characters up to the next printable one after the first
// (+0 the position). Spaces, line ends and colour codes go with the next character. At speed (+0xA) 0 a frame takes
// everything; otherwise a printable character costs speed frames, and ! - . : ? before a space or the end five
// times that. 0xFEF0..0xFEF7 set the speed, 0xFEF8 + n pauses 8n frames.
void ComicScreen::printFrame() {
    if (printDelay_ > 0) { --printDelay_; return; }
    bool one = false;
    for (;;) {
        const char16_t c = printed_ < (int)text_.size() ? text_[printed_] : 0;
        if (!c) return;
        if (c >= 0x21 && c <= 0xFEEF) {   // printable
            if (one) return;
            if (printSpeed_) {
                printDelay_ = (uint8_t)(printSpeed_ - 1);
                const char16_t next = printed_ + 1 < (int)text_.size() ? text_[printed_ + 1] : 0;
                if (next <= 0x20 && (c == u'!' || c == u'-' || c == u'.' || c == u':' || c == u'?'))
                    printDelay_ = (uint8_t)(printSpeed_ - 1 + printSpeed_ * 4);
                one = true;
            }
        }
        if ((c & 0xFFF0) == 0xFEF0) {
            if (c >= 0xFEF8) printDelay_ = (uint8_t)(c * 8 + 0x40);
            else printSpeed_ = (uint8_t)(c + 0x10);
            one = true;
        }
        ++printed_;
    }
}

void ComicScreen::process(Sequence& seq) {
    if (!active_) return;
    fade_ = seq.fade;
    // the subtitle sprite
    for (Sequence::TextTrack& t : seq.texts) {
        if (t.flags & 1) {
            textShown_ = false;
            text_.clear();
            if (t.shown) { buildText(t.string); textShown_ = true; }
            t.flags ^= 1;
        }
        if (t.flags & 4) {
            textShown_ = t.shown != 0 && !text_.empty();
            t.flags ^= 4;
        }
    }
    // Gfx2d::cPrintingText::Update, once a frame
    if (seq.skipTextPrinting) { printed_ = (int)text_.size(); seq.skipTextPrinting = false; }
    if (textShown_ && printed_ < (int)text_.size()) printFrame();
    seq.textPrinting = textShown_ && printed_ < (int)text_.size();
    // the backgrounds
    for (Sequence::BgTrack& t : seq.bgs) {
        if (t.layer < 0 || t.layer >= 10) continue;
        Quad& q = bg_[t.layer];
        q.pos[0] = -t.pos[0] / 4096.f;
        q.pos[1] = -t.pos[1] / 4096.f;
        if (t.flags & 1) {   // cTwoDSequenceApp::ReloadBgImage
            if (t.visible) bgVisible_ |= 1u << t.layer;
            else bgVisible_ &= ~(1u << t.layer);
            q.image = t.image >= 1 && t.visible && t.image < bgImages_.size() ? &bgImages_[t.image] : nullptr;
            if (t.layer == 2) secondBg_ = true;
            else mainBg_ = t.layer;
            t.flags ^= 1;
        }
        if (t.flags & 2) t.flags &= ~2;
        if ((t.flags & 0x40) && (t.layer == 2 || t.layer == 3)) {   // the layer's clip rectangle
            uint16_t* r = rects_[t.layer - 2];
            const int x = t.rect[0] * 4, span = t.rect[2] - t.rect[0];
            const int w = span < 0 ? 1024 - x : span * 4;
            r[0] = (uint16_t)x;
            r[1] = 0x85;
            r[2] = (uint16_t)((t.rect[0] | w) == 0 ? 0x400 : w);
            r[3] = 0x200;
        }
    }
    // the sprites
    marker_ = 0xFFFF;
    bool spriteUp = false;
    for (size_t i = 0; i < seq.sprites.size() && i < 20; ++i) {
        Sequence::SpriteTrack& t = seq.sprites[i];
        Quad& q = sprite_[i];
        if (t.image != 0 && t.visible != 0) {
            spriteUp = true;
            spriteVisible_ |= 1u << i;
            q.image = t.image > 0 && (size_t)t.image < spriteImages_.size() ? &spriteImages_[t.image] : nullptr;
            if (t.behindBackground == 1) marker_ = (int)i;
        } else {
            q.image = nullptr;
            spriteVisible_ &= ~(1u << i);
        }
        q.pos[0] = t.pos[0] / 4096.f;
        q.pos[1] = t.pos[1] / 4096.f;
        q.alpha = (uint8_t)t.alpha;
        t.flags = 0;
    }
    const bool panel = spriteUp || ((bgVisible_ & 8) && mainBg_ >= 0) || ((bgVisible_ & 4) && secondBg_ && marker_ <= 19);
    hidesWorld_ = panel && fade_ != 0.f;
}

void ComicScreen::scissor(int layer) const {
    GLint vp[4];
    glGetIntegerv(GL_VIEWPORT, vp);
    const Virtual v(vp[2], vp[3]);
    const uint16_t* r = rects_[layer - 2];
    glScissor((GLint)((r[0] - v.left) * v.scale), (GLint)(r[1] * v.scale), (GLsizei)(r[2] * v.scale),
              (GLsizei)(r[3] * v.scale));
}

// cTexturedQuad::Render(top, bottom, true) and RenderOneBg: a vertical gradient over the 2 x 2 grid, then copies to
// the left and right until the strip is covered
void ComicScreen::drawBackground(const Quad& q) const {
    if (!q.image) return;
    const Image& e = *q.image;
    const unsigned tex = Assets_Texture(e.texture);
    if (!tex) return;
    const float k = q.alpha / 255.f * fade_;
    uint32_t top = scaled(top_, k), bottom = scaled(bottom_, k), mid = 0;
    for (int i = 0; i < 32; i += 8)
        mid |= (uint32_t)((((top >> i) & 0xFF) + ((bottom >> i) & 0xFF)) / 2) << i;
    const int rowH = std::clamp((int)(e.h * 2.125 * 0.5), -0x8000, 0x7FFF);
    const int colW = std::clamp((int)(e.w * 2.1333333f * 0.5f), -0x8000, 0x7FFF);
    const float topY = 768.f - (q.pos[1] + e.y) * 2.1333333f + 32.f - 70.f;
    const float baseX = e.x * 2.1333333f + q.pos[0] * 4.f;
    const int start = (int)(q.pos[0] / 256.f * 1024.f + e.x), tile = (int)(e.w * 2.1333333f);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, tex);
    auto one = [&](int offset) {
        const float x0 = baseX + offset, x1 = x0 + colW, x2 = x1 + colW;
        const float y0 = topY, y1 = y0 - rowH, y2 = y1 - rowH;
        glBegin(GL_QUADS);
        vertex(x0, y0, 0, 0, top);    vertex(x2, y0, 1, 0, top);
        vertex(x2, y1, 1, .5f, mid);  vertex(x0, y1, 0, .5f, mid);
        vertex(x0, y1, 0, .5f, mid);  vertex(x2, y1, 1, .5f, mid);
        vertex(x2, y2, 1, 1, bottom); vertex(x0, y2, 0, 1, bottom);
        glEnd();
    };
    one(0);
    if (tile > 0) {
        if (start >= 1)
            for (int k2 = 0; start + k2 > 0; k2 -= tile) one(-tile + k2);
        if (start + tile <= 0x3FF)
            for (int off = tile; start + off < 0x400; off += tile) one(off);
    }
}

// cTexturedQuad::Render(white, white, false): the sprite at 4 units per position unit, its image's offset and size
void ComicScreen::drawSprite(const Quad& q) const {
    if (!q.image) return;
    const Image& e = *q.image;
    const unsigned tex = Assets_Texture(e.texture);
    if (!tex) return;
    const uint32_t c = scaled(0xFFFFFFFFu, q.alpha / 255.f * fade_);
    const float left = q.pos[0] * 4.f + e.x, right = left + e.w;
    const float top = 768.f - (q.pos[1] * 4.f + e.y) + 64.f - 187.f, bottom = top - e.h;
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, tex);
    glBegin(GL_QUADS);
    vertex(left, top, 0, 0, c);
    vertex(right, top, 1, 0, c);
    vertex(right, bottom, 1, 1, c);
    vertex(left, bottom, 0, 1, c);
    glEnd();
}

void ComicScreen::render(int W, int H) const {
    if (!active_ || W <= 0 || H <= 0) return;
    const Virtual v(W, H);
    if (fade_ != 0.f) {
        glViewport(0, 0, W, H);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(v.left, v.left + W / v.scale, 0, 768, -1, 1);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glDisable(GL_LIGHTING);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_TEXTURE_2D);
        // the black page behind the panels (the phone's 1024-unit page; the display's wider sides are blacked too,
        // as the world is not drawn on a phone's screen around it)
        const float right = v.left + W / v.scale;
        glColor4f(0, 0, 0, fade_);
        glBegin(GL_QUADS);
        glVertex2f(v.left, 0); glVertex2f(right, 0); glVertex2f(right, 768); glVertex2f(v.left, 768);
        glEnd();
        glEnable(GL_SCISSOR_TEST);
        scissor(3);
        if ((bgVisible_ & 8) && mainBg_ >= 0) drawBackground(bg_[mainBg_]);
        for (int i = 19; i >= 0; --i) {
            if (i == marker_) {
                scissor(2);
                if ((bgVisible_ & 4) && secondBg_) drawBackground(bg_[2]);
            }
            if (spriteVisible_ >> i & 1) drawSprite(sprite_[i]);
        }
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_TEXTURE_2D);
    }
    // the subtitle: under the comic strip, wrapped to the text box (AddSpriteImpl's 678 units)
    if (textShown_ && !text_.empty()) {
        Hud_Begin(W, H);
        const float scale = v.scale * 1.25f, width = 678.f * v.scale;
        const std::u16string wrapped = Hud_Wrap16(text_, width, scale);
        // the text sprite sits above a line 1.2 font heights over the screen's bottom band (cTwoDSequenceApp::
        // Process); the whole printed text is laid out at once so the block does not move while it types
        int lines = 1;
        for (char16_t c : wrapped) lines += c == u'\n';
        const float lineH = Hud_LineHeight(scale);
        const float x = (1024.f / 2 - 678.f / 2 - v.left) * v.scale;
        const float y = std::max(645.f * v.scale + 0.25f * lineH, H - 0.9f * lineH - lines * lineH);
        int shown = 0, chars = 0;   // the printed share of the wrapped text (the wrap adds newlines)
        while (shown < (int)wrapped.size() && chars < printed_) {
            if (wrapped[shown] != u'\n' || (chars < (int)text_.size() && text_[chars] == u'\n')) ++chars;
            ++shown;
        }
        Hud_Text16(x, y, scale, 0xFFFFFFFF, wrapped.substr(0, shown));
        Hud_End();
    }
}
