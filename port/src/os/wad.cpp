// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "wad.h"
#include <algorithm>
#include <cstring>


uint32_t Wad::hashName(const char* s) {
    uint32_t h = 0;
    for (; *s; ++s) {
        uint8_t c = (uint8_t)*s;
        if (c >= 'a' && c <= 'z') c -= 0x20;
        h = (h + c) * 0x401u;
        h ^= h >> 6;
    }
    h *= 9u;
    return (h ^ (h >> 11)) * 0x8001u;
}

bool Wad::open(const std::string& dir) {
    std::string tocPath = dir + "/rom.toc", wadPath = dir + "/rom.wad";
    std::vector<uint8_t> toc;
    if (!Data_ReadAll(tocPath, toc)) return false;
    toc_.resize(toc.size() / sizeof(Entry));
    if (!toc_.empty()) memcpy(toc_.data(), toc.data(), toc_.size() * sizeof(Entry));
    return file_.open(wadPath);
}

const Wad::Entry* Wad::find(const char* name) const {
    uint32_t h = hashName(name);
    auto it = std::lower_bound(toc_.begin(), toc_.end(), h,
                               [](const Entry& e, uint32_t v) { return e.hash < v; });
    return (it != toc_.end() && it->hash == h) ? &*it : nullptr;
}

bool Wad::exists(const char* name) const { return find(name) != nullptr; }

bool Wad::size(const char* name, uint32_t* out) const {
    const Entry* e = find(name);
    if (!e) return false;
    *out = e->size;
    return true;
}

bool Wad::read(const char* name, std::vector<uint8_t>& out) const {
    const Entry* e = find(name);
    if (!e || !file_.isOpen()) return false;
    out.resize(e->size);
    if (!file_.seek(e->offset)) return false;
    return file_.read(out.data(), e->size) == e->size;
}
