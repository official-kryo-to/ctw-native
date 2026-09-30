// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// game.pak resource archive (verified: all 4180 entries tile the file exactly, no overlaps).
//   Header: u32 version, u32 seg1, u32 seg2, u32 seg3, u32 count, u32 endPage; u16 page table from byte 0x18.
//   The table does not fit in the first 4 KB page: like cResourceManager::Init, the remainder is read from
//   endPage * 4096 (the tail of the file). Pages are 4 KB; the u16 page wraps every 0x10000 pages, so
//   page = table[id] + 0x10000 * segment, segment = 0 below seg1, 1 below seg2, 2 below seg3, else 3.
//   size = (table[id+1] - table[id]) & 0xFFFF pages; 0xFFFF entries mean "no resource".
#pragma once
#include <cstdint>
#include "datafile.h"
#include <cstdio>
#include <string>
#include <vector>

class Pak {
public:
    Pak() = default;
    Pak(const Pak&) = delete;
    Pak& operator=(const Pak&) = delete;
    bool open(const std::string& path);
    uint32_t count() const { return count_; }
    bool sizeBytes(uint32_t id, uint32_t* out) const;
    bool read(uint32_t id, std::vector<uint8_t>& out) const;   // whole page-rounded resource
    bool readPrefix(uint32_t id, size_t maxBytes, std::vector<uint8_t>& out) const;
private:
    uint64_t pageOf(uint32_t id) const;
    mutable DataFile file_;
    uint32_t seg1_ = 0, seg2_ = 0, seg3_ = 0, count_ = 0, endPage_ = 0;
    std::vector<uint16_t> table_;
};
