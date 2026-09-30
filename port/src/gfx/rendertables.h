// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
// Layout of render lookup data prepared locally by scripts/setup_game.py.
#pragma once
#include <cstdint>
#include <string>

struct RenderTables {
    int32_t renderList[9]{};
    uint8_t layerSlot[4][6]{};
    int32_t angle[8][2]{};
    uint8_t textColour[8][4]{};
    bool load(const std::string& path);
};
