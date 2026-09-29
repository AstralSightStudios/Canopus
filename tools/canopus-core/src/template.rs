//! Module scaffolding templates (CAN-REL-004).
//!
//! `canopus module new <name> --lang c|rust --target <id>` renders a working
//! module skeleton: source, package manifest and a build script that produces
//! a verifier-PASSED zero-import ELF32 ET_REL for the target.

use std::collections::BTreeMap;

/// Renders the file set (filename -> contents) for a new module.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ModuleLang {
    C,
    Rust,
}

pub fn render(name: &str, target_id: &str, lang: ModuleLang) -> BTreeMap<String, String> {
    render_with_constructor_discovery(name, target_id, lang, "init_array")
        .expect("init_array templates are supported")
}

/// Renders a module skeleton for the target loader's constructor-discovery ABI.
/// Unknown profiles fail closed rather than emitting an ELF with the wrong entry contract.
pub fn render_with_constructor_discovery(
    name: &str,
    target_id: &str,
    lang: ModuleLang,
    constructor_discovery: &str,
) -> Result<BTreeMap<String, String>, String> {
    let entry_style = match constructor_discovery {
        "init_array" => EntryStyle::InitArray,
        "module_initialize" => EntryStyle::ModuleInitialize,
        other => {
            return Err(format!(
                "unsupported constructor discovery profile '{other}'"
            ));
        }
    };

    match (lang, entry_style) {
        (ModuleLang::C, style) => Ok(c_module(name, target_id, style)),
        (ModuleLang::Rust, EntryStyle::InitArray) => Ok(rust_module(name, target_id)),
        (ModuleLang::Rust, EntryStyle::ModuleInitialize) => {
            Err("Rust module template does not yet provide a module_initialize C shim".into())
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum EntryStyle {
    InitArray,
    ModuleInitialize,
}

fn safe_name(name: &str) -> String {
    let mut out: String = name
        .chars()
        .map(|c| {
            if c.is_ascii_alphanumeric() || c == '_' {
                c
            } else {
                '_'
            }
        })
        .collect();
    while out.starts_with('_') {
        out.remove(0);
    }
    if out.is_empty() {
        out = "module".to_string();
    }
    out
}

fn manifest(name: &str, target_id: &str) -> String {
    format!(
        r#"{{
  "schema": 1,
  "package_id": "org.canopus.{name}",
  "module_id": "org.canopus.{name}",
  "version": "0.1.0",
  "build_generation": 1,
  "canopus_abi": "1",
  "lifecycle": "removable",
  "artifacts": [
    {{
      "target_id": "{target_id}",
      "target_pack_revision": 1,
      "firmware_sha256": "0000000000000000000000000000000000000000000000000000000000000000",
      "path": "artifacts/{target_id}/module.elf",
      "sha256": "0000000000000000000000000000000000000000000000000000000000000000"
    }}
  ],
  "capabilities": {{ "required": [] }},
  "target_pack_revision": 1,
  "signature": {{ "key_id": "dev-key-001", "algorithm": "ed25519" }},
  "min_manager_version": "0.1.0",
  "reboot_required": false
}}
"#,
        name = safe_name(name)
    )
}

fn c_source(name: &str, target_id: &str, entry_style: EntryStyle) -> String {
    let n = safe_name(name);
    let p65 = target_id == "xiaomi-p65-3.100.043"
        && entry_style == EntryStyle::ModuleInitialize;
    let registration_glue = if p65 {
        r#"#include "canopus_module_registration.h"
#include "canopus_p65_abi.h"
static uint8_t s_registered;
static int s_registration_close_error;
static int register_with_supervisor(void)
{
    const struct canopus_module_registration_io_v1 io = {
        (int (*)(const char *, int, ...))(uintptr_t)CANOPUS_SUP_NUTTX_OPEN,
        (int32_t (*)(int, const void *, uint32_t))(uintptr_t)CANOPUS_SUP_NUTTX_WRITE,
        (int (*)(int))(uintptr_t)CANOPUS_SUP_NUTTX_CLOSE,
        2 /* P65 O_WRONLY; open existing endpoint, never create. */
    };
    int rc = canopus_module_register_fd(&io, (uint32_t)(uintptr_t)&g_descriptor,
                                       (const char *)g_descriptor.module_id,
                                       &s_registration_close_error);
    if (rc == 0) s_registered = 1u;
    return rc;
}
"#
    } else { "" };
    let unload_guard = if p65 {
        "    if (s_registered) return -16; /* Supervisor owns descriptor pointers. */\n"
    } else { "" };
    let register_entry = if p65 {
        format!("    if (register_with_supervisor() != 0) {{\n        (void){n}_stop(0);\n        return -1;\n    }}\n")
    } else { String::new() };
    let lifecycle_glue = match entry_style {
        EntryStyle::InitArray => format!(
            r#"__attribute__((constructor)) static void {n}_ctor(void)
{{
    /* Fail closed on a firmware mismatch BEFORE any use. The identity guard
     * reads firmware addresses, which also keeps this constructor
     * non-eliminable under -ffunction-sections/-Os. */
    if (canopus_identity_guard() != 0) {{
        return;
    }}
    (void){n}_prepare(0);
}}

__attribute__((destructor)) static void {n}_dtor(void)
{{
    (void){n}_stop(0);
}}
"#,
            n = n
        ),
        EntryStyle::ModuleInitialize => format!(
            r#"struct canopus_modlib_unload_pair_v1 {{
    int32_t (*callback)(void *context);
    void *context;
}};

_Static_assert(sizeof(struct canopus_modlib_unload_pair_v1) == 8u,
               "P65 modlib unload pair must contain two 32-bit words");

static int32_t {n}_modlib_unload(void *context)
{{
    (void)context;
{unload_guard}    return {n}_stop(0);
}}

/* P65 ET_REL modlib calls e_entry with module-record words +104/+108.
 * It does not discover this module through .init_array/.fini_array. */
__attribute__((used, section(".text.canopus_module_entry")))
int32_t canopus_module_initialize(
    struct canopus_modlib_unload_pair_v1 *unload_pair)
{{
    if (unload_pair == 0) {{
        return -1;
    }}
    unload_pair->callback = 0;
    unload_pair->context = 0;

    if (canopus_identity_guard() != 0) {{
        return -1;
    }}
    if ({n}_prepare(0) != 0) {{
        (void){n}_stop(0);
        return -1;
    }}

{register_entry}    unload_pair->callback = {n}_modlib_unload;
    unload_pair->context = 0;
    return 0;
}}
"#,
            n = n
        ),
    };
    format!(
        r#"/* {n}.c — Canopus removable module. Generated by `canopus module new`. */
#include "canopus_abi.h"
#include "canopus_runtime.h"
#include "canopus_veneer.h"
#include <stddef.h>

#define {U}_MAGIC 0x4D4F4455u /* "MODU" */

static uint32_t s_ready;

static int {n}_prepare(const struct canopus_context_v1 *ctx)
{{
    (void)ctx;
    s_ready = 0u;
    return 0;
}}

static int {n}_activate(const struct canopus_context_v1 *ctx)
{{
    (void)ctx;
    /* fail closed on a firmware mismatch */
    if (canopus_identity_guard() != 0) {{
        return -1;
    }}
    s_ready = 1u;
    return 0;
}}

static int {n}_deactivate(const struct canopus_context_v1 *ctx)
{{
    (void)ctx;
    s_ready = 0u;
    return 0;
}}

static int {n}_stop(const struct canopus_context_v1 *ctx)
{{
    (void)ctx;
    s_ready = 0u;
    return 0;
}}

static int {n}_query(struct canopus_status_writer_v1 *writer)
{{
    if (writer != 0) {{
        struct canopus_status_writer_v1 tmp = *writer;
        canopus_status_put_u32(&tmp, {U}_MAGIC);
        canopus_status_put_u32(&tmp, s_ready);
        canopus_status_writer_publish(&tmp);
        *writer = tmp;
    }}
    return 0;
}}

static const struct canopus_module_descriptor_v1 g_descriptor = {{
    .struct_size = sizeof(struct canopus_module_descriptor_v1),
    .abi_major = CANOPUS_ABI_MAJOR,
    .abi_minor = CANOPUS_ABI_MINOR,
    .flags = 0u,
    .module_id = "org.canopus.{n}",
    .module_version = "0.1.0",
    .build_id = "{n}-0.1.0",
    .target_id = "{target_id}",
    .prepare = {n}_prepare,
    .activate = {n}_activate,
    .deactivate = {n}_deactivate,
    .stop = {n}_stop,
    .query = {n}_query,
}};

const struct canopus_module_descriptor_v1 *{n}_descriptor(void)
{{
    return &g_descriptor;
}}

{registration_glue}
{lifecycle_glue}
"#,
        n = n,
        U = n.to_uppercase(),
        target_id = target_id,
        lifecycle_glue = lifecycle_glue
    )
}

fn c_build_sh(dir_name: &str, safe: &str, target_id: &str, entry_style: EntryStyle) -> String {
    let n = safe.to_string();
    let p65 = target_id == "xiaomi-p65-3.100.043"
        && entry_style == EntryStyle::ModuleInitialize;
    let extra_flags = if p65 { "-DCANOPUS_SUP_P65_RESIDENT=1" } else { "" };
    let extra_include = if p65 { "-I$ROOT/manager/target/p65" } else { "" };
    let verifier_setup = if p65 {
        "python3 \"$ROOT/scripts/generate_p65_experimental_pack.py\" --output \"$OUT/targets\"\nVERIFY_TARGETS=\"$OUT/targets\""
    } else { "VERIFY_TARGETS=\"$ROOT/targets\"" };
    let entry_link_flags = match entry_style {
        EntryStyle::InitArray => String::new(),
        EntryStyle::ModuleInitialize => {
            "-e canopus_module_initialize -T \"$ROOT/scripts/canopus_module_sections.ld\"".into()
        }
    };
    format!(
        r#"#!/bin/sh
# Builds the {n} module for {target_id}.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
TARGET_ID="{target_id}"
PACK_DIR="$ROOT/targets/$TARGET_ID"
GENERATED="$PACK_DIR/generated/canopus_veneer.h"
OUT="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/build"
CC=${{CC:-clang}}

[ -f "$GENERATED" ] || {{
    echo "error: run 'canopus target generate-veneer $TARGET_ID' first"
    exit 1
}}
mkdir -p "$OUT"
cd "$ROOT"

TARGET_FLAGS="--target=arm-none-eabi -mcpu=cortex-m33 -mthumb -mfloat-abi=soft \
  -ffreestanding -fno-common -fno-builtin -fno-stack-protector \
  -fno-unwind-tables -fno-asynchronous-unwind-tables -fdata-sections \
  -ffunction-sections -Os -Wall -Wextra -Werror -DCANOPUS_TARGET=1 {extra_flags}"

INC="-I$ROOT/sdk/c -I$ROOT/runtime/lifecycle -I$ROOT/runtime/resources \
  -I$ROOT/runtime/diagnostics -I$ROOT/runtime/control -I$ROOT/runtime/module \
  -I$PACK_DIR/generated -Imodules/{dir} {extra_include}"

SRCS="runtime/control/canopus_control.c runtime/lifecycle/canopus_lifecycle.c \
  runtime/resources/canopus_resource.c runtime/diagnostics/canopus_diagnostics.c \
  runtime/module/canopus_module.c modules/{dir}/{n}.c"

for s in $SRCS; do
    $CC $TARGET_FLAGS $INC -c "$ROOT/$s" -o "$OUT/$(basename "${{s%.c}}").o"
done

LD=${{LD:-ld.lld}}
$LD -r {entry_link_flags} -o "$OUT/{n}_module.elf" "$OUT"/canopus_control.o \
    "$OUT"/canopus_lifecycle.o "$OUT"/canopus_resource.o \
    "$OUT"/canopus_diagnostics.o "$OUT"/canopus_module.o "$OUT"/{n}.o

{verifier_setup}
"$ROOT/target/debug/canopus" verify "$OUT/{n}_module.elf" \
    --target "$TARGET_ID" --targets-dir "$VERIFY_TARGETS"
"#,
        n = n,
        dir = dir_name,
        target_id = target_id,
        entry_link_flags = entry_link_flags
    )
}

fn c_module(name: &str, target_id: &str, entry_style: EntryStyle) -> BTreeMap<String, String> {
    let n = safe_name(name);
    let mut files = BTreeMap::new();
    files.insert(format!("{n}.c"), c_source(&n, target_id, entry_style));
    files.insert("package.json".into(), manifest(&n, target_id));
    files.insert(
        "build.sh".into(),
        c_build_sh(name, &n, target_id, entry_style),
    );
    files
}

fn rust_source(name: &str, target_id: &str) -> String {
    let n = safe_name(name);
    format!(
        r#"//! {n} — Canopus no_std Rust module. Generated by `canopus module new`.
#![no_std]

use canopus_abi::*;
use canopus_runtime::*;
use core::sync::atomic::Ordering;

const {U}_MAGIC: u32 = 0x4D4F4455; // "MODU"

#[no_mangle]
pub extern "C" fn {n}_prepare(_ctx: *const ContextV1) -> i32 {{ 0 }}

#[no_mangle]
pub extern "C" fn {n}_activate(_ctx: *const ContextV1) -> i32 {{
    #[cfg(feature = "device")]
    {{
        if canopus_target_generated::canopus_identity_guard() != 0 {{
            return -1;
        }}
    }}
    0
}}

#[no_mangle]
pub extern "C" fn {n}_deactivate(_ctx: *const ContextV1) -> i32 {{ 0 }}

#[no_mangle]
pub extern "C" fn {n}_stop(_ctx: *const ContextV1) -> i32 {{ 0 }}

#[no_mangle]
pub extern "C" fn {n}_query(w: *mut StatusWriterV1) -> i32 {{
    if w.is_null() {{
        return -1;
    }}
    let w = unsafe {{ &mut *w }};
    unsafe {{
        if !status_put_u32(w, {U}_MAGIC) {{
            return -1;
        }}
        status_writer_publish(w);
    }}
    0
}}

#[no_mangle]
pub static canopus_module_descriptor: ModuleDescriptorV1 = ModuleDescriptorV1 {{
    struct_size: core::mem::size_of::<ModuleDescriptorV1>() as u32,
    abi_major: ABI_MAJOR,
    abi_minor: ABI_MINOR,
    flags: 0,
    module_id: pack(b"org.canopus.{n}"),
    module_version: pack(b"0.1.0"),
    build_id: pack(b"{n}-0.1.0"),
    target_id: pack(b"{target_id}"),
    prepare: Some({n}_prepare),
    activate: Some({n}_activate),
    deactivate: Some({n}_deactivate),
    stop: Some({n}_stop),
    query: Some({n}_query),
    publish_native_app: None,
    publish_native_app_stage: None,
}};

const fn pack<const N: usize>(s: &[u8]) -> [u8; N] {{
    let mut out = [0u8; N];
    let mut i = 0;
    while i < s.len() && i < N {{
        out[i] = s[i];
        i += 1;
    }}
    out
}}

#[no_mangle]
pub extern "C" fn canopus_module_descriptor_ptr() -> *const ModuleDescriptorV1 {{
    &canopus_module_descriptor
}}
"#,
        n = n,
        U = n.to_uppercase(),
        target_id = target_id
    )
}

fn rust_cargo(n: &str) -> String {
    format!(
        r#"[package]
name = "{n}"
description = "Canopus Rust module (CAN-REL-004 template)"
version = "0.1.0"
edition = "2021"
license = "AGPL-3.0"

[lib]
name = "{n}"
crate-type = ["rlib"]

[features]
default = []
device = ["dep:canopus-target-generated"]

[dependencies]
canopus-abi = {{ path = "../../sdk/rust/canopus-abi" }}
canopus-runtime = {{ path = "../../sdk/rust/canopus-runtime" }}
canopus-target-generated = {{ path = "../../sdk/rust/canopus-target-generated", optional = true }}

[dev-dependencies]
canopus-host-fake = {{ path = "../../sdk/rust/canopus-host-fake" }}
"#
    )
}

fn rust_module(name: &str, target_id: &str) -> BTreeMap<String, String> {
    let n = safe_name(name);
    let mut files = BTreeMap::new();
    files.insert("Cargo.toml".into(), rust_cargo(&n));
    files.insert("src/lib.rs".into(), rust_source(&n, target_id));
    files.insert("package.json".into(), manifest(&n, target_id));
    files
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn c_template_renders_valid_structure() {
        let files = render("my-fn", "xiaomi-band-10-pro-3.101.030", ModuleLang::C);
        assert_eq!(files.len(), 3);
        let src = &files["my_fn.c"];
        assert!(src.contains("canopus_identity_guard"));
        assert!(src.contains("org.canopus.my_fn"));
        assert!(src.contains("__attribute__((constructor))"));
        let sh = &files["build.sh"];
        assert!(sh.contains("xiaomi-band-10-pro-3.101.030"));
        assert!(sh.contains("verify"));
        // manifest parses as JSON
        let m: serde_json::Value = serde_json::from_str(&files["package.json"]).unwrap();
        assert_eq!(m["module_id"], "org.canopus.my_fn");
        assert_eq!(m["lifecycle"], "removable");
    }

    #[test]
    fn module_initialize_c_template_emits_entry_and_unload_pair() {
        let files = render_with_constructor_discovery(
            "p65-probe",
            "xiaomi-p65-3.100.043",
            ModuleLang::C,
            "module_initialize",
        )
        .unwrap();
        let src = &files["p65_probe.c"];
        assert!(src.contains("canopus_module_initialize"));
        assert!(src.contains(".text.canopus_module_entry"));
        assert!(src.contains("unload_pair->callback = p65_probe_modlib_unload"));
        assert!(src.contains("int32_t (*callback)(void *context)"));
        assert!(src.contains("static int32_t p65_probe_modlib_unload"));
        assert!(src.contains("return p65_probe_stop(0);"));
        assert!(!src.contains("__attribute__((constructor))"));
        assert!(!src.contains("__attribute__((destructor))"));

        let build = &files["build.sh"];
        assert!(build.contains("-e canopus_module_initialize"));
        assert!(build.contains("canopus_module_sections.ld"));
        assert!(src.contains("register_with_supervisor()"));
        assert!(src.contains("if (s_registered) return -16;"));
        assert!(build.contains("generate_p65_experimental_pack.py"));
    }

    #[test]
    fn module_initialize_rust_template_fails_closed_until_shim_exists() {
        let result = render_with_constructor_discovery(
            "p65-probe",
            "xiaomi-p65-3.100.043",
            ModuleLang::Rust,
            "module_initialize",
        );
        assert!(result.is_err());
    }

    #[test]
    fn unknown_constructor_discovery_fails_closed() {
        let result = render_with_constructor_discovery(
            "probe",
            "xiaomi-p65-3.100.043",
            ModuleLang::C,
            "unknown",
        );
        assert!(result.is_err());
    }

    #[test]
    fn rust_template_renders_valid_structure() {
        let files = render("counter", "xiaomi-band-10-pro-3.101.030", ModuleLang::Rust);
        assert_eq!(files.len(), 3);
        let src = &files["src/lib.rs"];
        assert!(src.contains("#![no_std]"));
        assert!(src.contains("canopus_identity_guard"));
        assert!(src.contains("canopus_module_descriptor"));
        let cargo = &files["Cargo.toml"];
        assert!(cargo.contains("canopus-abi"));
        assert!(cargo.contains("canopus-runtime"));
        assert!(src.contains("no_std"));
    }

    #[test]
    fn unsafe_names_sanitized() {
        assert_eq!(safe_name("my module!"), "my_module_");
        assert_eq!(safe_name("___"), "module");
        assert_eq!(safe_name("ok"), "ok");
    }

    #[test]
    fn manifest_validates_against_schema() {
        let files = render("t", "xiaomi-band-10-pro-3.101.030", ModuleLang::C);
        let value: serde_json::Value = serde_json::from_str(&files["package.json"]).unwrap();
        crate::schema::validate(crate::schema::SchemaKind::Package, &value).unwrap();
    }
}
