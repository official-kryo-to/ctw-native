// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
//! The standalone game folder. A port of `scripts/export_pc.py`, with the game exe and the mod kit taken from the
//! payload the Porter carries instead of from a local build:
//!
//! ```text
//! GTACTW.exe      the game
//! data/           the game files from the APK
//! mods/           the mod menu and the example mods, or empty
//! ```

use crate::apk::prepare_with;
use crate::{PortError, Progress};
use std::fs;
use std::io::{Cursor, Read};
use std::path::{Path, PathBuf};
use std::sync::atomic::AtomicBool;
use zip::ZipArchive;

pub const GAME_EXE: &str = "GTACTW.exe";
/// Goes into the exe instead of the data folder.
const ICON: &str = "gtactw.ico";

pub struct ExportOptions {
    /// Put the mod menu and the example mods into mods/.
    pub with_modkit: bool,
}

/// Whether a payload (a zip with `GTACTW.exe` and `mods/`) carries the game.
pub fn payload_has_game(payload: &[u8]) -> bool {
    ZipArchive::new(Cursor::new(payload)).is_ok_and(|mut z| z.by_name(GAME_EXE).is_ok())
}

/// Writes the finished game folder to `out`, which must not exist yet. Returns the path of the game exe.
pub fn export(
    apk: &Path,
    out: &Path,
    payload: &[u8],
    options: &ExportOptions,
    progress: &mut dyn FnMut(Progress),
    cancel: &AtomicBool,
) -> Result<PathBuf, PortError> {
    export_with(apk, out, payload, options, progress, cancel, &crate::tables::extract_tables)
}

pub(crate) fn export_with(
    apk: &Path,
    out: &Path,
    payload: &[u8],
    options: &ExportOptions,
    progress: &mut dyn FnMut(Progress),
    cancel: &AtomicBool,
    tables: &crate::apk::TablesFn,
) -> Result<PathBuf, PortError> {
    if out.exists() {
        return Err(PortError::Exists(out.to_path_buf()));
    }
    if !payload_has_game(payload) {
        return Err(PortError::NoGame);
    }
    let parent = out.parent().map(Path::to_path_buf).unwrap_or_else(|| PathBuf::from("."));
    fs::create_dir_all(&parent)?;
    // Staged next to the result, so publishing it is a rename on the same drive.
    let temporary = tempfile::Builder::new().prefix(".ctw-export-").tempdir_in(&parent)?;
    let package = temporary.path().join("GTACTW");
    fs::create_dir(&package)?;
    prepare_with(apk, &package.join("data"), progress, cancel, tables)?;
    progress(Progress::Installing);
    let mut archive = ZipArchive::new(Cursor::new(payload))?;
    let exe = package.join(GAME_EXE);
    unpack(&mut archive, &package, |name| name == GAME_EXE)?;
    let icon = package.join("data").join(ICON);
    if icon.is_file() {
        // An exe without its icon still runs; not worth failing the whole port over.
        let _ = set_exe_icon(&exe, &fs::read(&icon)?);
        fs::remove_file(&icon)?;
    }
    fs::create_dir(package.join("mods"))?;
    if options.with_modkit {
        unpack(&mut archive, &package, |name| name.starts_with("mods/"))?;
    }
    fs::rename(&package, out)?;
    Ok(out.join(GAME_EXE))
}

/// Extracts the payload entries `wanted` picks into `into`.
fn unpack(
    archive: &mut ZipArchive<Cursor<&[u8]>>,
    into: &Path,
    wanted: impl Fn(&str) -> bool,
) -> Result<(), PortError> {
    for index in 0..archive.len() {
        let mut entry = archive.by_index(index)?;
        let Some(relative) = entry.enclosed_name() else { continue };
        if !wanted(entry.name()) {
            continue;
        }
        let target = into.join(relative);
        if entry.is_dir() {
            fs::create_dir_all(&target)?;
            continue;
        }
        if let Some(dir) = target.parent() {
            fs::create_dir_all(dir)?;
        }
        let mut data = Vec::with_capacity(entry.size() as usize);
        entry.read_to_end(&mut data)?;
        fs::write(&target, data)?;
    }
    Ok(())
}

/// A desktop shortcut to the game: an Internet Shortcut file pointing at the exe, which Windows shows with the
/// exe's icon. Needs no COM and no admin rights.
pub fn write_shortcut(shortcut: &Path, exe: &Path) -> std::io::Result<()> {
    let target = exe.to_string_lossy().replace('\\', "/");
    let url: String = target
        .bytes()
        .map(|b| match b {
            b'A'..=b'Z' | b'a'..=b'z' | b'0'..=b'9' | b'/' | b':' | b'-' | b'_' | b'.' | b'~' => {
                (b as char).to_string()
            }
            other => format!("%{other:02X}"),
        })
        .collect();
    let prefix = if url.starts_with('/') { "file://" } else { "file:///" };
    let body = format!(
        "[InternetShortcut]\r\nURL={prefix}{url}\r\nIconFile={}\r\nIconIndex=0\r\nWorkingDirectory={}\r\n",
        exe.display(),
        exe.parent().map(|p| p.display().to_string()).unwrap_or_default()
    );
    fs::write(shortcut, body)
}

/// Puts an .ico file's images into the exe's resources (Windows only; elsewhere the exe keeps no icon).
#[cfg(windows)]
pub fn set_exe_icon(exe: &Path, ico: &[u8]) -> std::io::Result<()> {
    use std::os::windows::ffi::OsStrExt;
    use windows_sys::Win32::System::LibraryLoader::{BeginUpdateResourceW, EndUpdateResourceW, UpdateResourceW};
    let bad = || std::io::Error::new(std::io::ErrorKind::InvalidData, "not an .ico file");
    let u16_at = |p: usize| ico.get(p..p + 2).map(|b| u16::from_le_bytes([b[0], b[1]])).ok_or_else(bad);
    let u32_at = |p: usize| ico.get(p..p + 4).map(|b| u32::from_le_bytes(b.try_into().unwrap())).ok_or_else(bad);
    let count = u16_at(4)? as usize;
    let path: Vec<u16> = exe.as_os_str().encode_wide().chain(Some(0)).collect();
    unsafe {
        let handle = BeginUpdateResourceW(path.as_ptr(), 0);
        if handle.is_null() {
            return Err(std::io::Error::last_os_error());
        }
        let mut group: Vec<u8> = Vec::new();
        group.extend(0u16.to_le_bytes());
        group.extend(1u16.to_le_bytes());
        group.extend((count as u16).to_le_bytes());
        let mut ok = true;
        for i in 0..count {
            let at = 6 + 16 * i;
            let head = ico.get(at..at + 4).ok_or_else(bad)?.to_vec(); // width, height, colours, reserved
            let (planes, bits, size, offset) =
                (u16_at(at + 4)?, u16_at(at + 6)?, u32_at(at + 8)?, u32_at(at + 12)? as usize);
            let data = ico.get(offset..offset + size as usize).ok_or_else(bad)?;
            let id = (i + 1) as u16;
            // RT_ICON = 3, language en-US
            ok &= UpdateResourceW(handle, 3 as _, id as usize as _, 0x409, data.as_ptr() as _, data.len() as u32) != 0;
            group.extend(&head[..3]);
            group.push(0);
            group.extend(planes.to_le_bytes());
            group.extend(bits.to_le_bytes());
            group.extend(size.to_le_bytes());
            group.extend(id.to_le_bytes());
        }
        // RT_GROUP_ICON = 14
        ok &= UpdateResourceW(handle, 14 as _, 1 as _, 0x409, group.as_ptr() as _, group.len() as u32) != 0;
        if EndUpdateResourceW(handle, if ok { 0 } else { 1 }) == 0 {
            return Err(std::io::Error::last_os_error());
        }
        if !ok {
            return Err(std::io::Error::other("could not write the icon resources"));
        }
    }
    Ok(())
}

#[cfg(not(windows))]
pub fn set_exe_icon(_exe: &Path, _ico: &[u8]) -> std::io::Result<()> {
    Ok(())
}
