// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#pragma once
#include <string>

// The new game's intro movie. kena01::cIntroDrive::StartMovie plays CTW_iPhone_intro.mp4 (OS_MoviePlayinWindow, not
// skippable by a tap); Android's MediaPlayer stretches it over the whole screen, the game's own sounds are paused
// (cKEN_A01::StartIntro: PauseSound) and UpdateMovieCallback stops it once a touch or any pad button has been held
// for more than a second. The plane prologue before it and the rest of the first mission are not ported yet.
namespace IntroMovie {
// Plays the movie to its end or until skipped. False when the window was closed meanwhile.
bool play(const std::string& dataDir);
// UpdateMovieCallback's skip timer: `held` is whether anything is held this tick, `nowMs` the time (OS_TimeMS).
// Returns true when the movie should stop.
bool skipHeld(bool held, unsigned nowMs, unsigned& startMs);
}
