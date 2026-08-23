//! SDK single-target assumption audit (CAN-MULTI-004).
//!
//! The PUBLIC SDK — the C ABI, the Rust abi/runtime crates and the public
//! native-app descriptor API — must never leak target-private content:
//! firmware-specific names, absolute addresses, or recovered structs
//! (architecture §3.5).
//!
//! `app-sdk/launcher` is deliberately EXCLUDED: it is the per-target launcher
//! ADAPTER layer, whose whole job is to speak this firmware's launcher ABI
//! (e.g. the ordered app list wire format recovered in EVID-APP-003). The
//! audit forces such adapter code to live there and nowhere public.

const PUBLIC_SDK_DIRS: &[&str] = &[
    "sdk/c",
    "sdk/rust/canopus-abi/src",
    "sdk/rust/canopus-runtime/src",
    "app-sdk/c",
];

/// Anything that would couple the public SDK to one firmware.
const TARGET_PRIVATE_MARKERS: &[&str] = &[
    "xiaomi",
    "band-10-pro",
    "0x0C4F",     // firmware text segment base (as written in the pack)
    "0x200D090C", // launcher runtime app list
    "app_launcher_",
    "launcher_app_",
    "ordered_app_entry",
    "stock_timespec",
    "protobuf_set_ordered_app_list",
    "hidden_and_show_app_cb",
];

fn repo_root() -> std::path::PathBuf {
    std::path::PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .join("..")
        .join("..")
}

fn walk(dir: &std::path::Path, out: &mut Vec<std::path::PathBuf>) {
    for entry in std::fs::read_dir(dir).unwrap() {
        let p = entry.unwrap().path();
        if p.is_dir() {
            walk(&p, out);
        } else if p.extension().and_then(|e| e.to_str()) == Some("rs")
            || p.extension().and_then(|e| e.to_str()) == Some("c")
            || p.extension().and_then(|e| e.to_str()) == Some("h")
        {
            out.push(p);
        }
    }
}

#[test]
fn public_sdk_is_target_agnostic() {
    let root = repo_root();
    for dir in PUBLIC_SDK_DIRS {
        let d = root.join(dir);
        assert!(d.is_dir(), "missing SDK dir {dir}");
        let mut files = Vec::new();
        walk(&d, &mut files);
        assert!(!files.is_empty(), "no source files under {dir}");
        for f in files {
            let text = std::fs::read_to_string(&f).unwrap();
            for marker in TARGET_PRIVATE_MARKERS {
                assert!(
                    !text.contains(marker),
                    "public SDK leaks target-private marker '{marker}' in {}",
                    f.display()
                );
            }
        }
    }
}

#[test]
fn public_sdk_markers_are_actually_caught_by_the_audit() {
    // Guard against the marker list going stale: every marker must appear in
    // the target pack (so the audit really has teeth) and the audit must flag
    // a planted marker.
    let root = repo_root();
    let pack_dir = root.join("targets/xiaomi-band-10-pro-3.101.036");
    for marker in TARGET_PRIVATE_MARKERS {
        let found = find_in_tree(&pack_dir, marker);
        assert!(
            found,
            "marker '{marker}' not found anywhere in the target pack — remove it from the audit or it is dead"
        );
    }
}

fn find_in_tree(dir: &std::path::Path, needle: &str) -> bool {
    if !dir.is_dir() {
        return false;
    }
    for entry in std::fs::read_dir(dir).unwrap() {
        let p = entry.unwrap().path();
        if p.is_dir() {
            if find_in_tree(&p, needle) {
                return true;
            }
        } else if let Ok(text) = std::fs::read_to_string(&p)
            && text.contains(needle)
        {
            return true;
        }
    }
    false
}
