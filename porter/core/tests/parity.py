# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Kryo.to
# See LICENSE in the repository root.
"""The Python side of the parity check (see parity.sh): prints what scripts/extract_tables.py sees, in the same
form as `cargo run --example parity`.

    parity.py tables <libGame.so>              all six tables, hex
    parity.py calls <any.so> <names file>      constant_calls for each function name (build ID not checked)
"""
from pathlib import Path
import sys
import types

SCRIPTS = Path(__file__).resolve().parents[3] / 'scripts'
sys.path.insert(0, str(SCRIPTS))
mode, path = sys.argv[1], sys.argv[2]
data = Path(path).read_bytes()
if mode == 'tables':
    from extract_tables import extract_tables
    try:
        for name, table in extract_tables(data).items():
            print(name, table.hex())
    except Exception:  # the Rust side prints its own error text; only "failed" has to agree
        print('ERROR')
else:
    source = (SCRIPTS / 'extract_tables.py').read_text().replace('if ids != [', 'if False and ids != [')
    module = types.ModuleType('extract_tables_any_build')
    exec(compile(source, 'extract_tables.py', 'exec'), module.__dict__)
    game = module.GameBinary(data)
    text = lambda v: 'null' if v is None else str(v)
    for name in Path(sys.argv[3]).read_text().splitlines():
        try:
            # Rust returns a Result<Vec<_>>: a failed function has no partial call list. Collect before printing
            # so a late unreadable constant load is compared as the same failure on both sides.
            calls = list(game.constant_calls(name))
            for call, regs, stack in calls:
                r = sorted(f'{k}={text(v)}' for k, v in regs.items())
                s = sorted(f'{k}={text(v)}' for k, v in stack.items())
                print(f"{name} -> {call} | {','.join(r)} | {','.join(s)}")
        except Exception:
            print(f'{name} ERROR')
