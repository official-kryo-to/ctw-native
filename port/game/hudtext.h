// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// HUDText's on-screen strings: the Gfx2d sprites they are made of (cGlobalSpriteManager's render lists,
// cSprite::CalculateVerts and RefreshSprite, AddSpriteImpl's text path), cHudString, the text-and-box elements
// cTextWithBox (g_ObjectiveText's cObjectiveText and g_HelpText's cHelpText_DynamicHudElement) and g_DynamicHud
// (cDynamicHudLayout), which lays them out in the 1024 x 768 HUD space every frame.
#pragma once
#include "gfx/gametext.h"
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <string>
#include <vector>

struct SpriteDef;

namespace HudText {

// Gfx2d::cPrintingText: a text typed into its sprite a few characters at a time (0xFEF0..0xFEF7 set the speed,
// 0xFEF8 + n pauses), each line started where its justification puts it
struct PrintingText {
    std::u16string text;                   // +0x10
    uint16_t pos = 0, lineEnd = 0;         // +0, +2
    unsigned justification = 0;            // +4: the sprite's, put back when it is done
    uint8_t lineY = 0xFF, delay = 0, speed = 0;   // +8, +9, +0xA
    bool update(GameText::TextSprite& s);  // cPrintingText::Update: true once it is all typed
};

// Gfx2d::cSprite: a cTextSprite, or an image from a sprite sheet
struct Sprite {
    std::shared_ptr<GameText::TextSprite> text;
    const SpriteDef* image = nullptr;      // +0x88 (cSpriteData)
    int16_t x = 0, y = 0;                  // +0x28, +0x2A: the HUD space, y down
    bool visible = true;                   // +0x18 (ShowSprite)
    uint8_t priority = 0;                  // +0x1E: its render list (lists 6 down to 0 are drawn)
    uint8_t style = 0x10;                  // +0x1F: 0xE draws the black shadow, 0x11 an outline
    unsigned ortho = 0;                    // +0x9C: cPdaApp::OrthoCorrectionBeforeRender's mode
    float alpha = 1.f;                     // +0x64
    uint16_t scaleX = 0x100, scaleY = 0x100;   // +0x30, +0x32 (8.8)
    int16_t uvAdd[4] = {0, 0, 0, 0};       // +0x50 u0, +0x54 v0, +0x58 u1, +0x5C v1 (the sheet's 2048ths)
    std::shared_ptr<Sprite> shadow;        // +0x10, moved, shown and re-listed with it
    std::unique_ptr<PrintingText> printing;   // +0x90 while +0x21 is 3
    void setPos(int px, int py);           // SetSpritePos
    void show(bool on);                    // ShowSprite
    void setScale(int32_t sx, int32_t sy); // SetSpriteScale (cFixed 20.12)
    void setImage(const SpriteDef* d);     // SetSpriteData
    int width() const;                     // GetSpriteWidth
    int height() const;                    // +0x24
};

// the sheet cTaskBar::GetSpriteDataAllwaysLoaded draws from (SS_AlwaysLoaded): its definitions and texture
void SetAlwaysLoaded(const std::vector<SpriteDef>* defs, unsigned texture, int pngWidth, int pngHeight);
const SpriteDef* AlwaysLoaded(int index);

// cGlobalSpriteManager::AddSpriteImpl(text, font, x, y, restriction, style, width, justification, first, max, ..):
// nothing when the text has no height
std::shared_ptr<Sprite> AddText(const std::u16string& text, int font, int x, int y, unsigned style, unsigned width,
                                int justification, uint16_t firstLine, uint16_t maxLines);
std::shared_ptr<Sprite> AddImage(const SpriteDef* image);   // AddSpriteImpl(data, .., 0, 0, 0, 0, style 0x10)
void SetPriority(const std::shared_ptr<Sprite>& s, unsigned priority);
// RefreshSprite's cPrintingText::Update for the typing sprites (once a frame), and its Skip once one is typed or the
// player asks (cSprite::SkipTextPrinting): the whole text again at its justification. scale: the text scale they type
// at (cutSceneScale in a cutscene, else 1.5)
void UpdatePrinting(float scale);
void SkipPrinting(const std::shared_ptr<Sprite>& s, float scale);   // SetPriority(p, true): re-listed as newest
void RemoveSprite(const std::shared_ptr<Sprite>& s);   // RemoveSprite(s, true): it and its shadow
void RenderSprites(int width, int height);           // cPda::Render's ProcessSprites over the lists
// a screen point (drawable pixels) in the HUD's aspect-corrected 1024 x 768 space (cTouchScreen's coordinates)
void ScreenToHud(int sx, int sy, int width, int height, int out[2]);

// cHudString<uint> and cHudString<HELP_TEXT_PARAMS>
struct HudString {
    std::u16string text;
    int font = 2;
    unsigned frames = 0, total = 0;        // +0xC counts down to 1 (0 stays), +0x10
    int hjust = 0, vjust = 0;              // +0x14, +0x18 (eDynamicHudJustify)
    int height = 0;                        // +0x1C: TextSize's at the phone text scale 1.5
    bool keep = false;                     // MarkForNotClear (+0x24, the help's +0x50)
    bool flag = false;                     // +0x25 / +0x51: SetAlphaModifiable(!flag)
    // HELP_TEXT_PARAMS: the icon (an SS_AlwaysLoaded sprite), whether it sits left of the text, the callback a touch
    // on the box invokes
    unsigned icon = 0x4E;
    bool iconLeft = false;
    std::function<void()> callback;
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
    virtual bool areTouchesToBeHeeded() { return false; }
    virtual void heedTouchEvent() {}
};

// cTextWithBox<T> on cEasyClearAndRemoveTextQueue<T>: a queue of strings shown one at a time in a box that grows to
// each string's height; the box's own look is the subclass's
class TextWithBox : public Element {
public:
    TextWithBox(int x, int printWidth, int width, float speed)
        : printWidth_(printWidth), x_(x), width_(width), speed_(speed) {}
    void add(std::unique_ptr<HudString> s, bool showNow = false);   // cHudTextQueue::Add (onto the tail, or the head)
    void clearQueue();                     // cEasyClearAndRemoveTextQueue::ClearQueue
    HudString* findInQueue(const std::u16string& text);   // the current string or a queued one
    HudString* current() const { return current_.get(); }
    HudString* head() const { return queue_.empty() ? nullptr : queue_.front().get(); }
    void dropHead() { if (!collapsing_ && !queue_.empty()) queue_.pop_front(); }   // PluckQueueHead and delete
    void markForNotClear(const std::u16string& text) {
        for (auto& s : queue_) if (s->text == text) { s->keep = true; break; }
        if (current_ && current_->text == text) current_->keep = true;
    }
    int state() const { return state_; }
    int printWidth() const { return printWidth_; }

    void updateDimensions(int& w, int& h, int globalJustify, bool paused) override;
    void reposition(int x, int y, int w, int h) override;
    void beginCollapse() override { collapsing_ = true; collapseRequested_ = true; }
    bool hasCollapsed() override { return state_ == 0; }
    void endCollapse() override { collapsing_ = false; }
    int horizontalAlignment() override { return hAlign_; }
    int verticalAlignment() override { return vAlign_; }
    int xCoordWhenNotAligned() override { return x_; }

protected:
    virtual int computeBoxHeight(const HudString& s) = 0;
    virtual void beginBoxFadeOn(HudString& s) = 0;
    virtual bool boxFadeOn() = 0;
    virtual void endBoxFadeOn() = 0;
    virtual void beginBoxFadeOff() = 0;
    virtual bool boxFadeOff() = 0;
    virtual void endBoxFadeOff() = 0;
    virtual void repositionBox(HudString& s, int x, int y, int w, int h) = 0;
    virtual unsigned printJustification(int globalJustify, const HudString& s) = 0;
    std::shared_ptr<Sprite> sprite_;       // +0x68: the printed string
    std::unique_ptr<HudString> current_;   // +0x40
private:
    void commitToNextText();
    void printCurrent(int globalJustify);
    std::list<std::unique_ptr<HudString>> queue_;
    int printWidth_;                       // +0x38
    bool collapsing_ = false;              // +0x3C
    int state_ = 0;                        // +0x50
    int x_, width_;                        // +0x54, +0x58
    float height_ = 0.f, target_ = 0.f;    // +0x5C, +0x60
    float speed_;                          // +0x64
    int hAlign_ = 0, vAlign_ = 0;          // +0x70, +0x74
    bool collapseRequested_ = false;       // +0x78
};

// cObjectiveText: no box drawn, its fades are the string shown and hidden
class ObjectiveText : public TextWithBox {
public:
    ObjectiveText() : TextWithBox(0xD5, 0x257, 0x32B, 5.f) {}
    int leftRightAlignBorder() override { return 0xB; }
protected:
    int computeBoxHeight(const HudString& s) override { return s.height + 6; }
    void beginBoxFadeOn(HudString&) override { if (sprite_) sprite_->show(true); }
    bool boxFadeOn() override { return true; }
    void endBoxFadeOn() override {}
    void beginBoxFadeOff() override { if (sprite_) sprite_->show(false); }
    bool boxFadeOff() override { return true; }
    void endBoxFadeOff() override {}
    void repositionBox(HudString& s, int x, int y, int w, int h) override;
    unsigned printJustification(int globalJustify, const HudString& s) override;
};

// cHelpText_DynamicHudElement: the box is a column of SS_AlwaysLoaded pieces (0x27 on top, 0x26 in sixteens, 0x25
// below) that opens and closes by its height, with the string's icon beside the text
class HelpText : public TextWithBox {
public:
    HelpText() : TextWithBox(0xD5, 0x21B, 0x257, 5.f) {}
    void initializeForFirstUse() override;
    void updateDimensions(int& w, int& h, int globalJustify, bool paused) override;
    bool wasTouched() const { return touched_; }
    bool areTouchesToBeHeeded() override { return state() == 4; }
    // HeedTouchEvent: the string's callback, else it goes at the next update and the box counts as touched
    void heedTouchEvent() override;
    std::function<void(int sfx, int volume)> playHudSfx;   // cAudioManager::PlayHudSfx
protected:
    int computeBoxHeight(const HudString& s) override { return middleSections(s) * 16 + 32; }
    void beginBoxFadeOn(HudString& s) override;
    bool boxFadeOn() override;
    void endBoxFadeOn() override { if (sprite_) sprite_->show(true); }
    void beginBoxFadeOff() override { if (sprite_) sprite_->show(false); }
    bool boxFadeOff() override;
    void endBoxFadeOff() override { showBoxSprites(false); }
    void repositionBox(HudString& s, int x, int y, int w, int h) override;
    unsigned printJustification(int, const HudString&) override { return 0x4000; }
private:
    static int middleSections(const HudString& s);   // CalculateMiddleSections
    void showBoxSprites(bool on);
    std::shared_ptr<Sprite> top_, bottom_, icon_, middle_[15];   // +0x80, +0x100, +0x108, +0x88..
    int middles_ = 0;                      // +0x7C
    float boxAlpha_ = 0.f;                 // +0x110
    bool touched_ = false;                 // +0x114
};

// cDynamicHudLayout
class Layout {
public:
    explicit Layout(std::vector<Element*> elements, int top) : elements_(std::move(elements)), dims_(elements_.size()),
                                                               top_(top) {}
    // touch: a press this frame in the HUD's aspect-corrected 1024 x 768 space (y down), or null
    void updateAndLayout(bool paused, const int* touch = nullptr);
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

// cScriptText in a cScriptTextQueue (the win/lose line, ...): a string printed at its place for a number of frames,
// the queue's head on screen
struct ScriptText {
    std::u16string text;                   // +0x20
    int16_t x = 0, y = 0;                  // +0x30, +0x32
    uint16_t frames = 0, total = 0;        // +0x34, +0x36 (0: until removed)
    uint16_t width = 0x3F8;                // +0x38: TextSize's (with the wrap given before Init)
    uint8_t height = 0;                    // +0x3A
    bool flashing = false;                 // +0x3B
    int font = 0;                          // +0x3C
    bool centred = false;                  // +0x3E
    std::shared_ptr<Sprite> sprite;        // +0x28
    // cScriptText::Init(.., text, x, y, frames, font, flashing, centred): its width and height at this text scale
    void init(const std::u16string& s, int px, int py, unsigned n, int f, bool flash, bool centre);
    void print();                          // cScriptText::Print
    void setPosition(int px, int py);      // cScriptText::SetPosition
    void remove();                         // cScriptText::Remove
};

class ScriptTextQueue {
public:
    void add(std::unique_ptr<ScriptText> t, bool now);   // cScriptTextQueue::Add: now (or into an empty queue) shown
    void update(bool paused);              // cScriptTextQueue::Update
    void clear();
    ScriptText* top() { return list_.empty() ? nullptr : list_.front().get(); }   // GetTopScriptText
private:
    std::list<std::unique_ptr<ScriptText>> list_;
};

// HUDText
class Hud {
public:
    Hud() : layout_({&objective_, &help_}, 0x28) {}
    // HUDText::DisplayObjective(text, font, frames, .., clear, ..): font 2, the queue cleared first if asked
    void displayObjective(const std::u16string& text, unsigned frames, bool clear);
    void deleteQueue() { objective_.clearQueue(); }   // HUDText::DeleteQueue
    bool objectiveShown() const { return objective_.current() != nullptr; }
    bool isObjectiveBeingDisplayed() const { return objective_.state() == 4; }
    // HUDText::DisplayHelp(text, font, frames, showNow, clear, .., callback, 0, 0): font 2; the same text again
    // only refreshes it
    void displayHelp(const std::u16string& text, unsigned frames, bool showNow, bool clear,
                     std::function<void()> callback = nullptr);
    void clearHelp();                      // HUDText::ClearHelp
    void deleteCurrentHelp();              // HUDText::DeleteCurrentHelp
    const std::u16string* currentHelp() const;   // HUDText::GetCurrentHelp
    void resetCurrentHelpTimer();          // HUDText::ResetCurrentHelpTimer
    HelpText& help() { return help_; }
    // HUDText::DisplayWinLose(text, reason, font, frames, now, clearObjectives, ..): centred on the screen at the
    // phone text scale 1.5 (the reason line under it: no mission here gives one)
    void displayWinLose(const std::u16string& text, int font, unsigned frames, bool now, bool clearObjectives);
    // HUDText::DisplayBustedWasted(text, font, frames, now, clearObjectives): centred by its width on the screen, or
    // just above the middle with a win / lose line up (which then sits at the middle)
    void displayBustedWasted(const std::u16string& text, int font, unsigned frames, bool now, bool clearObjectives);
    // HUDText::UpdateText: the script-text queues, then g_DynamicHud
    void update(bool paused, const int* touch = nullptr) {
        winLose_.update(paused);
        bustedWasted_.update(paused);
        layout_.updateAndLayout(paused, touch);
    }
private:
    ObjectiveText objective_;
    HelpText help_;
    Layout layout_;
    ScriptTextQueue winLose_;              // m_WinLoseTextQueue
    ScriptTextQueue bustedWasted_;         // m_BustedWastedTextQueue
};

}
