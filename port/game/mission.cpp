// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include <cstddef>
#include <algorithm>
#include "mission.h"
#include "text/gxt.h"
#include <cctype>

void Mission::wait(int frames, std::function<void()> fn) { waits_.push_back({frames, std::move(fn)}); }

void Mission::when(std::function<bool()> test, std::function<void()> fn) {
    whens_.push_back({std::move(test), std::move(fn)});
}

void Mission::cancelAll() {
    waits_.clear();
    whens_.clear();
    ++generation_;
}

void Mission::tick(Game& game) {
    if (finished_) return;
    onFrame(game);
    if (finished_) return;
    const unsigned gen = generation_;
    // the callbacks may add or cancel waits: run the due ones from copies
    std::vector<std::function<void()>> due;
    for (size_t i = 0; i < waits_.size();) {
        if (--waits_[i].frames <= 0) { due.push_back(std::move(waits_[i].fn)); waits_.erase(waits_.begin() + i); }
        else ++i;
    }
    for (size_t i = 0; i < whens_.size();) {
        if (whens_[i].test()) { due.push_back(std::move(whens_[i].fn)); whens_.erase(whens_.begin() + i); }
        else ++i;
    }
    for (auto& fn : due) {
        if (generation_ != gen || finished_) break;
        fn();
    }
}

void ScriptProcess::wait(int frames, std::function<void()> fn) { waits_.push_back({frames, std::move(fn)}); }

void ScriptProcess::when(std::function<bool()> test, std::function<void()> fn) {
    whens_.push_back({std::move(test), std::move(fn)});
}

void ScriptProcess::stop() {
    waits_.clear();
    whens_.clear();
    ++generation_;
}

void ScriptProcess::tick() {   // as Mission::tick: the due callbacks from copies, until one stops the process
    const unsigned gen = generation_;
    std::vector<std::function<void()>> due;
    for (size_t i = 0; i < waits_.size();) {
        if (--waits_[i].frames <= 0) { due.push_back(std::move(waits_[i].fn)); waits_.erase(waits_.begin() + i); }
        else ++i;
    }
    for (size_t i = 0; i < whens_.size();) {
        if (whens_[i].test()) { due.push_back(std::move(whens_[i].fn)); whens_.erase(whens_.begin() + i); }
        else ++i;
    }
    for (auto& fn : due) {
        if (generation_ != gen) break;
        fn();
    }
}

bool Mission::loadText(const std::string& dataDir, const std::string& name) {
    GxtFile gxt;
    std::string file = name;
    for (char& c : file) c = (char)std::tolower((unsigned char)c);
    if (!gxt.load(dataDir + "/e_" + file + ".gxt")) return false;
    text_.clear();
    for (size_t i = 0; i < gxt.count(); ++i) text_.push_back(gxt.get(i));
    return true;
}

std::u16string Mission::text(int id) const {
    const int i = id - 0x52F;   // the mission's strings follow e_main.gxt's
    if (i < 0 || i >= (int)text_.size()) return {};
    std::u16string s = text_[i];
    while (!s.empty() && s.back() == 0) s.pop_back();
    return s;
}

void ScreenFade::toBlack(int frames, std::function<void()> done) {
    done_ = std::move(done);
    white = false;   // a fade to black (the flash bang re-sets white after priming the fade)
    target_ = 0x10;
    const int32_t rest = 0x10000 - level_, step = rest / std::max(frames, 1);
    step_ = step ? step : rest;
    alpha = level_ / 65536.f;
    if (step_ == 0 && done_) { auto fn = std::move(done_); done_ = nullptr; fn(); }
}

void ScreenFade::in(int frames, std::function<void()> done) {
    done_ = std::move(done);
    target_ = 0;
    const int32_t step = -level_ / std::max(frames, 1);
    step_ = step ? step : -level_;
    alpha = level_ / 65536.f;
    if (step_ == 0 && done_) { auto fn = std::move(done_); done_ = nullptr; fn(); }
}

void ScreenFade::tick() {
    if (step_ == 0) return;
    level_ += step_;
    const int32_t end = target_ << 12;
    const bool over = step_ > 0 ? ((level_ >> 12) >= target_ || end - step_ < level_)
                                : ((level_ >> 12) <= target_ || level_ < end + step_);
    if (over) {
        level_ = end;
        step_ = 0;
    }
    alpha = level_ / 65536.f;
    if (over && done_) { auto fn = std::move(done_); done_ = nullptr; fn(); }
}
