// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// Gui::cTwoDSequenceApp, the PDA app (0x3A) that draws a sequence's comic overlay: background panels, character
// sprites and the subtitles, in a 1024 x 768 screen whose width follows the display.
//
// Images: gGameDir[24] (backgrounds) and gGameDir[25] (sprites) are tables of 16-byte sSeqImage records {u16
// texture, u16 x, u16 y, u16 width, u16 height, ...}, indexed by the tracks' image numbers. Backgrounds are greyscale
// panels lit by two time cycle colours (interpolators 0x20 at the top and 0x23 at the bottom), drawn by
// cTexturedQuad::RenderOneBg as a 2 x 2 grid stretched by 1024/480 and wrapped sideways; layer 3 is the main panel
// and layer 2 a second one drawn behind the sprite marked behindBackground. Sprites are drawn from the highest slot
// down, at 4 units per position unit. Everything is clipped to the comic strip from y 133 to 645.
#pragma once
#include "sequence.h"
#include <cstdint>
#include <string>
#include <vector>

class TimeCycle;

class ComicScreen {
public:
    bool load(const std::string& dataDir);
    void begin(const Sequence& seq, const TimeCycle& tc);   // cTwoDSequenceApp::Init / SoftReset for a sequence
    void process(Sequence& seq);                           // cTwoDSequenceApp::Process, once per 30 Hz frame
    void render(int width, int height) const;              // cTwoDSequenceApp::Render and the subtitle sprite
    // cSequence::Stop: cTwoDSequenceApp::SoftReset lets the panels and the subtitle go; then the game's own app comes
    // back (end) unless the script asked to keep this one (SetRestorePDA), which then goes on drawing its black page
    // (and keeps the city hidden if it was) until the next sequence is loaded. end() is cTwoDSequenceApp::DeInit.
    void stop(bool keepApp);
    void end();
    bool hidesWorld() const { return hidesWorld_; }        // cRenderWorld::DisableRendering while a panel is up
    bool active() const { return active_; }

private:
    struct Image { uint16_t texture, x, y, w, h, w2, h2, pad; };
    struct Quad { float pos[2] = {0, 0}; const Image* image = nullptr; uint8_t alpha = 0xFF; };   // cTexturedQuad
    void drawBackground(const Quad& q) const;
    void drawSprite(const Quad& q) const;
    void scissor(int layer) const;
    void buildText(int string);
    void printFrame();

    std::vector<Image> bgImages_, spriteImages_;
    std::string dataDir_, table_;
    std::vector<std::u16string> strings_;
    Quad bg_[10], sprite_[20];
    uint32_t bgVisible_ = 0, spriteVisible_ = 0;            // +0x1E0, +0x1E4
    int mainBg_ = -1;                                       // +0x1B8: the last non-second layer loaded
    bool secondBg_ = false;                                 // +0x1C0
    int marker_ = 0xFFFF;                                   // +0x1E8
    uint16_t rects_[2][4] = {{0, 0, 1024, 768}, {0, 0, 1024, 768}};   // +0x1C8: layers 2 and 3
    uint32_t top_ = 0xFFFFFFFF, bottom_ = 0xFFFFFFFF;       // +0x1F8, +0x1FC
    float fade_ = 0.f;                                      // +0x208
    bool active_ = false, hidesWorld_ = false;
    bool loaded_ = false;                                   // the app is running (Gui::cPda::RunningAppType 0x3A)
    // the subtitle (a printing text sprite, Gfx2d::cPrintingText)
    std::u16string text_;
    bool textShown_ = false;
    int printed_ = 0;                                       // cPrintingText +0
    uint8_t printDelay_ = 0, printSpeed_ = 0;               // +9, +0xA
};
