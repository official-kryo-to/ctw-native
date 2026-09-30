// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "gxt.h"
#include "os/datafile.h"
#include <cstdio>
#include <cstring>

bool GxtFile::parse(const std::vector<uint8_t>& b) {
    strings_.clear();
    if (b.size() < 8 || memcmp(b.data(), "DS_GXT", 6) != 0) return false;
    uint16_t n;
    memcpy(&n, &b[6], 2);
    size_t i = 8;
    strings_.reserve(n);
    for (uint16_t k = 0; k < n; ++k) {
        if (i + 2 > b.size()) return false;
        uint16_t len;
        memcpy(&len, &b[i], 2);
        i += 2;
        if (i + (size_t)len * 2 > b.size()) return false;
        std::u16string s(len, u'\0');
        memcpy(&s[0], &b[i], (size_t)len * 2);
        strings_.push_back(std::move(s));
        i += (size_t)len * 2;
    }
    return i == b.size();   // the format has no trailing data
}

bool GxtFile::load(const std::string& path) {
    std::vector<uint8_t> b;
    return Data_ReadAll(path, b) && parse(b);
}
