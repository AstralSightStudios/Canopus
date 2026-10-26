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
        (
            "FLAG_NATIVE_APP_INTEGRATED",
            FLAG_NATIVE_APP_INTEGRATED as u64,
        ),
        (
            "FLAG_NATIVE_APP_STANDALONE",
            FLAG_NATIVE_APP_STANDALONE as u64,
        ),
        (
            "FLAG_REGISTERS_LAUNCHER_ENTRY",
            FLAG_REGISTERS_LAUNCHER_ENTRY as u64,
        ),
        (
            "FLAG_REQUIRES_UI_DISPATCHER",
            FLAG_REQUIRES_UI_DISPATCHER as u64,
        ),
        (
            "FLAG_APP_UNREGISTER_REBOOT_REQUIRED",
            FLAG_APP_UNREGISTER_REBOOT_REQUIRED as u64,
        ),
        ("LIFECYCLE_REMOVABLE", LIFECYCLE_REMOVABLE as u64),
        (
            "LIFECYCLE_RESIDENT_AFTER_ACTIVATION",
            LIFECYCLE_RESIDENT_AFTER_ACTIVATION as u64,
        ),
        (
            "LIFECYCLE_ALWAYS_RESIDENT",
            LIFECYCLE_ALWAYS_RESIDENT as u64,
        ),
        (
            "LIFECYCLE_PATCH_REBOOT_REQUIRED",
            LIFECYCLE_PATCH_REBOOT_REQUIRED as u64,
        ),
        ("STATUS_RECORD_MAX", STATUS_RECORD_MAX as u64),
        ("CAP_MAGIC", CAP_MAGIC as u64),
        ("CONNECT_TEXT", CONNECT_TEXT as u64),
        ("CONNECT_BINARY", CONNECT_BINARY as u64),
        ("CONNECT_DATA_MAX", CONNECT_DATA_MAX as u64),
        ("CONNECT_NOT_LISTENING", CONNECT_NOT_LISTENING as u64),
        ("LAUNCHER_ENTRIES", LAUNCHER_ENTRIES as u64),
        ("LAUNCHER_LABEL_MAX", LAUNCHER_LABEL_MAX as u64),
        ("LAUNCHER_ICON_MAX", LAUNCHER_ICON_MAX as u64),
        ("NATIVE_APP_PAGES_MAX", NATIVE_APP_PAGES_MAX as u64),
    ];
    for (name, rust) in table {
        assert_eq!(c_define(name), *rust, "CANOPUS_{name}");
    }
}

#[test]
fn ui_and_bus_layouts_match_the_c_headers() {
    use core::mem::{align_of, offset_of, size_of};
    use std::io::Write;
    use std::process::{Command, Stdio};

    let layouts = [
        ("sizeof(struct canopus_launcher_obj)", size_of::<CanopusLauncherObj>()),
        (
            "_Alignof(struct canopus_launcher_obj)",
            align_of::<CanopusLauncherObj>(),
        ),
        (
            "offsetof(struct canopus_launcher_obj, register_app)",
            offset_of!(CanopusLauncherObj, register_app),
        ),
        (
            "sizeof(struct canopus_app_entry_v1)",
            size_of::<AppEntryV1>(),
        ),
        (
            "_Alignof(struct canopus_app_entry_v1)",
            align_of::<AppEntryV1>(),
        ),
        (
            "offsetof(struct canopus_app_entry_v1, struct_size)",
            offset_of!(AppEntryV1, struct_size),
        ),
        (
            "offsetof(struct canopus_app_entry_v1, label)",
            offset_of!(AppEntryV1, label),
        ),
        (
            "offsetof(struct canopus_app_entry_v1, icon)",
            offset_of!(AppEntryV1, icon),
        ),
        (
            "offsetof(struct canopus_app_entry_v1, pages)",
            offset_of!(AppEntryV1, pages),
        ),
        (
            "offsetof(struct canopus_app_entry_v1, page_count)",
            offset_of!(AppEntryV1, page_count),
        ),
        ("sizeof(struct canopus_fs_obj)", size_of::<CanopusFsObj>()),
        (
            "offsetof(struct canopus_fs_obj, get_data_path)",
            offset_of!(CanopusFsObj, get_data_path),
        ),
        (
            "offsetof(struct canopus_fs_obj, rmdir)",
            offset_of!(CanopusFsObj, rmdir),
        ),
        ("sizeof(struct canopus_ui_event_v1)", size_of::<UiEventV1>()),
        (
            "_Alignof(struct canopus_ui_event_v1)",
            align_of::<UiEventV1>(),
        ),
        (
            "offsetof(struct canopus_ui_event_v1, type)",
            offset_of!(UiEventV1, type_),
        ),
        (
            "offsetof(struct canopus_ui_event_v1, content_height)",
            offset_of!(UiEventV1, content_height),
        ),
        (
            "sizeof(struct canopus_ui_visual_v1)",
            size_of::<UiVisualV1>(),
        ),
        (
            "_Alignof(struct canopus_ui_visual_v1)",
            align_of::<UiVisualV1>(),
        ),
        (
            "offsetof(struct canopus_ui_visual_v1, content_height)",
            offset_of!(UiVisualV1, content_height),
        ),
        (
            "sizeof(struct canopus_ui_scroll_metrics_v1)",
            size_of::<UiScrollMetricsV1>(),
        ),
        (
            "sizeof(struct canopus_module_state_v1)",
            size_of::<ModuleStateV1>(),
        ),
        (
            "offsetof(struct canopus_module_state_v1, activation_error)",
            offset_of!(ModuleStateV1, activation_error),
        ),
        ("sizeof(struct canopus_bus_obj)", size_of::<CanopusBusObj>()),
        (
            "offsetof(struct canopus_bus_obj, query_module)",
            offset_of!(CanopusBusObj, query_module),
        ),
        (
            "sizeof(struct canopus_context_v1)",
            size_of::<CanopusContext>(),
        ),
        (
            "offsetof(struct canopus_context_v1, launcher)",
            offset_of!(CanopusContext, launcher),
        ),
        ("sizeof(struct canopus_ui_obj)", size_of::<CanopusUiObj>()),
        (
            "offsetof(struct canopus_ui_obj, node_set_style)",
            offset_of!(CanopusUiObj, node_set_style),
        ),
        (
            "offsetof(struct canopus_ui_obj, set_entry)",
            offset_of!(CanopusUiObj, set_entry),
        ),
        (
            "offsetof(struct canopus_ui_obj, close_page)",
            offset_of!(CanopusUiObj, close_page),
        ),
        (
            "offsetof(struct canopus_ui_obj, route)",
            offset_of!(CanopusUiObj, route),
        ),
        (
            "offsetof(struct canopus_ui_obj, node_set_visual)",
            offset_of!(CanopusUiObj, node_set_visual),
        ),
        (
            "offsetof(struct canopus_ui_obj, control)",
            offset_of!(CanopusUiObj, control),
        ),
        (
            "offsetof(struct canopus_ui_obj, post)",
            offset_of!(CanopusUiObj, post),
        ),
        ("sizeof(struct canopus_ui_entry_v1)", size_of::<UiEntryV1>()),
        (
            "offsetof(struct canopus_ui_entry_v1, user_data)",
            offset_of!(UiEntryV1, user_data),
        ),
        (
            "offsetof(struct canopus_ui_entry_v1, page_count)",
            offset_of!(UiEntryV1, page_count),
        ),
    ];
    let mut source = String::from(
        "#include <stddef.h>\n#include \"canopus_services.h\"\n#include \"canopus_ui.h\"\n",
    );
    for (expression, rust) in layouts {
        source.push_str(&format!(
            "_Static_assert({expression} == {rust}, \"{expression}\");\n"
        ));
    }
    let include = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../c");
    let mut compiler = Command::new(std::env::var("CC").unwrap_or_else(|_| "cc".into()))
        .args(["-std=c11", "-x", "c", "-fsyntax-only", "-"])
        .arg("-I")
        .arg(include)
        .stdin(Stdio::piped())
        .stderr(Stdio::piped())
        .spawn()
        .expect("C compiler required for C/Rust ABI parity");
    compiler
        .stdin
        .take()
        .unwrap()
        .write_all(source.as_bytes())
        .unwrap();
    let output = compiler.wait_with_output().unwrap();
    assert!(
        output.status.success(),
        "{}",
        String::from_utf8_lossy(&output.stderr)
    );
}
