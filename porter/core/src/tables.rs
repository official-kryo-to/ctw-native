// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
//! Reads the lookup tables from the supported ARM64 `libGame.so`; never executes it.
//! A port of `scripts/extract_tables.py`: the two must produce the same bytes (see `tests/parity.rs`).

use crate::elf::{Elf, Section, EM_AARCH64, PT_LOAD, SHT_DYNSYM, SHT_REL, SHT_RELA, SHT_SYMTAB};
use capstone::arch::arm64::{ArchMode, Arm64OperandType, Arm64Shift};
use capstone::arch::{ArchOperand, BuildsCapstone};
use capstone::{Capstone, Insn, RegId};
use std::collections::HashMap;

pub const BUILD_ID: &str = "a4c441f4943abbcc72e8270ec18248e4358a89e2";
const SP_BASE: i128 = 0x7000_0000_0000;
const X0_BASE: i128 = 0x6000_0000_0000;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct TableError(pub String);

impl std::fmt::Display for TableError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str(&self.0)
    }
}

impl std::error::Error for TableError {}

fn err<T>(message: impl Into<String>) -> Result<T, TableError> {
    Err(TableError(message.into()))
}

/// Register values tracked by the straight-line interpreter; `None` is "known to be unknown".
pub type Regs = HashMap<String, Option<i128>>;
pub type Stack = HashMap<i128, Option<i128>>;

pub struct GameBinary<'a> {
    elf: Elf<'a>,
    symbols: HashMap<String, (u64, u64)>,
    names: HashMap<u64, String>,
    relocations: HashMap<u64, String>,
    cs: Capstone,
}

impl<'a> GameBinary<'a> {
    pub fn new(data: &'a [u8]) -> Result<Self, TableError> {
        Self::open(data, true)
    }

    /// `check_build` off is for tests on synthetic binaries only: the fixed offsets are verified for one build.
    pub fn open(data: &'a [u8], check_build: bool) -> Result<Self, TableError> {
        let invalid = |_| TableError("Invalid ELF game binary.".into());
        let elf = Elf::parse(data).map_err(invalid)?;
        if elf.machine != EM_AARCH64 || !elf.little_endian {
            return err("Expected a little-endian ARM64 libGame.so.");
        }
        let dynsym = match elf.section_by_name(".dynsym") {
            Some(s) => s,
            None => return err("The game binary has no exported symbol table."),
        };
        // Same order as the Python dicts: a name keeps its first position and its last symbol.
        let mut order: Vec<String> = Vec::new();
        let mut symbols: HashMap<String, (u64, u64)> = HashMap::new();
        for s in elf.symbols(dynsym).map_err(invalid)? {
            if symbols.insert(s.name.clone(), (s.value, s.size)).is_none() {
                order.push(s.name);
            }
        }
        let mut names = HashMap::new();
        for name in &order {
            let (value, _) = symbols[name];
            if value != 0 {
                names.insert(value, name.clone());
            }
        }
        if check_build {
            let ids: Vec<String> = match elf.section_by_name(".note.gnu.build-id") {
                Some(note) => elf
                    .notes(note)
                    .map_err(invalid)?
                    .into_iter()
                    .filter(|n| n.n_type == 3)
                    .map(|n| if n.name.as_deref() == Some("GNU") { hex(&n.desc) } else { String::new() })
                    .collect(),
                None => vec![],
            };
            if ids != [BUILD_ID] {
                return err("Unsupported ARM64 build ID; fixed lookup offsets are not verified for this binary.");
            }
        }
        let mut relocations = HashMap::new();
        for section in elf.sections.iter().filter(|s| s.sh_type == SHT_RELA || s.sh_type == SHT_REL) {
            let table: Option<&Section> =
                elf.sections.get(section.link as usize).filter(|s| s.sh_type == SHT_DYNSYM || s.sh_type == SHT_SYMTAB);
            let table_symbols = match table {
                Some(t) => Some(elf.symbols(t).map_err(invalid)?),
                None => None,
            };
            for (offset, sym) in elf.relocations(section).map_err(invalid)? {
                if sym == 0 {
                    continue;
                }
                if let Some(list) = &table_symbols {
                    let name = list
                        .get(sym as usize)
                        .map(|s| s.name.clone())
                        .ok_or_else(|| TableError("Invalid ELF game binary.".into()))?;
                    relocations.insert(offset, name);
                }
            }
        }
        let cs = Capstone::new()
            .arm64()
            .mode(ArchMode::Arm)
            .detail(true)
            .build()
            .map_err(|e| TableError(format!("Could not start the disassembler: {e}")))?;
        Ok(GameBinary { elf, symbols, names, relocations, cs })
    }

    pub fn read(&self, address: u64, count: u64) -> Result<&'a [u8], TableError> {
        for seg in self.elf.segments.iter().filter(|s| s.p_type == PT_LOAD) {
            if address >= seg.vaddr {
                let offset = address - seg.vaddr;
                if offset + count <= seg.filesz {
                    let start = (seg.offset + offset) as usize;
                    return self
                        .elf
                        .data
                        .get(start..start + count as usize)
                        .ok_or_else(|| TableError(format!("Address 0x{address:x} is not file-backed.")));
                }
            }
        }
        err(format!("Address 0x{address:x} is not file-backed."))
    }

    fn symbol_data(&self, name: &str, count: u64) -> Result<&'a [u8], TableError> {
        match self.symbols.get(name) {
            Some(&(value, size)) if size == count => self.read(value, count),
            _ => err(format!("Unexpected layout for {name}; only Android 4.4.243 is supported.")),
        }
    }

    fn reg_name(&self, reg: RegId) -> Option<String> {
        self.cs.reg_name(reg)
    }

    /// Resolves an AArch64 PLT stub through its GOT relocation.
    fn call_name(&self, address: u64) -> Result<String, TableError> {
        if let Some(name) = self.names.get(&address) {
            return Ok(name.clone());
        }
        let code = self.read(address, 16)?;
        let insns = self.cs.disasm_all(code, address).map_err(|e| TableError(e.to_string()))?;
        let mut page: Option<i128> = None;
        for insn in insns.iter() {
            let ops = operands(&self.cs, insn)?;
            match insn.mnemonic() {
                Some("adrp") => {
                    page = ops.get(1).and_then(imm).map(i128::from);
                }
                Some("ldr") if page.is_some() => {
                    let disp = ops.get(1).and_then(mem).map(|(_, d)| d as i128).unwrap_or(0);
                    let slot = page.unwrap() + disp;
                    return Ok(u64::try_from(slot)
                        .ok()
                        .and_then(|s| self.relocations.get(&s).cloned())
                        .unwrap_or_default());
                }
                _ => {}
            }
        }
        Ok(String::new())
    }

    /// Tracks constant call arguments in a straight-line table initializer: every `bl` with the registers and
    /// stack slots known at that point.
    pub fn constant_calls(&self, name: &str) -> Result<Vec<(String, Regs, Stack)>, TableError> {
        let (value, size) = match self.symbols.get(name) {
            Some(&s) => s,
            None => return err(format!("Missing initializer {name}.")),
        };
        let code = self.read(value, size)?;
        self.constant_calls_in(code, value)
    }

    pub fn constant_calls_in(&self, code: &[u8], address: u64) -> Result<Vec<(String, Regs, Stack)>, TableError> {
        let mut regs: Regs = HashMap::from([("sp".to_string(), Some(SP_BASE)), ("x0".to_string(), Some(X0_BASE))]);
        let mut stack: Stack = HashMap::new();
        let mut calls = Vec::new();
        let insns = self.cs.disasm_all(code, address).map_err(|e| TableError(e.to_string()))?;
        for insn in insns.iter() {
            let ops = operands(&self.cs, insn)?;
            let regname = |reg: RegId| -> String {
                let n = self.reg_name(reg).unwrap_or_default();
                match n.strip_prefix('w') {
                    Some(rest) => format!("x{rest}"),
                    None => n,
                }
            };
            let value = |regs: &Regs, op: &Operand| -> Option<i128> {
                match op.kind {
                    Arm64OperandType::Imm(v) => Some(v as i128),
                    Arm64OperandType::Reg(r) => {
                        let n = regname(r);
                        if n == "xzr" || n == "wzr" {
                            Some(0)
                        } else {
                            regs.get(&n).copied().flatten()
                        }
                    }
                    _ => None,
                }
            };
            let write = |regs: &mut Regs, op: &Operand, number: Option<i128>| {
                if let Arm64OperandType::Reg(r) = op.kind {
                    let wide = self.reg_name(r).unwrap_or_default();
                    let number = if wide.starts_with('w') { number.map(|n| n & 0xFFFF_FFFF) } else { number };
                    regs.insert(regname(r), number);
                }
            };
            let op = |i: usize| {
                ops.get(i).ok_or_else(|| TableError(format!("Unexpected operands at 0x{:x}.", insn.address())))
            };
            match insn.mnemonic().unwrap_or("") {
                "mov" | "adrp" => {
                    let v = value(&regs, op(1)?);
                    write(&mut regs, op(0)?, v);
                }
                m @ ("add" | "sub") => {
                    let a = value(&regs, op(1)?);
                    let mut b = value(&regs, op(2)?);
                    let shift = shift_value(&op(2)?.shift);
                    if let (Some(v), true) = (b, shift != 0) {
                        b = Some(v << shift);
                    }
                    let result = match (a, b) {
                        (Some(a), Some(b)) => Some(if m == "add" { a + b } else { a - b }),
                        _ => None,
                    };
                    write(&mut regs, op(0)?, result);
                }
                "str" | "strb" | "stur" => {
                    if let Some((base, disp)) = mem(op(1)?) {
                        if let Some(base) = regs.get(&regname(base)).copied().flatten() {
                            let v = value(&regs, op(0)?);
                            stack.insert(base + disp as i128, v);
                        }
                    }
                }
                "ldr" if matches!(op(0)?.kind, Arm64OperandType::Reg(r) if self.reg_name(r).is_some_and(|n| n.starts_with('d'))) =>
                {
                    let number = match mem(op(1)?) {
                        Some((base, disp)) => match regs.get(&regname(base)).copied().flatten() {
                            Some(base) => {
                                let address = u64::try_from(base + disp as i128)
                                    .map_err(|_| TableError("Invalid constant load address.".into()))?;
                                Some(u64::from_le_bytes(self.read(address, 8)?.try_into().unwrap()) as i128)
                            }
                            None => None,
                        },
                        None => None,
                    };
                    write(&mut regs, op(0)?, number);
                }
                "bl" => {
                    let target = imm(op(0)?).unwrap_or(0) as u64;
                    calls.push((self.call_name(target)?, regs.clone(), stack.clone()));
                    for r in 0..19 {
                        regs.remove(&format!("x{r}"));
                    }
                }
                "ret" => return Ok(calls),
                _ => {
                    let detail = self.cs.insn_detail(insn).map_err(|e| TableError(e.to_string()))?;
                    for &reg in detail.regs_write() {
                        regs.remove(&regname(reg));
                    }
                }
            }
        }
        Ok(calls)
    }

    pub fn population(&self) -> Result<Vec<u8>, TableError> {
        let mut profiles: Vec<Vec<i128>> = Vec::new();
        for (name, regs, stack) in self.constant_calls("_ZN12cZoneManager17DefinePopProfilesEv")? {
            if !name.starts_with("_ZN17PopulationProfileC") {
                continue;
            }
            let bad = || TableError("Could not resolve population profile arguments.".into());
            let sp = regs.get("sp").copied().flatten().ok_or_else(bad)?;
            let mut values = Vec::new();
            for i in 1..8 {
                values.push(regs.get(&format!("x{i}")).copied().flatten());
            }
            for i in 0..5 {
                values.push(stack.get(&(sp + i * 8)).copied().flatten());
            }
            let values: Vec<i128> = values.into_iter().collect::<Option<_>>().ok_or_else(bad)?;
            if values.iter().any(|v| !(0..=255).contains(v)) {
                return Err(bad());
            }
            profiles.push(values);
        }
        if profiles.is_empty() {
            return err("No population profiles found.");
        }
        let mut zones: Vec<u8> = Vec::new();
        let mut zone_count = 0u32;
        for (name, regs, stack) in self.constant_calls("_ZN12cZoneManager22SetupDefaultPopulationEv")? {
            if !name.starts_with("_ZNK8ZoneImpl17SetPopulationZone") {
                continue;
            }
            let get = |r: &str| regs.get(r).copied().flatten();
            let (address, profile) = match (get("x1"), get("x3")) {
                (Some(a), Some(p)) => (a, p),
                _ => return err("Could not resolve population zone arguments."),
            };
            let layout = || TableError("Unsupported population zone layout.".into());
            let raw = self.read(u64::try_from(address).map_err(|_| layout())?, 16)?;
            let zone_name = &raw[..raw.iter().position(|&b| b == 0).unwrap_or(raw.len())];
            let (index, remainder) = ((profile - X0_BASE).div_euclid(20), (profile - X0_BASE).rem_euclid(20));
            let sp = get("sp").ok_or_else(layout)?;
            let values: Vec<Option<i128>> = vec![
                get("x2"),
                Some(index),
                get("x4"),
                get("x5"),
                get("x6"),
                get("x7"),
                stack.get(&sp).copied().flatten(),
                stack.get(&(sp + 8)).copied().flatten(),
            ];
            if !(1..=8).contains(&zone_name.len())
                || remainder != 0
                || !(0..profiles.len() as i128).contains(&index)
                || values.iter().any(Option::is_none)
            {
                return Err(layout());
            }
            let mut padded = [0u8; 8];
            padded[..zone_name.len()].copy_from_slice(zone_name);
            zones.extend(padded);
            for v in values.into_iter().flatten() {
                zones.extend(i32_le(v)?);
            }
            zone_count += 1;
        }
        if zone_count == 0 {
            return err("No population zones found.");
        }
        let mut out = b"CTWZONE1".to_vec();
        out.extend(zone_count.to_le_bytes());
        out.extend((profiles.len() as u32).to_le_bytes());
        out.extend(zones);
        for p in profiles {
            for v in p {
                out.extend(i32_le(v)?);
            }
        }
        Ok(out)
    }

    pub fn sound(&self) -> Result<Vec<u8>, TableError> {
        let mut data = b"CTWSND2\0".to_vec();
        for (name, size) in [
            ("gEventInfo", 156 * 16),
            ("gGears", 20 * 48),
            ("gCarCollisionEventsLow", 12),
            ("gCarCollisionEventsMed", 12),
            ("gCarCollisionEventsHigh", 12),
        ] {
            data.extend(self.symbol_data(name, size)?);
        }
        // The rev-after-shift table has no exported name in this supported version.
        data.extend(self.read(0x480920, 16)?);
        data.extend(self.symbol_data("gPropSfx", 57 * 4)?);
        Ok(data)
    }

    pub fn render(&self) -> Result<Vec<u8>, TableError> {
        let mut out = b"CTWREND1".to_vec();
        out.extend(self.read(0x486b04, 36)?);
        out.extend(self.read(0x486a8c, 24)?);
        for a in [0x46a980, 0x469b00, 0x468600, 0x46a450] {
            out.extend(self.read(a, 16)?);
        }
        out.extend(&self.symbol_data("TextColours", 92)?[..32]);
        Ok(out)
    }

    pub fn gameplay(&self) -> Result<Vec<u8>, TableError> {
        let mut out = b"CTWGAME1".to_vec();
        out.extend(self.read(0x4872c0, 64)?);
        // The top-ratio table stores 64-bit integers even though its values fit in Q12 int32.
        for chunk in self.read(0x487250, 48)?.as_chunks::<8>().0 {
            out.extend(i32_le(i64::from_le_bytes(*chunk) as i128)?);
        }
        for (a, n) in [(0x481b44, 7), (0x481b4b, 7), (0x481b52, 52), (0x481b86, 52), (0x48718e, 10), (0x469dd0, 16)] {
            out.extend(self.read(a, n)?);
        }
        Ok(out)
    }

    pub fn radio(&self) -> Result<Vec<u8>, TableError> {
        let icons = self.read(0x484732, 11)?;
        let streams = self.read(0x484740, 44)?.as_chunks::<4>().0;
        let labels = self.read(0x48476c, 44)?.as_chunks::<4>().0;
        let names = self.read(0x484798, 44)?.as_chunks::<4>().0;
        let mut out = b"CTWRAD2\0".to_vec();
        out.extend(11u32.to_le_bytes());
        out.extend(33u32.to_le_bytes());
        out.extend(self.read(0x484728, 10)?);
        for i in 0..11 {
            out.extend((icons[i] as i32).to_le_bytes());
            out.extend(streams[i]);
            out.extend(labels[i]);
            out.extend(names[i]);
        }
        out.extend(self.read(0x480b64, 33 * 40)?);
        Ok(out)
    }

    pub fn restart(&self) -> Result<Vec<u8>, TableError> {
        restart_table(self.constant_calls("_ZN11CScriptMain19DefineRestartPointsEv")?)
    }
}

fn restart_table(calls: Vec<(String, Regs, Stack)>) -> Result<Vec<u8>, TableError> {
    let mut points = Vec::new();
    for (name, regs, stack) in calls {
        if !name.contains("AddHospitalRestartPoint") {
            continue;
        }
        let bad = || TableError("Could not resolve hospital restart coordinates.".into());
        let pointer = regs.get("x1").copied().flatten().ok_or_else(bad)?;
        let packed = stack.get(&pointer).copied().flatten().ok_or_else(bad)?;
        let packed = u64::try_from(packed).map_err(|_| bad())?.to_le_bytes();
        let z = stack.get(&(pointer + 8)).copied().flatten().ok_or_else(bad)?;
        let heading = regs.get("x2").copied().flatten().ok_or_else(bad)?;
        let heading = u32::try_from(heading).map_err(|_| bad())? as i32;
        let mut point = packed.to_vec();
        point.extend(i32_le(z)?);
        point.extend(heading.to_le_bytes());
        points.push(point);
    }
    if points.len() != 5 {
        return err("Unexpected hospital restart point count.");
    }
    let mut out = b"CTWRESP1".to_vec();
    out.extend((points.len() as u32).to_le_bytes());
    out.extend(points.into_iter().flatten());
    Ok(out)
}

/// Every table, keyed by the file name it is saved as in the game's data folder.
pub fn extract_tables(binary: &[u8]) -> Result<Vec<(&'static str, Vec<u8>)>, TableError> {
    let game = GameBinary::new(binary)?;
    Ok(vec![
        ("population_tables.bin", game.population()?),
        ("sound_tables.bin", game.sound()?),
        ("render_tables.bin", game.render()?),
        ("gameplay_tables.bin", game.gameplay()?),
        ("radio_tables.bin", game.radio()?),
        ("restart_tables.bin", game.restart()?),
    ])
}

fn i32_le(v: i128) -> Result<[u8; 4], TableError> {
    i32::try_from(v).map(i32::to_le_bytes).map_err(|_| TableError("A table value does not fit in 32 bits.".into()))
}

fn hex(bytes: &[u8]) -> String {
    bytes.iter().map(|b| format!("{b:02x}")).collect()
}

pub struct Operand {
    kind: Arm64OperandType,
    shift: Arm64Shift,
}

fn operands(cs: &Capstone, insn: &Insn) -> Result<Vec<Operand>, TableError> {
    let detail = cs.insn_detail(insn).map_err(|e| TableError(e.to_string()))?;
    Ok(detail
        .arch_detail()
        .operands()
        .into_iter()
        .filter_map(|o| match o {
            ArchOperand::Arm64Operand(op) => Some(Operand { kind: op.op_type, shift: op.shift }),
            _ => None,
        })
        .collect())
}

fn imm(op: &Operand) -> Option<i64> {
    match op.kind {
        Arm64OperandType::Imm(v) => Some(v),
        _ => None,
    }
}

fn mem(op: &Operand) -> Option<(RegId, i32)> {
    match &op.kind {
        Arm64OperandType::Mem(m) => Some((m.base(), m.disp())),
        _ => None,
    }
}

fn shift_value(shift: &Arm64Shift) -> u32 {
    match *shift {
        Arm64Shift::Invalid => 0,
        Arm64Shift::Lsl(v) | Arm64Shift::Msl(v) | Arm64Shift::Lsr(v) | Arm64Shift::Asr(v) | Arm64Shift::Ror(v) => v,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn hospital_call() -> (String, Regs, Stack) {
        let packed = u64::from_le_bytes([(-4096i32).to_le_bytes(), 8192i32.to_le_bytes()].concat().try_into().unwrap());
        (
            "AddHospitalRestartPoint".into(),
            HashMap::from([("x1".into(), Some(SP_BASE)), ("x2".into(), Some((-90i32 as u32) as i128))]),
            HashMap::from([(SP_BASE, Some(packed as i128)), (SP_BASE + 8, Some(123))]),
        )
    }

    #[test]
    fn restart_preserves_signed_coordinates_and_heading() {
        let mut calls = vec![hospital_call(); 5];
        calls.insert(0, ("OtherRestartPoint".into(), Regs::new(), Stack::new()));
        let table = restart_table(calls).unwrap();
        assert_eq!(&table[..8], b"CTWRESP1");
        assert_eq!(&table[8..12], &5u32.to_le_bytes());
        let point: Vec<u8> = [-4096i32, 8192, 123, -90].into_iter().flat_map(i32::to_le_bytes).collect();
        assert_eq!(&table[12..], point.repeat(5));
    }

    #[test]
    fn restart_requires_five_resolved_points() {
        assert!(restart_table(vec![hospital_call(); 4]).is_err());
        let mut calls = vec![hospital_call(); 5];
        calls[2].1.remove("x1");
        assert!(restart_table(calls).is_err());
        let mut calls = vec![hospital_call(); 5];
        calls[2].2.remove(&(SP_BASE + 8));
        assert!(restart_table(calls).is_err());
    }
}
