// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "hudtext.h"
#include "gfx/spriteset.h"
#include <cstddef>
#include <glad/gl.h>
#include <algorithm>
#include <cmath>

namespace HudText {

namespace {
constexpr int kLists = 7;   // cPda::Render draws ProcessSprites(6) .. ProcessSprites(0)

// cGlobalSpriteManager's render lists: each in the order its sprites were added (or moved to it), oldest first
std::vector<std::shared_ptr<Sprite>>& list(unsigned priority) {
    static std::vector<std::shared_ptr<Sprite>> lists[kLists];
    return lists[std::min(priority, (unsigned)kLists - 1)];
}

void unlist(Sprite* p) {
    auto& l = list(p->priority);
    l.erase(std::remove_if(l.begin(), l.end(), [p](const std::shared_ptr<Sprite>& q) { return q.get() == p; }), l.end());
}

struct Sheet {
    const std::vector<SpriteDef>* defs = nullptr;
    unsigned texture = 0;
    int shiftU = 0, shiftV = 0;   // 11 - the log2 of the game's own sheet (the PNG at twice its size)
} g_sheet;

int log2Up(int w) {
    int n = 0;
    while ((1 << n) < w) ++n;
    return n;
}

int roundQ12(int v) {   // cSprite::CalculateVerts' rounding: to the nearest, halves away from zero
    if (v >= 1) return (v >> 12) + (v >> 11 & 1);
    const int n = -v;
    return -((n >> 12) + (n >> 11 & 1));
}

uint32_t premultiplied(uint32_t rgb, unsigned a) {
    auto k = [a](unsigned v) { return ((v * a & 0xFFFF) * 0x8081u) >> 23; };
    return k(rgb & 0xFF) | k(rgb >> 8 & 0xFF) << 8 | k(rgb >> 16 & 0xFF) << 16 | a << 24;
}

// cPdaApp::OrthoCorrectionBeforeRender: mode 8 is SetAspectCorrectOrthoMatrix, the 1024 x 768 space widened to the
// screen's aspect about its middle; bit 0 keeps it on the screen's left edge, bit 1 on its right; 4 is the plain
// 1024 x 768. y is up.
void ortho(unsigned mode, int width, int height) {
    const int vw = (int)((unsigned)(width * 3 * 256) / (unsigned)(height ? height : 1));
    int t = vw - 0x400;
    if (t < 0) t = vw - 0x3FF;
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    if (mode & 2) glOrtho((double)(-(t >> 1) * 2), 1024.0, 0.0, 768.0, 0.0, 1.0);
    else if (mode & 1) glOrtho(0.0, (double)((t >> 1) * 2 + 0x400), 0.0, 768.0, 0.0, 1.0);
    else if (mode & 8) glOrtho((double)(-(t >> 1)), (double)((t >> 1) + 0x400), 0.0, 768.0, 0.0, 1.0);
    else glOrtho(0.0, 1024.0, 0.0, 768.0, 0.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

// cSprite::CalculateVerts (unrotated) and RefreshSprite's quad: two triangles, the colour white at the alpha
void refreshImage(const Sprite& s, float alpha) {
    const SpriteDef& d = *s.image;
    const unsigned scaled = (unsigned)(std::min(alpha, s.alpha) * 255.f);
    const unsigned a = scaled < 0xFF ? scaled : 0xFF;
    if (a < 0xF || !g_sheet.texture) return;
    const int cx = (0x1000 * s.scaleX) >> 8, cy = (0x1000 * s.scaleY) >> 8;
    const int w1 = d.w + 1, h1 = d.h + 1;
    const int left = roundQ12(-d.offX * cx) + s.x, right = roundQ12((w1 - d.offX) * cx) + s.x;
    const int base = 0x300 - s.y;
    const int top = roundQ12(d.offY * cy) + base, bottom = roundQ12((d.offY - h1) * cy) + base;
    const int u0 = d.x << g_sheet.shiftU, u1 = u0 + (w1 << g_sheet.shiftU);
    const int v0 = d.y << g_sheet.shiftV, v1 = v0 + (h1 << g_sheet.shiftV);
    const float k = 1.f / 2048.f;
    const float U0 = (int16_t)(u0 + s.uvAdd[0]) * k, U1 = (int16_t)(u1 + s.uvAdd[2]) * k;
    const float V0 = (int16_t)(v0 + s.uvAdd[1]) * k, V1 = (int16_t)(v1 + s.uvAdd[3]) * k;
    const uint32_t c = premultiplied(0xFFFFFF, a);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, g_sheet.texture);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4ub((GLubyte)c, (GLubyte)(c >> 8), (GLubyte)(c >> 16), (GLubyte)(c >> 24));
    const float X0 = (float)(int16_t)left, X1 = (float)(int16_t)right, Y0 = (float)(int16_t)top,
                Y1 = (float)(int16_t)bottom;
    glBegin(GL_TRIANGLES);
    glTexCoord2f(U0, V0); glVertex2f(X0, Y0);
    glTexCoord2f(U1, V0); glVertex2f(X1, Y0);
    glTexCoord2f(U0, V1); glVertex2f(X0, Y1);
    glTexCoord2f(U1, V0); glVertex2f(X1, Y0);
    glTexCoord2f(U1, V1); glVertex2f(X1, Y1);
    glTexCoord2f(U0, V1); glVertex2f(X0, Y1);
    glEnd();
    glColor4f(1, 1, 1, 1);
}

// cSprite::RefreshSprite for a text sprite: drawn at (x, 768 - y), the shadow style black, the outline style black
// at the four diagonals first (at 0.9 of the alpha)
void refreshText(const Sprite& s, float alpha) {
    const float a = std::min(alpha, s.alpha);
    if (a < 0.08f) return;
    const float x = (float)s.x, y = (float)(0x300 - s.y);
    if (s.style == 0x11) {
        static const float d[4][2] = {{1, 1}, {-1, 1}, {1, -1}, {-1, -1}};
        for (const auto& o : d) {
            glLoadIdentity();
            glTranslatef(x + o[0], y + o[1], 0.f);
            s.text->render(true, a * 0.9f);
        }
    }
    glLoadIdentity();
    glTranslatef(x, y, 0.f);
    s.text->render(s.style == 0xE, a);
}

std::shared_ptr<Sprite> addSprite(const std::shared_ptr<GameText::TextSprite>& text, int x, int y, unsigned style) {
    auto s = std::make_shared<Sprite>();
    s->text = text;
    s->x = (int16_t)x;
    s->y = (int16_t)y;
    s->style = (uint8_t)style;
    list(0).push_back(s);
    return s;
}
}

void SetAlwaysLoaded(const std::vector<SpriteDef>* defs, unsigned texture, int pngWidth, int pngHeight) {
    g_sheet.defs = defs;
    g_sheet.texture = texture;
    g_sheet.shiftU = 11 - log2Up(pngWidth / 2);
    g_sheet.shiftV = 11 - log2Up(pngHeight / 2);
}

const SpriteDef* AlwaysLoaded(int index) {
    if (!g_sheet.defs || index < 0 || index >= (int)g_sheet.defs->size()) return nullptr;
    return &(*g_sheet.defs)[(size_t)index];
}

void Sprite::setPos(int px, int py) {
    x = (int16_t)px;
    y = (int16_t)py;
    if (shadow) shadow->setPos(px + 1, py + 1);
}

void Sprite::show(bool on) {
    for (Sprite* s = this; s; s = s->shadow.get()) s->visible = on;
}

void Sprite::setScale(int32_t sx, int32_t sy) {
    scaleX = (uint16_t)((uint32_t)sx >> 4);
    scaleY = (uint16_t)((uint32_t)sy >> 4);
    if (shadow) shadow->setScale(sx, sy);
}

void Sprite::setImage(const SpriteDef* d) {
    image = d;
    if (shadow) shadow->setImage(d);
}

int Sprite::width() const {
    if (image) return image->w;
    return 0;
}

int Sprite::height() const { return image ? image->h : 0; }

std::shared_ptr<Sprite> AddText(const std::u16string& text, int font, int x, int y, unsigned style, unsigned width,
                                int justification, uint16_t firstLine, uint16_t maxLines) {
    GameText::FontManager& m = GameText::Fonts();
    if (!m.ok()) return nullptr;
    if (width > 0x3F8) x += (int)(width - 0x3F8) >> 1;   // at most 1016 wide, centred on the asked width
    width = std::min(width, 0x3F8u);
    font = m.usableFont(text.c_str(), font);
    std::vector<char16_t> src(0x200, 0);
    if (font == 3) GameText::MakeUpperCase(src.data(), text.c_str(), 0x200);
    else {   // UnicodeStrcpy into the 512-character buffer
        const size_t n = std::min(text.size(), (size_t)0x1FF);
        std::copy(text.begin(), text.begin() + (ptrdiff_t)n, src.begin());
    }
    // typed (a speed or pause code in it, while the font manager's +8 asks for typing): a cPrintingText, no shadow
    const bool typed = m.replaceUnsupported(src.data(), font);
    const uint32_t size = m.textSize(src.data(), (int)width, font, firstLine, maxLines);
    if (size < 0x10000) return nullptr;
    if (justification == 1) m.justification = 0x4000;
    else if (justification == 2) m.justification = 0x8000;
    auto sprite = std::make_shared<GameText::TextSprite>((uint16_t)width, (uint16_t)((int32_t)size >> 16), font);
    sprite->alignment(m.justification);
    if (typed) {
        auto s = addSprite(sprite, x, y, style);
        s->printing = std::make_unique<PrintingText>();
        s->printing->text.assign(src.data());
        s->printing->justification = sprite->width() & 0xC000;
        sprite->alignment(0);
        m.justification = 0;
        return s;
    }
    sprite->print(src.data(), firstLine, maxLines);
    auto s = addSprite(sprite, x, y, style);
    // FontWantsDropShadow's fonts (not the icons) get the shadow sprite unless the text is black: one down and
    // right, style 0xE, in colour 6, listed just before the string so that it is drawn under it
    if ((font < 4 || font == 5) && m.colourTop != 7) {
        const uint8_t c = m.colourTop;
        m.colourTop = m.colourBottom = 6;
        auto shadow = std::make_shared<Sprite>();
        shadow->text = sprite;
        shadow->x = (int16_t)(x + 1);
        shadow->y = (int16_t)(y + 1);
        shadow->style = 0xE;
        auto& l = list(0);
        l.insert(l.end() - 1, shadow);
        s->shadow = shadow;
        m.colourTop = m.colourBottom = c;
    }
    m.justification = 0;
    return s;
}

std::shared_ptr<Sprite> AddImage(const SpriteDef* image) {
    auto s = std::make_shared<Sprite>();
    s->image = image;
    list(0).push_back(s);
    return s;
}

bool PrintingText::update(GameText::TextSprite& s) {
    if (delay) { --delay; return false; }
    if (s.endY() >= s.maxY()) return true;
    GameText::FontManager& m = GameText::Fonts();
    if ((int)s.endY() != lineY) {   // a new line: where it starts, and where it ends
        const char16_t* end = nullptr;
        const int width = s.width() & 0x3FFF;
        const int w = m.lineEnd(text.c_str() + pos, width, s.font(), &end);
        const int off = (width - w) >> (justification == 0x4000 ? 1 : 0);
        lineY = (uint8_t)s.endY();
        s.setX((int16_t)(justification ? off : 0));
        lineEnd = (uint16_t)(end - text.c_str());
    }
    // the characters up to the next printable one after the first (spaces, line ends and codes go with it)
    std::u16string chunk;
    bool got = false;
    for (;;) {
        const char16_t c = pos < text.size() ? text[pos] : 0;
        const bool printable = c && (unsigned)(c - 0x21) <= 0xFECEu;
        if (printable || !c) {
            if (got) break;
            if (speed) {
                delay = (uint8_t)(speed - 1);
                const char16_t next = pos + 1 < text.size() ? text[pos + 1] : 0;
                got = true;
                if (next <= 0x20 && c <= 0x3F && (0x8400600200000000ull >> c & 1))   // ! - . : ? before a space
                    delay = (uint8_t)(speed - 1 + speed * 4);
            }
            if (!c) return true;
        }
        if ((c & 0xFFF0) == 0xFEF0) {   // a pause or a speed: it counts as the one
            if (c >= 0xFEF8) delay = (uint8_t)(c * 8 + 0x40);
            else speed = (uint8_t)(c + 0x10);
            got = true;
            ++pos;
            continue;
        }
        chunk += c;
        ++pos;
    }
    s.print(chunk.c_str(), 0, 0xFFFF);
    if (pos >= lineEnd && s.endY() == lineY) s.newLine();
    return false;
}

void UpdatePrinting(float scale) {
    GameText::FontManager& m = GameText::Fonts();
    std::vector<std::shared_ptr<Sprite>> done;
    for (unsigned p = 0; p < kLists; ++p)
        for (const auto& s : list(p)) {
            if (!s->printing || !s->text || !s->visible) continue;   // (RefreshSprite types only shown sprites)
            m.textScale = scale;
            const bool finished = s->printing->update(*s->text);
            m.textScale = 1.f;
            if (finished) done.push_back(s);
        }
    for (const auto& s : done) SkipPrinting(s, scale);
}

void SkipPrinting(const std::shared_ptr<Sprite>& s, float scale) {
    if (!s || !s->printing || !s->text) return;
    GameText::FontManager& m = GameText::Fonts();
    s->text->alignment(s->printing->justification);
    m.textScale = scale;
    s->text->reprint(s->printing->text.c_str(), 0, 0xFFFF);
    m.textScale = 1.f;
    s->printing.reset();
}

void SetPriority(const std::shared_ptr<Sprite>& s, unsigned priority) {
    if (!s || s->priority == priority) return;
    if (s->shadow) SetPriority(s->shadow, priority);   // ChangeRenderList: the newest in its new list
    unlist(s.get());
    s->priority = (uint8_t)priority;
    list(priority).push_back(s);
}

void RemoveSprite(const std::shared_ptr<Sprite>& s) {
    for (Sprite* p = s.get(); p; p = p->shadow.get()) unlist(p);
}

void RenderSprites(int width, int height) {
    glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_TEXTURE_BIT | GL_CURRENT_BIT);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_LIGHTING);
    glDisable(GL_ALPHA_TEST);
    for (int p = kLists - 1; p >= 0; --p) {
        const std::vector<std::shared_ptr<Sprite>> sprites = list((unsigned)p);   // (a copy: drawing changes nothing)
        for (const auto& s : sprites) {
            if (!s->visible) continue;
            ortho(s->ortho, width, height);
            if (s->text) refreshText(*s, 1.f);
            else if (s->image) refreshImage(*s, 1.f);
        }
    }
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glPopAttrib();
}

// ---- cHudString ----

HudString::HudString(const std::u16string& s, int f, unsigned n, int h, int v, bool flg, int printWidth)
    : text(s), font(f), frames(n), total(n), hjust(h), vjust(v), flag(flg) {
    GameText::FontManager& m = GameText::Fonts();
    m.textScale = 1.5f;   // SetPhoneTextScale(1.5, 1.5)
    height = m.ok() ? (int32_t)m.textSize(text.c_str(), printWidth, font, 0, 0xFFFF) >> 16 : 0;
    m.textScale = 1.f;    // RestorePhoneTextScale(1.0)
}

std::shared_ptr<Sprite> HudString::print(int width, unsigned justification) const {
    GameText::FontManager& m = GameText::Fonts();
    m.justification = justification;
    m.textScale = 1.5f;
    auto s = AddText(text, font, 0, 0, 0x10, (unsigned)width, 0, 0, 0xFFFF);
    if (s) {
        s->ortho = 8;
        if (s->shadow) s->shadow->ortho = 8;
    }
    m.textScale = 1.f;
    m.justification = 0;
    return s;
}

// ---- cTextWithBox ----

void TextWithBox::add(std::unique_ptr<HudString> s, bool showNow) {
    if (showNow) {   // at the head, and the string showing goes at the next update (ShowNowEncountered)
        queue_.push_front(std::move(s));
        if (current_) current_->frames = 1;
    } else {
        queue_.push_back(std::move(s));
    }
}

void TextWithBox::clearQueue() {
    for (auto it = queue_.begin(); it != queue_.end();) {
        if ((*it)->keep) { (*it)->keep = false; ++it; }
        else it = queue_.erase(it);
    }
    if (current_) {
        if (!current_->keep) current_->frames = 1;   // ShowNowEncountered: it goes at the next update
        current_->keep = false;
    }
}

HudString* TextWithBox::findInQueue(const std::u16string& text) {
    if (current_ && current_->text == text) return current_.get();
    for (auto& s : queue_) if (s->text == text) return s.get();
    return nullptr;
}

void TextWithBox::commitToNextText() {
    if (collapsing_ || current_ || queue_.empty()) return;
    current_ = std::move(queue_.front());
    queue_.pop_front();
}

void TextWithBox::printCurrent(int globalJustify) {
    sprite_ = current_->print(printWidth_, printJustification(globalJustify, *current_));
}

void TextWithBox::updateDimensions(int& w, int& h, int globalJustify, bool paused) {
    // the string to show next: the current one unless it has run out, else the queue's head
    auto next = [this]() -> HudString* {
        if (current_ && current_->frames == 1) {
            current_.reset();
            if (queue_.empty() || collapsing_) return nullptr;
        }
        if (current_) return current_.get();
        return queue_.empty() ? nullptr : queue_.front().get();
    };
    auto commitAndPrint = [this, globalJustify] {
        commitToNextText();
        hAlign_ = current_->hjust;
        printCurrent(globalJustify);
        if (sprite_) sprite_->show(false);
        beginBoxFadeOn(*current_);
        state_ = 3;
    };
    switch (state_) {
        case 0:
            if (collapsing_) break;
            if (HudString* s = next()) {
                target_ = (float)computeBoxHeight(*s);
                vAlign_ = s->vjust;
                hAlign_ = s->hjust;
                state_ = 1;
            }
            break;
        case 1: {   // the box grows to the string's height, then the string is printed into it
            HudString* s = collapsing_ ? nullptr : next();
            if (collapseRequested_) { state_ = 7; break; }
            if (!s || vAlign_ != s->vjust) { target_ = 0.f; state_ = 2; break; }
            const float want = (float)computeBoxHeight(*s);
            if (target_ > want) { target_ = want; state_ = 2; break; }
            if (target_ < want) { target_ = want; break; }
            height_ += speed_;
            if (height_ < target_) break;
            height_ = target_;
            commitAndPrint();
            break;
        }
        case 2: {   // the box shrinks to the next string's height (or away)
            HudString* s = collapsing_ ? nullptr : next();
            if (collapseRequested_) { state_ = 7; break; }
            if (s && vAlign_ == s->vjust) {
                const float want = (float)computeBoxHeight(*s);
                target_ = want;
                if (height_ < want) { state_ = 1; break; }
                height_ -= speed_;
                if (height_ > want) break;
                height_ = want;
                commitAndPrint();
            } else {
                target_ = 0.f;
                height_ -= speed_;
                if (height_ > 0.f) break;
                height_ = 0.f;
                state_ = 0;
            }
            break;
        }
        case 3:
            if (boxFadeOn()) { endBoxFadeOn(); state_ = 4; }
            break;
        case 4: {   // shown: the frames count down unless paused
            if (current_->frames >= 2 && !paused) --current_->frames;
            if (current_->frames == 1) {
                if (collapseRequested_) { beginBoxFadeOff(); state_ = 6; break; }
                if (!queue_.empty() && !collapsing_) {
                    HudString* s = queue_.front().get();
                    if ((float)computeBoxHeight(*s) == height_ && s->vjust == vAlign_ && s->hjust == hAlign_) {
                        RemoveSprite(sprite_);   // the same box: the next string replaces this one straight away
                        current_ = std::move(queue_.front());
                        queue_.pop_front();
                        printCurrent(globalJustify);
                        if (sprite_) sprite_->show(true);
                        break;
                    }
                }
                beginBoxFadeOff();
                state_ = 5;
            } else if (collapseRequested_) {
                beginBoxFadeOff();
                state_ = 6;
            }
            break;
        }
        case 5:   // faded off: the string goes
            if (boxFadeOff()) {
                endBoxFadeOff();
                RemoveSprite(sprite_);
                sprite_.reset();
                current_.reset();
                target_ = 0.f;
                state_ = 2;
            }
            break;
        case 6:
            if (boxFadeOff()) {
                endBoxFadeOff();
                RemoveSprite(sprite_);
                sprite_.reset();
                state_ = 7;
            }
            break;
        case 7:   // collapsed for a change of global alignment
            target_ = 0.f;
            height_ -= speed_;
            if (height_ > 0.f) break;
            height_ = 0.f;
            collapseRequested_ = false;
            state_ = 0;
            break;
        default: break;
    }
    w = width_;
    h = (int)height_;
}

void TextWithBox::reposition(int x, int y, int w, int h) {
    if (state_ < 3 || state_ > 6 || !current_) return;
    repositionBox(*current_, x, y, w, h);
}

// ---- cObjectiveText ----

unsigned ObjectiveText::printJustification(int globalJustify, const HudString&) {
    switch (current_->vjust) {
        case 0: return globalJustify == 2 ? 0x8000 : globalJustify != 1 ? 0x4000 : 0;
        case 2: return 0x8000;
        case 3: case 4: return 0x4000;
        default: return 0;
    }
}

// RepositionBox: the string in the middle of the box's height; low on the screen it moves 100 right
void ObjectiveText::repositionBox(HudString& s, int x, int y, int w, int h) {
    (void)w;
    if (!sprite_) return;
    const int ty = y + ((h - s.height + 4) >> 1);
    sprite_->setPos(ty > 0x180 ? (int)((float)x + 100.f) : x, ty);   // (HasIOSIdiotBar is false here)
}

// ---- cHelpText_DynamicHudElement ----

int HelpText::middleSections(const HudString& s) {
    const int n = s.height / 16 - 1;
    return n > 1 ? n : 1;
}

void HelpText::initializeForFirstUse() {
    static const int16_t kInset[4] = {0, 4, 0, -4};   // xmmword_46A680: the pieces' v half a texel in
    auto piece = [](int index, bool inset) {
        auto s = AddImage(AlwaysLoaded(index));
        SetPriority(s, 1);
        s->show(false);
        if (inset) std::copy(kInset, kInset + 4, s->uvAdd);
        return s;
    };
    bottom_ = piece(0x25, true);
    for (auto& m : middle_) m = piece(0x26, true);
    top_ = piece(0x27, true);
    icon_ = piece(0x4E, false);
}

void HelpText::updateDimensions(int& w, int& h, int globalJustify, bool paused) {
    TextWithBox::updateDimensions(w, h, globalJustify, paused);
    touched_ = false;
}

void HelpText::heedTouchEvent() {
    if (current_ && current_->callback) { current_->callback(); return; }
    touched_ = true;
    if (current_) current_->frames = 1;   // ShowNowEncountered
}

void HelpText::showBoxSprites(bool on) {
    if (bottom_) bottom_->show(on);
    for (int i = 0; i < middles_ && i < 15; ++i) middle_[i]->show(on);
    if (top_) top_->show(on);
    if (icon_) icon_->show(on);
}

void HelpText::beginBoxFadeOn(HudString& s) {
    boxAlpha_ = 0.05f;
    middles_ = middleSections(*current_);
    if (icon_) icon_->setImage(AlwaysLoaded((int)s.icon));
    showBoxSprites(true);   // (SetAlphaModifiable(!s.iconLeft) only matters to the HUD's fade)
    if (playHudSfx) playHudSfx(0x1E6, 0x7F);
}

bool HelpText::boxFadeOn() {
    const float a = boxAlpha_ * 1.5f;
    boxAlpha_ = a > 1.f ? 1.f : a;
    return a > 1.f;
}

bool HelpText::boxFadeOff() {
    const float a = boxAlpha_ / 1.5f;
    boxAlpha_ = a < 0.05f ? 0.05f : a;
    return a < 0.05f;
}

void HelpText::repositionBox(HudString& s, int x, int y, int w, int h) {
    (void)h;
    const int boxH = computeBoxHeight(*current_);
    // the pieces open by their height: the box's alpha as their y scale
    const int open = (int)std::fmaf(boxAlpha_, 4096.f, boxAlpha_ < 0.f ? -0.5f : 0.5f);
    int py = y + 0x10;
    top_->setPos(x, py);
    top_->setScale(0x1000, open);
    for (int i = 0; i < middles_ && i < 15; ++i) {
        py += 0x10;
        middle_[i]->setPos(x, py);
        middle_[i]->setScale(0x1000, open);
    }
    bottom_->setPos(x, py + 0x10);
    bottom_->setScale(0x1000, open);
    const int ty = y + ((boxH - current_->height + 2) >> 1);
    const int mid = y + (boxH >> 1);
    if (s.iconLeft) {   // the icon left of the text, centred on its middle
        if (sprite_) { sprite_->setPos(x + 0x30, ty); SetPriority(sprite_, 1); }
        icon_->setPos(x - (icon_->width() >> 1) + 0x1C, mid - (int)((unsigned)icon_->height() >> 1));
    } else {
        if (sprite_) { sprite_->setPos(x + 0xC, ty); SetPriority(sprite_, 1); }
        icon_->setPos(x + 0x23B, mid);
    }
    icon_->setScale(open, open);
    // near the screen's sides the pieces keep to that edge as the aspect changes
    unsigned mode = w + x > 0x39C ? 0x802 : 8;
    if (x < 100) mode = 0x801;
    auto setMode = [mode](const std::shared_ptr<Sprite>& p) {
        for (Sprite* q = p.get(); q; q = q->shadow.get()) q->ortho = mode;
    };
    setMode(top_);
    setMode(bottom_);
    for (int i = 0; i < middles_ && i < 15; ++i) setMode(middle_[i]);
    setMode(icon_);
    setMode(sprite_);
}

// ---- cDynamicHudLayout ----

void Layout::switchGlobalAlignment(int h, int v) {
    pendingH_ = h;
    pendingV_ = v;
    if (state_ == 0 && (globalH_ != h || globalV_ != v)) state_ = 1;
}

void ScreenToHud(int sx, int sy, int width, int height, int out[2]) {
    const int vw = (int)((unsigned)(width * 3 * 256) / (unsigned)(height ? height : 1));
    int t = vw - 0x400;
    if (t < 0) t = vw - 0x3FF;
    const int left = -(t >> 1), span = 0x400 + 2 * (t >> 1);
    out[0] = left + (int)((int64_t)sx * span / (width ? width : 1));
    out[1] = (int)((int64_t)sy * 768 / (height ? height : 1));
}

void Layout::updateAndLayout(bool paused, const int* touch) {
    if (state_ == 1) {   // collapse everything for the new alignment
        for (Element* e : elements_) e->beginCollapse();
        state_ = 2;
    } else if (state_ == 2) {
        bool all = true;
        for (Element* e : elements_) all = e->hasCollapsed() && all;
        if (all) {
            for (Element* e : elements_) e->endCollapse();
            globalH_ = pendingH_;
            globalV_ = pendingV_;
            state_ = 0;
        }
    }
    int total = 0;   // the heights of the elements without a vertical alignment of their own
    for (size_t i = 0; i < elements_.size(); ++i) {
        Element* e = elements_[i];
        Dims& d = dims_[i];
        if (firstUse_) e->initializeForFirstUse();
        e->updateDimensions(d.w, d.h, globalH_, paused);
        switch (e->horizontalAlignment()) {
            case 1: d.x = e->leftRightAlignBorder(); break;
            case 2: d.x = 0x400 - d.w - e->leftRightAlignBorder(); break;
            case 3: d.x = (0x400 - d.w) >> 1; break;
            default:   // the global alignment's
                switch (globalH_) {
                    case 0: d.x = e->xCoordWhenNotAligned(); break;
                    case 1: d.x = e->leftRightAlignBorder(); break;
                    case 2: d.x = 0x3EE - d.w - e->leftRightAlignBorder(); break;
                    case 3: d.x = (0x400 - d.w) >> 1; break;
                    default: break;
                }
                break;
        }
        if (e->verticalAlignment() == 0) total += d.h;
    }
    int y = 0;
    bool upward = false;
    switch (globalV_) {
        case 0: y = top_; break;
        case 1: y = total + 8; upward = true; break;   // (lower by the top when the wanted stars show: not ported)
        case 2: y = 0x300 - total; break;
        case 3: { const int t = 0x300 - total; y = (t < 0 ? t + 1 : t) >> 1; break; }
        default: break;
    }
    for (size_t i = 0; i < elements_.size(); ++i) {
        Element* e = elements_[i];
        Dims& d = dims_[i];
        switch (e->verticalAlignment()) {
            case 1: d.y = top_; break;
            case 2: d.y = 0x300 - d.h; break;
            case 3: d.y = (0x300 - d.h) >> 1; break;
            case 4: d.y = ((0x85 - d.h) >> 1) + 0x27B; break;   // in the cutscene border at the bottom
            default:
                if (upward) { y -= d.h; d.y = y; }
                else { d.y = y; y += d.h; }
                break;
        }
        e->reposition(d.x, d.y, d.w, d.h);
        // PerfomTouchScreenCheck: a press this frame within the element's box
        if (e->areTouchesToBeHeeded() && !paused && touch && touch[0] >= d.x && touch[1] >= d.y &&
            d.x + d.w > touch[0] && d.y + d.h > touch[1])
            e->heedTouchEvent();
    }
    firstUse_ = false;
    lastBottom_ = y;
}

// ---- cScriptText ----

void ScriptText::init(const std::u16string& s, int px, int py, unsigned n, int f, bool flash, bool centre) {
    text = s;
    frames = total = (uint16_t)n;
    x = (int16_t)px;
    y = (int16_t)py;
    font = f;
    flashing = flash;
    centred = centre;
    GameText::FontManager& m = GameText::Fonts();
    if (!text.empty() && m.ok()) {
        const uint32_t size = m.textSize(text.c_str(), width, font, 0, 0xFFFF);
        width = (uint16_t)size;
        height = (uint8_t)(size >> 16);
    } else {
        width = 0;
        height = 0;
    }
}

// cScriptText::Print: the sprite at its place, centred in its width if asked, style 0, in the 1024 x 768 HUD
// (cScriptText::bUseZonesHack is false)
void ScriptText::print() {
    if (text.empty() || sprite) return;
    GameText::FontManager& m = GameText::Fonts();
    if (centred) m.justification = 0x4000;
    sprite = AddText(text, font, x, y, 0x10, width, 0, 0, 0xFFFF);
    if (sprite) {
        for (Sprite* q = sprite.get(); q; q = q->shadow.get()) q->ortho = 8;
        sprite->style = 0;   // (SetFlashing(flashing) at speed 10: no text here flashes)
    }
    if (centred) m.justification = 0;
}

void ScriptText::setPosition(int px, int py) {
    x = (int16_t)px;
    y = (int16_t)py;
    if (sprite) sprite->setPos(x, y);
}

void ScriptText::remove() {
    RemoveSprite(sprite);
    sprite.reset();
}

void ScriptTextQueue::add(std::unique_ptr<ScriptText> t, bool now) {
    if (!now && !list_.empty()) {
        list_.push_back(std::move(t));
        return;
    }
    if (!list_.empty()) list_.front()->remove();
    list_.push_front(std::move(t));
    list_.front()->print();
}

void ScriptTextQueue::update(bool paused) {
    for (auto it = list_.begin(); it != list_.end();) {
        ScriptText& t = **it;
        if (!paused) --t.frames;
        if (t.total && !t.frames) {   // run out: it goes, and the next is printed
            t.remove();
            it = list_.erase(it);
            if (it != list_.end()) (*it)->print();
            continue;
        }
        ++it;
    }
}

void ScriptTextQueue::clear() {
    for (auto& t : list_) t->remove();
    list_.clear();
}

// ---- HUDText ----

void Hud::displayBustedWasted(const std::u16string& text, int font, unsigned frames, bool now, bool clearObjectives) {
    GameText::FontManager& m = GameText::Fonts();
    m.textScale = 1.5f;   // SetPhoneTextScale(1.5, 1.5)
    if (clearObjectives) objective_.clearQueue();
    if (m.ok()) {
        const uint32_t size = m.textSize(text.c_str(), 0x3F8, font, 0, 0xFFFF);
        const int h = (int32_t)size >> 16, w = (int16_t)size;
        int dy = 0x300 - h;
        if (dy < 0) dy = 0x301 - h;
        int y = dy >> 1;
        if (ScriptText* wl = winLose_.top()) {   // a win / lose line up: it moves to the middle, this just above
            wl->setPosition(wl->x, 0x180);
            wl->remove();
            wl->print();
            y = 0x180 - h;
        }
        int dx = 0x400 - w;
        if (dx < 0) dx = 0x401 - w;
        auto t = std::make_unique<ScriptText>();
        t->width = 0x400;
        t->init(text, dx >> 1, y, frames, font, false, false);
        bustedWasted_.add(std::move(t), now);
    }
    m.textScale = 1.f;    // SetPhoneTextScale(1.0)
}

void Hud::displayWinLose(const std::u16string& text, int font, unsigned frames, bool now, bool clearObjectives) {
    GameText::FontManager& m = GameText::Fonts();
    m.textScale = 1.5f;   // SetPhoneTextScale(1.5, 1.5)
    if (clearObjectives) objective_.clearQueue();
    if (m.ok()) {
        const uint32_t size = m.textSize(text.c_str(), 0x3F8, font, 0, 0xFFFF);
        const int h = (int32_t)size >> 16, w = (int16_t)size;
        int dy = 0x300 - h;
        if (dy < 0) dy = 0x301 - h;
        int dx = 0x400 - w;
        if (dx < 0) dx = 0x401 - w;
        int y = dy >> 1;
        if (ScriptText* b = bustedWasted_.top()) {   // the busted / wasted line goes just above the middle
            b->setPosition(b->x, 0x180 - b->height);
            b->remove();
            b->print();
            y = 0x180;
        }
        auto t = std::make_unique<ScriptText>();
        t->width = 0x400;
        t->init(text, dx >> 1, y, frames, font, false, true);
        winLose_.add(std::move(t), now);
    }
    m.textScale = 1.f;    // RestorePhoneTextScale(1.0)
}

void Hud::displayObjective(const std::u16string& text, unsigned frames, bool clear, bool inBorder,
                           bool inCutscene) {
    if (clear) objective_.clearQueue();
    const int h = inBorder ? 3 : 0, v = inBorder ? 4 : 0;
    objective_.add(std::make_unique<HudString>(text, 2, frames, h, v, inCutscene, objective_.printWidth()));
}

void Hud::displayHelp(const std::u16string& text, unsigned frames, bool showNow, bool clear,
                      std::function<void()> callback) {
    // (with the HUD app running, SwitchGlobalAlignment(0, 0): a change only after the global StartCutscene, which
    // the missions here never make)
    if (clear) {   // the same text (its first queued copy, and the string showing) is kept through the clear
        help_.markForNotClear(text);
        help_.clearQueue();
    }
    if (HudString* s = help_.findInQueue(text)) {   // already there: its frames, icon and callback anew
        s->frames = frames;
        s->icon = 0x4E;
        s->iconLeft = false;
        s->callback = std::move(callback);
        return;
    }
    auto s = std::make_unique<HudString>(text, 2, frames, 0, 0, false, help_.printWidth());
    s->callback = std::move(callback);
    help_.add(std::move(s), showNow);
}

void Hud::clearHelp() { help_.clearQueue(); }

void Hud::deleteCurrentHelp() {
    if (HudString* s = help_.current()) s->frames = 1;
    else help_.dropHead();
}

const std::u16string* Hud::currentHelp() const {
    if (const HudString* s = help_.current()) return &s->text;
    if (const HudString* s = help_.head()) return &s->text;
    return nullptr;
}

void Hud::resetCurrentHelpTimer() {
    if (HudString* s = help_.current()) s->frames = s->total;
}

}
