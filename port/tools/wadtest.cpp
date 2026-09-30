// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "os/wad.h"
#include <cstdio>
int main(int argc, char** argv) {
    Wad w;
    if (!w.open(argc > 1 ? argv[1] : "data")) { puts("open failed"); return 1; }
    printf("entries: %zu\n", w.count());
    const char* names[] = {"SS_Portraits.bin", "SS_Hud.bin", "ammunation_hel16x16.bin", "nonexistent.xyz"};
    int rc = 0;
    for (auto n : names) {
        std::vector<uint8_t> d;
        bool ok = w.read(n, d);
        printf("%-28s %s size=%zu first=%02x\n", n, ok ? "OK  " : "MISS", d.size(), ok && !d.empty() ? d[0] : 0);
    }
    return rc;
}
