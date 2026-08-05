//! Smoke tests for the generic ELF verifier against minimal hand-built
//! ELF32 ARM ET_REL objects.

// Test-only builders take one argument per ELF field on purpose.
#![allow(clippy::too_many_arguments)]

use canopus_core::model::TargetPack;
use canopus_elf::Verifier;
use std::sync::OnceLock;

const EM_ARM: u16 = 40;
const ET_REL: u16 = 1;
const SHT_PROGBITS: u32 = 1;
const SHT_SYMTAB: u32 = 2;
const SHT_STRTAB: u32 = 3;
const SHF_ALLOC_EXEC: u32 = 0x6; // SHF_ALLOC | SHF_EXECINSTR
const SHN_UNDEF: u16 = 0;
const STT_NOTYPE: u8 = 0;
const STB_GLOBAL: u8 = 1;

fn pack() -> &'static TargetPack {
    static P: OnceLock<TargetPack> = OnceLock::new();
    P.get_or_init(|| {
        let json = std::fs::read_to_string(concat!(
            env!("CARGO_MANIFEST_DIR"),
            "/../../tests/fixtures/targets/valid/xiaomi-band-10-pro.json"
        ))
        .unwrap();
        serde_json::from_str(&json).unwrap()
    })
}

/// Minimal ELF32 header.
fn elf_header(shoff: u32, shnum: u16, shstrndx: u16) -> [u8; 52] {
    let mut h = [0u8; 52];
    h[0..4].copy_from_slice(&[0x7f, b'E', b'L', b'F']);
    h[4] = 1; // ELFCLASS32
    h[5] = 1; // LSB
    h[6] = 1; // EV_CURRENT
    h[16..18].copy_from_slice(&ET_REL.to_le_bytes());
    h[18..20].copy_from_slice(&EM_ARM.to_le_bytes());
    h[20..24].copy_from_slice(&1u32.to_le_bytes());
    h[32..36].copy_from_slice(&shoff.to_le_bytes());
    h[36..40].copy_from_slice(&0x05000000u32.to_le_bytes()); // EABI5
    h[40..42].copy_from_slice(&52u16.to_le_bytes()); // e_ehsize
    h[42..44].copy_from_slice(&0u16.to_le_bytes()); // e_phentsize
    h[44..46].copy_from_slice(&0u16.to_le_bytes()); // e_phnum
    h[46..48].copy_from_slice(&40u16.to_le_bytes()); // e_shentsize
    h[48..50].copy_from_slice(&shnum.to_le_bytes()); // e_shnum
    h[50..52].copy_from_slice(&shstrndx.to_le_bytes()); // e_shstrndx
    h
}

fn sh(
    name: u32,
    ty: u32,
    flags: u32,
    off: u32,
    size: u32,
    link: u32,
    align: u32,
    entsize: u32,
) -> [u8; 40] {
    let mut s = [0u8; 40];
    s[0..4].copy_from_slice(&name.to_le_bytes());
    s[4..8].copy_from_slice(&ty.to_le_bytes());
    s[8..12].copy_from_slice(&flags.to_le_bytes());
    s[16..20].copy_from_slice(&off.to_le_bytes());
    s[20..24].copy_from_slice(&size.to_le_bytes());
    s[24..28].copy_from_slice(&link.to_le_bytes());
    s[32..36].copy_from_slice(&entsize.to_le_bytes());
    s[36..40].copy_from_slice(&align.to_le_bytes());
    s
}

/// A valid ET_REL with a single .text section and nothing else.
fn valid_elf() -> Vec<u8> {
    let text_data = [0x00, 0xbf, 0x00, 0xbf]; // Thumb NOP x2
    let shstrtab = b"\0.text\0.shstrtab\0".to_vec();
    let text_off = 52u32;
    let str_off = text_off + text_data.len() as u32;
    let shoff = str_off + shstrtab.len() as u32;

    let mut out = elf_header(shoff, 3, 2).to_vec();
    out.extend_from_slice(&text_data);
    out.extend_from_slice(&shstrtab);
    out.extend_from_slice(&sh(0, 0, 0, 0, 0, 0, 0, 0));
    out.extend_from_slice(&sh(
        1,
        SHT_PROGBITS,
        SHF_ALLOC_EXEC,
        text_off,
        text_data.len() as u32,
        0,
        4,
        0,
    ));
    out.extend_from_slice(&sh(
        7,
        SHT_STRTAB,
        0,
        str_off,
        shstrtab.len() as u32,
        0,
        1,
        0,
    ));
    out
}

/// The same layout plus one undefined global symbol `foo`.
fn elf_with_undefined_symbol() -> Vec<u8> {
    let text_data = [0x00, 0xbf];
    let shstrtab = b"\0.text\0.symtab\0.strtab\0.shstrtab\0".to_vec();
    let text_off = 52u32;
    let symtab_off = text_off + text_data.len() as u32;
    let strtab_off = symtab_off + 32u32; // 2 symtab entries
    let shstr_off = strtab_off + 5u32; // strtab = "\0foo\0" (5 bytes)
    let shoff = shstr_off + shstrtab.len() as u32;

    // 5 sections: [0]=null, [1]=.text, [2]=.symtab, [3]=.strtab, [4]=.shstrtab
    let mut out = elf_header(shoff, 5, 4).to_vec();
    out.extend_from_slice(&text_data);
    // symtab: [0]=null, [1]=foo undefined global
    out.extend_from_slice(&[0u8; 16]);
    let mut data = [0u8; 16];
    data[0..4].copy_from_slice(&1u32.to_le_bytes()); // name -> "\0foo\0" offset 1
    data[4] = (STB_GLOBAL << 4) | STT_NOTYPE;
    data[14..16].copy_from_slice(&SHN_UNDEF.to_le_bytes());
    out.extend_from_slice(&data);
    // strtab
    out.extend_from_slice(b"\0foo\0");
    out.extend_from_slice(&shstrtab);
    out.extend_from_slice(&sh(0, 0, 0, 0, 0, 0, 0, 0));
    out.extend_from_slice(&sh(
        1,
        SHT_PROGBITS,
        SHF_ALLOC_EXEC,
        text_off,
        text_data.len() as u32,
        0,
        4,
        0,
    ));
    out.extend_from_slice(&sh(7, SHT_SYMTAB, 0, symtab_off, 32, 3, 4, 16));
    out.extend_from_slice(&sh(14, SHT_STRTAB, 0, strtab_off, 5, 0, 1, 0));
    out.extend_from_slice(&sh(
        21,
        SHT_STRTAB,
        0,
        shstr_off,
        shstrtab.len() as u32,
        0,
        1,
        0,
    ));
    out
}

#[test]
fn valid_minimal_elf_passes() {
    let elf = valid_elf();
    let v = Verifier {
        target: pack(),
        allowed_addresses: &[],
    };
    let report = v.verify(&elf);
    assert!(report.ok, "expected pass: {:?}", report.errors);
    assert_eq!(report.summary.undefined_symbols, 0);
    assert_eq!(report.summary.relocation_count, 0);
}

#[test]
fn undefined_symbol_fails_zero_import() {
    let elf = elf_with_undefined_symbol();
    let v = Verifier {
        target: pack(),
        allowed_addresses: &[],
    };
    let report = v.verify(&elf);
    assert!(
        !report.ok,
        "expected failure, got errors={:?} summary={:?}",
        report.errors, report.summary
    );
    assert!(
        report
            .errors
            .iter()
            .any(|e| e.contains("undefined symbol 'foo'")),
        "errors were: {:?}",
        report.errors
    );
}

#[test]
fn garbage_fails() {
    let v = Verifier {
        target: pack(),
        allowed_addresses: &[],
    };
    let report = v.verify(b"not an elf at all");
    assert!(!report.ok);
}

// ---- CAN-P1-011: direct absolute-address scan ------------------------

const SHF_ALLOC: u32 = 0x2;

struct TestSection {
    name: &'static str,
    flags: u32,
    data: Vec<u8>,
}

/// Builds an ET_REL with the given PROGBITS sections (plus shstrtab).
fn build_elf(sections: Vec<TestSection>) -> Vec<u8> {
    let mut shstrtab = vec![0u8];
    let mut name_offsets = Vec::new();
    for s in &sections {
        name_offsets.push(shstrtab.len() as u32);
        shstrtab.extend_from_slice(s.name.as_bytes());
        shstrtab.push(0);
    }
    let shstrtab_name_off = shstrtab.len() as u32;
    shstrtab.extend_from_slice(b".shstrtab\0");

    let mut off = 52u32;
    let mut sec_offsets = Vec::new();
    for s in &sections {
        sec_offsets.push(off);
        off += s.data.len() as u32;
    }
    let shstrtab_off = off;
    off += shstrtab.len() as u32;
    let shoff = off;
    let shnum = (sections.len() + 2) as u16;
    let shstrndx = shnum - 1;

    let mut out = elf_header(shoff, shnum, shstrndx).to_vec();
    for (s, so) in sections.iter().zip(&sec_offsets) {
        let _ = so;
        out.extend_from_slice(&s.data);
    }
    out.extend_from_slice(&shstrtab);
    out.extend_from_slice(&sh(0, 0, 0, 0, 0, 0, 0, 0));
    for (i, (s, so)) in sections.iter().zip(&sec_offsets).enumerate() {
        out.extend_from_slice(&sh(
            name_offsets[i],
            SHT_PROGBITS,
            s.flags,
            *so,
            s.data.len() as u32,
            0,
            4,
            0,
        ));
    }
    out.extend_from_slice(&sh(
        shstrtab_name_off,
        SHT_STRTAB,
        0,
        shstrtab_off,
        shstrtab.len() as u32,
        0,
        1,
        0,
    ));
    out
}

/// Encodes a Thumb-2 MOVW (imm16 -> r3) as 4 bytes.
fn movw_enc(imm16: u16) -> [u8; 4] {
    let imm4 = (imm16 >> 12) & 0xF;
    let i = (imm16 >> 11) & 1;
    let imm3 = (imm16 >> 8) & 0x7;
    let imm8 = imm16 & 0xFF;
    let rd = 3u16;
    let first = 0xF240u16 | (i << 10) | imm4;
    let second = (imm3 << 12) | (rd << 8) | imm8;
    [
        (first & 0xFF) as u8,
        ((first >> 8) & 0xFF) as u8,
        (second & 0xFF) as u8,
        ((second >> 8) & 0xFF) as u8,
    ]
}

/// Encodes a Thumb-2 MOVT (imm16 -> r3) as 4 bytes.
fn movt_enc(imm16: u16) -> [u8; 4] {
    let imm4 = (imm16 >> 12) & 0xF;
    let i = (imm16 >> 11) & 1;
    let imm3 = (imm16 >> 8) & 0x7;
    let imm8 = imm16 & 0xFF;
    let rd = 3u16;
    let first = 0xF2C0u16 | (i << 10) | imm4;
    let second = (imm3 << 12) | (rd << 8) | imm8;
    [
        (first & 0xFF) as u8,
        ((first >> 8) & 0xFF) as u8,
        (second & 0xFF) as u8,
        ((second >> 8) & 0xFF) as u8,
    ]
}

fn elf_with_movw_movt(addr: u64) -> Vec<u8> {
    let mut text = movw_enc((addr & 0xFFFF) as u16).to_vec();
    text.extend_from_slice(&movt_enc(((addr >> 16) & 0xFFFF) as u16));
    build_elf(vec![TestSection {
        name: ".text",
        flags: SHF_ALLOC_EXEC,
        data: text,
    }])
}

fn elf_with_data_word(addr: u64) -> Vec<u8> {
    build_elf(vec![TestSection {
        name: ".rodata",
        flags: SHF_ALLOC,
        data: (addr as u32).to_le_bytes().to_vec(),
    }])
}

#[test]
fn movw_movt_allowed_address_passes() {
    let addr: u64 = 0x0C1A0D51;
    let elf = elf_with_movw_movt(addr);
    let v = Verifier {
        target: pack(),
        allowed_addresses: &[addr],
    };
    let report = v.verify(&elf);
    assert!(report.ok, "expected pass: {:?}", report.errors);
    assert_eq!(report.summary.absolute_address_hits, 0);
}

#[test]
fn movw_movt_unknown_address_fails() {
    let addr: u64 = 0x0C1A0D51;
    let elf = elf_with_movw_movt(addr);
    let v = Verifier {
        target: pack(),
        allowed_addresses: &[],
    };
    let report = v.verify(&elf);
    assert!(!report.ok, "expected failure");
    assert!(
        report
            .errors
            .iter()
            .any(|e| e.contains("0xc1a0d51")),
        "errors were: {:?}",
        report.errors
    );
    assert!(report.summary.absolute_address_hits >= 1);
}

#[test]
fn data_word_absolute_address_checked() {
    let addr: u64 = 0x0C1A0D51;
    let elf = elf_with_data_word(addr);
    let ok = Verifier {
        target: pack(),
        allowed_addresses: &[addr],
    };
    assert!(ok.verify(&elf).ok, "allowed word must pass");
    let bad = Verifier {
        target: pack(),
        allowed_addresses: &[],
    };
    assert!(!bad.verify(&elf).ok, "unknown firmware-range word must fail");
}

#[test]
fn out_of_range_constant_not_flagged() {
    // a constant well outside the firmware space is a normal value, not an
    // absolute address
    let elf = elf_with_data_word(0x0000_4000);
    let v = Verifier {
        target: pack(),
        allowed_addresses: &[],
    };
    assert!(v.verify(&elf).ok, "non-firmware constant must not be flagged");
}
