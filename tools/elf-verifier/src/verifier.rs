//! Generic ELF module verifier (CAN-BLD-003).
//!
//! Checks a compiled module ELF against the target loader profile and the
//! Canopus static constraints from architecture §13.4. Failures are errors;
//! warnings do not fail verification.

use canopus_core::model::{AddressRange, TargetPack};
use object::{
    Architecture, BinaryFormat, Object, ObjectKind, ObjectSection, ObjectSymbol, RelocationFlags,
    RelocationTarget, SectionFlags, SectionKind, SymbolSection,
};
use serde::Serialize;
use sha2::{Digest, Sha256};

#[derive(Debug, Clone, Serialize, Default)]
pub struct VerifyReport {
    pub ok: bool,
    pub sha256: Option<String>,
    pub errors: Vec<String>,
    pub warnings: Vec<String>,
    pub summary: Summary,
}

#[derive(Debug, Clone, Serialize, Default)]
pub struct Summary {
    pub format: Option<String>,
    pub machine: Option<String>,
    pub kind: Option<String>,
    pub endianness: Option<String>,
    pub section_count: usize,
    pub undefined_symbols: usize,
    pub relocation_count: usize,
    pub constructor_count: usize,
    pub destructor_count: usize,
    pub absolute_address_hits: usize,
}

/// Sections that are never allowed in a Canopus module.
///
/// `.ARM.exidx` / `.ARM.extab` are NOT here: the stock loader demonstrably
/// handles them — btpatch_phase5.bin carries a combined `.ARM.exidx` and runs
/// on device. `.got` / `.tdata` / `.eh_frame` remain forbidden (position-
/// independent GOT, TLS and C++ exception unwinding are loader-unsupported).
const FORBIDDEN_SECTIONS: &[&str] = &[
    ".got",
    ".got.plt",
    ".tdata",
    ".tbss",
    ".eh_frame",
    ".eh_frame_hdr",
];

pub struct Verifier<'a> {
    pub target: &'a TargetPack,
    /// Additional allowed callable addresses (from the target symbol table).
    pub allowed_addresses: &'a [u64],
}

impl<'a> Verifier<'a> {
    pub fn verify(&self, data: &[u8]) -> VerifyReport {
        let mut report = VerifyReport {
            ok: true,
            sha256: Some(hex_sha256(data)),
            ..Default::default()
        };

        let file = match object::File::parse(data) {
            Ok(f) => f,
            Err(e) => {
                report.errors.push(format!("ELF parse failed: {e}"));
                report.ok = false;
                return report;
            }
        };

        // --- format / machine / endianness ---------------------------
        report.summary.format = Some(format!("{:?}", file.format()));
        report.summary.machine = Some(format!("{:?}", file.architecture()));
        report.summary.kind = Some(format!("{:?}", file.kind()));
        report.summary.endianness = Some(if file.is_little_endian() {
            "little".into()
        } else {
            "big".into()
        });

        if file.format() != BinaryFormat::Elf {
            report
                .errors
                .push(format!("expected ELF, got {:?}", file.format()));
        }
        if file.architecture() != Architecture::Arm {
            report.errors.push(format!(
                "expected ARM (EM_ARM), got {:?}",
                file.architecture()
            ));
        }
        if !file.is_little_endian() {
            report.errors.push("expected little-endian".into());
        }
        if file.kind() != ObjectKind::Relocatable {
            report.errors.push(format!(
                "expected relocatable (ET_REL), got {:?}",
                file.kind()
            ));
        }

        let profile = self.target.loader_profile.as_ref();

        // --- undefined symbols (zero-import policy) ------------------
        for sym in file.symbols() {
            if sym.is_undefined() {
                let name = sym.name().unwrap_or("<unnamed>");
                if !name.is_empty() {
                    report.summary.undefined_symbols += 1;
                    report.errors.push(format!(
                        "undefined symbol '{name}': zero-import target forbids it"
                    ));
                }
            }
        }

        // --- sections ------------------------------------------------
        for sec in file.sections() {
            let name = sec.name().unwrap_or("<unnamed>");
            let flags = sec.flags();
            let mut sh_exec = false;
            let mut sh_write = false;
            if let SectionFlags::Elf { sh_flags } = flags {
                sh_exec = sh_flags & 0x4 != 0; // SHF_EXECINSTR
                sh_write = sh_flags & 0x1 != 0; // SHF_WRITE
            }
            let kind = sec.kind();
            let is_exec = sh_exec || matches!(kind, SectionKind::Text);
            let is_writable = sh_write
                || matches!(
                    kind,
                    SectionKind::Data
                        | SectionKind::UninitializedData
                        | SectionKind::Tls
                        | SectionKind::UninitializedTls
                );

            if is_exec && is_writable {
                report
                    .errors
                    .push(format!("writable+executable section '{name}'"));
            }
            if FORBIDDEN_SECTIONS.contains(&name) {
                report.errors.push(format!(
                    "unexpected section '{name}' (not supported by stock loader)"
                ));
            }
        }
        report.summary.section_count = file.sections().count();

        // --- relocations vs loader allowlist --------------------------
        let allow: Vec<u32> = profile
            .and_then(|p| p.relocations.as_ref())
            .map(|r| r.iter().map(|r| r.arm_type).collect())
            .unwrap_or_default();

        for sec in file.sections() {
            for (_offset, reloc) in sec.relocations() {
                report.summary.relocation_count += 1;
                let rt: u32 = match reloc.flags() {
                    RelocationFlags::Elf { r_type } => r_type,
                    _ => 0,
                };
                // Only enforce if the loader profile lists relocations.
                if !allow.is_empty() && !allow.contains(&rt) {
                    report.errors.push(format!(
                        "relocation type {} (section '{}') not in loader allowlist",
                        rt,
                        sec.name().unwrap_or("<unnamed>")
                    ));
                }

                // Absolute-address policy scan: a relocation that targets an
                // SHN_ABS symbol (a hard absolute address baked by the
                // veneer generator) whose address is not in the target pack
                // allowlist is rejected. Local ET_REL symbols carry
                // section-relative st_value and are not absolute addresses.
                if let RelocationTarget::Symbol(idx) = reloc.target()
                    && let Ok(sym) = file.symbol_by_index(idx)
                    && matches!(sym.section(), SymbolSection::Absolute)
                {
                    let addr = sym.address();
                    if !self.allowed_addresses.contains(&addr) {
                        report.errors.push(format!(
                                    "relocation targets absolute symbol '{}' at 0x{addr:x} not in target pack allowlist",
                                    sym.name().unwrap_or("<unnamed>")
                                ));
                    }
                }
            }
        }

        // --- CAN-P1-011: direct absolute-address scan -----------------
        // A compiler may bake a firmware address into the module as a
        // MOVW/MOVT pair (code) or as a literal/data word (data) without
        // producing any SHN_ABS relocation. Scan both and require every
        // value inside the target's firmware address space to be in the
        // allowlist. When the target declares no firmware ranges the scan
        // is skipped (nothing to classify). Scanning .text as raw words is
        // intentionally NOT done: two consecutive 16-bit Thumb instructions
        // can alias a firmware-range word, so only the precise MOVW/MOVT
        // decode and non-executable data sections are classified.
        if let Some(ranges) = self.target.firmware_address_ranges.as_deref() {
            let mut hits: Vec<String> = Vec::new();
            for sec in file.sections() {
                let name = sec.name().unwrap_or("<unnamed>").to_string();
                let (is_exec, is_alloc) = match sec.flags() {
                    SectionFlags::Elf { sh_flags } => (sh_flags & 0x4 != 0, sh_flags & 0x2 != 0),
                    _ => (false, false),
                };
                if matches!(sec.kind(), SectionKind::Text) || is_exec {
                    scan_executable_section(&sec, &name, ranges, self.allowed_addresses, &mut hits);
                } else if is_alloc {
                    scan_data_section(&sec, &name, ranges, self.allowed_addresses, &mut hits);
                }
            }
            report.summary.absolute_address_hits = hits.len();
            for h in hits {
                report.errors.push(format!(
                    "embedded absolute address {h} not in target pack allowlist"
                ));
            }
        }

        // --- constructors / destructors ------------------------------
        for sec in file.sections() {
            match sec.name().ok() {
                Some(".init_array" | ".preinit_array") => {
                    report.summary.constructor_count += sec.size() as usize / 4;
                }
                Some(".fini_array") => {
                    report.summary.destructor_count += sec.size() as usize / 4;
                }
                _ => {}
            }
        }

        report.ok = report.errors.is_empty();
        report
    }
}

pub fn hex_sha256(data: &[u8]) -> String {
    let mut h = Sha256::new();
    h.update(data);
    format!("{:x}", h.finalize())
}

// ---- CAN-P1-011: Thumb-2 MOVW/MOVT + data-word absolute-address scan --

fn in_firmware_space(addr: u64, ranges: &[AddressRange]) -> bool {
    ranges
        .iter()
        .any(|r| addr >= r.base && addr < r.base.saturating_add(r.size))
}

/// True when a halfword starts a 32-bit Thumb-2 instruction (top five bits
/// are 11101, 11110 or 11111).
fn is_32bit_thumb(hw: u16) -> bool {
    matches!((hw >> 11) & 0x1F, 0x1D..=0x1F)
}

#[derive(Clone, Copy, PartialEq)]
enum MovKind {
    W,
    T,
}

/// Decodes a Thumb-2 MOVW/MOVT into (register, kind, imm16). Returns None
/// for anything else.
fn decode_mov(first: u16, second: u16) -> Option<(u8, MovKind, u16)> {
    let op = first & 0xFBF0;
    let kind = if op == 0xF240 {
        MovKind::W
    } else if op == 0xF2C0 {
        MovKind::T
    } else {
        return None;
    };
    if second & 0x8000 != 0 {
        return None; // second halfword must begin with 0
    }
    let i = (first >> 10) & 1;
    let imm4 = first & 0xF;
    let rd = (second >> 8) & 0xF;
    let imm3 = (second >> 12) & 0x7;
    let imm8 = second & 0xFF;
    let imm16 = (imm4 << 12) | (i << 11) | (imm3 << 8) | imm8;
    Some((rd as u8, kind, imm16))
}

fn check_absolute_addr(
    addr: u64,
    loc: &str,
    ranges: &[AddressRange],
    allowed: &[u64],
    hits: &mut Vec<String>,
) {
    if !in_firmware_space(addr, ranges) {
        return;
    }
    if !allowed.contains(&addr) {
        hits.push(format!("{loc} 0x{addr:x}"));
    }
}

/// Walks an executable section as Thumb instructions and reports MOVW/MOVT
/// absolute addresses (the compiler's canonical way to bake in a 32-bit
/// constant such as a veneer address). A MOVT is paired only with a MOVW of
/// the same register at the immediately preceding instruction, so an
/// unrelated constant can never be combined into a false firmware address.
fn scan_executable_section<'data, S: ObjectSection<'data>>(
    sec: &S,
    name: &str,
    ranges: &[AddressRange],
    allowed: &[u64],
    hits: &mut Vec<String>,
) {
    let Ok(bytes) = sec.data() else { return };
    // previous instruction's MOVW target, if the previous 4 bytes were one
    let mut prev_movw: Option<(u8, u16)> = None;
    let mut off = 0usize;
    while off + 2 <= bytes.len() {
        let hw = u16::from_le_bytes([bytes[off], bytes[off + 1]]);
        if is_32bit_thumb(hw) {
            if off + 4 > bytes.len() {
                break;
            }
            let second = u16::from_le_bytes([bytes[off + 2], bytes[off + 3]]);
            let prev = prev_movw.take();
            if let Some((rd, kind, imm)) = decode_mov(hw, second) {
                match kind {
                    MovKind::W => prev_movw = Some((rd, imm)),
                    MovKind::T => {
                        if let Some((prd, pimm)) = prev
                            && prd == rd
                        {
                            let addr = ((imm as u64) << 16) | pimm as u64;
                            check_absolute_addr(
                                addr,
                                &format!("{name}+0x{off:x}"),
                                ranges,
                                allowed,
                                hits,
                            );
                        }
                    }
                }
            }
            off += 4;
        } else {
            off += 2;
        }
    }
}

/// Reports 32-bit LE words inside the target firmware address space from an
/// allocatable, non-executable data section. Section-relative offsets (the
/// module's own layout, fixed by relocations) live well below the firmware
/// space, so they are naturally excluded.
fn scan_data_section<'data, S: ObjectSection<'data>>(
    sec: &S,
    name: &str,
    ranges: &[AddressRange],
    allowed: &[u64],
    hits: &mut Vec<String>,
) {
    let Ok(bytes) = sec.data() else { return };
    for (i, chunk) in bytes.chunks_exact(4).enumerate() {
        let word = u32::from_le_bytes([chunk[0], chunk[1], chunk[2], chunk[3]]) as u64;
        if word == 0 {
            continue;
        }
        check_absolute_addr(word, &format!("{name}+0x{:x}", i * 4), ranges, allowed, hits);
    }
}
