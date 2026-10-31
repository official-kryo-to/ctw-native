// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// cContextHelp (gpContextHelp), the script that owns the help box: missions show their help through it
// (DisplayMissionHelpKey) and it holds the box (+0xE8) until the help's frames and a pause have passed. Its own
// tutorial hints (cContextHelp::ContextHelp after the lock is let go: wanted level, health, e-mail, ...) are not
// ported; it stops there.
#pragma once
#include <functional>
#include <string>

class Game;

class ContextHelp {
public:
    // DisplayMissionHelpKey(id, force, frames, overObjective, callback): the help unless it is showing already (its
    // timer restarts) or the screen is busy; true if it is (or was already) shown
    bool displayMissionHelpKey(Game& g, const std::u16string& text, bool force, unsigned frames, bool overObjective,
                               std::function<void()> callback = nullptr);
    // cContextHelp::DisplayHelp(id, frames, brief, callback): only while the box is free
    bool displayHelp(Game& g, const std::u16string& text, unsigned frames, std::function<void()> callback = nullptr);
    void clearHelp(Game& g);               // cContextHelp::ClearHelp
    bool locked() const { return locked_; }
    void tick(Game& g);                    // its TimerImpl::Wait, once a frame

private:
    bool isSafeForHelp(Game& g, bool ignoreCurrent, bool overObjective) const;   // IsSafeForHelp
    void postHelpPause(bool loop, unsigned frames);   // PostHelpPause
    void loopHelp(Game& g);                // LoopHelp
    void contextHelp();                    // ContextHelp: the lock goes (the hints after it are not ported)
    enum Next { kNone, kShortPause, kLoop };
    bool locked_ = false;                  // +0xE8
    int wait_ = 0;
    Next next_ = kNone;
};
