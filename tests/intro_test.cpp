// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "intro.h"
#include "video/movie.h"
#include <cstdio>

static int failures;
static void check(bool ok, const char* what) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
}

int main() {
    unsigned start = 0;
    check(!IntroMovie::skipHeld(false, 5000, start) && start == 0, "nothing held keeps the movie running");
    check(!IntroMovie::skipHeld(true, 5000, start) && start == 5000, "the first held tick starts the timer");
    check(!IntroMovie::skipHeld(true, 6000, start), "exactly one second held is not enough");
    check(IntroMovie::skipHeld(true, 6001, start), "more than a second held stops the movie");
    check(!IntroMovie::skipHeld(false, 6034, start) && start == 0, "letting go resets the timer");
    check(!IntroMovie::skipHeld(true, 7000, start) && !IntroMovie::skipHeld(true, 7900, start),
          "a new hold starts counting again");
    Movie movie;
    check(!movie.open("missing-intro.mp4") && !movie.update(1.0) && !movie.frameReady(),
          "a missing movie fails safely and is skipped");
    return failures ? 1 : 0;
}
