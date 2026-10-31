// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// HUDText's on-screen strings: the Gfx2d text sprites (cGlobalSpriteManager::AddSpriteImpl's text path and
// cSprite::RefreshSprite), cHudString, the objective line g_ObjectiveText (cObjectiveText on cTextWithBox<uint>) and
// g_DynamicHud (cDynamicHudLayout), which lays its elements out in the 1024 x 768 HUD space every frame.
#pragma once
#include "gfx/gametext.h"
#include <cstdint>
#include <list>
#include <memory>
#include <string>
#include <vector>

namespace HudText {

// Gfx2d::cSprite carrying a cTextSprite
struct Sprite {
    std::shared_ptr<GameText::TextSprite> text;
    int16_t x = 0, y = 0;                 // +0x28, +0x2A: the HUD space, y down
    bool visible = true;                  // +0x18 (ShowSprite)
    uint8_t layer = 0x10;                 // +0x1F: 0xE draws the black shadow, 0x11 an outline
    unsigned ortho = 0;                   // +0x9C: cPdaApp::OrthoCorrectionBeforeRender's mode
    float alpha = 1.f;                    // +0x64
    std::shared_ptr<Sprite> shadow;       // +0x10, moved and shown with it
    void setPos(int px, int py);          // SetSpritePos
    void show(bool on);                   // ShowSprite
};

// cGlobalSpriteManager::AddSpriteImpl(text, font, x, y, restriction, layer, width, justification, first, max, ..):
// nothing when the text has no height
std::shared_ptr<Sprite> AddText(const std::u16string& text, int font, int x, int y, unsigned layer, unsigned width,
                                int justification, uint16_t firstLine, uint16_t maxLines);
void RemoveSprite(const std::shared_ptr<Sprite>& s);   // RemoveSprite(s, true): it and its shadow
void RenderSprites(int width, int height);           // cGlobalSpriteManager::ProcessSprites over the HUD layers

// cHudString<uint>
struct HudString {
    std::u16string text;
    int font = 2;
    unsigned frames = 0, total = 0;       // +0xC counts down to 1 (0 stays), +0x10
    int hjust = 0, vjust = 0;             // +0x14, +0x18 (eDynamicHudJustify)
    int height = 0;                       // +0x1C: TextSize's at the phone text scale 1.5
    bool keep = false;                    // +0x24: MarkForNotClear
    bool flag = false;                    // +0x25 (SetAlphaModifiable(!flag))
    HudString(const std::u16string& s, int font, unsigned frames, int hjust, int vjust, bool flag, int printWidth);
    std::shared_ptr<Sprite> print(int width, unsigned justification) const;   // cHudString::Print(int, int)
};

// cDynamicHudElement
class Element {
public:
    virtual ~Element() = default;
    virtual void initializeForFirstUse() {}
    virtual void updateDimensions(int& w, int& h, int globalJustify, bool paused) = 0;
    virtual void reposition(int x, int y, int w, int h) = 0;
    virtual void beginCollapse() = 0;
    virtual bool hasCollapsed() = 0;
    virtual void endCollapse() = 0;
    virtual int horizontalAlignment() = 0;
    virtual int verticalAlignment() = 0;
    virtual int xCoordWhenNotAligned() = 0;
    virtual int leftRightAlignBorder() { return 8; }
};

// cTextWithBox<uint> as cObjectiveText has it (no box drawn: its fades are the sprite shown and hidden)
class ObjectiveText : public Element {
public:
    ObjectiveText(int x, int printWidth, int width, float speed)
        : printWidth_(printWidth), x_(x), width_(width), speed_(speed) {}
    void add(std::unique_ptr<HudString> s) { queue_.push_back(std::move(s)); }   // onto the queue's tail
    void clearQueue();                    // cEasyClearAndRemoveTextQueue<uint>::ClearQueue
    bool shown() const { return current_ != nullptr; }
    int printWidth() const { return printWidth_; }

    void updateDimensions(int& w, int& h, int globalJustify, bool paused) override;
    void reposition(int x, int y, int w, int h) override;
    void beginCollapse() override { collapsing_ = true; collapseRequested_ = true; }
    bool hasCollapsed() override { return state_ == 0; }
    void endCollapse() override { collapsing_ = false; }
    int horizontalAlignment() override { return hAlign_; }
    int verticalAlignment() override { return vAlign_; }
    int xCoordWhenNotAligned() override { return x_; }
    int leftRightAlignBorder() override { return 0xB; }

private:
    HudString* commitToNextText();
    int boxHeight(const HudString& s) const { return s.height + 6; }   // ComputeBoxHeight
    unsigned printJustification(int globalJustify) const;            // GetTextPrintJustification
    void printCurrent(int globalJustify);
    void dropCurrent();
    std::list<std::unique_ptr<HudString>> queue_;
    std::unique_ptr<HudString> current_;   // +0x40
    std::shared_ptr<Sprite> sprite_;       // +0x68
    int printWidth_;                       // +0x38
    bool collapsing_ = false;              // +0x3C
    int state_ = 0;                        // +0x50
    int x_, width_;                        // +0x54, +0x58
    float height_ = 0.f, target_ = 0.f;    // +0x5C, +0x60
    float speed_;                          // +0x64
    int hAlign_ = 0, vAlign_ = 0;          // +0x70, +0x74
    bool collapseRequested_ = false;       // +0x78
};

// cDynamicHudLayout
class Layout {
public:
    explicit Layout(std::vector<Element*> elements, int top) : elements_(std::move(elements)), dims_(elements_.size()),
                                                               top_(top) {}
    void updateAndLayout(bool paused);
    void switchGlobalAlignment(int h, int v);
    int lastBottomY() const { return lastBottom_; }
private:
    struct Dims { int x = 0, y = 0, w = 0, h = 0; };
    std::vector<Element*> elements_;
    std::vector<Dims> dims_;
    int state_ = 0;                        // +0
    int globalH_ = 0, globalV_ = 0;        // +0x20, +0x24
    int pendingH_ = 0, pendingV_ = 0;      // +0x28, +0x2C
    int top_;                              // +0x30
    int lastBottom_ = 0;                   // +0x34
    bool firstUse_ = true;                 // +0x38
};

// HUDText
class Hud {
public:
    Hud() : objective_(0xD5, 0x257, 0x32B, 5.f), layout_({&objective_}, 0x28) {}
    // HUDText::DisplayObjective(text, font, frames, .., clear, ..): font 2, the queue cleared first if asked
    void displayObjective(const std::u16string& text, unsigned frames, bool clear);
    void deleteQueue() { objective_.clearQueue(); }   // HUDText::DeleteQueue
    bool objectiveShown() const { return objective_.shown(); }
    void update(bool paused) { layout_.updateAndLayout(paused); }   // HUDText::UpdateText's g_DynamicHud
private:
    ObjectiveText objective_;
    Layout layout_;
};

}
