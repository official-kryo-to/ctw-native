// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
//! The Porter's window and the commands behind it. The work itself is `ctw-porter-core`; this file runs it on a
//! background thread and reports progress to the page as events.

use ctw_porter_core::export::{write_shortcut, ExportOptions};
use ctw_porter_core::{check_apk, export, payload_has_game, PortError, Progress};
use serde::Serialize;
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::time::{Duration, Instant};
use tauri::{AppHandle, Emitter, Manager, State};

/// GTACTW.exe and the mod kit, packed by build.rs.
static PAYLOAD: &[u8] = include_bytes!(concat!(env!("OUT_DIR"), "/payload.zip"));
const FOLDER_NAME: &str = "GTA Chinatown Wars PC";
const SHORTCUT_NAME: &str = "GTA Chinatown Wars.url";

#[derive(Default)]
struct Porting {
    running: AtomicBool,
    cancel: Arc<AtomicBool>,
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
struct ApkSummary {
    path: String,
    file_name: String,
    version: String,
    files: usize,
    bytes: u64,
    suggested_folder: String,
}

#[derive(Serialize, Clone)]
#[serde(rename_all = "camelCase")]
struct ProgressEvent {
    stage: &'static str,
    files_done: usize,
    files_total: usize,
    bytes_done: u64,
    bytes_total: u64,
}

#[derive(Serialize, Clone)]
struct Finished {
    exe: String,
    folder: String,
    shortcut: Option<String>,
}

#[tauri::command]
fn bundled_game() -> bool {
    payload_has_game(PAYLOAD)
}

/// `folder`, or "folder (2)", "(3)"... whichever does not exist yet.
#[tauri::command]
fn free_folder(folder: String) -> String {
    let base = PathBuf::from(&folder);
    if !base.exists() {
        return folder;
    }
    (2..)
        .map(|n| PathBuf::from(format!("{folder} ({n})")))
        .find(|p| !p.exists())
        .unwrap()
        .to_string_lossy()
        .into_owned()
}

#[tauri::command]
async fn inspect_apk(path: String) -> Result<ApkSummary, String> {
    tauri::async_runtime::spawn_blocking(move || {
        let apk = PathBuf::from(&path);
        if apk.is_dir() {
            return Err("That is a folder. Drop the APK file itself.".to_string());
        }
        let info = check_apk(&apk).map_err(|e| e.to_string())?;
        let dir = apk.parent().map(Path::to_path_buf).unwrap_or_default();
        Ok(ApkSummary {
            file_name: apk.file_name().map(|n| n.to_string_lossy().into_owned()).unwrap_or_default(),
            path,
            version: info.version,
            files: info.files,
            bytes: info.bytes,
            suggested_folder: dir.join(FOLDER_NAME).to_string_lossy().into_owned(),
        })
    })
    .await
    .map_err(|e| e.to_string())?
}

#[tauri::command]
fn start_port(
    app: AppHandle,
    state: State<'_, Porting>,
    apk: String,
    folder: String,
    with_modkit: bool,
    shortcut: bool,
) -> Result<(), String> {
    if state.running.swap(true, Ordering::SeqCst) {
        return Err("A port is already running.".into());
    }
    state.cancel.store(false, Ordering::SeqCst);
    let cancel = state.cancel.clone();
    std::thread::spawn(move || {
        let mut last = Instant::now() - Duration::from_secs(1);
        let emitter = app.clone();
        let mut report = move |p: Progress| {
            // Every stage change goes out; byte counts at most ~12 times a second.
            let assets = matches!(p, Progress::Assets { done, total, .. } if done < total);
            if assets && last.elapsed() < Duration::from_millis(80) {
                return;
            }
            last = Instant::now();
            let event = match p {
                Progress::Checking => {
                    ProgressEvent { stage: "checking", files_done: 0, files_total: 0, bytes_done: 0, bytes_total: 0 }
                }
                Progress::Tables => {
                    ProgressEvent { stage: "tables", files_done: 0, files_total: 0, bytes_done: 0, bytes_total: 0 }
                }
                Progress::Assets { done, total, bytes_done, bytes_total } => {
                    ProgressEvent { stage: "assets", files_done: done, files_total: total, bytes_done, bytes_total }
                }
                Progress::Installing => {
                    ProgressEvent { stage: "installing", files_done: 0, files_total: 0, bytes_done: 1, bytes_total: 1 }
                }
            };
            let _ = emitter.emit("porter://progress", event);
        };
        let out = PathBuf::from(&folder);
        let result = export(Path::new(&apk), &out, PAYLOAD, &ExportOptions { with_modkit }, &mut report, &cancel);
        app.state::<Porting>().running.store(false, Ordering::SeqCst);
        match result {
            Ok(exe) => {
                let link = shortcut
                    .then(|| app.path().desktop_dir().ok())
                    .flatten()
                    .map(|desktop| desktop.join(SHORTCUT_NAME))
                    .filter(|link| write_shortcut(link, &exe).is_ok());
                let _ = app.emit(
                    "porter://finished",
                    Finished {
                        exe: exe.to_string_lossy().into_owned(),
                        folder: out.to_string_lossy().into_owned(),
                        shortcut: link.map(|l| l.to_string_lossy().into_owned()),
                    },
                );
            }
            Err(PortError::Cancelled) => {
                let _ = app.emit("porter://failed", "Cancelled.");
            }
            Err(e) => {
                let _ = app.emit("porter://failed", e.to_string());
            }
        }
    });
    Ok(())
}

#[tauri::command]
fn cancel_port(state: State<'_, Porting>) {
    state.cancel.store(true, Ordering::SeqCst);
}

/// Starts the game from its own folder, detached from the Porter.
#[tauri::command]
fn play(exe: String) -> Result<(), String> {
    let exe = PathBuf::from(exe);
    let dir = exe.parent().map(Path::to_path_buf).unwrap_or_default();
    std::process::Command::new(&exe)
        .current_dir(dir)
        .spawn()
        .map(|_| ())
        .map_err(|e| format!("Could not start the game: {e}"))
}

pub fn run() {
    tauri::Builder::default()
        .plugin(tauri_plugin_dialog::init())
        .plugin(tauri_plugin_opener::init())
        .manage(Porting::default())
        .invoke_handler(tauri::generate_handler![bundled_game, free_folder, inspect_apk, start_port, cancel_port, play])
        .on_window_event(|window, event| {
            // Closing mid-port: stop first, so the half-written staging folder is removed before the process ends.
            if let tauri::WindowEvent::CloseRequested { api, .. } = event {
                let state = window.state::<Porting>();
                if state.running.load(Ordering::SeqCst) {
                    state.cancel.store(true, Ordering::SeqCst);
                    api.prevent_close();
                    let window = window.clone();
                    std::thread::spawn(move || {
                        while window.state::<Porting>().running.load(Ordering::SeqCst) {
                            std::thread::sleep(Duration::from_millis(50));
                        }
                        let _ = window.destroy();
                    });
                }
            }
        })
        .run(tauri::generate_context!())
        .expect("error while running the Porter");
}
