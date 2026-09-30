// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "gamefs.h"
#include "datafile.h"
#include <algorithm>
#include <cctype>
#include <cstdio>

static std::string lower(std::string s) {
    for (auto& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

bool GameFs::open(const std::string& dataDir) {
    dir_ = dataDir;
    for (const std::string& rel : Data_List(dataDir)) loose_[lower(rel)] = dataDir + "/" + rel;
    return wad_.open(dataDir);
}

bool GameFs::isNonWad(const std::string& name, bool useOldFonts) {
    std::string n = lower(name);
    return n.find(".gxt") != std::string::npos || n.compare(0, 3, "jp/") == 0 ||
           n.find("gtactwjapanese") != std::string::npos ||
           n.find("iphone_hel_20x20_lrg") != std::string::npos ||
           (!useOldFonts && n.find("iphone_hel") != std::string::npos);
}

std::string GameFs::loosePath(const std::string& name) const {
    std::string n = lower(name);
    for (auto& c : n) if (c == '\\') c = '/';
    auto it = loose_.find(n);
    return it == loose_.end() ? std::string() : it->second;
}

bool GameFs::exists(const std::string& name) const {
    if (isNonWad(name, oldFonts_) && !loosePath(name).empty()) return true;
    return wad_.exists(name.c_str());
}

bool GameFs::read(const std::string& name, std::vector<uint8_t>& out) const {
    if (isNonWad(name, oldFonts_)) {
        std::string p = loosePath(name);
        if (!p.empty()) {
            return Data_ReadAll(p, out);
        }
    }
    return wad_.read(name.c_str(), out);   // Open() falls back to the WAD when no loose file exists
}
