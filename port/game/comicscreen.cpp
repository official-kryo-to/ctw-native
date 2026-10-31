// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "comicscreen.h"
#include "hud.h"
#include "os/os.h"
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
    removeSubtitle();
    active_ = true;
    loaded_ = true;
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

void ComicScreen::stop(bool keepApp) {
    active_ = false;   // (not processed any more)
    bgVisible_ = spriteVisible_ = 0;   // SoftReset: the images released, the text sprite removed
    removeSubtitle();
    if (!keepApp) end();   // cPda::LoadApp(the sequence's restore app)
}

void ComicScreen::end() {
    loaded_ = false;
    active_ = false;
    hidesWorld_ = false;
    removeSubtitle();
}

void ComicScreen::removeSubtitle() {
    HudText::RemoveSprite(subtitle_);
    subtitle_.reset();
    text_.clear();
}

// cTwoDSequenceApp::Process builds the printing text: with a style (a speaker's line), the string whole, and when
// it starts with a name colour (0xFF03..0xFF0C) speed 1 (0xFEF1) and an 8-frame pause (0xFEF9) put in before the
// colour reset (0xFF00) that ends the name, so the name shows at once; with none, speed 1 and the string less the
// line before its first newline. (A cPrintingText starts at speed 0: all at once.)
void ComicScreen::buildText(int string, int style) {
    text_.clear();
    if (string < 0 || (size_t)string >= strings_.size()) return;
    const std::u16string& s = strings_[string];
    if (style) {
        const char16_t c0 = s.empty() ? 0 : s[0];
        bool tagged = false;
        for (size_t i = 0; i < s.size() && s[i]; ++i) {
            if (s[i] == 0xFF00 && c0 >= 0xFF03 && c0 <= 0xFF0C && !tagged) {
                text_ += (char16_t)0xFEF1;
                text_ += (char16_t)0xFEF9;
            }
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

// cTwoDSequenceApp::Process's subtitle sprite: font 2, centred, outlined (style 0x11), at cutSceneScale (2 on a
// screen at least 1501 wide, else 1.5), typed (the save's typing setting on). A speaker's line (a style) spans the
// screen less a margin of a hundredth of its width a side; otherwise the aspect-corrected page's width, or its left
// or right half (the track's justify 0 and 2), less the margins. It sits a font height and a fifth over the comic
// strip's bottom edge on a wide screen (wider than 1.34), else centred on that edge.
void ComicScreen::createSubtitle(const Sequence::TextTrack& t) {
    buildText(t.string, t.style);
    if (text_.empty()) return;
    const unsigned W = OS_ScreenGetWidth(), H = std::max(OS_ScreenGetHeight(), 1u);
    const float aspect = (float)W / (float)H;
    float band = (aspect + -1.3333334f) / -0.66666663f + 1.f;
    band *= 133.f;
    const float bottomEdge = 768.f - band, middleEdge = band * -0.5f + 768.f;   // S8, S9
    const float cutSceneScale = W >= 0x5DD ? 2.f : 1.5f;
    const float margin = (float)(W * 10) / 1024.f;
    const float less = (float)(W * 10) * -0.0009765625f;
    auto spread = [](unsigned w, unsigned h) {   // the aspect-corrected page's extra width
        const int vw = (int)((w * 3 * 256) / h);
        int e = vw - 0x400;
        if (e < 0) e = vw - 0x3FF;
        return e;
    };
    int x;
    unsigned width;
    if (t.style) {
        x = (int)margin;
        width = (unsigned)std::fmaf(less, 2.f, 1024.f);
    } else {
        const int e = spread(W, H);
        x = (int)(margin + (float)(-(e >> 1)));
        const int full = (e >> 1) + 0x400 + (e >> 1);
        if (t.justify == 1) {
            width = (unsigned)std::fmaf(less, 2.f, (float)full);
        } else {
            const float half = (float)((full < 0 ? full + 1 : full) >> 1);
            width = (unsigned)std::fmaf(less, 2.f, half);
            if (t.justify == 2) x = (int)((float)x + (margin + half));
        }
    }
    GameText::FontManager& m = GameText::Fonts();
    const unsigned justification = m.justification;
    m.justification = 0x4000;
    m.printing = true;
    m.textScale = cutSceneScale;
    subtitle_ = HudText::AddText(text_, 2, x, 0x2A6, 0x11, width, 1, 0, 0xFFFF);
    if (subtitle_) {
        subtitle_->ortho = 8;   // (its own 0: the app's projection, the page's aspect-corrected 1024 x 768)
        const int spriteH = subtitle_->text->maxY(), fontH = m.fontHeight(2);
        const float y = aspect > 1.34f ? std::fmaf((float)fontH, -1.2f, bottomEdge - (float)spriteH)
                                       : middleEdge - (float)((unsigned)spriteH >> 1);
        subtitle_->setPos(subtitle_->x, (int)y);
        subtitle_->show(t.shown != 0);
    }
    m.textScale = 1.f;
    m.printing = false;
    m.justification = justification;
}

void ComicScreen::process(Sequence& seq) {
    if (!active_) return;
    fade_ = seq.fade;
    // the subtitle sprite
    for (Sequence::TextTrack& t : seq.texts) {
        if (t.flags & 1) {
            HudText::RemoveSprite(subtitle_);
            subtitle_.reset();
            if (t.shown) createSubtitle(t);
            t.flags ^= 1;
        }
        if (t.flags & 4) {
            if (subtitle_) subtitle_->show(t.shown != 0);
            t.flags ^= 4;
        }
    }
    // a tap finishes the typing (cTwoDSequenceApp::SkipTextPrinting); IsTextPrinting while it types
    if (seq.skipTextPrinting) {
        HudText::SkipPrinting(subtitle_, OS_ScreenGetWidth() >= 0x5DD ? 2.f : 1.5f);
        seq.skipTextPrinting = false;
    }
    seq.textPrinting = subtitle_ && subtitle_->printing;
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
    if (!loaded_ || W <= 0 || H <= 0) return;
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
}
