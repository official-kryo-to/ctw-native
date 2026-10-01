#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kryo.to
# See LICENSE in the repository root.
#
# Checks that the Porter's Rust table extraction (porter/core/src/tables.rs) produces the same bytes as
# scripts/extract_tables.py. No game files are needed:
#   1. synthetic ARM64 "libGame.so" builds (fixture/, carrying the supported build ID) must give identical tables;
#   2. the register tracking must agree on every exported function of the ARM64 system libraries.
# Needs: g++-aarch64-linux-gnu, Python with requirements-setup.txt, cargo.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
porter="$(cd "$here/../.." && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

cargo build --quiet --release --manifest-path "$porter/Cargo.toml" -p ctw-porter-core --example parity
rust="$porter/target/release/examples/parity"

python3 - "$work/blob.bin" <<'PY'
import random, struct, sys
random.seed(7)
blob = bytearray(random.randrange(256) for _ in range(0x30000))
blob[0x27250:0x27250 + 48] = struct.pack('<6q', 4096, -4096, 123, 0, 2**31 - 1, -2**31)  # int64 table, fits int32
open(sys.argv[1], 'wb').write(blob)
PY
cp "$here"/fixture/* "$work/"
fail=0
compare() {  # compare <label> <python output> <rust output>
    sed 's/^ERROR.*/ERROR/; s/ ERROR .*/ ERROR/' "$2" > "$work/py.normalized"
    sed 's/^ERROR.*/ERROR/; s/ ERROR .*/ ERROR/' "$3" > "$work/rs.normalized"
    if cmp -s "$work/py.normalized" "$work/rs.normalized"; then
        echo "same: $1 ($(wc -l < "$2") lines)"
    else
        echo "DIFFERENT: $1"
        # diff returns 1 for a mismatch; do not let pipefail stop the remaining checks or hide the final result.
        diff -U2 "$work/py.normalized" "$work/rs.normalized" | head -25 | cut -c1-200 || true
        fail=1
    fi
}
for opt in O1 O2 Os; do
    so="$work/libGame_$opt.so"
    (cd "$work" && aarch64-linux-gnu-g++ -$opt -fPIC -shared -o "$so" game.cpp data.cpp blob.S restart.S \
        -Wl,--build-id=0xa4c441f4943abbcc72e8270ec18248e4358a89e2 -Wl,--section-start=.blob=0x460000)
    python3 "$here/parity.py" tables "$so" > "$work/py.txt"
    "$rust" tables "$so" > "$work/rs.txt"
    grep -q ERROR "$work/py.txt" && { echo "fixture $opt did not extract"; fail=1; }
    for table in population sound render gameplay radio restart; do
        grep -q "^${table}_tables.bin " "$work/py.txt"
        grep -q "^${table}_tables.bin " "$work/rs.txt"
    done
    compare "tables, fixture -$opt" "$work/py.txt" "$work/rs.txt"
done
for lib in libc.so.6 libm.so.6 libstdc++.so.6; do
    so="$(readlink -f "/usr/aarch64-linux-gnu/lib/$lib")"
    python3 - "$so" > "$work/names.txt" <<'PY'
import sys
from elftools.elf.elffile import ELFFile
symbols = ELFFile(open(sys.argv[1], 'rb')).get_section_by_name('.dynsym').iter_symbols()
print('\n'.join(sorted({s.name for s in symbols if s['st_info']['type'] == 'STT_FUNC' and s['st_size'] and s['st_value']})))
PY
    python3 "$here/parity.py" calls "$so" "$work/names.txt" > "$work/py.txt"
    "$rust" calls "$so" "$work/names.txt" > "$work/rs.txt"
    compare "calls, $lib" "$work/py.txt" "$work/rs.txt"
done
exit $fail
