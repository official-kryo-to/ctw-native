// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
//! Synthetic APKs: what is refused, and the folder a successful port produces. Mirrors `tests/test_setup.py`.

use crate::apk::{asset_entries, prepare_with};
use crate::export::{export_with, payload_has_game, ExportOptions};
use crate::manifest::tests::binary_manifest;
use crate::tables::TableError;
use crate::{PortError, Progress};
use std::io::{Cursor, Write};
use std::sync::atomic::AtomicBool;
use zip::write::SimpleFileOptions;
use zip::{ZipArchive, ZipWriter};

fn zip_of(entries: &[(&str, &[u8])]) -> Vec<u8> {
    let mut writer = ZipWriter::new(Cursor::new(Vec::new()));
    for (name, data) in entries {
        writer.start_file(*name, SimpleFileOptions::default()).unwrap();
        writer.write_all(data).unwrap();
    }
    writer.finish().unwrap().into_inner()
}

fn apk(extra: &[(&str, &[u8])]) -> Vec<u8> {
    let manifest = binary_manifest(true);
    let mut entries: Vec<(&str, &[u8])> = vec![("AndroidManifest.xml", &manifest)];
    entries.extend_from_slice(extra);
    zip_of(&entries)
}

fn entries_error(extra: &[(&str, &[u8])]) -> String {
    let data = apk(extra);
    match asset_entries(&mut ZipArchive::new(Cursor::new(data)).unwrap()) {
        Err(e) => e.to_string(),
        Ok(_) => String::new(),
    }
}

const GAME: [(&str, &[u8]); 3] =
    [("assets/game.pak", b"synthetic"), ("assets/rom.wad", b"synthetic"), ("assets/rom.toc", b"synthetic")];

fn stub_tables(_: &[u8]) -> Result<Vec<(&'static str, Vec<u8>)>, TableError> {
    Ok(vec![("fixture_tables.bin", b"fixture".to_vec())])
}

#[test]
fn unsafe_asset_paths() {
    for name in
        ["assets/../escape", "assets//absolute", "assets/C:/escape", "assets/a\\b", "assets/a./file", "assets/a /b"]
    {
        let mut extra = GAME.to_vec();
        extra.push((name, b""));
        assert!(entries_error(&extra).contains("Unsafe"), "{name}");
    }
}

#[test]
fn case_aliases_and_missing_assets() {
    assert!(entries_error(&[("assets/game.pak", b""), ("assets/GAME.PAK", b"")]).contains("Duplicate"));
    assert!(entries_error(&[]).contains("missing game files"));
}

#[test]
fn wrong_version_is_refused() {
    let manifest = binary_manifest(true);
    let patched: Vec<u8> = {
        // "4.4.243" -> "4.4.244" in the string pool
        let mut m = manifest.clone();
        let at = m.windows(7).position(|w| w == b"4.4.243").unwrap();
        m[at + 6] = b'4';
        m
    };
    let mut entries: Vec<(&str, &[u8])> = vec![("AndroidManifest.xml", &patched)];
    entries.extend_from_slice(&GAME);
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("game.apk");
    std::fs::write(&path, zip_of(&entries)).unwrap();
    let error = crate::check_apk(&path).unwrap_err();
    assert!(matches!(error, PortError::WrongGame(_)), "{error}");
    assert!(error.to_string().contains("version 4.4.244"), "{error}");
    std::fs::write(&path, b"not a zip").unwrap();
    assert!(matches!(crate::check_apk(&path).unwrap_err(), PortError::NotApk(_)));
}

#[test]
fn prepare_extracts_assets_only_and_writes_tables() {
    let mut extra = GAME.to_vec();
    extra.push(("assets/sub/dir/file.bin", b"nested"));
    extra.push(("lib/arm64-v8a/libGame.so", b"not executable"));
    extra.push(("other/private.dat", b"not an asset"));
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("game.apk");
    std::fs::write(&path, apk(&extra)).unwrap();
    let data = dir.path().join("out").join("data");
    let mut steps = Vec::new();
    let count = prepare_with(&path, &data, &mut |p| steps.push(p), &AtomicBool::new(false), &stub_tables).unwrap();
    assert_eq!(count, 4);
    assert_eq!(std::fs::read(data.join("game.pak")).unwrap(), b"synthetic");
    assert_eq!(std::fs::read(data.join("sub/dir/file.bin")).unwrap(), b"nested");
    assert_eq!(std::fs::read(data.join("fixture_tables.bin")).unwrap(), b"fixture");
    assert!(!data.join("libGame.so").exists() && !data.join("private.dat").exists());
    assert_eq!(steps.first(), Some(&Progress::Checking));
    assert!(steps.contains(&Progress::Assets { done: 4, total: 4, bytes_done: 33, bytes_total: 33 }));
    // Only the finished folder is left behind: no staging directories.
    let left: Vec<_> = std::fs::read_dir(dir.path().join("out")).unwrap().map(|e| e.unwrap().file_name()).collect();
    assert_eq!(left, vec![std::ffi::OsString::from("data")]);
    // A second run never overwrites.
    let again = prepare_with(&path, &data, &mut |_| {}, &AtomicBool::new(false), &stub_tables);
    assert!(matches!(again, Err(PortError::Exists(_))));
}

#[test]
fn cancel_leaves_nothing() {
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("game.apk");
    let mut extra = GAME.to_vec();
    extra.push(("lib/arm64-v8a/libGame.so", b""));
    std::fs::write(&path, apk(&extra)).unwrap();
    let out = dir.path().join("out");
    let result = prepare_with(&path, &out.join("data"), &mut |_| {}, &AtomicBool::new(true), &stub_tables);
    assert!(matches!(result, Err(PortError::Cancelled)));
    assert_eq!(std::fs::read_dir(&out).unwrap().count(), 0);
}

#[test]
fn export_builds_the_game_folder() {
    let payload = zip_of(&[
        ("GTACTW.exe", b"MZ game"),
        ("mods/ModMenu.dll", b"MZ menu"),
        ("mods/Trumpify/mod.ini", b"name = Trumpify"),
    ]);
    assert!(payload_has_game(&payload) && !payload_has_game(b"") && !payload_has_game(&zip_of(&[("x", b"")])));
    let dir = tempfile::tempdir().unwrap();
    let path = dir.path().join("game.apk");
    let mut extra = GAME.to_vec();
    extra.push(("lib/arm64-v8a/libGame.so", b""));
    std::fs::write(&path, apk(&extra)).unwrap();
    for with_modkit in [true, false] {
        let out = dir.path().join(format!("GTA {with_modkit}"));
        let exe = export_with(
            &path,
            &out,
            &payload,
            &ExportOptions { with_modkit },
            &mut |_| {},
            &AtomicBool::new(false),
            &stub_tables,
        )
        .unwrap();
        assert_eq!(exe, out.join("GTACTW.exe"));
        assert_eq!(std::fs::read(&exe).unwrap(), b"MZ game");
        assert!(out.join("data/game.pak").is_file());
        assert!(out.join("mods").is_dir());
        assert_eq!(out.join("mods/ModMenu.dll").is_file(), with_modkit);
        assert_eq!(out.join("mods/Trumpify/mod.ini").is_file(), with_modkit);
    }
    let empty = export_with(
        &path,
        &dir.path().join("x"),
        b"",
        &ExportOptions { with_modkit: true },
        &mut |_| {},
        &AtomicBool::new(false),
        &stub_tables,
    );
    assert!(matches!(empty, Err(PortError::NoGame)));
    let names: Vec<String> =
        std::fs::read_dir(dir.path()).unwrap().map(|e| e.unwrap().file_name().to_string_lossy().into_owned()).collect();
    assert!(names.iter().all(|n| !n.starts_with(".ctw-")), "{names:?}");
}

#[test]
fn shortcut_points_at_the_exe() {
    let dir = tempfile::tempdir().unwrap();
    let link = dir.path().join("GTA.url");
    crate::export::write_shortcut(&link, std::path::Path::new("C:\\Games\\GTA CTW\\GTACTW.exe")).unwrap();
    let text = std::fs::read_to_string(link).unwrap();
    assert!(text.contains("URL=file:///C:/Games/GTA%20CTW/GTACTW.exe"), "{text}");
}
