// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "dxtbin.h"
#include <cstdio>
#include <cstring>

// Only the 32 KB offset table is kept in memory; texture blobs are read on demand (the file is ~50 MB).
bool DxtBin::open(const std::string& path) {
    if (!file_.open(path)) return false;
    fileSize_ = (size_t)file_.size();
    offs_.resize(0x2000);
    if (fileSize_ < 0x8000 || file_.read(offs_.data(), 4 * 0x2000) != 4 * 0x2000) return false;
    ids_.clear();
    for (int i = 0; i < 0x2000; ++i)
        if (offs_[i]) ids_.push_back(i);
    return true;
}

bool DxtBin::get(int id, DxtTexture& t) const {
    if (!file_.isOpen() || id < 0 || id >= 0x2000 || !offs_[id]) return false;
    size_t o = offs_[id];
    if (o + 12 > fileSize_) return false;
    uint8_t hdr[12];
    if (!file_.seek(o) || file_.read(hdr, 12) != 12) return false;
    uint16_t w, h, fmt;
    memcpy(&w, hdr, 2);
    memcpy(&h, hdr + 2, 2);
    memcpy(&fmt, hdr + 4, 2);
    // Same size rule as the original GetDXTData: DXT5 (0x83F3) is 8 bpp, everything else 4 bpp.
    size_t sz = fmt == 0x83f3 ? (size_t)w * h : (size_t)w * h / 2;
    if (o + 12 + sz > fileSize_) return false;
    t.id = id; t.width = w; t.height = h; t.glFormat = fmt;
    t.data.resize(sz);
    return file_.read(t.data.data(), sz) == sz;
}
