// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
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

void Mission::tick(Game&) {
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
    if (frames <= 0) { alpha = 1.f; step_ = 0.f; }
    else step_ = 1.f / frames;
    if (step_ == 0.f && done_) { auto fn = std::move(done_); done_ = nullptr; fn(); }
}

void ScreenFade::in(int frames, std::function<void()> done) {
    done_ = std::move(done);
    if (frames <= 0) { alpha = 0.f; step_ = 0.f; }
    else step_ = -1.f / frames;
    if (step_ == 0.f && done_) { auto fn = std::move(done_); done_ = nullptr; fn(); }
}

void ScreenFade::tick() {
    if (step_ == 0.f) return;
    alpha += step_;
    if (alpha <= 0.f || alpha >= 1.f) {
        alpha = alpha <= 0.f ? 0.f : 1.f;
        step_ = 0.f;
        if (done_) { auto fn = std::move(done_); done_ = nullptr; fn(); }
    }
}
