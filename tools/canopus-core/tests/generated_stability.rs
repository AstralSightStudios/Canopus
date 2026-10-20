//! Generated-artifact stability regression tests.
//!
//! `canopus target generate` must reproduce
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
    repo_root().join("targets/xiaomi-band-10-pro-3.101.036")
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
        repo_root().join("sdk/rust/canopus-target-generated/src/generated_xiaomi_band_10_pro_3_101_036.rs"),
    )
    .unwrap();
    assert_eq!(
        committed, regenerated,
        "generated Rust bindings are stale; run `canopus target generate --all`"
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
        "generated veneer header is stale; run `canopus target generate --all`"
    );
}

#[test]
fn additional_target_artifacts_regenerate_identically() {
    let cases = [
        (
            "xiaomi-band-10-pro-3.101.036",
            "sdk/rust/canopus-target-generated/src/generated_xiaomi_band_10_pro_3_101_036.rs",
        ),
        (
            "xiaomi-band-10-pro-3.101.043",
            "sdk/rust/canopus-target-generated/src/generated_xiaomi_band_10_pro_3_101_043.rs",
        ),
        (
            "xiaomi-band-11-4.100.139",
            "sdk/rust/canopus-target-generated/src/generated_xiaomi_band_11_4_100_139.rs",
        ),
        (
            "xiaomi-band-11-4.100.155",
            "sdk/rust/canopus-target-generated/src/generated_xiaomi_band_11_4_100_155.rs",
        ),
        (
            "xiaomi-band-11-4.100.179",
            "sdk/rust/canopus-target-generated/src/generated_xiaomi_band_11_4_100_179.rs",
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
        if target == "xiaomi-band-11-4.100.139" {
            assert!(config.contains("#define CANOPUS_SUP_PLATFORM_COMPLETE 0"));
            assert!(
                !config.contains("#define FW_"),
                "incomplete target leaked firmware addresses"
            );
            assert!(!config.contains("#define CANOPUS_SUP_REGISTER_DRIVER("));
            assert!(!config.contains("#define CANOPUS_SUP_NUTTX_"));
        }

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

#[test]
fn stock_modlib_supervisor_platform_remains_complete() {
    let dir = repo_root().join("targets/xiaomi-band-10-pro-3.101.036");
    let pack = canopus_core::registry::load_target_pack(&dir.join("target.toml")).unwrap();
    let (symbols, _) = load_records(&dir).unwrap();
    let config = TargetConfigGen {
        pack: &pack,
        symbols: &symbols,
    }
    .generate();

    assert!(config.contains("#define CANOPUS_SUP_PLATFORM_COMPLETE 1"));
}

/// A port never inherits trust: .179 was matched against the reviewed .155 pack, so
/// every record must start over as restricted, PENDING, not probed and without a
/// promotion record, whatever the .155 source had earned.
#[test]
fn band11_179_carries_no_approval_from_155() {
    let dir = repo_root().join("targets/xiaomi-band-11-4.100.179");
    let pack = canopus_core::registry::load_target_pack(&dir.join("target.toml")).unwrap();
    let (symbols, _) = load_records(&dir).unwrap();
    assert!(symbols.len() > 250, "the port lost records");
    for symbol in &symbols {
        assert!(!symbol.approved_for_codegen(), "{} is approved", symbol.name);
        assert_ne!(symbol.status, "DEVICE_PROVEN", "{}", symbol.name);
        assert_ne!(symbol.policy, "managed", "{} is managed", symbol.name);
        assert!(symbol.promotion.is_none(), "{} carries a promotion", symbol.name);
        assert_ne!(symbol.proof.device.as_deref(), Some("proven"), "{}", symbol.name);
        assert_eq!(symbol.target_id, pack.target_id);
        assert_eq!(symbol.provenance.firmware_sha256, pack.firmware_sha256, "{}", symbol.name);
        if symbol.status == "CANDIDATE" {
            assert!(symbol.callable_address.is_none(), "{} is a callable candidate", symbol.name);
        }
    }
}

#[test]
fn band11_errno_is_the_only_public_approved_callable() {
    for target in [
        "xiaomi-band-11-4.100.139",
        "xiaomi-band-11-4.100.155",
    ] {
        let dir = repo_root().join("targets").join(target);
        let (symbols, _) = load_records(&dir).unwrap();
        let approved: Vec<_> = symbols
            .iter()
            .filter(|s| {
                s.kind == "function"
                    && s.approved_for_codegen()
                    && matches!(s.policy.as_str(), "managed" | "native-full-trust")
            })
            .collect();
        assert_eq!(
            approved.len(),
            1,
            "unexpected approval expansion for {target}"
        );
        let errno = approved[0];
        assert_eq!(errno.name, "errno_location");
        assert_eq!(errno.policy, "managed");
        assert_eq!(errno.prototype.as_deref(), Some("int *(void)"));
        assert_eq!(
            parse_address(errno.callable_address.as_deref().unwrap()),
            0x0c349539
        );
        let header = std::fs::read_to_string(dir.join("generated/canopus_veneer.h")).unwrap();
        assert!(header.contains("typedef int * (*canopus_fw_errno_location_fn)(void);"));
        assert!(header.contains("static inline int * canopus_fw_errno_location(void)"));
        assert!(!header.contains("errno_location: skipped"));
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
fn band10_pro_043_resource_thunks_remain_exact_and_private() {
    let target = "xiaomi-band-10-pro-3.101.043";
    let evidence_id = "EVID-RESOURCE-XIP-THUNKS-1043-001";
    let dir = repo_root().join("targets").join(target);
    let pack = canopus_core::registry::load_target_pack(&dir.join("target.toml")).unwrap();
    let (symbols, types) = load_records(&dir).unwrap();
    let header = VeneerGen {
        pack: &pack,
        symbols: &symbols,
        types: &types,
    }
    .generate();
    let rust = RustGen {
        pack: &pack,
        symbols: &symbols,
        types: &types,
    }
    .generate();
    let evidence: serde_json::Value = serde_json::from_str(
        &std::fs::read_to_string(dir.join(format!("evidence/{evidence_id}.json"))).unwrap(),
    )
    .unwrap();
    canopus_core::schema::validate(canopus_core::schema::SchemaKind::Evidence, &evidence).unwrap();
    assert_eq!(evidence["candidate_symbols"].as_array().unwrap().len(), 2);
    for (name, entry) in [
        ("lv_image_create_xip_thunk", 0x0c588d58),
        ("lv_obj_set_style_bg_opa_xip_thunk", 0x0c5883f8),
    ] {
        let id = format!("{target}.private_abi.{name}");
        let symbol = symbols.iter().find(|s| s.symbol_id == id).unwrap();
        assert_eq!(symbol.policy, "restricted");
        assert_eq!(symbol.status, "STATIC_RECOVERED");
        assert_eq!(symbol.approval_state.as_deref(), Some("PENDING"));
        assert_eq!(symbol.proof.device.as_deref(), Some("not_probed"));
        assert_eq!(symbol.provenance.firmware_sha256, pack.firmware_sha256);
        assert_eq!(
            parse_address(symbol.entry_address.as_deref().unwrap()),
            entry
        );
        assert_eq!(
            parse_address(symbol.callable_address.as_deref().unwrap()),
            entry | 1
        );
        assert!(
            symbol
                .proof
                .evidence_ids
                .as_ref()
                .unwrap()
                .contains(&evidence_id.into())
        );
        assert!(
            evidence["candidate_symbols"]
                .as_array()
                .unwrap()
                .contains(&id.into())
        );
        // Identity metadata must not add public or target-private raw wrappers.
        assert!(symbol.prototype.is_none());
        assert!(!header.contains(&format!("canopus_fw_{name}(")));
        assert!(!rust.contains(&format!("pub unsafe fn canopus_fw_{name}(")));
        assert!(rust.contains(&format!("CANOPUS_FW_{}_CALLABLE", name.to_uppercase())));
    }
}

const APPROVED_139_PRIVATE: [&str; 30] = [
    "app_watchface_take_snapshot",
    "hal_cache_invalidate_rom_thunk",
    "hal_cache_sync",
    "lv_display_remove_event_cb_with_user_data",
    "lv_event_add",
    "vg_lite_finish",
    "lv_obj_update_layout",
    "background_object_event_add",
    "lv_obj_remove_event_cb",
    "lv_draw_rect",
    "lv_draw_rect_dsc_init",
    "lv_obj_area_is_visible",
    "calendar_draw_buf_invalidate",
    "calendar_font_create",
    "calendar_font_destroy",
    "calendar_font_resolve",
    "calendar_localtime",
    "calendar_object_delete",
    "calendar_premultiply",
    "calendar_realtime",
    "calendar_root_style",
    "calendar_snapshot",
    "calendar_snapshot_free",
    "calendar_text_align",
    "calendar_text_color",
    "calendar_text_font",
    "calendar_weekday",
    "image_snapshot_release",
    "lv_draw_buf_create_ex",
    "lv_obj_set_local_style_prop",
];

/// Firmware entry points the maintainer verified on a real device (the UI host:
/// Manager and module pages, 2026-10-19). Authorized by the maintainer; each target
/// has its own evidence record. They stay `restricted` (target-private).
const DEVICE_VERIFIED_UI: [(&str, &str, &[&str]); 2] = [
    (
        "xiaomi-band-10-pro-3.101.043",
        "EVID-UI-1043-DEVICE-001",
        &[
            "lv_obj_align_to",
            "lv_bar_create",
            "lv_bar_set_range",
            "lv_bar_set_value",
            "lvx_page_content_create",
            "lvx_content_pad_bottom",
            "lv_obj_add_event_cb",
            "lv_event_get_code",
            "lv_image_create",
            "lv_image_set_src",
            "lvx_label_create",
            "lvx_label_set_text",
            "lvx_list_row_create",
            "lvx_list_row_trailing",
            "lvx_list_item_update",
            "lvx_msgbox_create",
            "lvx_msgbox_set_content",
            "lvx_object_add_flag",
            "lvx_object_align",
            "lv_obj_clear_flag",
            "lvx_object_delete",
            "lvx_object_set_size",
            "lvx_page_title_create",
            "lvx_style_apply",
            "style_misans_medium_24",
            "style_misans_medium_28",
            "page_goto",
            "page_finish",
        ],
    ),
    (
        "xiaomi-band-11-4.100.155",
        "EVID-UI-4155-DEVICE-001",
        &[
            "page_goto",
            "page_finish",
            "lvx_list_item_add_content",
            "lvx_list_item_create",
            "lvx_list_item_update",
            "lvx_msgbox_create",
            "lvx_msgbox_set_content",
            "lvx_style_apply",
            "lv_obj_align_to",
            "lv_bar_create",
            "lv_bar_set_range",
            "lv_bar_set_value",
            "lv_obj_add_event_cb",
            "lv_image_create",
            "lv_image_set_src",
            "lvx_label_create",
            "lvx_label_set_long_mode",
            "lvx_label_set_text",
            "lvx_object_add_flag",
            "lv_obj_clear_flag",
            "lvx_object_create",
            "lvx_object_delete",
            "lv_obj_set_style_pad_bottom",
            "lv_obj_set_style_pad_top",
            "lv_obj_remove_style_all",
            "lvx_object_set_size",
            "lv_obj_set_width",
            "lvx_page_title_create",
            "style_misans_medium_24",
            "style_misans_medium_28",
        ],
    ),
];

fn device_verified(target: &str, name: &str) -> bool {
    DEVICE_VERIFIED_UI
        .iter()
        .any(|(t, _, names)| *t == target && names.contains(&name))
}

#[test]
fn device_verified_ui_symbols_are_exactly_the_pinned_list() {
    for (target, evidence_id, names) in DEVICE_VERIFIED_UI {
        let dir = repo_root().join("targets").join(target);
        let (symbols, types) = load_records(&dir).unwrap();
        let pack = canopus_core::registry::load_target_pack(&dir.join("target.toml")).unwrap();
        let rust = RustGen {
            pack: &pack,
            symbols: &symbols,
            types: &types,
        }
        .generate();

        let proven: std::collections::BTreeSet<&str> = symbols
            .iter()
            .filter(|s| s.status == "DEVICE_PROVEN")
            .map(|s| s.name.as_str())
            .collect();
        let pinned: std::collections::BTreeSet<&str> = names.iter().copied().collect();
        assert_eq!(proven, pinned, "{target}: DEVICE_PROVEN differs from the pinned list");

        for symbol in symbols.iter().filter(|s| s.status == "DEVICE_PROVEN") {
            assert_eq!(symbol.proof.device.as_deref(), Some("proven"), "{}", symbol.name);
            assert_eq!(symbol.approval_state.as_deref(), Some("APPROVED"), "{}", symbol.name);
            assert!(
                symbol.proof.evidence_ids.as_ref().unwrap().contains(&evidence_id.to_string()),
                "{} lacks {evidence_id}",
                symbol.name
            );
            let promotion = symbol.promotion.as_ref().expect("promotion record");
            assert_eq!(promotion.firmware_sha256, pack.firmware_sha256, "{}", symbol.name);
            assert_eq!(symbol.provenance.firmware_sha256, pack.firmware_sha256);
            if symbol.policy == "restricted" {
                assert!(
                    !rust.contains(&format!("pub unsafe fn canopus_fw_{}(", symbol.name)),
                    "restricted {} emitted a public wrapper",
                    symbol.name
                );
            }
        }

        let evidence: serde_json::Value = serde_json::from_str(
            &std::fs::read_to_string(dir.join("evidence").join(format!("{evidence_id}.json")))
                .unwrap(),
        )
        .unwrap();
        assert_eq!(evidence["verdict"], "DEVICE_PROVEN");
        assert_eq!(evidence["target_id"], target);
    }
}

#[test]
fn private_abi_records_have_exact_thumb_callables() {
    for target in [
        "xiaomi-band-10-pro-3.101.036",
        "xiaomi-band-10-pro-3.101.043",
        "xiaomi-band-11-4.100.139",
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
            if symbol.approval_state.as_deref() == Some("APPROVED") {
                // Only the user-authorized .139 restricted blur/calendar port
                // (background-calendar-port-approval.md) and the device-verified UI
                // entry points (DEVICE_VERIFIED_UI) may be APPROVED.
                if device_verified(target, &symbol.name) {
                    assert_eq!(symbol.status, "DEVICE_PROVEN", "{}", symbol.name);
                    assert_eq!(symbol.proof.device.as_deref(), Some("proven"));
                } else {
                    assert!(
                        target == "xiaomi-band-11-4.100.139"
                            && APPROVED_139_PRIVATE.contains(&symbol.name.as_str()),
                        "{} was unexpectedly promoted",
                        symbol.name
                    );
                    assert_eq!(symbol.status, "STATIC_RECOVERED", "{}", symbol.name);
                    assert_eq!(symbol.proof.device.as_deref(), Some("not_probed"));
                }
                assert_eq!(symbol.provenance.firmware_sha256, pack.firmware_sha256);
                assert_eq!(
                    symbol.promotion.as_ref().unwrap().firmware_sha256,
                    pack.firmware_sha256
                );
            }
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
    assert!(text.contains("canopus_thumb_callable(0xc1ec8b5usize)"));
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
    assert!(text.contains("canopus_str_neq"));
    assert!(text.contains("* bt_adapter_register_a2dp_callbacks: FORBIDDEN"));
}

#[test]
fn line_terminated_identity_guard_uses_a_bounded_token() {
    let dir = repo_root().join("targets/xiaomi-band-11-4.100.139");
    let pack = canopus_core::registry::load_target_pack(&dir.join("target.toml")).unwrap();
    let (symbols, types) = load_records(&dir).unwrap();
    let text = VeneerGen {
        pack: &pack,
        symbols: &symbols,
        types: &types,
    }
    .generate();

    assert!(text.contains("canopus_identity_token_neq"));
    assert!(text.contains(r"*actual != '\0' && *actual != '\n' && *actual != '\r'"));
    assert!(text.contains("canopus_identity_token_neq(actual_version, expect_version)"));
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
    assert!(text.contains("b\"3.101.036\""));
    assert!(text.contains("b\"CONBINE_LTALM078_T3.101.036_06242053\""));
    assert!(text.contains("fn c_str_eq"));
    // Generated indirect calls expose and consume the shared normalized callable
    // constant, so callback-table comparisons never take the host wrapper address.
    assert!(text.contains("pub const CANOPUS_FW_APP_LOOKUP_CALLABLE: usize"));
    assert!(text.contains("canopus_thumb_callable(0xca5107dusize)"));
    assert!(text.contains("transmute(CANOPUS_FW_APP_LOOKUP_CALLABLE)"));
    assert!(text.contains("pub const CANOPUS_FW_REGISTER_DRIVER_CALLABLE: usize"));
    assert!(text.contains("canopus_thumb_callable(0xc1a0d51usize)"));
    assert!(text.contains("transmute(CANOPUS_FW_REGISTER_DRIVER_CALLABLE)"));
}

// Helper assertions reused by the tests above (kept as a compile check that the
// pack path exists).
#[allow(dead_code)]
fn _assert_pack_exists(_p: &Path) {}
