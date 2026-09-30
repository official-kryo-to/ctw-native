// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
//! The little of ELF64 that the table extraction reads: program headers, sections, symbols, relocations and
//! notes. Reads only; nothing in the binary is ever run.

pub const EM_AARCH64: u16 = 183;
pub const PT_LOAD: u32 = 1;
pub const SHT_SYMTAB: u32 = 2;
pub const SHT_RELA: u32 = 4;
pub const SHT_NOTE: u32 = 7;
pub const SHT_REL: u32 = 9;
pub const SHT_DYNSYM: u32 = 11;

#[derive(Debug)]
pub struct ElfError;

pub struct Segment {
    pub p_type: u32,
    pub offset: u64,
    pub vaddr: u64,
    pub filesz: u64,
}

pub struct Section {
    pub name: String,
    pub sh_type: u32,
    pub offset: u64,
    pub size: u64,
    pub link: u32,
    pub entsize: u64,
}

#[derive(Clone)]
pub struct Symbol {
    pub name: String,
    pub value: u64,
    pub size: u64,
}

pub struct Note {
    pub name: Option<String>,
    pub n_type: u32,
    pub desc: Vec<u8>,
}

pub struct Elf<'a> {
    pub data: &'a [u8],
    pub machine: u16,
    pub little_endian: bool,
    pub segments: Vec<Segment>,
    pub sections: Vec<Section>,
}

fn bytes(d: &[u8], pos: u64, n: u64) -> Result<&[u8], ElfError> {
    let start = usize::try_from(pos).map_err(|_| ElfError)?;
    let end = start.checked_add(usize::try_from(n).map_err(|_| ElfError)?).ok_or(ElfError)?;
    d.get(start..end).ok_or(ElfError)
}

fn u16_at(d: &[u8], pos: u64) -> Result<u16, ElfError> {
    let b = bytes(d, pos, 2)?;
    Ok(u16::from_le_bytes([b[0], b[1]]))
}

fn u32_at(d: &[u8], pos: u64) -> Result<u32, ElfError> {
    let b = bytes(d, pos, 4)?;
    Ok(u32::from_le_bytes(b.try_into().unwrap()))
}

fn u64_at(d: &[u8], pos: u64) -> Result<u64, ElfError> {
    let b = bytes(d, pos, 8)?;
    Ok(u64::from_le_bytes(b.try_into().unwrap()))
}

fn cstr(d: &[u8], pos: u64) -> Result<String, ElfError> {
    let start = usize::try_from(pos).map_err(|_| ElfError)?;
    let rest = d.get(start..).ok_or(ElfError)?;
    let end = rest.iter().position(|&b| b == 0).unwrap_or(rest.len());
    Ok(rest[..end].iter().map(|&b| b as char).collect())
}

impl<'a> Elf<'a> {
    /// Parses the headers. Only 64-bit files are read in full; anything else reports its machine as 0, which the
    /// caller rejects as "not ARM64".
    pub fn parse(data: &'a [u8]) -> Result<Self, ElfError> {
        if data.len() < 16 || &data[..4] != b"\x7fELF" {
            return Err(ElfError);
        }
        let class64 = data[4] == 2;
        let little_endian = data[5] == 1;
        if !class64 || !little_endian {
            return Ok(Elf { data, machine: 0, little_endian, segments: vec![], sections: vec![] });
        }
        let machine = u16_at(data, 18)?;
        let (phoff, shoff) = (u64_at(data, 32)?, u64_at(data, 40)?);
        let (phentsize, phnum) = (u16_at(data, 54)? as u64, u16_at(data, 56)? as u64);
        let (shentsize, shnum, shstrndx) =
            (u16_at(data, 58)? as u64, u16_at(data, 60)? as u64, u16_at(data, 62)? as u64);
        let mut segments = Vec::new();
        for i in 0..phnum {
            let at = phoff + i * phentsize;
            segments.push(Segment {
                p_type: u32_at(data, at)?,
                offset: u64_at(data, at + 8)?,
                vaddr: u64_at(data, at + 16)?,
                filesz: u64_at(data, at + 32)?,
            });
        }
        let mut raw = Vec::new();
        for i in 0..shnum {
            let at = shoff + i * shentsize;
            raw.push((
                u32_at(data, at)?,
                u32_at(data, at + 4)?,
                u64_at(data, at + 24)?,
                u64_at(data, at + 32)?,
                u32_at(data, at + 40)?,
                u64_at(data, at + 56)?,
            ));
        }
        let names_offset = raw.get(shstrndx as usize).map(|s| s.2);
        let mut sections = Vec::new();
        for (name, sh_type, offset, size, link, entsize) in raw {
            let name = match names_offset {
                Some(base) => cstr(data, base + name as u64)?,
                None => String::new(),
            };
            sections.push(Section { name, sh_type, offset, size, link, entsize });
        }
        Ok(Elf { data, machine, little_endian, segments, sections })
    }

    pub fn section_by_name(&self, name: &str) -> Option<&Section> {
        self.sections.iter().find(|s| s.name == name)
    }

    /// The symbols of a symbol table section, in file order (index 0 included), using its linked string table.
    pub fn symbols(&self, section: &Section) -> Result<Vec<Symbol>, ElfError> {
        let strings = self.sections.get(section.link as usize).ok_or(ElfError)?;
        let entsize = if section.entsize == 0 { 24 } else { section.entsize };
        let mut out = Vec::new();
        for i in 0..section.size / entsize {
            let at = section.offset + i * entsize;
            let name = u32_at(self.data, at)? as u64;
            out.push(Symbol {
                name: cstr(self.data, strings.offset + name)?,
                value: u64_at(self.data, at + 8)?,
                size: u64_at(self.data, at + 16)?,
            });
        }
        Ok(out)
    }

    /// `(r_offset, symbol index)` of a REL or RELA section.
    pub fn relocations(&self, section: &Section) -> Result<Vec<(u64, u32)>, ElfError> {
        let default = if section.sh_type == SHT_RELA { 24 } else { 16 };
        let entsize = if section.entsize == 0 { default } else { section.entsize };
        let mut out = Vec::new();
        for i in 0..section.size / entsize {
            let at = section.offset + i * entsize;
            out.push((u64_at(self.data, at)?, (u64_at(self.data, at + 8)? >> 32) as u32));
        }
        Ok(out)
    }

    /// The notes of a note section; names and descriptions are 4-byte aligned (as pyelftools reads them).
    pub fn notes(&self, section: &Section) -> Result<Vec<Note>, ElfError> {
        let mut out = Vec::new();
        let mut offset = section.offset;
        let end = section.offset + section.size;
        while offset + 12 < end {
            let (namesz, descsz, n_type) = (
                u32_at(self.data, offset)? as u64,
                u32_at(self.data, offset + 4)? as u64,
                u32_at(self.data, offset + 8)?,
            );
            offset += 12;
            let name = if namesz > 0 {
                let disk = (namesz + 3) & !3;
                let raw = bytes(self.data, offset, disk)?;
                offset += disk;
                let end = raw.iter().position(|&b| b == 0).unwrap_or(raw.len());
                Some(raw[..end].iter().map(|&b| b as char).collect())
            } else {
                None
            };
            let desc = bytes(self.data, offset, descsz)?.to_vec();
            offset += (descsz + 3) & !3;
            out.push(Note { name, n_type, desc });
        }
        Ok(out)
    }
}
