// SPDX-License-Identifier: GPL-3.0-only
//! Local import/parity helper; never includes donor assets in the Porter payload.
use std::path::Path;
use std::sync::atomic::AtomicBool;
fn main() {
    let args: Vec<_> = std::env::args().collect();
    if args.len() != 3 {
        eprintln!("usage: lcs <own 2.4.379 APK> <new character folder>");
        std::process::exit(2);
    }
    if Path::new(&args[2]).exists() {
        eprintln!("output already exists");
        std::process::exit(2);
    }
    if let Err(e) =
        ctw_porter_core::lcs::extract(Path::new(&args[1]), Path::new(&args[2]), &mut |_| {}, &AtomicBool::new(false))
    {
        eprintln!("{e}");
        std::process::exit(1);
    }
}
