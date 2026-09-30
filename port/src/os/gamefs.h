// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// The game's virtual filesystem: ROM.WAD plus loose files, routed the way the original
// cFileManager::Open / IsNonWad does it (loose files win for .gxt, jp/*, GTACTWJapanese,
// IPhone_Hel_20x20_lrg and, unless "old fonts" is set, IPhone_Hel*; everything else comes from the WAD).
#pragma once
#include "wad.h"
#include <map>
#include <string>
#include <vector>

class GameFs {
public:
    bool open(const std::string& dataDir);
    bool read(const std::string& name, std::vector<uint8_t>& out) const;
    bool exists(const std::string& name) const;
    static bool isNonWad(const std::string& name, bool useOldFonts = false);
    void setUseOldFonts(bool v) { oldFonts_ = v; }
private:
    std::string loosePath(const std::string& name) const;   // "" if there is no such loose file
    Wad wad_;
    std::string dir_;
    std::map<std::string, std::string> loose_;              // lower-case name -> real path
    bool oldFonts_ = false;
};
