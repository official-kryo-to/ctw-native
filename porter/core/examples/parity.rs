// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
//! Prints what the table extraction sees, in the same form as `tests/parity.py` (run both with `tests/parity.sh`), so the Rust port can be
//! compared with `scripts/extract_tables.py` on any ARM64 binary:
//!
//!   parity tables <libGame.so>                 all six tables, hex
//!   parity calls <any.so> <names file>         constant_calls for each function name (build ID not checked)
use ctw_porter_core::tables::{extract_tables, GameBinary};

fn value(v: &Option<i128>) -> String {
    v.map_or("null".to_string(), |n| n.to_string())
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let data = std::fs::read(&args[2]).expect("read binary");
    match args[1].as_str() {
        "tables" => match extract_tables(&data) {
            Ok(tables) => {
                for (name, bytes) in tables {
                    println!("{name} {}", bytes.iter().map(|b| format!("{b:02x}")).collect::<String>());
                }
            }
            Err(e) => println!("ERROR {e}"),
        },
        "calls" => {
            let game = GameBinary::open(&data, false).expect("open");
            for name in std::fs::read_to_string(&args[3]).expect("names").lines() {
                match game.constant_calls(name) {
                    Ok(calls) => {
                        for (call, regs, stack) in calls {
                            let mut r: Vec<String> = regs.iter().map(|(k, v)| format!("{k}={}", value(v))).collect();
                            let mut s: Vec<String> = stack.iter().map(|(k, v)| format!("{k}={}", value(v))).collect();
                            r.sort();
                            s.sort();
                            println!("{name} -> {call} | {} | {}", r.join(","), s.join(","));
                        }
                    }
                    Err(e) => println!("{name} ERROR {e}"),
                }
            }
        }
        _ => panic!("tables or calls"),
    }
}
