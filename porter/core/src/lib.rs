// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
//! The CTW-Native Porter's engine: checks a GTA: Chinatown Wars Android APK and turns it into the PC game folder.
//!
//! The same steps as `scripts/setup_game.py` and `scripts/export_pc.py`, in Rust so the Porter is one exe with
//! nothing to install. Nothing from the APK is ever run: it is read as a zip, the manifest as bytes and the game
//! binary only through a disassembler.

pub mod apk;
pub mod elf;
pub mod export;
pub mod manifest;
pub mod tables;

use std::fmt;
use std::path::PathBuf;

pub use apk::{check_apk, prepare, ApkInfo};
pub use export::{export, payload_has_game, ExportOptions};

/// What the Porter is doing, for the progress bar.
#[derive(Debug, Clone, PartialEq)]
pub enum Progress {
    /// Opening the APK and checking it is the supported game.
    Checking,
    /// Reading the lookup tables from the game binary.
    Tables,
    /// Copying the game files out of the APK.
    Assets { done: usize, total: usize, bytes_done: u64, bytes_total: u64 },
    /// Putting the exe, the icon and the mods in place.
    Installing,
}

#[derive(Debug)]
pub enum PortError {
    /// Not an APK (or a damaged one).
    NotApk(String),
    /// An APK, but not the supported game or version.
    WrongGame(String),
    /// Something in the APK that is not safe to extract.
    Unsafe(String),
    Tables(tables::TableError),
    /// The output folder is already there.
    Exists(PathBuf),
    NoSpace {
        needed: u64,
        available: u64,
    },
    /// This Porter was built without the game (a development build).
    NoGame,
    Cancelled,
    Io(std::io::Error),
}

impl fmt::Display for PortError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            PortError::NotApk(why) => write!(f, "This file could not be opened as an APK ({why})."),
            PortError::WrongGame(why) | PortError::Unsafe(why) => f.write_str(why),
            PortError::Tables(e) => write!(f, "The game binary in this APK could not be read: {e}"),
            PortError::Exists(p) => {
                write!(f, "{} already exists. Choose another folder or remove it first.", p.display())
            }
            PortError::NoSpace { needed, available } => write!(
                f,
                "Not enough free space: the game needs {} and the drive has {}.",
                size_text(*needed),
                size_text(*available)
            ),
            PortError::NoGame => f.write_str(
                "This copy of the Porter was built without the game. Download the Porter from the releases page, \
                 or build the game first (see porter/README.md).",
            ),
            PortError::Cancelled => f.write_str("Cancelled."),
            PortError::Io(e) => write!(f, "{e}"),
        }
    }
}

impl std::error::Error for PortError {}

impl From<std::io::Error> for PortError {
    fn from(e: std::io::Error) -> Self {
        PortError::Io(e)
    }
}

impl From<tables::TableError> for PortError {
    fn from(e: tables::TableError) -> Self {
        PortError::Tables(e)
    }
}

impl From<zip::result::ZipError> for PortError {
    fn from(e: zip::result::ZipError) -> Self {
        match e {
            zip::result::ZipError::Io(e) => PortError::Io(e),
            other => PortError::NotApk(other.to_string()),
        }
    }
}

/// "1.2 GB", "340 MB".
pub fn size_text(bytes: u64) -> String {
    let b = bytes as f64;
    if b >= 1e9 {
        format!("{:.1} GB", b / 1e9)
    } else if b >= 1e6 {
        format!("{:.0} MB", b / 1e6)
    } else {
        format!("{:.0} KB", b / 1e3)
    }
}

#[cfg(test)]
mod tests;
