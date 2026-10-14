//! Every target backend exposes the same function set (and `ERR_UNSUPPORTED`,
//! `SELECTED_TARGET_ID`, `capabilities`); a function a firmware lacks is a stub
//! that returns `ERR_UNSUPPORTED`. Checked on the sources so no firmware
//! feature has to be selected.
use std::collections::BTreeSet;

fn api(source: &str) -> BTreeSet<String> {
    let mut names = BTreeSet::new();
    let mut in_use = false;
    for line in source.lines() {
        let t = line.trim_start();
        if let Some(rest) = t.strip_prefix("pub unsafe fn ").or_else(|| t.strip_prefix("pub fn ")) {
            if !line.starts_with(' ') {
                names.insert(rest.split(['(', '<']).next().unwrap().to_string());
            }
        }
        if let Some(rest) = line.strip_prefix("pub use ") {
            if let Some((_, alias)) = rest.trim_end_matches(';').rsplit_once(" as ") {
                names.insert(alias.trim().to_string());
            }
        }
        if line.starts_with("pub use canopus_target_generated::{") {
            in_use = true;
        }
        if in_use {
            for part in line.split(['{', '}', ',']) {
                let n = part.trim();
                if n.starts_with("canopus_fw_") {
                    names.insert(n.to_string());
                }
            }
            if line.contains('}') {
                in_use = false;
            }
        }
    }
    names
}

#[test]
fn all_targets_expose_the_same_functions() {
    let targets = [
        ("036", include_str!("../src/targets/xiaomi_band_10_pro_3_101_036.rs")),
        ("043", include_str!("../src/targets/xiaomi_band_10_pro_3_101_043.rs")),
        ("band11-139", include_str!("../src/targets/xiaomi_band_11_4_100_139.rs")),
        ("band11-155", include_str!("../src/targets/xiaomi_band_11_4_100_155.rs")),
        ("band11-177", include_str!("../src/targets/xiaomi_band_11_4_100_177.rs")),
    ];
    let sets: Vec<_> = targets.iter().map(|(n, s)| (*n, api(s))).collect();
    let union: BTreeSet<_> = sets.iter().flat_map(|(_, s)| s.iter().cloned()).collect();
    for (name, set) in &sets {
        let missing: Vec<_> = union.difference(set).collect();
        assert!(missing.is_empty(), "{name} lacks {missing:?}; add an ERR_UNSUPPORTED stub");
    }
    for (name, source) in targets {
        for item in ["pub const ERR_UNSUPPORTED: i32 = -38;", "pub const SELECTED_TARGET_ID"] {
            assert!(source.contains(item), "{name} lacks `{item}`");
        }
    }
}
