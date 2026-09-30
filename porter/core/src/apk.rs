// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
//! Checking an APK and extracting its game files. A port of `scripts/setup_game.py`.

use crate::manifest::manifest_info;
use crate::tables::{extract_tables, GameBinary};
use crate::{PortError, Progress};
use std::collections::HashSet;
use std::fs::{self, File};
use std::io::{self, Read, Seek, Write};
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicBool, Ordering};
use zip::ZipArchive;

pub const PACKAGE: &str = "com.rockstargames.gtactw";
pub const VERSION: &str = "4.4.243";
pub const REQUIRED: [&str; 3] = ["game.pak", "rom.wad", "rom.toc"];
pub const GAME_BINARY: &str = "lib/arm64-v8a/libGame.so";
/// Room for the tables, the exe and the mods on top of the game files.
const EXTRA_SPACE: u64 = 64 << 20;

/// One file under `assets/`: its index in the zip, its path below `assets/` and its size.
pub struct AssetEntry {
    pub index: usize,
    pub parts: Vec<String>,
    pub size: u64,
}

/// What a check of an APK found.
#[derive(Debug, Clone, PartialEq)]
pub struct ApkInfo {
    pub package: String,
    pub version: String,
    pub files: usize,
    /// Space the game folder needs, about.
    pub bytes: u64,
}

fn unsafe_path(name: &str) -> PortError {
    PortError::Unsafe(format!("Unsafe APK asset path: {name}"))
}

/// The files under `assets/`, checked: no traversal, no Windows drive or stream names, no aliases or symlinks, and
/// the three files the game cannot start without.
pub fn asset_entries<R: Read + Seek>(archive: &mut ZipArchive<R>) -> Result<Vec<AssetEntry>, PortError> {
    let mut entries = Vec::new();
    let mut seen = HashSet::new();
    for index in 0..archive.len() {
        let entry = archive.by_index_raw(index)?;
        let raw = entry.name_raw();
        // Python's zipfile truncates at NUL (and turns '\' into '/' on Windows): an asset that differs is refused.
        if raw.starts_with(b"assets/") && raw.contains(&0) {
            return Err(unsafe_path(entry.name()));
        }
        let filename = entry.name().to_string();
        if !filename.starts_with("assets/") || filename.ends_with('/') {
            continue;
        }
        let name = &filename["assets/".len()..];
        let symlink = entry.unix_mode().is_some_and(|m| m & 0o170000 == 0o120000);
        if name.is_empty()
            || name.starts_with('/')
            || name.contains('\\')
            || name.contains(':')
            || name.split('/').any(|p| p.is_empty() || p == "." || p == ".." || p.ends_with(' ') || p.ends_with('.'))
            || symlink
        {
            return Err(unsafe_path(&filename));
        }
        let key = name.to_lowercase();
        if !seen.insert(key) {
            return Err(PortError::Unsafe(format!("Duplicate APK asset path: {filename}")));
        }
        entries.push(AssetEntry { index, parts: name.split('/').map(str::to_string).collect(), size: entry.size() });
    }
    let missing: Vec<&str> = REQUIRED.iter().copied().filter(|r| !seen.contains(*r)).collect();
    if !missing.is_empty() {
        return Err(PortError::WrongGame(format!(
            "This APK is missing game files ({}). It needs to be the full GTA: Chinatown Wars APK, not a split or \
             'lite' one.",
            missing.join(", ")
        )));
    }
    Ok(entries)
}

fn read_entry<R: Read + Seek>(archive: &mut ZipArchive<R>, name: &str) -> Result<Vec<u8>, PortError> {
    let mut entry = match archive.by_name(name) {
        Ok(e) => e,
        Err(zip::result::ZipError::FileNotFound) => {
            return Err(PortError::WrongGame(format!(
                "This APK has no {name}. CTW-Native needs the arm64 build of GTA: Chinatown Wars for Android {VERSION}."
            )))
        }
        Err(e) => return Err(e.into()),
    };
    let mut data = Vec::with_capacity(entry.size() as usize);
    entry.read_to_end(&mut data)?;
    Ok(data)
}

fn check_manifest<R: Read + Seek>(archive: &mut ZipArchive<R>) -> Result<(String, String), PortError> {
    let manifest = {
        let mut entry = archive.by_name("AndroidManifest.xml").map_err(|e| match e {
            zip::result::ZipError::FileNotFound => PortError::NotApk("it has no AndroidManifest.xml".into()),
            other => other.into(),
        })?;
        let mut data = Vec::new();
        entry.read_to_end(&mut data)?;
        data
    };
    let (package, version) = manifest_info(&manifest).map_err(|e| PortError::NotApk(e.to_string()))?;
    if package.as_deref() != Some(PACKAGE) || version.as_deref() != Some(VERSION) {
        let found = match (&package, &version) {
            (Some(p), v) if p == PACKAGE => {
                format!("GTA: Chinatown Wars version {}", v.as_deref().unwrap_or("(unknown)"))
            }
            (Some(p), Some(v)) => format!("{p} {v}"),
            (Some(p), None) => p.clone(),
            _ => "an app without a package name".into(),
        };
        return Err(PortError::WrongGame(format!(
            "This APK is {found}. CTW-Native needs GTA: Chinatown Wars for Android, version {VERSION}."
        )));
    }
    Ok((package.unwrap(), version.unwrap()))
}

fn open(apk: &Path) -> Result<ZipArchive<File>, PortError> {
    let file = File::open(apk)?;
    Ok(ZipArchive::new(file)?)
}

/// Everything short of extracting: the manifest, the asset list and the game binary's build. Fast enough to run
/// as soon as a file is dropped.
pub fn check_apk(apk: &Path) -> Result<ApkInfo, PortError> {
    let mut archive = open(apk)?;
    let (package, version) = check_manifest(&mut archive)?;
    let entries = asset_entries(&mut archive)?;
    let binary = read_entry(&mut archive, GAME_BINARY)?;
    GameBinary::new(&binary).map_err(|e| {
        if e.0.starts_with("Unsupported ARM64 build ID") {
            PortError::WrongGame(format!(
                "This APK says it is version {VERSION}, but its game binary is a different build than the one \
                 CTW-Native supports."
            ))
        } else {
            PortError::Tables(e)
        }
    })?;
    Ok(ApkInfo {
        package,
        version,
        files: entries.len(),
        bytes: entries.iter().map(|e| e.size).sum::<u64>() + EXTRA_SPACE,
    })
}

/// The game's launcher icon as .ico bytes (for GTACTW.exe), or None without one.
pub fn launcher_icon<R: Read + Seek>(archive: &mut ZipArchive<R>) -> Option<Vec<u8>> {
    use image::codecs::ico::{IcoEncoder, IcoFrame};
    use image::imageops::FilterType;
    for name in ["res/mipmap-xxxhdpi-v4/ic_launcher.png", "res/mipmap-xxhdpi-v4/ic_launcher.png"] {
        let Ok(data) = read_entry(archive, name) else { continue };
        let Ok(image) = image::load_from_memory(&data) else { continue };
        let base = image.to_rgba8();
        let base = image::imageops::resize(&base, 256, 256, FilterType::Lanczos3);
        let mut frames = Vec::new();
        for size in [256u32, 128, 64, 48, 32, 16] {
            let img = if size == 256 {
                base.clone()
            } else {
                image::imageops::resize(&base, size, size, FilterType::Lanczos3)
            };
            frames.push(IcoFrame::as_png(img.as_raw(), size, size, image::ExtendedColorType::Rgba8).ok()?);
        }
        let mut out = Vec::new();
        IcoEncoder::new(&mut out).encode_images(&frames).ok()?;
        return Some(out);
    }
    None
}

/// Free space on the drive that holds `path` (or its nearest existing parent).
pub fn available_space(path: &Path) -> Option<u64> {
    let mut at = Some(path);
    while let Some(p) = at {
        if p.exists() {
            return fs4::available_space(p).ok();
        }
        at = p.parent();
    }
    None
}

fn check_space(destination: &Path, needed: u64) -> Result<(), PortError> {
    if let Some(available) = available_space(destination) {
        if available < needed {
            return Err(PortError::NoSpace { needed, available });
        }
    }
    Ok(())
}

/// Extracts the game files and writes the lookup tables to `destination`, which must not exist yet. Only a
/// complete extraction is published: an interrupted run leaves no partial folder.
pub fn prepare(
    apk: &Path,
    destination: &Path,
    progress: &mut dyn FnMut(Progress),
    cancel: &AtomicBool,
) -> Result<usize, PortError> {
    prepare_with(apk, destination, progress, cancel, &extract_tables)
}

pub(crate) type TablesFn = dyn Fn(&[u8]) -> Result<Vec<(&'static str, Vec<u8>)>, crate::tables::TableError>;

/// `prepare`, with the table extraction passed in (tests use synthetic APKs without a real game binary).
pub(crate) fn prepare_with(
    apk: &Path,
    destination: &Path,
    progress: &mut dyn FnMut(Progress),
    cancel: &AtomicBool,
    tables: &TablesFn,
) -> Result<usize, PortError> {
    if destination.exists() {
        return Err(PortError::Exists(destination.to_path_buf()));
    }
    progress(Progress::Checking);
    let mut archive = open(apk)?;
    check_manifest(&mut archive)?;
    let entries = asset_entries(&mut archive)?;
    let bytes_total: u64 = entries.iter().map(|e| e.size).sum();
    let parent = destination.parent().map(Path::to_path_buf).unwrap_or_else(|| PathBuf::from("."));
    fs::create_dir_all(&parent)?;
    check_space(&parent, bytes_total + EXTRA_SPACE)?;
    progress(Progress::Tables);
    let tables = tables(&read_entry(&mut archive, GAME_BINARY)?).map_err(|e| {
        if e.0.starts_with("Unsupported ARM64 build ID") {
            PortError::WrongGame("This APK's game binary is a different build than the one CTW-Native supports.".into())
        } else {
            PortError::Tables(e)
        }
    })?;
    let temporary = tempfile::Builder::new().prefix(".ctw-setup-").tempdir_in(&parent)?;
    let stage = temporary.path().join("data");
    fs::create_dir(&stage)?;
    let (total, mut bytes_done) = (entries.len(), 0u64);
    let mut buffer = vec![0u8; 1 << 20];
    for (done, entry) in entries.iter().enumerate() {
        if cancel.load(Ordering::Relaxed) {
            return Err(PortError::Cancelled);
        }
        let target = entry.parts.iter().fold(stage.clone(), |p, part| p.join(part));
        if let Some(dir) = target.parent() {
            fs::create_dir_all(dir)?;
        }
        let mut source = archive.by_index(entry.index)?;
        let mut output = io::BufWriter::new(File::create(&target)?);
        loop {
            let n = source.read(&mut buffer)?;
            if n == 0 {
                break;
            }
            output.write_all(&buffer[..n])?;
            bytes_done += n as u64;
            if cancel.load(Ordering::Relaxed) {
                return Err(PortError::Cancelled);
            }
            progress(Progress::Assets { done, total, bytes_done, bytes_total });
        }
        output.flush()?;
        progress(Progress::Assets { done: done + 1, total, bytes_done, bytes_total });
    }
    for (name, data) in &tables {
        fs::write(stage.join(name), data)?;
    }
    if let Some(icon) = launcher_icon(&mut archive) {
        fs::write(stage.join("gtactw.ico"), icon)?; // picked up by CMake for development builds
    }
    fs::rename(&stage, destination)?;
    Ok(total)
}
