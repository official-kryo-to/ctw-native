// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// Cutscene sequences (cSequence and its tracks) and the sequence camera, on a made-up .seq, without game data.
#include "sequence.h"
#include <cstdio>
#include <cstring>
#include <vector>

static int failures;
static void check(bool value, const char* message) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); ++failures; }
}

namespace {
struct File {
    std::vector<uint8_t> d;
    template <class T> void put(T v) {
        const size_t at = d.size();
        d.resize(at + sizeof v);
        std::memcpy(d.data() + at, &v, sizeof v);
    }
    void i32(int32_t v) { put(v); }
    void u16(uint16_t v) { put(v); }
    void u8(uint8_t v) { put(v); }
};

// two cameras, two callbacks, a background that slides, a sprite, a subtitle, skip points and a fade in
std::vector<uint8_t> makeFile() {
    File f;
    for (int32_t v : {1, 2, 2, 1, 1, 1, 1, 1}) f.i32(v);
    const char name[16] = "CKNA01";
    for (char c : name) f.u8((uint8_t)c);
    for (int i = 0; i < 2; ++i) {   // cameras: a cut at frame 0, then a linear move to x = 30 by frame 30
        f.i32(i ? 30 : 0);
        f.i32(i ? 30 * 4096 : 0); f.i32(0); f.i32(0);
        f.i32(i ? 3 : 1); f.i32(0); f.i32(0);
        f.u16(0x1000); f.u16(0x2000);
    }
    f.i32(10); f.i32(0);   // callbacks: id 0 at frame 10, id 1 at frame 20
    f.i32(20); f.i32(1);
    f.i32(3); f.i32(2);    // background layer 3: from x 0 to 100 linearly over frames 0..20
    for (int i = 0; i < 2; ++i) {
        f.u16(1); f.u16(0);
        f.i32(i ? 100 * 4096 : 0); f.i32(0);
        f.i32(1); f.i32(i ? 0 : 1); f.i32(i ? 20 : 0);
        f.i32(0);
    }
    f.i32(5); f.i32(1); f.i32(1);   // sprite 5, drawn in front of the second background, from frame 5
    f.i32(0); f.i32(0); f.i32(1); f.u16(1); f.u16(0); f.i32(5); f.u16(255); f.u16(1);
    f.i32(2);   // a subtitle shown from frame 2 to 15
    f.i32(0); f.u8(0); f.u8(1); f.u8(1); f.u8(0); f.i32(2);
    f.i32(0); f.u8(0); f.u8(0); f.u8(1); f.u8(0); f.i32(15);
    f.i32(3);   // skip points: holding skips from the start, tapping from 12, the end at 25
    f.i32(0); f.u8(0); f.u8(1); f.u8(0); f.u8(0);
    f.i32(12); f.u8(1); f.u8(1); f.u8(1); f.u8(1);
    f.i32(25); f.u8(0); f.u8(2); f.u8(0); f.u8(0);
    f.i32(1);   // the overlay fades in over 10 frames
    f.i32(0); f.u8(1); f.u8(10); f.u16(0);
    return f.d;
}
}

int main() {
    const std::vector<uint8_t> file = makeFile();
    {   // playing it through
        Sequence s;
        check(s.parse(file, "test.seq"), "the sequence parses");
        check(s.textTable() == "CKNA01", "the subtitle table comes from the header");
        check(s.bgs.size() == 1 && s.sprites.size() == 1 && s.texts.size() == 1, "one track of each kind");
        int places = 0, firstFrames = -1, cb0 = -1, cb1 = -1, ended = 0, skipped = 0;
        uint16_t firstPitch = 0;
        s.placeCamera = [&](const int32_t*, uint16_t pitch, uint16_t, int frames, int) {
            if (places++ == 0) { firstFrames = frames; firstPitch = pitch; }
        };
        s.setCallback(0, [&] { cb0 = s.frame(); });
        s.setCallback(1, [&] { cb1 = s.frame(); });
        s.setCallback(Sequence::kEnded, [&] { ++ended; });
        s.setCallback(Sequence::kSkipped, [&] { ++skipped; });
        s.play();
        check(places == 2 && firstFrames == 0, "the first key cuts, then the move to the second key starts");
        check(firstPitch == 0xB000, "the pitch is 0xC000 minus the key's");
        float fadeAt5 = -1, bgAt10 = -1;
        bool textShown = false, spriteAt6 = false;
        int frames = 0;
        while (s.update(false, false, false, false) && frames < 100) {
            ++frames;
            if (s.frame() == 6) { fadeAt5 = s.fade; spriteAt6 = s.sprites[0].visible != 0; }
            if (s.frame() == 11) bgAt10 = s.bgs[0].pos[0] / 4096.f;
            textShown |= s.texts[0].shown != 0;
        }
        check(cb0 == 10 && cb1 == 20, "the callbacks fire on their frames");
        check(ended == 1 && skipped == 0, "a sequence played to the end reports the normal end");
        check(fadeAt5 == 0.5f, "the fade is halfway in after five of its ten frames");
        check(bgAt10 == 50.f, "the background is halfway along at frame 10");
        check(spriteAt6 && textShown, "the sprite and the subtitle come up");
        check(frames == 31, "the sequence runs to its last camera key");
    }
    {   // tapping on to the next point, and holding to skip
        Sequence s;
        s.parse(file, "test.seq");
        int skipped = 0;
        s.setCallback(Sequence::kSkipped, [&] { ++skipped; });
        s.play();
        for (int i = 0; i < 3; ++i) s.update(true, false, false, false);
        check(s.frame() == 3, "before the first tap point a tap does nothing");
        while (s.frame() < 13) s.update(false, false, false, false);
        s.update(true, false, false, false);
        check(s.frame() == 26, "a tap moves on to the next skip point");
        Sequence h;
        h.parse(file, "test.seq");
        h.play();
        h.update(false, false, false, false);
        h.update(false, false, true, false);   // Start / Escape
        check(h.frame() == 26, "skipping goes to the end point");
        Sequence t;
        t.parse(file, "test.seq");
        t.play();
        for (int i = 0; i < 20; ++i) t.update(false, true, false, false);
        check(t.frame() == 20, "holding skips only after 36 frames");
    }
    {   // the sequence camera
        SequenceCamera c;
        const int32_t a[3] = {0, 0, 0}, b[3] = {30 * 4096, 0, 0};
        c.place(a, 0, 0, 0, 1);
        check(c.active && c.eye[0] == 0, "a cut puts the camera there");
        c.place(b, 0x1000, 0, 30, 3);
        for (int i = 0; i < 15; ++i) c.tick();
        // (cBaseCam::Update moves a share of what is left each frame, so the angle is only nearly halfway)
        check(c.eye[0] == 15 * 4096 && c.pitch >= 0x7F0 && c.pitch <= 0x810, "a linear move is halfway after half its frames");
        for (int i = 0; i < 15; ++i) c.tick();
        check(c.eye[0] == 30 * 4096 && c.pitch == 0x1000, "and there at the end");
        c.place(a, 0, 0, 30, 0);
        for (int i = 0; i < 30; ++i) c.tick();
        check(c.eye[0] == 0 && c.pitch == 0, "an eased move ends exactly on the key");
    }
    if (failures) std::fprintf(stderr, "%d failure(s)\n", failures);
    else std::printf("sequence tests passed\n");
    return failures ? 1 : 0;
}
