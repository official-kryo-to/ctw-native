// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "hudtext.h"
#include <glad/gl.h>
#include <algorithm>

namespace HudText {

namespace {
std::vector<std::shared_ptr<Sprite>>& sprites() {   // cGlobalSpriteManager's render lists
    static std::vector<std::shared_ptr<Sprite>> list;
    return list;
}

std::shared_ptr<Sprite> addSprite(const std::shared_ptr<GameText::TextSprite>& text, int x, int y, unsigned layer) {
    auto s = std::make_shared<Sprite>();
    s->text = text;
    s->x = (int16_t)x;
    s->y = (int16_t)y;
    s->layer = (uint8_t)layer;
    sprites().push_back(s);
    return s;
}

// cPdaApp::OrthoCorrectionBeforeRender: mode 8 is SetAspectCorrectOrthoMatrix, the 1024 x 768 space widened to the
// screen's aspect about its middle (y up); mode 4 is the plain 1024 x 768
void ortho(unsigned mode, int width, int height) {
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    if (mode & 8) {
        const int vw = (int)((unsigned)(width * 3 * 256) / (unsigned)(height ? height : 1));
        int t = vw - 0x400;
        if (t < 0) t = vw - 0x3FF;
        glOrtho((float)(-(t >> 1)), (float)((t >> 1) + 0x400), 0.0, 768.0, 0.0, 1.0);
    } else {
        glOrtho(0.0, 1024.0, 0.0, 768.0, 0.0, 1.0);
    }
    glMatrixMode(GL_MODELVIEW);
}

// cSprite::RefreshSprite for a text sprite: drawn at (x, 768 - y), the shadow layer black, an outline layer black
// at the four diagonals first (at 0.9 of the alpha)
void refresh(const Sprite& s, float alpha, int width, int height) {
    if (!s.visible || !s.text) return;
    const float a = std::min(alpha, s.alpha);
    if (a < 0.08f) return;
    ortho(s.ortho, width, height);
    const float x = (float)s.x, y = (float)(0x300 - s.y);
    if (s.layer == 0x11) {
        static const float d[4][2] = {{1, 1}, {-1, 1}, {1, -1}, {-1, -1}};
        for (const auto& o : d) {
            glLoadIdentity();
            glTranslatef(x + o[0], y + o[1], 0.f);
            s.text->render(true, a * 0.9f);
        }
    }
    glLoadIdentity();
    glTranslatef(x, y, 0.f);
    s.text->render(s.layer == 0xE, a);
}
}

void Sprite::setPos(int px, int py) {
    x = (int16_t)px;
    y = (int16_t)py;
    if (shadow) shadow->setPos(px + 1, py + 1);
}

void Sprite::show(bool on) {
    for (Sprite* s = this; s; s = s->shadow.get()) s->visible = on;
}

std::shared_ptr<Sprite> AddText(const std::u16string& text, int font, int x, int y, unsigned layer, unsigned width,
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
    m.replaceUnsupported(src.data(), font);   // (cPrintingText's typed text is never asked for: +8 stays clear)
    const uint32_t size = m.textSize(src.data(), (int)width, font, firstLine, maxLines);
    if (size < 0x10000) return nullptr;
    if (justification == 1) m.justification = 0x4000;
    else if (justification == 2) m.justification = 0x8000;
    auto sprite = std::make_shared<GameText::TextSprite>((uint16_t)width, (uint16_t)((int32_t)size >> 16), font);
    sprite->alignment(m.justification);
    sprite->print(src.data(), firstLine, maxLines);
    auto s = addSprite(sprite, x, y, layer);
    // FontWantsDropShadow's fonts (not the icons) get the shadow sprite unless the text is black: one down and
    // right, on layer 0xE, in colour 6
    if ((font < 4 || font == 5) && m.colourTop != 7) {
        const uint8_t c = m.colourTop;
        m.colourTop = m.colourBottom = 6;
        s->shadow = addSprite(sprite, x + 1, y + 1, 0xE);
        m.colourTop = m.colourBottom = c;
    }
    m.justification = 0;
    return s;
}

void RemoveSprite(const std::shared_ptr<Sprite>& s) {
    if (!s) return;
    auto& list = sprites();
    for (Sprite* p = s.get(); p; p = p->shadow.get())
        list.erase(std::remove_if(list.begin(), list.end(), [p](const std::shared_ptr<Sprite>& q) { return q.get() == p; }),
                   list.end());
}

void RenderSprites(int width, int height) {
    auto& list = sprites();
    if (list.empty()) return;
    glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_TEXTURE_BIT | GL_CURRENT_BIT);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_LIGHTING);
    glDisable(GL_ALPHA_TEST);
    std::vector<const Sprite*> order;   // the layers in turn, each in the order its sprites were added
    for (const auto& s : list) order.push_back(s.get());
    std::stable_sort(order.begin(), order.end(), [](const Sprite* a, const Sprite* b) { return a->layer < b->layer; });
    for (const Sprite* s : order) refresh(*s, 1.f, width, height);
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glPopAttrib();
}

// ---- cHudString<uint> ----

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

// ---- cObjectiveText / cTextWithBox<uint> ----

void ObjectiveText::clearQueue() {
    for (auto it = queue_.begin(); it != queue_.end();) {
        if ((*it)->keep) { (*it)->keep = false; ++it; }
        else it = queue_.erase(it);
    }
    if (current_) {
        if (!current_->keep) current_->frames = 1;   // ShowNowEncountered: it goes at the next update
        current_->keep = false;
    }
}

HudString* ObjectiveText::commitToNextText() {
    if (collapsing_) return nullptr;
    if (current_) return current_.get();
    if (queue_.empty()) return nullptr;
    current_ = std::move(queue_.front());
    queue_.pop_front();
    return current_.get();
}

unsigned ObjectiveText::printJustification(int globalJustify) const {
    switch (current_->vjust) {
        case 0: return globalJustify == 2 ? 0x8000 : globalJustify != 1 ? 0x4000 : 0;
        case 2: return 0x8000;
        case 3: case 4: return 0x4000;
        default: return 0;
    }
}

void ObjectiveText::printCurrent(int globalJustify) {
    sprite_ = current_->print(printWidth_, printJustification(globalJustify));
    if (sprite_) sprite_->show(false);
}

void ObjectiveText::dropCurrent() {
    RemoveSprite(sprite_);
    sprite_.reset();
    current_.reset();
}

void ObjectiveText::updateDimensions(int& w, int& h, int globalJustify, bool paused) {
    // the string to show next: the current one unless it has run out, else the queue's head
    auto next = [this]() -> HudString* {
        if (current_ && current_->frames == 1) {
            current_.reset();
            if (queue_.empty() || collapsing_) return nullptr;
        }
        if (current_) return current_.get();
        return queue_.empty() ? nullptr : queue_.front().get();
    };
    switch (state_) {
        case 0:
            if (collapsing_) break;
            if (HudString* s = next()) {
                target_ = (float)boxHeight(*s);
                vAlign_ = s->vjust;
                hAlign_ = s->hjust;
                state_ = 1;
            }
            break;
        case 1: {   // the box grows to the string's height, then the string is printed into it
            HudString* s = collapsing_ ? nullptr : next();
            if (collapseRequested_) { state_ = 7; break; }
            if (!s || vAlign_ != s->vjust) { target_ = 0.f; state_ = 2; break; }
            const float want = (float)boxHeight(*s);
            if (target_ > want) { target_ = want; state_ = 2; break; }
            if (target_ < want) { target_ = want; break; }
            height_ += speed_;
            if (height_ < target_) break;
            height_ = target_;
            commitToNextText();
            hAlign_ = current_->hjust;
            printCurrent(globalJustify);
            if (sprite_) sprite_->show(true);   // BeginBoxFadeOn
            state_ = 3;
            break;
        }
        case 2: {   // the box shrinks to the next string's height (or away)
            HudString* s = collapsing_ ? nullptr : next();
            if (collapseRequested_) { state_ = 7; break; }
            if (s && vAlign_ == s->vjust) {
                const float want = (float)boxHeight(*s);
                target_ = want;
                if (height_ < want) { state_ = 1; break; }
                height_ -= speed_;
                if (height_ > want) break;
                height_ = want;
                commitToNextText();
                hAlign_ = current_->hjust;
                printCurrent(globalJustify);
                if (sprite_) sprite_->show(true);
                state_ = 3;
            } else {
                target_ = 0.f;
                height_ -= speed_;
                if (height_ > 0.f) break;
                height_ = 0.f;
                state_ = 0;
            }
            break;
        }
        case 3:   // BoxFadeOn: at once
            state_ = 4;
            break;
        case 4: {   // shown: the frames count down unless paused
            if (current_->frames >= 2 && !paused) --current_->frames;
            if (current_->frames == 1) {
                if (collapseRequested_) { if (sprite_) sprite_->show(false); state_ = 6; break; }
                if (!queue_.empty() && !collapsing_) {
                    HudString* s = queue_.front().get();
                    if ((float)boxHeight(*s) == height_ && s->vjust == vAlign_ && s->hjust == hAlign_) {
                        dropCurrent();   // the same box: the next string replaces this one straight away
                        current_ = std::move(queue_.front());
                        queue_.pop_front();
                        printCurrent(globalJustify);
                        if (sprite_) sprite_->show(true);
                        break;
                    }
                }
                if (sprite_) sprite_->show(false);   // BeginBoxFadeOff
                state_ = 5;
            } else if (collapseRequested_) {
                if (sprite_) sprite_->show(false);
                state_ = 6;
            }
            break;
        }
        case 5:   // BoxFadeOff: at once, the string goes
            dropCurrent();
            target_ = 0.f;
            state_ = 2;
            break;
        case 6:
            RemoveSprite(sprite_);
            sprite_.reset();
            state_ = 7;
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

// RepositionBox: the string in the middle of the box's height; low on the screen it moves 100 right
void ObjectiveText::reposition(int x, int y, int w, int h) {
    (void)w;
    if (state_ < 3 || state_ > 6 || !sprite_ || !current_) return;
    const int ty = y + ((h - current_->height + 4) >> 1);
    sprite_->setPos(ty > 0x180 ? (int)((float)x + 100.f) : x, ty);   // (HasIOSIdiotBar is false here)
}

// ---- cDynamicHudLayout ----

void Layout::switchGlobalAlignment(int h, int v) {
    pendingH_ = h;
    pendingV_ = v;
    if (state_ == 0 && (globalH_ != h || globalV_ != v)) state_ = 1;
}

void Layout::updateAndLayout(bool paused) {
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
    }
    firstUse_ = false;
    lastBottom_ = y;
}

// ---- HUDText ----

void Hud::displayObjective(const std::u16string& text, unsigned frames, bool clear) {
    if (clear) objective_.clearQueue();
    // the player's +0x118A (never set here) would centre it in the cutscene border instead
    objective_.add(std::make_unique<HudString>(text, 2, frames, 0, 0, false, objective_.printWidth()));
}

}
