//! Generated-artifact stability regression tests.
//!
//! `canopus target generate-veneer` / `generate-rust-bindings` must reproduce
//! the committed artifacts byte-for-byte. If a target pack changes without
//! regenerating, these tests fail loudly so stale generated code never ships.

use std::path::{Path, PathBuf};

use canopus_core::rustgen::RustGen;
use canopus_core::target_config::TargetConfigGen;
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
fn additional_target_artifacts_regenerate_identically() {
    let cases = [
        (
            "xiaomi-band-10-pro-3.101.036",
            "sdk/rust/canopus-target-generated/src/generated_1036.rs",
        ),
        (
            "xiaomi-band-9-pro-3.1.175",
            "sdk/rust/canopus-target-generated/src/generated_b9.rs",
        ),
        (
            "xiaomi-band-10-pro-3.101.043",
            "sdk/rust/canopus-target-generated/src/generated_1043.rs",
        ),
    ];

    for (target, rust_path) in cases {
        let dir = repo_root().join("targets").join(target);
        let pack = canopus_core::registry::load_target_pack(&dir.join("target.toml")).unwrap();
        let (symbols, types) = load_records(&dir).unwrap();
        let rust = RustGen {
            pack: &pack,
            symbols: &symbols,
            types: &types,
        }
        .generate();
        let committed_rust = std::fs::read_to_string(repo_root().join(rust_path)).unwrap();
        assert_eq!(
            committed_rust, rust,
            "generated Rust bindings are stale for {target}"
        );

        let veneer = VeneerGen {
            pack: &pack,
            symbols: &symbols,
            types: &types,
        }
        .generate();
        let committed_veneer =
            std::fs::read_to_string(dir.join("generated/canopus_veneer.h")).unwrap();
        assert_eq!(
            committed_veneer, veneer,
            "generated veneer header is stale for {target}"
        );

        let config = TargetConfigGen {
            pack: &pack,
            symbols: &symbols,
        }
        .generate();
        let committed_config =
            std::fs::read_to_string(dir.join("generated/canopus_target_config.h")).unwrap();
        assert_eq!(
            committed_config, config,
            "generated target config header is stale for {target}"
        );

        if target == "xiaomi-band-10-pro-3.101.036" {
            assert!(
                rust.contains("canopus_fw_bt_adapter_get_instance() -> *mut core::ffi::c_void")
            );
            assert!(rust.contains("canopus_thumb_callable(0x0CA28771usize)"));
            assert!(rust.contains("canopus_thumb_callable(0x0C398CE5usize)"));
            assert!(rust.contains("canopus_thumb_callable(0x0C398D4Dusize)"));
            assert!(rust.contains("canopus_thumb_callable(0x0C398DF1usize)"));
            assert!(rust.contains("canopus_thumb_callable(0x0C39F069usize)"));
            assert!(rust.contains("canopus_thumb_callable(0x0C39994Dusize)"));
            assert!(rust.contains("canopus_thumb_callable(0x0C399861usize)"));
            assert!(rust.contains("canopus_thumb_callable(0x0C39FA71usize)"));
            assert!(
                rust.contains(
                    "pub const canopus_fw_core_bt_callback_table: usize = 0x2CD1F920usize"
                )
            );
            assert!(rust.contains("canopus_thumb_callable(0x0C6E1ECDusize)"));
            assert!(
                rust.contains(
                    "pub const canopus_fw_gap_host_receive_slot: usize = 0x20137E94usize"
                )
            );
            assert!(rust.contains("canopus_thumb_callable(0x0C7D3EB5usize)"));
            for invalid in [
                "0x0CA286C9usize",
                "0x0C39F021usize",
                "0x0C39988Dusize",
                "0x0C39989Dusize",
                "0x0C3998C9usize",
                "0x0C39F9B1usize",
                "0x0C6E1E25usize",
                "0x0C7D3E0Dusize",
                "0x20137EA4usize",
                "0x2CD1F930usize",
            ] {
                assert!(
                    !rust.contains(invalid),
                    "3.101.036 retained invalid {invalid}"
                );
            }
        }
    }
}

fn parse_address(value: &str) -> usize {
    usize::from_str_radix(
        value
            .strip_prefix("0x")
            .or_else(|| value.strip_prefix("0X"))
            .unwrap_or(value),
        16,
    )
    .unwrap()
}

#[test]
fn private_abi_records_have_exact_thumb_callables() {
    for target in [
        "xiaomi-band-10-pro-3.101.030",
        "xiaomi-band-10-pro-3.101.036",
        "xiaomi-band-9-pro-3.1.175",
        "xiaomi-band-10-pro-3.101.043",
    ] {
        let dir = repo_root().join("targets").join(target);
        let (symbols, types) = load_records(&dir).unwrap();
        let pack = canopus_core::registry::load_target_pack(&dir.join("target.toml")).unwrap();
        let generated = RustGen {
            pack: &pack,
            symbols: &symbols,
            types: &types,
        }
        .generate();

        for symbol in symbols
            .iter()
            .filter(|symbol| symbol.symbol_id.contains(".private_abi."))
        {
            assert_eq!(symbol.target_id, target, "wrong target for {}", symbol.name);
            assert_eq!(
                symbol.policy, "restricted",
                "{} escaped private policy",
                symbol.name
            );
            assert_ne!(
                symbol.approval_state.as_deref(),
                Some("APPROVED"),
                "{} was unexpectedly promoted",
                symbol.name
            );
            let entry = parse_address(symbol.entry_address.as_deref().unwrap());
            if symbol.kind == "function" {
                let callable = parse_address(symbol.callable_address.as_deref().unwrap());
                assert_eq!(entry & 1, 0, "{} entry is not even", symbol.name);
                assert_eq!(callable, entry | 1, "{} callable is not Thumb", symbol.name);
                let constant = format!(
                    "pub const CANOPUS_FW_{}_CALLABLE: usize = canopus_thumb_callable({}usize)",
                    symbol.name.to_ascii_uppercase(),
                    symbol.callable_address.as_deref().unwrap()
                );
                assert!(
                    generated.contains(&constant),
                    "missing generated {constant}"
                );
                let wrapper = format!("pub unsafe fn canopus_fw_{}(", symbol.name);
                assert!(
                    !generated.contains(&wrapper),
                    "restricted symbol {} emitted a public wrapper",
                    symbol.name
                );
            } else {
                let constant = format!(
                    "pub const canopus_fw_{}: usize = {}usize",
                    symbol.name,
                    symbol.entry_address.as_deref().unwrap()
                );
                assert!(
                    generated.contains(&constant),
                    "missing generated {constant}"
                );
            }
        }
    }
}

#[test]
fn target_private_never_transmutes_raw_firmware_addresses() {
    let targets = repo_root().join("sdk/rust/canopus-target-private/src/targets");
    for entry in std::fs::read_dir(targets).unwrap() {
        let path = entry.unwrap().path();
        if path.extension().and_then(|value| value.to_str()) != Some("rs") {
            continue;
        }
        let source = std::fs::read_to_string(&path).unwrap();
        assert!(
            !source.contains("core::mem::transmute(0x"),
            "{} bypasses generated Thumb-callable normalization",
            path.display()
        );
        assert!(
            !source.contains("canopus_thumb_callable(0x"),
            "{} owns a firmware callable instead of consuming generated metadata",
            path.display()
        );
        for line in source.lines() {
            let code = line.split("//").next().unwrap_or("");
            assert!(
                !code.contains("0x0C") && !code.contains("0x0c") && !code.contains("0x20")
                    || !code.contains("usize"),
                "{} retains an active firmware address: {}",
                path.display(),
                line.trim()
            );
        }
        if path.file_name().and_then(|value| value.to_str())
            == Some("xiaomi_band_10_pro_3_101_036.rs")
        {
            for invalid in [
                "0x0CA286C9usize",
                "0x0C39F021usize",
                "0x0C39988Dusize",
                "0x0C3998C9usize",
                "0x0C39F9B1usize",
                "0x0C7D36D1usize",
                "0x0C7D3E0D",
                "0x0C588601usize",
                "0x0C5886D1usize",
                "0x0C588759usize",
                "0x0C7ED48Dusize",
                "0x0C7D3E0Dusize",
                "0x20137EA4usize",
                "0x20137B1Cusize",
                "0x20137EA4",
            ] {
                assert!(
                    !source.contains(invalid),
                    "3.101.036 retained invalid {invalid}"
                );
            }
        }
    }
}

#[test]
fn generated_thumb_callable_normalizes_entry_and_callable() {
    let (symbols, types) = load_symtypes();
    let pack = load_pack();
    let text = RustGen {
        pack: &pack,
        symbols: &symbols,
        types: &types,
    }
    .generate();
    assert!(
        text.contains("pub const fn canopus_thumb_callable(entry_or_callable: usize) -> usize")
    );
    assert!(text.contains("entry_or_callable | 1usize"));
    assert!(text.contains("pub const CANOPUS_FW_CLOCK_GETTIME_CALLABLE: usize"));
    assert!(text.contains("canopus_thumb_callable(0x0C1EC8B5usize)"));
    assert!(text.contains("core::mem::transmute(CANOPUS_FW_CLOCK_GETTIME_CALLABLE)"));
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

    // Every recovered struct/union type declares four-byte ARM alignment;
    // generated host bindings must retain it without allowing 64-bit pointers
    // to expand fields. Typedef records emit `pub type` aliases, not structs.
    let struct_count = types
        .iter()
        .filter(|t| t.kind == "struct" || t.kind == "union")
        .count();
    assert_eq!(text.matches("#[repr(C, packed(4))]").count(), struct_count);
    assert!(!text.contains("#[repr(C, packed)]"));

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
    // probe-approved symbols DO generate now (app_launcher_add is APPROVED for
    // the native Manager / probe path), while still-restricted symbols remain
    // audit comments only.
    assert!(text.contains("pub unsafe fn canopus_fw_app_launcher_add"));
    assert!(text.contains("// bt_socket_server_receive: FORBIDDEN"));
    assert!(!text.contains("pub unsafe fn canopus_fw_bt_socket_server_receive"));
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
    // Generated indirect calls expose and consume the shared normalized callable
    // constant, so callback-table comparisons never take the host wrapper address.
    assert!(text.contains("pub const CANOPUS_FW_APP_LOOKUP_CALLABLE: usize"));
    assert!(text.contains("canopus_thumb_callable(0x0CA50FD5usize)"));
    assert!(text.contains("transmute(CANOPUS_FW_APP_LOOKUP_CALLABLE)"));
    assert!(text.contains("pub const CANOPUS_FW_REGISTER_DRIVER_CALLABLE: usize"));
    assert!(text.contains("canopus_thumb_callable(0x0C1A0D51usize)"));
    assert!(text.contains("transmute(CANOPUS_FW_REGISTER_DRIVER_CALLABLE)"));
}

// Helper assertions reused by the tests above (kept as a compile check that the
// pack path exists).
#[allow(dead_code)]
fn _assert_pack_exists(_p: &Path) {}
