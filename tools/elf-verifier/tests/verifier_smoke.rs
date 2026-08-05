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

fn sh(name: u32, ty: u32, flags: u32, off: u32, size: u32, link: u32, align: u32, entsize: u32) -> [u8; 40] {
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
    out.extend_from_slice(&sh(1, SHT_PROGBITS, SHF_ALLOC_EXEC, text_off, text_data.len() as u32, 0, 4, 0));
    out.extend_from_slice(&sh(7, SHT_STRTAB, 0, str_off, shstrtab.len() as u32, 0, 1, 0));
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
    out.extend_from_slice(&sh(1, SHT_PROGBITS, SHF_ALLOC_EXEC, text_off, text_data.len() as u32, 0, 4, 0));
    out.extend_from_slice(&sh(7, SHT_SYMTAB, 0, symtab_off, 32, 3, 4, 16));
    out.extend_from_slice(&sh(14, SHT_STRTAB, 0, strtab_off, 5, 0, 1, 0));
    out.extend_from_slice(&sh(21, SHT_STRTAB, 0, shstr_off, shstrtab.len() as u32, 0, 1, 0));
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
    assert!(!report.ok, "expected failure, got errors={:?} summary={:?}", report.errors, report.summary);
    assert!(report.errors.iter().any(|e| e.contains("undefined symbol 'foo'")),
        "errors were: {:?}", report.errors);
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
