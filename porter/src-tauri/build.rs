// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
//! Packs the game into the Porter: GTACTW.exe and the mod kit (mods/) from a release build of the game, zipped
//! into OUT_DIR/payload.zip and included in the exe. The folder is CTW_GAME_BUILD, or port/build-release (what
//! `scripts/export_pc.py` builds). Without it the Porter still builds, and says it has no game inside.
use std::fs;
use std::io::Write;
use std::path::{Path, PathBuf};

/// Files the game writes into mods/ at run time; never shipped.
const NOT_MODS: [&str; 2] = ["log.txt", "enabled.ini"];

fn add_dir(zip: &mut zip::ZipWriter<fs::File>, root: &Path, dir: &Path, options: zip::write::SimpleFileOptions) {
    let mut entries: Vec<PathBuf> = fs::read_dir(dir).expect("read mods").map(|e| e.unwrap().path()).collect();
    entries.sort();
    for path in entries {
        let name = path.strip_prefix(root).unwrap().to_string_lossy().replace('\\', "/");
        if path.is_dir() {
            add_dir(zip, root, &path, options);
        } else if !NOT_MODS.contains(&path.file_name().unwrap().to_str().unwrap_or("")) {
            zip.start_file(name, options).unwrap();
            zip.write_all(&fs::read(&path).unwrap()).unwrap();
        }
    }
}

fn main() {
    println!("cargo:rerun-if-env-changed=CTW_GAME_BUILD");
    let game = std::env::var_os("CTW_GAME_BUILD")
        .map(PathBuf::from)
        .unwrap_or_else(|| Path::new(env!("CARGO_MANIFEST_DIR")).join("../../port/build-release"));
    let out = PathBuf::from(std::env::var("OUT_DIR").unwrap()).join("payload.zip");
    let exe = game.join("GTACTW.exe");
    println!("cargo:rerun-if-changed={}", exe.display());
    println!("cargo:rerun-if-changed={}", game.join("mods").display());
    if exe.is_file() {
        let mut zip = zip::ZipWriter::new(fs::File::create(&out).unwrap());
        let options = zip::write::SimpleFileOptions::default().compression_method(zip::CompressionMethod::Deflated);
        zip.start_file("GTACTW.exe", options).unwrap();
        zip.write_all(&fs::read(&exe).unwrap()).unwrap();
        if game.join("mods").is_dir() {
            add_dir(&mut zip, &game, &game.join("mods"), options);
        }
        zip.finish().unwrap();
    } else {
        println!("cargo:warning=No game build in {}: this Porter will have no game inside.", game.display());
        fs::write(&out, b"").unwrap();
    }
    tauri_build::build()
}
