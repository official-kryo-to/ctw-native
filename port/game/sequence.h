// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// Cutscene sequences (cSequence, cSequenceManager): the CSS_*.seq files in ROM.WAD. A sequence is a camera path,
// frame callbacks for the mission script and the comic overlay that Gui::cTwoDSequenceApp draws (comicscreen.h):
// background panels, character sprites, subtitles, the overlay's fade and the skip points.
//
// cSequence::Load reads a 0x30-byte header at +0x54: i32 +0x04 cameras (32 bytes each), +0x08 callbacks (8),
// +0x0C background tracks, +0x10 sprite tracks, +0x14 text tracks, +0x18 skip tracks, +0x1C fade tracks, and at
// +0x20 the name of the subtitle table (e_<name>.gxt). The tables follow in that order; every track is a small
// header and its keys:
//   camera     {i32 frame, pos[3] (20.12), i32 movement, i32 actor, i32 kind, u16 pitch (0xC000 - cBaseCam +0xE4),
//               i16 yaw}
//   callback   {i32 frame, i32 id}
//   background {i32 layer, i32 count} + count * {u16 image, u16, i32 pos[2], i32 visible, i32 interpolation,
//               i32 frame, u8 rect[4]}
//   sprite     {i32 image, i32 behindBackground, i32 count} + count * {i32 pos[2], i32 visible, u16 interpolation,
//               u16, i32 frame, u16 alpha, u16 alpha interpolation}
//   text       {i32 count} + count * {u32 string, u8, u8 shown, u8 justification, u8, i32 frame}
//   skip       {i32 count} + count * {i32 frame, u8 tap, u8 kind (1 = hold skips, 2 = the end), u8, u8}
//   fade       {i32 count} + count * {i32 frame, u8 in, u8 frames}
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class GameFs;

// cSequenceEaseCamera / cSequenceLinCamera / cSequenceEaseInNotOutCamera as CameraImpl::SeqPlace switches to them:
// the eye and the cBaseCam angles (+0xE4 pitch, +0xE8 yaw) tweened from the previous camera over a number of frames.
struct SequenceCamera {
    bool active = false;
    int32_t eye[3] = {0, 0, 0};
    uint16_t pitch = 0, yaw = 0;
    // CameraImpl::SeqPlace: movement 0 and 2 ease in and out (cosine), 3 and 4 are linear, 7 and 8 ease out
    // (fast first), anything else eases in. A duration of zero cuts.
    void place(const int32_t pos[3], uint16_t pitch, uint16_t yaw, int frames, int movement);
    void tick();          // one 30 Hz frame of the tween (cBaseCam::Update and the subclasses' Update)
private:
    enum Curve { Linear, Ease, EaseIn, EaseOut } curve_ = Linear;
    int remaining_ = 0, total_ = 0;
    int32_t from_[3] = {0, 0, 0}, delta_[3] = {0, 0, 0};
    uint16_t fromPitch_ = 0, fromYaw_ = 0;
    int16_t dPitch_ = 0, dYaw_ = 0;
};

class Sequence {
public:
    // eCallBackValue slots of cSequence::SetCallBack that the sequence itself fires
    enum { kFadeOutCheck = 0x1A, kEnded = 27, kSkipped = 28, kSkippedWithB = 29 };

    struct CameraKey { int32_t frame, pos[3], movement, actor, kind; uint16_t pitch; int16_t yaw; };
    struct CallbackKey { int32_t frame, id; };
    struct BgKey { uint16_t image, extra; int32_t pos[2], visible, interp, frame; uint8_t rect[4]; };
    struct SpriteKey { int32_t pos[2], visible; uint16_t interp, extra; int32_t frame; uint16_t alpha, alphaInterp; };
    struct TextKey { uint32_t string; uint8_t style, shown, justify, pad; int32_t frame; };
    struct SkipKey { int32_t frame; uint8_t tap, kind, catchCamera, catchCallbacks; };
    struct FadeKey { int32_t frame; uint8_t in, frames; };

    // cBgSequence: one comic background layer. flags: 1 image or visibility changed, 2 moved, 4 visibility,
    // 8 the next image is near, 0x10 extra, 0x40 rect.
    struct BgTrack {
        int32_t layer = 0;
        std::vector<BgKey> keys;
        uint8_t flags = 0;
        uint16_t image = 0, extra = 0;
        int32_t pos[2] = {0, 0}, visible = 1, interp = 1, frame = 0;
        uint8_t rect[4] = {0, 0, 0, 0};
        int32_t nextImage = -1;
        void update(int frame);   // cBgSequence::Update
    };
    // cSpriteSequence: one character sprite. flags: 2 moved, 4 visibility, 0x10 extra, 0x80 alpha.
    struct SpriteTrack {
        int32_t image = 0, behindBackground = 0;
        std::vector<SpriteKey> keys;
        uint8_t flags = 4;
        int32_t pos[2] = {0, 0}, visible = 0, frame = 0;
        uint16_t interp = 1, extra = 0, alpha = 0xFF;
        void update(int frame);   // cSpriteSequence::Update
    };
    // cTextSequence: one subtitle line. flags: 1 the string changed, 4 shown or hidden.
    struct TextTrack {
        std::vector<TextKey> keys;
        uint8_t flags = 4;
        int32_t string = -1;
        uint8_t style = 0, shown = 0, justify = 0;
        void update(int frame);   // cTextSequence::Update
    };
    // cSkipSequence::Update's answer for the current frame
    struct SkipState {
        int32_t next = -1, end = -1;
        bool tap = true, hold = true, catchCamera = true, nextCatchCamera = true, catchCallbacks = true,
             nextCatchCallbacks = true;
    };

    // cSequence::cSequence + Load. False when the file is missing.
    bool load(const GameFs& fs, const std::string& name);
    bool parse(const std::vector<uint8_t>& file, const std::string& name);   // the file's bytes
    const std::string& name() const { return name_; }
    const std::string& textTable() const { return textTable_; }   // the header's table name ("CKNA01")

    // cSequence::SetCallBack(id, callback)
    void setCallback(int id, std::function<void()> fn);
    // cSequence::Play(..., loop, ...): restarts the frame counters and places the first camera
    void play(bool loop = false);
    // cSequence::Update, once per 30 Hz frame. Input this frame: tapped (cTouchScreen::SingleClicked / B) and held
    // (the touch held down; Escape / Start skips at once). False once the sequence has stopped.
    bool update(bool tapped, bool held, bool startPressed, bool screenFading);
    void stop(bool fromSkip = false);   // cSequence::Stop
    // cScriptSequenceBase::SetRestorePDA(.., 0x3A or 0x59): the 2D sequence app is not swapped back for the game's
    // own (app 0x10, the default) when the sequence stops, so its black page stays until the next one is loaded
    bool restoreComic = false;
    bool playing() const { return playing_; }
    int frame() const { return frame_; }
    int lastFrame() const { return lastFrame_; }
    bool canSkip() const { return showSkip_; }   // the skip icon is up (a tap moves on)

    // what the comic overlay draws
    std::vector<BgTrack> bgs;
    std::vector<SpriteTrack> sprites;
    std::vector<TextTrack> texts;
    float fade = 0.f;            // the fade track's value (cSequence +0x408 [0] +0x10): the overlay's opacity
    bool textPrinting = false;   // Gui::cTwoDSequenceApp::IsTextPrinting (the overlay sets it)
    bool skipTextPrinting = false;   // a tap asked the overlay to finish printing (SkipTextPrinting)
    bool hasOverlay() const { return !bgs.empty(); }
    bool hasCameras() const { return !cameras_.empty(); }

    // CameraImpl::SeqPlace from the camera keys; the game applies it to its sequence camera
    std::function<void(const int32_t pos[3], uint16_t pitch, uint16_t yaw, int frames, int movement)> placeCamera;
    std::function<void()> restoreCamera;   // cCamera::RestoreCam: an in-game camera key at the end
    std::function<void()> fastForward;     // CameraImpl::FastForwardUpdate(1) while skipping catches up

private:
    void processCallbacks();     // cSequence::ProcessCallbacks
    void processCamera();        // cSequence::ProcessCamera
    void processTracks();        // cSequence::ProcessSprites
    bool updateSkip(bool tapped, bool held, bool startPressed, bool screenFading);   // cSequence::UpdateSkip
    SkipState skipState(int frame) const;     // cSkipSequence::Update
    void fire(int id);

    std::string name_, textTable_;
    std::vector<CameraKey> cameras_;
    std::vector<CallbackKey> callbacks_;
    std::vector<std::vector<SkipKey>> skips_;
    std::vector<std::vector<FadeKey>> fades_;
    std::function<void()> slots_[32];
    bool playing_ = false, loop_ = false;
    int frame_ = -1, cameraFrame_ = -1, callbackFrame_ = -1;   // +0x44, +0x3C, +0x40
    int cameraIndex_ = -1, callbackIndex_ = -1;               // +0x4C, +0x50
    int lastFrame_ = 0;                                       // +0x48
    int cameraCount_ = 0;                                     // +0x58
    bool skipNow_ = false, skipAll_ = false;                  // +0x1C, +0x1D
    bool fastForwarded_ = false;                              // +0x1E
    int holdFrames_ = 0;                                      // +0x14
    bool showSkip_ = false;
};
