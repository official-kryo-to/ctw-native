// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "rendertables.h"
#include <cstring>
#include "os/datafile.h"
#include <sstream>

bool RenderTables::load(const std::string& path) {
    *this = RenderTables{};
    RenderTables next;
    std::vector<uint8_t> bytes;
    if (!Data_ReadAll(path, bytes)) return false;
    std::istringstream file(std::string(bytes.begin(), bytes.end()), std::ios::binary);
    char header[8];
    if (!file.read(header, sizeof header) || std::memcmp(header, "CTWREND1", 8) ||
            !file.read(reinterpret_cast<char*>(next.renderList), sizeof next.renderList) ||
            !file.read(reinterpret_cast<char*>(next.layerSlot), sizeof next.layerSlot) ||
            !file.read(reinterpret_cast<char*>(next.angle), sizeof next.angle) ||
            !file.read(reinterpret_cast<char*>(next.textColour), sizeof next.textColour) ||
            file.peek() != std::istream::traits_type::eof()) return false;
    for (int32_t list : next.renderList) if (list < 0 || list > 255) return false;
    for (const auto& body : next.layerSlot)
        for (uint8_t slot : body) if (slot >= 16) return false;
    for (const auto& direction : next.angle)
        for (int32_t component : direction) if (component < -4096 || component > 4096) return false;
    *this = next;
    return true;
}
