// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
//! Package name and version from an Android manifest, without running anything from the APK.
//! A port of `scripts/apk_manifest.py`; the two must agree.

use std::collections::HashMap;

const ANDROID_NS: &str = "http://schemas.android.com/apk/res/android";

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ManifestError(pub String);

impl std::fmt::Display for ManifestError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str(&self.0)
    }
}

impl std::error::Error for ManifestError {}

fn err<T>(message: &str) -> Result<T, ManifestError> {
    Err(ManifestError(message.to_string()))
}

/// `(package, versionName)`; either is `None` when the manifest does not say.
pub fn manifest_info(data: &[u8]) -> Result<(Option<String>, Option<String>), ManifestError> {
    let text = data.trim_ascii_start();
    if text.starts_with(b"<") {
        return text_manifest(data);
    }
    binary_manifest(data).map_err(|e| match e {
        Parse::Error(e) => e,
        Parse::Malformed => ManifestError("Malformed Android manifest.".into()),
    })
}

fn text_manifest(data: &[u8]) -> Result<(Option<String>, Option<String>), ManifestError> {
    let malformed = || ManifestError("Malformed Android manifest.".into());
    let text = std::str::from_utf8(data).map_err(|_| malformed())?;
    let doc = roxmltree::Document::parse(text).map_err(|_| malformed())?;
    let root = doc.root_element();
    Ok((root.attribute("package").map(str::to_string), root.attribute((ANDROID_NS, "versionName")).map(str::to_string)))
}

enum Parse {
    Error(ManifestError),
    /// Python's struct/index/unicode errors: reported as a malformed manifest.
    Malformed,
}

impl From<ManifestError> for Parse {
    fn from(e: ManifestError) -> Self {
        Parse::Error(e)
    }
}

fn u8_at(d: &[u8], pos: usize) -> Result<u8, Parse> {
    d.get(pos).copied().ok_or(Parse::Malformed)
}

fn u16_at(d: &[u8], pos: usize) -> Result<u16, Parse> {
    let b = d.get(pos..pos.checked_add(2).ok_or(Parse::Malformed)?).ok_or(Parse::Malformed)?;
    Ok(u16::from_le_bytes([b[0], b[1]]))
}

fn u32_at(d: &[u8], pos: usize) -> Result<u32, Parse> {
    let b = d.get(pos..pos.checked_add(4).ok_or(Parse::Malformed)?).ok_or(Parse::Malformed)?;
    Ok(u32::from_le_bytes([b[0], b[1], b[2], b[3]]))
}

fn string(strings: &[String], index: u32) -> Result<&String, Parse> {
    strings.get(index as usize).ok_or(Parse::Malformed)
}

fn binary_manifest(data: &[u8]) -> Result<(Option<String>, Option<String>), Parse> {
    let (kind, header, total) = (u16_at(data, 0)?, u16_at(data, 2)?, u32_at(data, 4)? as usize);
    if kind != 3 || header < 8 || total != data.len() {
        return Err(ManifestError("Invalid Android manifest header.".into()).into());
    }
    let mut strings: Vec<String> = Vec::new();
    let mut offset = header as usize;
    while offset < total {
        let (kind, header_size, size) =
            (u16_at(data, offset)?, u16_at(data, offset + 2)? as usize, u32_at(data, offset + 4)? as usize);
        let end = offset + size;
        if header_size < 8 || size < header_size || end > total {
            return err("Invalid Android manifest chunk.").map_err(Parse::from);
        }
        let chunk = &data[offset..end];
        if kind == 1 {
            // ResStringPool
            let (count, flags, start) = (u32_at(chunk, 8)? as usize, u32_at(chunk, 16)?, u32_at(chunk, 20)? as usize);
            u32_at(chunk, 24)?;
            if header_size < 28 || header_size + count * 4 > size {
                return err("Invalid manifest string pool.").map_err(Parse::from);
            }
            let utf8 = flags & 0x100 != 0;
            let length = |pos: usize, utf8: bool| -> Result<(usize, usize), Parse> {
                if utf8 {
                    let value = u8_at(chunk, pos)? as usize;
                    if value & 0x80 != 0 {
                        return Ok((((value & 0x7F) << 8) | u8_at(chunk, pos + 1)? as usize, pos + 2));
                    }
                    Ok((value, pos + 1))
                } else {
                    let value = u16_at(chunk, pos)? as usize;
                    if value & 0x8000 != 0 {
                        return Ok((((value & 0x7FFF) << 16) | u16_at(chunk, pos + 2)? as usize, pos + 4));
                    }
                    Ok((value, pos + 2))
                }
            };
            strings.clear();
            for i in 0..count {
                let mut pos = start + u32_at(chunk, header_size + i * 4)? as usize;
                let (mut n, next) = length(pos, utf8)?;
                pos = next;
                if utf8 {
                    (n, pos) = length(pos, true)?;
                }
                if !utf8 {
                    n *= 2;
                }
                if pos + n > size {
                    return err("Truncated manifest string.").map_err(Parse::from);
                }
                let bytes = &chunk[pos..pos + n];
                let decoded = if utf8 {
                    String::from_utf8(bytes.to_vec()).map_err(|_| Parse::Malformed)?
                } else {
                    let units: Vec<u16> = bytes.as_chunks::<2>().0.iter().map(|c| u16::from_le_bytes(*c)).collect();
                    String::from_utf16(&units).map_err(|_| Parse::Malformed)?
                };
                strings.push(decoded);
            }
        } else if kind == 0x102 {
            // ResXMLStartElement
            let name = u32_at(chunk, header_size + 4)?;
            let attr_start = u16_at(chunk, header_size + 8)? as usize;
            let attr_size = u16_at(chunk, header_size + 10)? as usize;
            let count = u16_at(chunk, header_size + 12)? as usize;
            if string(&strings, name)? == "manifest" {
                if attr_size < 20 {
                    return err("Invalid manifest attributes.").map_err(Parse::from);
                }
                let mut values: HashMap<(Option<String>, String), String> = HashMap::new();
                for i in 0..count {
                    let pos = header_size + attr_start + i * attr_size;
                    if pos + 20 > size {
                        return err("Truncated manifest attribute.").map_err(Parse::from);
                    }
                    let (ns, key, raw) = (u32_at(chunk, pos)?, u32_at(chunk, pos + 4)?, u32_at(chunk, pos + 8)?);
                    let (value_type, value) = (u8_at(chunk, pos + 15)?, u32_at(chunk, pos + 16)?);
                    let decoded = if raw != 0xFFFF_FFFF {
                        string(&strings, raw)?.clone()
                    } else if value_type == 3 {
                        string(&strings, value)?.clone()
                    } else {
                        value.to_string()
                    };
                    let namespace = if ns == 0xFFFF_FFFF { None } else { Some(string(&strings, ns)?.clone()) };
                    values.insert((namespace, string(&strings, key)?.clone()), decoded);
                }
                return Ok((
                    values.get(&(None, "package".to_string())).cloned(),
                    values.get(&(Some(ANDROID_NS.to_string()), "versionName".to_string())).cloned(),
                ));
            }
        }
        offset = end;
    }
    err("No manifest element found.").map_err(Parse::from)
}

#[cfg(test)]
pub(crate) mod tests {
    use super::*;

    /// The same fixture as `tests/test_setup.py`.
    pub(crate) fn binary_manifest(utf8: bool) -> Vec<u8> {
        let strings = ["manifest", "package", "versionName", "com.rockstargames.gtactw", "4.4.243", ANDROID_NS];
        let (mut pool, mut offsets) = (Vec::new(), Vec::new());
        for s in strings {
            offsets.push(pool.len() as u32);
            if utf8 {
                pool.extend([s.len() as u8, s.len() as u8]);
                pool.extend(s.as_bytes());
                pool.push(0);
            } else {
                pool.extend((s.len() as u16).to_le_bytes());
                for u in s.encode_utf16() {
                    pool.extend(u.to_le_bytes());
                }
                pool.extend([0, 0]);
            }
        }
        let start = 28 + strings.len() as u32 * 4;
        let size = start + pool.len() as u32;
        let mut chunk = Vec::new();
        chunk.extend(1u16.to_le_bytes());
        chunk.extend(28u16.to_le_bytes());
        for v in [size, strings.len() as u32, 0, if utf8 { 0x100 } else { 0 }, start, 0] {
            chunk.extend(v.to_le_bytes());
        }
        for o in offsets {
            chunk.extend(o.to_le_bytes());
        }
        chunk.extend(pool);
        let attr = |ns: u32, key: u32, raw: u32, value: u32| {
            let mut a = Vec::new();
            a.extend(ns.to_le_bytes());
            a.extend(key.to_le_bytes());
            a.extend(raw.to_le_bytes());
            a.extend(8u16.to_le_bytes());
            a.extend([0, 3]);
            a.extend(value.to_le_bytes());
            a
        };
        let mut attrs = attr(0xFFFF_FFFF, 1, 3, 3);
        attrs.extend(attr(5, 2, 0xFFFF_FFFF, 4));
        let mut element = Vec::new();
        element.extend(0x102u16.to_le_bytes());
        element.extend(16u16.to_le_bytes());
        for v in [36 + attrs.len() as u32, 1, 0xFFFF_FFFF, 0xFFFF_FFFF, 0] {
            element.extend(v.to_le_bytes());
        }
        for v in [20u16, 20, 2, 0, 0, 0] {
            element.extend(v.to_le_bytes());
        }
        element.extend(attrs);
        let mut content = chunk;
        content.extend(element);
        let mut out = Vec::new();
        out.extend(3u16.to_le_bytes());
        out.extend(8u16.to_le_bytes());
        out.extend((8 + content.len() as u32).to_le_bytes());
        out.extend(content);
        out
    }

    #[test]
    fn string_encodings_and_typed_value() {
        for utf8 in [true, false] {
            assert_eq!(
                manifest_info(&binary_manifest(utf8)).unwrap(),
                (Some("com.rockstargames.gtactw".into()), Some("4.4.243".into()))
            );
        }
    }

    #[test]
    fn malformed() {
        let fixture = binary_manifest(true);
        let mut zeros = fixture[..16].to_vec();
        zeros.extend([0u8; 30]);
        for data in [b"bad".to_vec(), fixture[..fixture.len() - 1].to_vec(), zeros] {
            assert!(manifest_info(&data).is_err());
        }
    }

    #[test]
    fn text_manifest() {
        let xml = format!(r#"<manifest xmlns:android="{ANDROID_NS}" package="a.b" android:versionName="1.2"/>"#);
        assert_eq!(manifest_info(xml.as_bytes()).unwrap(), (Some("a.b".into()), Some("1.2".into())));
    }
}
