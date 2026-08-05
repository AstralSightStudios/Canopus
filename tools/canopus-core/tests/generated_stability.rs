//! Generated-artifact stability regression tests.
//!
//! `canopus target generate-veneer` / `generate-rust-bindings` must reproduce
//! the committed artifacts byte-for-byte. If a target pack changes without
//! regenerating, these tests fail loudly so stale generated code never ships.

use std::path::{Path, PathBuf};

use canopus_core::rustgen::RustGen;
use canopus_core::veneer::{VeneerGen, load_records};

fn repo_root() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .join("..")
        .join("..")
}

fn pack_dir() -> PathBuf {
    repo_root().join("targets/xiaomi-band-10-pro-3.101.030")
}

fn load_pack() -> canopus_core::model::TargetPack {
    let dir = pack_dir();
    canopus_core::registry::load_target_pack(&dir.join("target.toml")).unwrap()
}

fn load_symtypes() -> (
    Vec<canopus_core::model::Symbol>,
    Vec<canopus_core::model::TypeRecord>,
) {
    load_records(&pack_dir()).unwrap()
}

#[test]
fn rust_bindings_regenerate_identically() {
    let (symbols, types) = load_symtypes();
    let pack = load_pack();
    let r#gen = RustGen {
        pack: &pack,
        symbols: &symbols,
        types: &types,
    };
    let regenerated = r#gen.generate();
    let committed = std::fs::read_to_string(
        repo_root().join("sdk/rust/canopus-target-generated/src/generated.rs"),
    )
    .unwrap();
    assert_eq!(
        committed, regenerated,
        "generated Rust bindings are stale; run `canopus target generate-rust-bindings \
         xiaomi-band-10-pro-3.101.030` and copy into sdk/rust/canopus-target-generated/src/"
    );
}

#[test]
fn c_veneer_regenerates_identically() {
    let (symbols, types) = load_symtypes();
    let pack = load_pack();
    let r#gen = VeneerGen {
        pack: &pack,
        symbols: &symbols,
        types: &types,
    };
    let regenerated = r#gen.generate();
    let committed = std::fs::read_to_string(pack_dir().join("generated/canopus_veneer.h")).unwrap();
    assert_eq!(
        committed, regenerated,
        "generated veneer header is stale; run `canopus target generate-veneer \
         xiaomi-band-10-pro-3.101.030`"
    );
}

#[test]
fn rust_bindings_have_exact_recovered_layout() {
    let (symbols, types) = load_symtypes();
    let pack = load_pack();
    let r#gen = RustGen {
        pack: &pack,
        symbols: &symbols,
        types: &types,
    };
    let text = r#gen.generate();

    // stock file_operations is the full 0x30-byte target table.
    assert!(text.contains("pub ioctl: *mut core::ffi::c_void, // +0x14"));
    assert!(text.contains("pub _tail: [u8; 0x18], // 24"));

    // launcher_order_record: 128-byte name @0, u32 flags @132, total 140.
    assert!(text.contains("pub app_name: [u8; 128], // +0x0"));
    assert!(text.contains("pub _pad_80: [u8; 0x4], // 4"));
    assert!(text.contains("pub flags: u32, // +0x84"));
    assert!(text.contains("pub _tail: [u8; 0x4], // 4"));

    // ordered_app_entry: 16 bytes, name @0, enabled @8, hidden @9.
    assert!(text.contains("pub _pad_4: [u8; 0x4], // 4"));
    assert!(text.contains("pub enabled: u8, // +0x8"));
    assert!(text.contains("pub hidden: u8, // +0x9"));

    // forbidden symbols never produce a binding; they appear only as comments.
    assert!(text.contains("// bt_adapter_register_a2dp_callbacks: FORBIDDEN"));
    assert!(!text.contains("pub unsafe fn canopus_fw_bt_adapter_register_a2dp_callbacks"));
    // restricted symbols are audit comments only.
    assert!(text.contains("// app_launcher_add: restricted"));
    assert!(!text.contains("pub unsafe fn canopus_fw_app_launcher_add"));
}

#[test]
fn c_veneer_have_exact_recovered_layout() {
    let (symbols, types) = load_symtypes();
    let pack = load_pack();
    let r#gen = VeneerGen {
        pack: &pack,
        symbols: &symbols,
        types: &types,
    };
    let text = r#gen.generate();

    assert!(text.contains("uint8_t _tail[24];"));
    assert!(text.contains("uint8_t app_name[128]; /* +0x0 */"));
    assert!(text.contains("uint32_t flags; /* +0x84 */"));
    assert!(text.contains("* bt_adapter_register_a2dp_callbacks: FORBIDDEN"));
}

#[test]
fn identity_guard_uses_pack_version_build() {
    let (symbols, types) = load_symtypes();
    let pack = load_pack();
    let r#gen = RustGen {
        pack: &pack,
        symbols: &symbols,
        types: &types,
    };
    let text = r#gen.generate();
    assert!(text.contains("b\"3.101.030\""));
    assert!(text.contains("b\"CONBINE_LTALM078_T3.101.030_06011854\""));
    // Thumb callable addresses carry the +1 bit.
    assert!(text.contains("transmute(0x0C1EC8B5usize)"));
    assert!(text.contains("transmute(0x0C1A0D51usize)"));
}

// Helper assertions reused by the tests above (kept as a compile check that the
// pack path exists).
#[allow(dead_code)]
fn _assert_pack_exists(_p: &Path) {}
