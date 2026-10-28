// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "sequence.h"
#include "os/gamefs.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

static int fastsin(int a) { return (int)(sinf((float)a * 9.587378e-05f) * 4096.f); }

// ---- the camera (CameraImpl::SeqPlace, cBaseCam::TweenFrom and the sequence cameras' Update) ----

void SequenceCamera::place(const int32_t pos[3], uint16_t p, uint16_t y, int frames, int movement) {
    if (frames < 1) {   // TweenFrom with no duration: the new camera as it is
        std::memcpy(eye, pos, sizeof eye);
        pitch = p;
        yaw = y;
        remaining_ = total_ = 0;
        active = true;
        return;
    }
    if (movement == 0 || movement == 2) curve_ = Ease;
    else if (movement == 3 || movement == 4) curve_ = Linear;
    else curve_ = movement == 7 || movement == 8 ? EaseOut : EaseIn;
    for (int i = 0; i < 3; ++i) { from_[i] = eye[i]; delta_[i] = pos[i] - eye[i]; }
    fromPitch_ = pitch;
    fromYaw_ = yaw;
    dPitch_ = (int16_t)(uint16_t)(p - pitch);
    dYaw_ = (int16_t)(uint16_t)(y - yaw);
    remaining_ = total_ = frames;
}

void SequenceCamera::tick() {
    if (!active || remaining_ < 1 || total_ < 1) return;
    if (curve_ == Ease) {   // cSequenceEaseCamera::Update: (1 + sin) / 2 from -90 to 90 degrees
        const int s = (fastsin((remaining_ << 15) / total_ + 0x4000) + 0x1000) >> 1;
        pitch = (uint16_t)(fromPitch_ + ((s * dPitch_) >> 12));
        yaw = (uint16_t)(fromYaw_ + ((s * dYaw_) >> 12));
        for (int i = 0; i < 3; ++i) eye[i] = from_[i] + (int32_t)(((int64_t)delta_[i] * s) >> 12);
        if (--remaining_ == 0) {   // "Set to complete end position (t = 1)"
            for (int i = 0; i < 3; ++i) eye[i] = from_[i] + delta_[i];
            pitch = (uint16_t)(fromPitch_ + dPitch_);
            yaw = (uint16_t)(fromYaw_ + dYaw_);
        }
        return;
    }
    // cBaseCam::Update's tween (the angles of every other sequence camera too): a share of what is left each frame
    const int left = remaining_;
    const int16_t pitchStep = (int16_t)(dPitch_ / left), yawStep = (int16_t)(dYaw_ / left);
    dPitch_ = (int16_t)(dPitch_ - pitchStep);
    dYaw_ = (int16_t)(dYaw_ - yawStep);
    pitch = (uint16_t)(pitch + pitchStep);
    yaw = (uint16_t)(yaw + yawStep);
    --remaining_;
    if (curve_ == Linear) {
        for (int i = 0; i < 3; ++i) {
            const int32_t step = delta_[i] / left;
            delta_[i] -= step;
            eye[i] += step;
        }
        return;
    }
    // cSequenceEaseInNotOutCamera::Update: 1 - (left / total)^2 easing out, ((total - left) / total)^2 easing in
    const int64_t r = curve_ == EaseOut ? remaining_ : total_ - remaining_;
    int64_t f = ((r << 44) / ((int64_t)total_ << 12)) >> 20;
    f = (f * f) >> 12;
    if (curve_ == EaseOut) f = 4096 - f;
    for (int i = 0; i < 3; ++i) eye[i] = from_[i] + (int32_t)(((int64_t)delta_[i] * f) >> 12);
}

// ---- the tracks ----

// cBgSequence::Update / cSpriteSequence::Update: from the key at or before the frame towards the next one by the
// next key's interpolation (1 holds, 0 is linear, 3 eases out, 4 and the rest are the sine variants below).
static void interpolate(int32_t out[2], const int32_t from[2], const int32_t to[2], int f, int f0, int f1, int type) {
    out[0] = from[0];
    out[1] = from[1];
    const int span = f1 - f0, done = f - f0;
    if (span == 0) return;
    const int32_t dx = to[0] - from[0], dy = to[1] - from[1];
    if (type < 2) {
        out[0] += (int32_t)((int64_t)dx * done / span);
        out[1] += (int32_t)((int64_t)dy * done / span);
        return;
    }
    const int32_t w = (int32_t)((uint32_t)((done << 12) / span) << 15);
    int s;
    if (type == 3) s = fastsin(w >> 13);
    else if (type == 4) s = fastsin((w >> 13) - 0x4000);
    else s = fastsin((int32_t)((uint32_t)w + 0xFC000000u) >> 12) / 2;
    out[0] += (int32_t)(((int64_t)s * dx) >> 12);
    out[1] += (int32_t)(((int64_t)s * dy) >> 12);
}

void Sequence::BgTrack::update(int f) {
    const int n = (int)keys.size();
    if (n < 1) return;
    int at = -1;
    for (int i = 0; i < n; ++i) if (keys[i].frame <= f) at = i;
    if (at == -1) return;
    frame = f & 0xFFFF;
    const int next = at < n - 1 ? at + 1 : at;
    const BgKey& k = keys[at];
    interp = k.interp;
    int32_t p[2] = {k.pos[0], k.pos[1]};
    if (next != at && keys[next].interp != 1)
        interpolate(p, k.pos, keys[next].pos, f, k.frame, keys[next].frame, keys[next].interp);
    int soon = -1;   // the image four frames ahead, so it can be loaded in time
    for (int i = 0; i < n; ++i) if (keys[i].frame <= f + 4) soon = i;
    if (soon != -1 && nextImage != keys[soon].image) { nextImage = keys[soon].image; flags |= 8; }
    if (image != k.image) { image = k.image; flags |= 1; }
    if (visible != k.visible) { visible = k.visible; flags |= 5; }
    if (pos[0] != p[0] || pos[1] != p[1]) { pos[0] = p[0]; pos[1] = p[1]; flags |= 2; }
    if (extra != k.extra) { extra = k.extra; flags |= 0x10; }
    if (std::memcmp(rect, k.rect, 4)) { std::memcpy(rect, k.rect, 4); flags |= 0x40; }
}

void Sequence::SpriteTrack::update(int f) {
    const int n = (int)keys.size();
    if (n == 0) return;
    int at = -1;
    for (int i = 0; i < n; ++i) if (keys[i].frame <= f) at = i;
    if (at == -1) {
        if (visible) { visible = 0; flags |= 4; }
        return;
    }
    frame = f & 0xFFFF;
    const int next = at < n - 1 ? at + 1 : at;
    const SpriteKey& k = keys[at];
    interp = k.interp;
    int32_t p[2] = {k.pos[0], k.pos[1]};
    if (next != at && keys[next].interp != 1)
        interpolate(p, k.pos, keys[next].pos, f, k.frame, keys[next].frame, keys[next].interp);
    int a = k.alpha;
    if (next != at && keys[next].alphaInterp != 1 && keys[next].frame != k.frame)
        a += (keys[next].alpha - k.alpha) * (f - k.frame) / (keys[next].frame - k.frame);
    if (visible != k.visible) { visible = k.visible; flags |= 4; }
    if (pos[0] != p[0] || pos[1] != p[1]) { pos[0] = p[0]; pos[1] = p[1]; flags |= 2; }
    if (extra != k.extra) { extra = k.extra; flags |= 0x10; }
    if (alpha != (uint16_t)a) { alpha = (uint16_t)a; flags |= 0x80; }
}

void Sequence::TextTrack::update(int f) {
    const int n = (int)keys.size();
    if (n < 1) return;
    int at = -1;
    for (int i = 0; i < n; ++i) if (keys[i].frame <= f) at = i;
    if (at == -1) {
        if (shown) { shown = 0; flags |= 4; }
        return;
    }
    const TextKey& k = keys[at];
    if (style != k.style || shown != k.shown) {
        if (style != k.style) flags |= 1;
        style = k.style;
        shown = k.shown;
        justify = k.justify;
        flags |= 4;
        if (shown) flags |= 0x25;
    }
    if (string != (int32_t)k.string) {
        style = k.style;
        shown = k.shown;
        string = (int32_t)k.string;
        justify = k.justify;
        flags |= 1;
        if (shown) flags |= 0x21;
    }
}

// ---- loading ----

namespace {
struct Reader {
    const std::vector<uint8_t>& d;
    size_t at = 0;
    bool ok = true;
    template <class T> T get() {
        T v{};
        if (at + sizeof v > d.size()) { ok = false; return v; }
        std::memcpy(&v, d.data() + at, sizeof v);
        at += sizeof v;
        return v;
    }
};
}

bool Sequence::load(const GameFs& fs, const std::string& name) {
    std::vector<uint8_t> d;
    return fs.read(name, d) && parse(d, name);
}

bool Sequence::parse(const std::vector<uint8_t>& d, const std::string& name) {
    if (d.size() < 0x30) return false;
    name_ = name;
    Reader r{d};
    r.get<int32_t>();
    const int32_t nCam = r.get<int32_t>(), nCb = r.get<int32_t>(), nBg = r.get<int32_t>(), nSpr = r.get<int32_t>(),
                  nText = r.get<int32_t>(), nSkip = r.get<int32_t>(), nFade = r.get<int32_t>();
    char table[16];
    std::memcpy(table, d.data() + 0x20, 16);
    table[15] = 0;
    textTable_ = table;
    r.at = 0x30;
    for (int i = 0; i < nCam && r.ok; ++i) {
        CameraKey k{};
        k.frame = r.get<int32_t>();
        for (int& v : k.pos) v = r.get<int32_t>();
        k.movement = r.get<int32_t>();
        k.actor = r.get<int32_t>();
        k.kind = r.get<int32_t>();
        k.pitch = r.get<uint16_t>();
        k.yaw = r.get<int16_t>();
        cameras_.push_back(k);
    }
    for (int i = 0; i < nCb && r.ok; ++i) {
        CallbackKey k{};
        k.frame = r.get<int32_t>();
        k.id = r.get<int32_t>();
        callbacks_.push_back(k);
    }
    // cSequence::Load also notes the first and last frame of the overlay, for the fade it makes up below
    int first = 999999, last = 0, bgKeys = 0;
    bool spriteKeys = false;
    for (int i = 0; i < nBg && r.ok; ++i) {
        BgTrack t;
        t.layer = r.get<int32_t>();
        const int n = r.get<int32_t>();
        for (int j = 0; j < n && r.ok; ++j) {
            BgKey k{};
            k.image = r.get<uint16_t>();
            k.extra = r.get<uint16_t>();
            k.pos[0] = r.get<int32_t>();
            k.pos[1] = r.get<int32_t>();
            k.visible = r.get<int32_t>();
            k.interp = r.get<int32_t>();
            k.frame = r.get<int32_t>();
            for (uint8_t& b : k.rect) b = r.get<uint8_t>();
            t.keys.push_back(k);
        }
        if (!t.keys.empty()) {
            first = std::min(first, t.keys.front().frame);
            last = std::max(last, t.keys.back().frame);
            bgKeys += (int)t.keys.size();
        }
        bgs.push_back(std::move(t));
    }
    for (int i = 0; i < nSpr && r.ok; ++i) {
        SpriteTrack t;
        t.image = r.get<int32_t>();
        t.behindBackground = r.get<int32_t>();
        const int n = r.get<int32_t>();
        for (int j = 0; j < n && r.ok; ++j) {
            SpriteKey k{};
            k.pos[0] = r.get<int32_t>();
            k.pos[1] = r.get<int32_t>();
            k.visible = r.get<int32_t>();
            k.interp = r.get<uint16_t>();
            k.extra = r.get<uint16_t>();
            k.frame = r.get<int32_t>();
            k.alpha = r.get<uint16_t>();
            k.alphaInterp = r.get<uint16_t>();
            t.keys.push_back(k);
        }
        if (!t.keys.empty()) {
            first = std::min(first, t.keys.front().frame);
            if (t.keys.size() > 1) last = std::max(last, t.keys.back().frame);
            spriteKeys = true;
        }
        sprites.push_back(std::move(t));
    }
    for (int i = 0; i < nText && r.ok; ++i) {
        TextTrack t;
        const int n = r.get<int32_t>();
        for (int j = 0; j < n && r.ok; ++j) {
            TextKey k{};
            k.string = r.get<uint32_t>();
            k.style = r.get<uint8_t>();
            k.shown = r.get<uint8_t>();
            k.justify = r.get<uint8_t>();
            k.pad = r.get<uint8_t>();
            k.frame = r.get<int32_t>();
            t.keys.push_back(k);
        }
        texts.push_back(std::move(t));
    }
    for (int i = 0; i < nSkip && r.ok; ++i) {
        std::vector<SkipKey> keys;
        const int n = r.get<int32_t>();
        for (int j = 0; j < n && r.ok; ++j) {
            SkipKey k{};
            k.frame = r.get<int32_t>();
            k.tap = r.get<uint8_t>();
            k.kind = r.get<uint8_t>();
            k.catchCamera = r.get<uint8_t>();
            k.catchCallbacks = r.get<uint8_t>();
            keys.push_back(k);
        }
        skips_.push_back(std::move(keys));
    }
    for (int i = 0; i < nFade && r.ok; ++i) {
        std::vector<FadeKey> keys;
        const int n = r.get<int32_t>();
        for (int j = 0; j < n && r.ok; ++j) {
            FadeKey k{};
            k.frame = r.get<int32_t>();
            k.in = r.get<uint8_t>();
            k.frames = r.get<uint8_t>();
            r.get<uint16_t>();
            keys.push_back(k);
        }
        fades_.push_back(std::move(keys));
    }
    if (!r.ok) {
        std::fprintf(stderr, "sequence %s: truncated\n", name.c_str());
        return false;
    }
    // no fade of its own: the overlay fades in over a second from its first key and out from its last
    if (fades_.empty() && (bgKeys > 0 || spriteKeys))
        fades_.push_back({{0, 0, 0}, {first, 1, 30}, {last, 0, 30}});
    return true;
}

void Sequence::setCallback(int id, std::function<void()> fn) {
    if (id >= 0 && id < 32) slots_[id] = std::move(fn);
}

void Sequence::fire(int id) {
    if (id < 0 || id >= 32 || !slots_[id]) return;
    std::function<void()> fn = slots_[id];   // the callback may replace the slot
    fn();
}

// ---- playing ----

void Sequence::play(bool loop) {
    frame_ = callbackFrame_ = cameraFrame_ = 0;
    playing_ = true;
    loop_ = loop;
    showSkip_ = false;
    lastFrame_ = 0;
    cameraCount_ = (int)cameras_.size();
    if (cameraCount_) { cameraIndex_ = 0; lastFrame_ = cameras_.back().frame; }
    if (!callbacks_.empty()) {
        callbackIndex_ = 0;
        lastFrame_ = std::max(lastFrame_, callbacks_.back().frame);
        // with a fade-out check callback the sequence ends just after it, without its last camera ("Camera count
        // put down to %d")
        if (slots_[kFadeOutCheck])
            for (const CallbackKey& k : callbacks_)
                if (k.id == kFadeOutCheck) {
                    if (cameraCount_) --cameraCount_;
                    lastFrame_ = k.frame + 1;
                    break;
                }
    }
    for (BgTrack& t : bgs) {   // cSequence::InitDiffs
        t.frame = 0; t.image = 0; t.extra = 0; t.pos[0] = t.pos[1] = 0; t.visible = 1; t.interp = 1; t.flags = 0;
        t.nextImage = -1;
    }
    for (SpriteTrack& t : sprites) {
        t.pos[0] = t.pos[1] = 0; t.visible = 0; t.interp = 1; t.extra = 0; t.frame = 0; t.alpha = 0xFF; t.flags = 4;
    }
    for (TextTrack& t : texts) { t.style = 0; t.shown = 0; t.string = -1; t.flags = 4; }
    processCamera();
}

bool Sequence::update(bool tapped, bool held, bool startPressed, bool screenFading) {
    if (!playing_) return false;
    const bool skipped = updateSkip(tapped, held, startPressed, screenFading);
    if (!playing_) return false;
    if (frame_ == -1) return false;
    if (!screenFading && (skipped || frame_ > lastFrame_)) {
        stop(skipped);
        return playing_;
    }
    processCallbacks();
    if (!playing_) return false;
    processCamera();
    processTracks();
    skipNow_ = skipAll_ = false;
    ++frame_;
    return true;
}

void Sequence::stop(bool) {
    if (!playing_) return;
    const bool outOfSync = frame_ > callbackFrame_;   // skipped past callbacks: "OutofSync sequence End"
    const bool all = skipAll_;
    playing_ = false;
    frame_ = cameraIndex_ = callbackIndex_ = -1;
    if (outOfSync) fire(all && slots_[kSkippedWithB] ? kSkippedWithB : kSkipped);
    else fire(kEnded);
    if (loop_ && !playing_) play(true);
}

void Sequence::processCallbacks() {
    if (callbackIndex_ != -1 && !callbacks_.empty() && callbacks_[callbackIndex_].frame == callbackFrame_) {
        const int id = callbacks_[callbackIndex_].id;
        if (callbackIndex_ + 1 != (int)callbacks_.size()) ++callbackIndex_;
        if (id == kFadeOutCheck) std::printf("\n\n FADEOUT CHECK CALLBACK AT FRAME %d\n", frame_);
        fire(id);
    }
    ++callbackFrame_;
}

void Sequence::processCamera() {
    const int n = cameraCount_;
    if (cameraIndex_ != -1 && n && cameraIndex_ < n && cameras_[cameraIndex_].frame == cameraFrame_) {
        const int at = cameraIndex_;
        const CameraKey& k = cameras_[at];
        const bool inGame = k.kind == 1 || k.kind == 2;
        if (inGame && at == n - 1) {   // "End in-game camera!": back to the game's camera
            if (restoreCamera) restoreCamera();
        } else if (!(inGame && at == 0) && (!skipAll_ || fastForwarded_) && placeCamera) {
            const int nextMove = at + 1 < n ? cameras_[at + 1].movement : 0;
            if (k.movement == 1)
                placeCamera(k.pos, (uint16_t)(0xC000 - k.pitch), (uint16_t)k.yaw, 0, nextMove);
            if (at + 1 < n && nextMove != 1) {
                const CameraKey& next = cameras_[at + 1];
                placeCamera(next.pos, (uint16_t)(0xC000 - next.pitch), (uint16_t)next.yaw, next.frame - k.frame,
                            nextMove);
            }
        }
        if (at + 1 != n) ++cameraIndex_;
    }
    ++cameraFrame_;
}

void Sequence::processTracks() {
    for (BgTrack& t : bgs) t.update(frame_);
    for (SpriteTrack& t : sprites) t.update(frame_);
    for (TextTrack& t : texts) t.update(frame_);
    if (fades_.empty()) return;
    const std::vector<FadeKey>& keys = fades_[0];
    float value = 1.f;
    int at = -1;
    for (int i = 0; i < (int)keys.size(); ++i) if (keys[i].frame <= frame_) at = i;
    if (at != -1) {
        const FadeKey& k = keys[at];
        float t = 1.f;
        if (k.frames) t = std::min(1.f, std::max(0.f, (float)(frame_ - k.frame) / k.frames));
        value = k.in ? t : 1.f - t;
    }
    fade = value;
}

Sequence::SkipState Sequence::skipState(int f) const {
    SkipState s;
    const std::vector<SkipKey>& k = skips_[0];
    const int n = (int)k.size();
    if (n < 1) return s;
    int at = -1;
    bool end = false;
    for (int i = 0; i < n; ++i) {
        if (k[i].frame <= f) { at = i; end = k[i].kind == 2; }
        if (k[i].kind == 2) s.end = k[i].frame;
    }
    if (at == -1) {
        s.next = k[0].frame;
        s.tap = false;
        s.catchCamera = k[0].catchCamera != 0;
        return s;
    }
    s.tap = end ? false : k[at].tap != 0;
    s.hold = end ? false : k[at].kind == 1;
    s.catchCamera = k[at].catchCamera != 0;
    s.catchCallbacks = k[at].catchCallbacks != 0;
    if (at < n - 1 && !end) {
        s.next = k[at + 1].frame;
        s.nextCatchCamera = k[at + 1].catchCamera != 0;
        s.nextCatchCallbacks = k[at + 1].catchCallbacks != 0;
    }
    return s;
}

bool Sequence::updateSkip(bool tapped, bool held, bool startPressed, bool screenFading) {
    showSkip_ = false;
    fastForwarded_ = false;
    if (skips_.empty()) return false;
    const SkipState s = skipState(frame_);
    if (s.catchCamera) while (frame_ > cameraFrame_) { processCamera(); if (fastForward) fastForward(); }
    if (s.catchCallbacks) while (frame_ > callbackFrame_ && playing_) processCallbacks();
    if (!playing_) return false;
    if (s.hold && !screenFading) {
        bool start = startPressed;
        if (held) {
            if (++holdFrames_ >= 36) { holdFrames_ = 0; start = true; }
        } else holdFrames_ = 0;
        if (start) skipAll_ = true;
    }
    if (!skipNow_ && !skipAll_ && tapped) {
        if (textPrinting) { skipTextPrinting = true; return false; }
        if (s.tap) skipNow_ = true;
    }
    if (!screenFading && s.tap && !skipNow_ && !skipAll_) {
        showSkip_ = true;   // the skip icon
        return false;
    }
    int target;
    if (skipNow_) target = skipAll_ ? s.end : s.next;
    else if (skipAll_) target = s.end;
    else return false;
    std::printf("skipToPoint: %d\n", target);
    if (target == -1) { ++frame_; return true; }
    frame_ = target;
    if (s.nextCatchCamera || skipAll_) {
        while (target > cameraFrame_) { processCamera(); if (fastForward) fastForward(); }
        fastForwarded_ = true;
    }
    if (s.nextCatchCallbacks) {
        while (frame_ > callbackFrame_ && playing_) processCallbacks();
        fastForwarded_ = true;
    }
    if (!playing_) return false;
    processTracks();
    return target == lastFrame_ - 1;
}
