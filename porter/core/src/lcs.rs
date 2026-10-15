// SPDX-License-Identifier: GPL-3.0-only
//! User-supplied LCS 2.4.379 characters. Byte-for-byte counterpart of scripts/lcs_assets.py.
use crate::{manifest::manifest_info, PortError, Progress};
use std::collections::{BTreeMap, BTreeSet};
use std::fs::{self, File};
use std::io::{Read, Seek, SeekFrom};
use std::path::Path;
use std::sync::atomic::{AtomicBool, Ordering};
use zip::{CompressionMethod, ZipArchive};

pub const VERSION: &str = "2.4.379";
const PACKAGE: &str = "com.rockstargames.gtalcs";
const LIMIT: u64 = 256 << 20;
const RESOURCE_LIMIT: u64 = 32 << 20;
const WANTED: [&str; 14] = [
    "player", "cop", "male01", "male02", "male03", "b_man1", "worker1", "p_man1", "female01", "female02", "female03",
    "b_wom1", "shopper1", "p_wom1",
];
fn bad(why: &str) -> PortError {
    PortError::WrongGame(format!("LCS import: {why}"))
}
fn cancelled(cancel: &AtomicBool) -> Result<(), PortError> {
    if cancel.load(Ordering::SeqCst) {
        Err(PortError::Cancelled)
    } else {
        Ok(())
    }
}
fn u32_at(b: &[u8], at: usize) -> Result<u32, PortError> {
    b.get(at..at.saturating_add(4))
        .map(|v| u32::from_le_bytes(v.try_into().unwrap()))
        .ok_or_else(|| bad("truncated resource"))
}
fn crc(name: &str) -> u32 {
    let mut crc = u32::MAX;
    for b in name.to_ascii_lowercase().bytes() {
        crc ^= b as u32;
        for _ in 0..8 {
            crc = (crc >> 1) ^ (0xedb88320 & 0u32.wrapping_sub(crc & 1));
        }
    }
    crc
}
fn safe_name(name: &str) -> Result<(), PortError> {
    let reserved = matches!(name, "con" | "prn" | "aux" | "nul")
        || (name.len() == 4
            && (name.starts_with("com") || name.starts_with("lpt"))
            && matches!(name.as_bytes()[3], b'1'..=b'9'));
    if name.is_empty()
        || reserved
        || !name.bytes().all(|c| c.is_ascii_lowercase() || c.is_ascii_digit() || c == b'_' || c == b'-')
    {
        return Err(bad("invalid character resource name"));
    }
    Ok(())
}
struct Wad {
    file: File,
    start: u64,
    length: u64,
    entries: BTreeMap<u32, (u64, u64)>,
}
impl Wad {
    fn read(&mut self, at: u64, size: u64, cancel: &AtomicBool) -> Result<Vec<u8>, PortError> {
        if size > RESOURCE_LIMIT || at > self.length || size > self.length - at {
            return Err(bad("resource outside archive or too large"));
        }
        self.file.seek(SeekFrom::Start(self.start + at))?;
        let mut out = vec![0; size as usize];
        for (i, part) in out.chunks_mut(65536).enumerate() {
            cancelled(cancel)?;
            self.file.read_exact(part)?;
            for (j, b) in part.iter_mut().enumerate() {
                *b ^= if (at + i as u64 * 65536 + j as u64) & 1 == 0 { 0xaf } else { 0x66 };
            }
        }
        Ok(out)
    }
    fn get(&mut self, name: &str, cancel: &AtomicBool) -> Result<Vec<u8>, PortError> {
        let (at, size) = *self.entries.get(&crc(name)).ok_or_else(|| bad(&format!("APK is missing {name}")))?;
        self.read(at, size, cancel)
    }
}
fn open(apk: &Path, cancel: &AtomicBool) -> Result<Wad, PortError> {
    cancelled(cancel)?;
    let mut z = ZipArchive::new(File::open(apk)?)?;
    for name in ["AndroidManifest.xml", "assets/data_main.wad"] {
        if z.file_names().filter(|n| *n == name).count() != 1 {
            return Err(bad("APK needs one manifest and a full data_main.wad"));
        }
    }
    let mut manifest = Vec::new();
    {
        let mut entry = z.by_name("AndroidManifest.xml")?;
        if entry.size() > 1 << 20 {
            return Err(bad("manifest too large"));
        }
        entry.read_to_end(&mut manifest)?;
    }
    let (package, version) = manifest_info(&manifest).map_err(|e| bad(&e.to_string()))?;
    if package.as_deref() != Some(PACKAGE) || version.as_deref() != Some(VERSION) {
        return Err(bad(&format!(
            "This APK is {} {}. Enhancements needs Liberty City Stories for Android {VERSION}.",
            package.unwrap_or_default(),
            version.unwrap_or_default()
        )));
    }
    let entry = z.by_name("assets/data_main.wad")?;
    if entry.compression() != CompressionMethod::Stored {
        return Err(bad("data_main.wad must be stored without ZIP compression"));
    }
    let mut wad = Wad {
        file: File::open(apk)?,
        start: entry.data_start().ok_or_else(|| bad("missing WAD offset"))?,
        length: entry.size(),
        entries: BTreeMap::new(),
    };
    let head = wad.read(0, 12, cancel)?;
    if head != [0x44, 0x41, 0x57, 0x4c, 1, 0, 0, 0, 1, 0, 0, 0] {
        return Err(bad("unsupported WAD header"));
    }
    let dirs = u32_at(&wad.read(2048, 4, cancel)?, 0)? as u64;
    if dirs > 65536 {
        return Err(bad("invalid directory count"));
    }
    let at = 2052 + dirs * 8;
    let count = u32_at(&wad.read(at, 4, cancel)?, 0)? as u64;
    if count > 65536 {
        return Err(bad("invalid file count"));
    }
    let fat = wad.read(at + 4, count * 48, cancel)?;
    for row in fat.as_chunks::<48>().0 {
        let key = u32_at(row, 0)?;
        let nums: Vec<_> = row[16..].as_chunks::<8>().0.iter().map(|v| u64::from_le_bytes(*v)).collect();
        let (offset, size) = (nums[0], nums[1]);
        if offset > wad.length
            || size > wad.length - offset
            || size != nums[2]
            || nums[3] != 0
            || wad.entries.insert(key, (offset, size)).is_some()
        {
            return Err(bad("invalid or unsupported archive entry"));
        }
    }
    Ok(wad)
}
pub fn inspect(apk: &Path) -> Result<usize, PortError> {
    let cancel = AtomicBool::new(false);
    let mut wad = open(apk, &cancel)?;
    characters(&mut wad, &cancel)?;
    Ok(WANTED.len())
}
fn characters(wad: &mut Wad, cancel: &AtomicBool) -> Result<BTreeMap<String, String>, PortError> {
    let data = wad.get("data/default.ide", cancel)?;
    let text = std::str::from_utf8(&data).map_err(|_| bad("invalid character definitions"))?;
    let mut inside = false;
    let mut peds = BTreeMap::new();
    for line in text.lines() {
        let line = line.split('#').next().unwrap_or("").trim().to_ascii_lowercase();
        if line == "peds" {
            inside = true;
        } else if line == "end" {
            inside = false;
        } else if inside && line.contains(',') {
            let fields: Vec<_> = line.split(',').map(str::trim).collect();
            if fields.len() < 3 {
                return Err(bad("invalid character definition"));
            }
            if fields[1] != "null" {
                safe_name(fields[1])?;
                safe_name(fields[2])?;
                peds.insert(fields[1].to_owned(), fields[2].to_owned());
            }
        }
    }
    peds.insert("player".into(), "player".into());
    peds.retain(|model, _| WANTED.contains(&model.as_str()));
    Ok(peds)
}
fn chunk(data: &[u8], at: usize, end: usize) -> Result<(u32, usize, usize), PortError> {
    if at > end || end > data.len() || end - at < 12 {
        return Err(bad("truncated model chunk"));
    }
    let n = u32_at(data, at + 4)? as usize;
    if n > end - at - 12 {
        return Err(bad("invalid model chunk"));
    }
    Ok((u32_at(data, at)?, at + 12, at + 12 + n))
}
fn model_textures(data: &[u8]) -> Result<BTreeSet<String>, PortError> {
    fn scan(
        data: &[u8],
        mut at: usize,
        end: usize,
        texture: bool,
        names: &mut BTreeSet<String>,
    ) -> Result<(), PortError> {
        while at < end {
            let (kind, p, next) = chunk(data, at, end)?;
            if kind == 2 && texture {
                let value = std::str::from_utf8(&data[p..next])
                    .map_err(|_| bad("invalid texture name"))?
                    .split('\0')
                    .next()
                    .unwrap_or("")
                    .to_ascii_lowercase();
                if !value.is_empty() {
                    safe_name(&value)?;
                    names.insert(value);
                }
                return Ok(());
            }
            if kind == 7 || kind == 6 {
                scan(data, p, next, kind == 6, names)?;
            }
            at = next;
        }
        Ok(())
    }
    let (_, mut at, end) = chunk(data, 0, data.len())?;
    let mut names = BTreeSet::new();
    while at < end {
        let (kind, begin, next) = chunk(data, at, end)?;
        if kind == 20 {
            let (_, _, p) = chunk(data, begin, next)?;
            let (kind, geometry, _) = chunk(data, p, next)?;
            if kind != 15 {
                return Err(bad("missing geometry"));
            }
            let size = (u32_at(data, geometry + 16)? & 0xfffff) as usize;
            let (kind, start, finish) = chunk(data, geometry + size, next)?;
            if kind != 8 {
                return Err(bad("missing materials"));
            }
            scan(data, start, finish, false, &mut names)?;
        }
        at = next;
    }
    Ok(names)
}
pub fn extract(
    apk: &Path,
    into: &Path,
    progress: &mut dyn FnMut(Progress),
    cancel: &AtomicBool,
) -> Result<(), PortError> {
    progress(Progress::ModAssets { done: 0, total: 0 });
    let mut wad = open(apk, cancel)?;
    let peds = characters(&mut wad, cancel)?;
    let directory = wad.get("models/gta3.dir", cancel)?;
    if directory.len() % 32 != 0 {
        return Err(bad("invalid IMG directory"));
    }
    let (img_at, img_size) = *wad.entries.get(&crc("models/gta3.img")).ok_or_else(|| bad("missing gta3.img"))?;
    let mut entries = BTreeMap::new();
    for row in directory.as_chunks::<32>().0 {
        let at = u32_at(row, 0)? as u64 * 2048;
        let size = u32_at(row, 4)? as u64 * 2048;
        let name = std::str::from_utf8(&row[8..])
            .map_err(|_| bad("invalid IMG filename"))?
            .split('\0')
            .next()
            .unwrap_or("")
            .to_ascii_lowercase();
        if at > img_size || size > img_size - at || entries.insert(name, (img_at + at, size)).is_some() {
            return Err(bad("invalid or duplicate IMG entry"));
        }
    }
    let mut resources = BTreeMap::new();
    let mut bank = wad.get("anim/ped.ifp", cancel)?;
    for name in ["biked.ifp", "bikeh.ifp", "bikes.ifp", "bikev.ifp"] {
        let &(bike_at, bike_size) = entries.get(name).ok_or_else(|| bad("missing bike animations"))?;
        let bike = wad.read(bike_at, bike_size, cancel)?;
        if bike.get(..4) != Some(b"ANPK") {
            return Err(bad("invalid bike animation bank"));
        }
        let end = 8 + ((u32_at(&bike, 4)? as usize + 3) & !3);
        bank.extend_from_slice(bike.get(..end).ok_or_else(|| bad("truncated bike animations"))?);
    }
    resources.insert("ped.ifp".to_owned(), bank);
    let mut loaded = BTreeMap::new();
    for (model, txd) in peds {
        if let Some(&(at, size)) = entries.get(&format!("{model}.dff")) {
            resources.insert(format!("models/{model}.dff"), wad.read(at, size, cancel)?);
            loaded.insert(model, txd);
        }
    }
    if loaded.len() != WANTED.len() {
        return Err(bad("missing required character models"));
    }
    let mapping = wad.get("master_texture_mapping_process.csv", cancel)?;
    let text = std::str::from_utf8(&mapping).map_err(|_| bad("invalid texture mapping"))?;
    let mut mappings = BTreeMap::new();
    let mut fallback = BTreeMap::new();
    for line in text.lines() {
        let fields: Vec<_> = line.split(',').collect();
        if fields.len() < 3 {
            return Err(bad("invalid texture mapping row"));
        }
        let name = fields[2].to_ascii_lowercase();
        mappings.insert((fields[1].to_ascii_lowercase(), name.clone()), fields[0]);
        fallback.entry(name).or_insert(fields[0]);
    }
    for (model, txd) in &loaded {
        for name in model_textures(&resources[&format!("models/{model}.dff")])? {
            let path = mappings
                .get(&(txd.clone(), name.clone()))
                .or_else(|| fallback.get(&name))
                .ok_or_else(|| bad(&format!("missing texture {name}")))?;
            let parts: Vec<_> = path.split('/').collect();
            if parts.len() != 2 {
                return Err(bad("invalid texture path"));
            }
            for part in parts {
                safe_name(&part.to_ascii_lowercase())?;
            }
            resources
                .insert(format!("textures/{txd}/{name}.pvr"), wad.get(&format!("textures_etc/{path}.pvr"), cancel)?);
        }
    }
    let catalogue: String = loaded.iter().map(|(model, txd)| format!("{model},{txd}\n")).collect();
    resources.insert("characters.txt".into(), catalogue.into_bytes());
    resources.insert("source.txt".into(), format!("{PACKAGE}\n{VERSION}\n").into_bytes());
    if resources.values().map(|v| v.len() as u64).sum::<u64>() > LIMIT {
        return Err(bad("character pack exceeds import limit"));
    }
    let total = resources.len();
    for (index, (name, data)) in resources.into_iter().enumerate() {
        cancelled(cancel)?;
        let target = into.join(name);
        fs::create_dir_all(target.parent().unwrap())?;
        fs::write(target, data)?;
        progress(Progress::ModAssets { done: index + 1, total });
    }
    Ok(())
}
