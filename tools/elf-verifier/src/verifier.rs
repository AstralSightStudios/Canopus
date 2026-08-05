//! Generic ELF module verifier (CAN-BLD-003).
//!
//! Checks a compiled module ELF against the target loader profile and the
//! Canopus static constraints from architecture §13.4. Failures are errors;
//! warnings do not fail verification.

use canopus_core::model::TargetPack;
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
