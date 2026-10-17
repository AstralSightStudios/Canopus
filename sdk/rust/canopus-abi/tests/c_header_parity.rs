//! Every Rust ABI constant that has a `CANOPUS_<NAME>` define in
//! `sdk/c/canopus_abi.h` or `sdk/c/canopus_services.h` must carry the same value.
use canopus_abi::*;

const HEADERS: [&str; 2] = [
    include_str!("../../../c/canopus_abi.h"),
    include_str!("../../../c/canopus_services.h"),
];

fn c_define(name: &str) -> u64 {
    let key = format!("#define CANOPUS_{name} ");
    let line = HEADERS
        .iter()
        .flat_map(|header| header.lines())
        .find(|l| l.trim_start().starts_with(&key))
        .unwrap_or_else(|| panic!("CANOPUS_{name} missing in header"));
    let value = line.trim_start()[key.len()..]
        .split("/*")
        .next()
        .unwrap()
        .trim()
        .trim_matches(|c| c == '(' || c == ')');
    let num = |s: &str| {
        let s = s.trim().trim_end_matches(['u', 'U']);
        match s.strip_prefix("0x").or_else(|| s.strip_prefix("0X")) {
            Some(h) => u64::from_str_radix(h, 16).unwrap(),
            None => s.parse().unwrap(),
        }
    };
    match value.split_once("<<") {
        Some((a, b)) => num(a) << num(b),
        None => num(value),
    }
}

#[test]
fn constants_match_the_c_header() {
    let table: &[(&str, u64)] = &[
        ("ABI_MAJOR", ABI_MAJOR as u64),
        ("ABI_MINOR", ABI_MINOR as u64),
        ("FLAG_HAS_NATIVE_APP", FLAG_HAS_NATIVE_APP as u64),
        ("FLAG_NATIVE_APP_INTEGRATED", FLAG_NATIVE_APP_INTEGRATED as u64),
        ("FLAG_NATIVE_APP_STANDALONE", FLAG_NATIVE_APP_STANDALONE as u64),
        ("FLAG_REGISTERS_LAUNCHER_ENTRY", FLAG_REGISTERS_LAUNCHER_ENTRY as u64),
        ("FLAG_REQUIRES_UI_DISPATCHER", FLAG_REQUIRES_UI_DISPATCHER as u64),
        ("FLAG_APP_UNREGISTER_REBOOT_REQUIRED", FLAG_APP_UNREGISTER_REBOOT_REQUIRED as u64),
        ("LIFECYCLE_REMOVABLE", LIFECYCLE_REMOVABLE as u64),
        ("LIFECYCLE_RESIDENT_AFTER_ACTIVATION", LIFECYCLE_RESIDENT_AFTER_ACTIVATION as u64),
        ("LIFECYCLE_ALWAYS_RESIDENT", LIFECYCLE_ALWAYS_RESIDENT as u64),
        ("LIFECYCLE_PATCH_REBOOT_REQUIRED", LIFECYCLE_PATCH_REBOOT_REQUIRED as u64),
        ("STATUS_RECORD_MAX", STATUS_RECORD_MAX as u64),
        ("CAP_MAGIC", CAP_MAGIC as u64),
        ("CONNECT_TEXT", CONNECT_TEXT as u64),
        ("CONNECT_BINARY", CONNECT_BINARY as u64),
        ("CONNECT_DATA_MAX", CONNECT_DATA_MAX as u64),
        ("CONNECT_NOT_LISTENING", CONNECT_NOT_LISTENING as u64),
    ];
    for (name, rust) in table {
        assert_eq!(c_define(name), *rust, "CANOPUS_{name}");
    }
}
