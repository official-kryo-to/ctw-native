// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "contexthelp.h"
#include "game.h"

// IsSafeForHelp: not in a cutscene, with the screen neither black nor fading, no help showing (unless that is
// ignored), the player not being wasted or busted, and no objective on screen (unless the help may go over it)
bool ContextHelp::isSafeForHelp(Game& g, bool ignoreCurrent, bool overObjective) const {
    if (g.cutscene) return false;                                       // CameraImpl::IsCutsceneRunning
    if ((g.screenFade.level() & ~0xFFF) == 0x10000) return false;       // IsScreenFaded(true, false)
    if (g.screenFade.fading()) return false;                            // IsScreenFading(true, false)
    if (g.hudText.currentHelp() && !ignoreCurrent) return false;
    if (g.deathScreen.active()) return false;   // (HasDeathArrestBeenExecuted: the death or arrest screen here)
    return !g.hudText.isObjectiveBeingDisplayed() || overObjective;
}

// PostHelpPause: the script's waits stop; with frames, a wait of that many (then DoShortPause), or of 90 before
// LoopHelp. (Its watches for cheats, swapped weapons, being busted and the trip skip are the tutorial's: not ported.)
void ContextHelp::postHelpPause(bool loop, unsigned frames) {
    next_ = kNone;
    if (!frames) return;
    wait_ = loop ? 0x5A : (int)frames;
    next_ = loop ? kLoop : kShortPause;
}

void ContextHelp::loopHelp(Game& g) {
    if (isSafeForHelp(g, false, false)) contextHelp();
    else postHelpPause(true, 0xF0);   // DoShortPause
}

void ContextHelp::contextHelp() {
    next_ = kNone;
    locked_ = false;
}

void ContextHelp::tick(Game& g) {
    if (next_ == kNone || --wait_ > 0) return;
    const Next n = next_;
    next_ = kNone;
    if (n == kShortPause) postHelpPause(true, 0xF0);   // DoShortPause
    else loopHelp(g);
}

bool ContextHelp::displayMissionHelpKey(Game& g, const std::u16string& text, bool force, unsigned frames,
                                        bool overObjective, std::function<void()> callback) {
    const std::u16string* current = g.hudText.currentHelp();
    if (current && *current == text) {   // showing already: its timer from the start
        g.hudText.resetCurrentHelpTimer();
        postHelpPause(false, 0xF0);
        return true;
    }
    if (!overObjective && g.hudText.isObjectiveBeingDisplayed()) return false;
    if (!force) return displayHelp(g, text, frames, std::move(callback));
    if (!isSafeForHelp(g, true, overObjective)) return false;
    locked_ = true;
    g.hudText.displayHelp(text, frames, true, true, std::move(callback));   // HUDImpl::DisplayHelp(id, 2, frames, ..)
    postHelpPause(false, frames);
    return true;
}

bool ContextHelp::displayHelp(Game& g, const std::u16string& text, unsigned frames, std::function<void()> callback) {
    if (locked_) return false;
    if (!isSafeForHelp(g, false, false)) {
        postHelpPause(true, 0xF0);
        return false;
    }
    locked_ = true;
    g.hudText.displayHelp(text, frames, true, true, std::move(callback));
    postHelpPause(false, frames);
    return true;
}

void ContextHelp::clearHelp(Game& g) {
    if (!locked_) return;
    g.hudText.clearHelp();
    postHelpPause(true, 0xF0);
}
