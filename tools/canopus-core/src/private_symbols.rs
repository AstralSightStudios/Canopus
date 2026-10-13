//! Target-private symbol metadata and raw wrappers (ported from the former
//! `tools/generate_target_private_symbols.py`).
//!
//! Mechanical ABI data comes from exact target symbol records. Semantic
//! adapters (callback mirror/rollback, ownership, protocol translation,
//! lifecycle, fail-closed policy) remain hand-written in the SDK.

use crate::error::{Error, Result};
use serde_json::Value;
use std::collections::BTreeSet;
use std::path::Path;

fn parse_addr(value: Option<&str>) -> Option<u64> {
    let v = value?.trim().to_ascii_lowercase();
    u64::from_str_radix(v.strip_prefix("0x").unwrap_or(&v), 16).ok()
}

fn rust_string(value: &str) -> String {
    serde_json::to_string(value).expect("string serializes")
}

fn rust_ident(value: &str) -> String {
    let mut out = String::new();
    let mut pending_sep = false;
    for c in value.chars() {
        if c.is_ascii_alphanumeric() {
            if pending_sep && !out.is_empty() {
                out.push('_');
            }
            pending_sep = false;
            out.push(c.to_ascii_uppercase());
        } else {
            pending_sep = true;
        }
    }
    if out.is_empty() {
        out = "UNNAMED".into();
    }
    if out.starts_with(|c: char| c.is_ascii_digit()) {
        out = format!("S_{out}");
    }
    out
}

fn str_field<'a>(record: &'a Value, key: &str) -> &'a str {
    record.get(key).and_then(Value::as_str).unwrap_or("")
}

fn is_dead(record: &Value) -> bool {
    matches!(str_field(record, "status"), "FORBIDDEN" | "WITHDRAWN")
        || matches!(str_field(record, "policy"), "forbidden" | "withdrawn")
}

fn load_records(symbols_dir: &Path) -> Result<Vec<Value>> {
    let mut paths: Vec<_> = std::fs::read_dir(symbols_dir)?
        .filter_map(|e| e.ok().map(|e| e.path()))
        .filter(|p| p.extension().and_then(|e| e.to_str()) == Some("json"))
        .collect();
    paths.sort();
    let mut records = Vec::new();
    for path in paths {
        let value: Value = serde_json::from_str(&std::fs::read_to_string(&path)?)?;
        let kind = str_field(&value, "kind");
        if (kind == "function" || kind == "global")
            && !str_field(&value, "entry_address").is_empty()
        {
            records.push(value);
        }
    }
    Ok(records)
}

fn parse_prototype(prototype: &str) -> Option<(String, Vec<String>)> {
    if prototype.is_empty() || prototype.contains("...") || prototype.contains(" via ") {
        return None;
    }
    let left = prototype.find('(')?;
    let right = prototype.rfind(')')?;
    if left == 0 || right < left {
        return None;
    }
    let ret = prototype[..left].trim().to_string();
    let args_text = prototype[left + 1..right].trim();
    if args_text.is_empty() || args_text == "void" {
        return Some((ret, vec![]));
    }
    let args: Vec<String> = args_text.split(',').map(|p| p.trim().to_string()).collect();
    if args.iter().any(String::is_empty) {
        return None;
    }
    Some((ret, args))
}

fn primitive(c: &str) -> Option<&'static str> {
    Some(match c {
        "void" => "()",
        "int" | "int32_t" => "i32",
        "uint8_t" => "u8",
        "uint16_t" => "u16",
        "uint32_t" => "u32",
        "int64_t" => "i64",
        "uint64_t" => "u64",
        "uintptr_t" | "size_t" => "usize",
        "bool" => "bool",
        _ => return None,
    })
}

fn map_c_type(c_type: &str, generated: &BTreeSet<String>) -> Option<String> {
    let c = c_type.split_whitespace().collect::<Vec<_>>().join(" ");
    if let Some(p) = primitive(&c) {
        return Some(p.to_string());
    }
    if c == "void *" {
        return Some("*mut core::ffi::c_void".into());
    }
    if c == "const void *" {
        return Some("*const core::ffi::c_void".into());
    }
    if let Some(base) = c.strip_suffix(" *") {
        let base = base.trim();
        let (is_const, base) = match base.strip_prefix("const ") {
            Some(rest) => (true, rest.trim()),
            None => (false, base),
        };
        let rust_base = if base == "char" {
            "u8".to_string()
        } else if let Some(p) = primitive(base).filter(|p| *p != "()") {
            p.to_string()
        } else if generated.contains(base) {
            format!("canopus_target_generated::{base}")
        } else {
            return None;
        };
        return Some(format!(
            "*{} {rust_base}",
            if is_const { "const" } else { "mut" }
        ));
    }
    if generated.contains(&c) {
        return Some(format!("canopus_target_generated::{c}"));
    }
    None
}

/// Names of `pub struct|type|union` items in generated Rust bindings.
fn generated_type_names(text: &str) -> BTreeSet<String> {
    let mut out = BTreeSet::new();
    for line in text.lines() {
        let mut rest = line;
        while let Some(i) = rest.find("pub ") {
            let after = &rest[i + 4..];
            for kw in ["struct ", "type ", "union "] {
                if let Some(tail) = after.strip_prefix(kw) {
                    let name: String = tail
                        .chars()
                        .take_while(|c| c.is_ascii_alphanumeric() || *c == '_')
                        .collect();
                    if !name.is_empty() && !name.starts_with(|c: char| c.is_ascii_digit()) {
                        out.insert(name);
                    }
                }
            }
            rest = after;
        }
    }
    out
}

fn render_raw_wrappers(records: &[Value], generated: &BTreeSet<String>) -> (Vec<String>, usize) {
    let mut lines = vec![
        "// ---- generated target-private raw wrappers ----".to_string(),
        "// Mechanical ABI only; ownership and policy stay in the handwritten facade.".into(),
        String::new(),
    ];
    let mut count = 0;
    for record in records {
        if str_field(record, "kind") != "function" || is_dead(record) {
            continue;
        }
        let Some((ret, args)) = parse_prototype(str_field(record, "prototype")) else {
            continue;
        };
        if str_field(record, "callable_address").is_empty() {
            continue;
        }
        let Some(rust_ret) = map_c_type(&ret, generated) else {
            continue;
        };
        let rust_args: Option<Vec<String>> =
            args.iter().map(|a| map_c_type(a, generated)).collect();
        let Some(rust_args) = rust_args else {
            continue;
        };
        let name = str_field(record, "name");
        let params = rust_args
            .iter()
            .enumerate()
            .map(|(i, a)| format!("a{i}: {a}"))
            .collect::<Vec<_>>()
            .join(", ");
        let call_args = (0..rust_args.len())
            .map(|i| format!("a{i}"))
            .collect::<Vec<_>>()
            .join(", ");
        lines.push(format!(
            "pub unsafe fn raw_{name}({params}) -> {rust_ret} {{"
        ));
        lines.push(format!(
            "    type F = unsafe extern \"C\" fn({}) -> {rust_ret};",
            rust_args.join(", ")
        ));
        lines.push(format!(
            "    let f: F = unsafe {{ core::mem::transmute(canopus_target_generated::CANOPUS_FW_{}_CALLABLE) }};",
            name.to_ascii_uppercase()
        ));
        lines.push(format!("    unsafe {{ f({call_args}) }}"));
        lines.push("}".into());
        lines.push(String::new());
        count += 1;
    }
    lines.push(format!("// generated target-private raw wrapper count: {count}"));
    lines.push(String::new());
    (lines, count)
}

fn callable_of(record: &Value, entry: u64) -> u64 {
    parse_addr(record.get("callable_address").and_then(Value::as_str)).unwrap_or(
        if str_field(record, "kind") == "function" {
            entry | 1
        } else {
            entry
        },
    )
}

/// Unformatted Rust source plus the raw wrapper count. `generated_rust` is the
/// generated target bindings source (used to resolve struct/type names).
pub fn render(
    target_id: &str,
    records: &[Value],
    firmware_sha256: &str,
    generated_rust: &str,
) -> (String, usize) {
    let generated = generated_type_names(generated_rust);
    let active: Vec<&Value> = records
        .iter()
        .filter(|r| {
            !matches!(str_field(r, "status"), "FORBIDDEN" | "WITHDRAWN")
                && str_field(r, "policy") != "forbidden"
        })
        .collect();
    let mut lines: Vec<String> = vec![
        "// Generated by `canopus target generate`. DO NOT EDIT.".into(),
        "//".into(),
        format!("// target_id: {target_id}"),
        format!("// firmware_sha256: {firmware_sha256}"),
        "// Source: targets/<target-id>/symbols/*.json.".into(),
        String::new(),
        "#![allow(dead_code)]".into(),
        "#![allow(clippy::missing_safety_doc)]".into(),
        String::new(),
        "#[derive(Copy, Clone, Debug, Eq, PartialEq)]".into(),
        "pub struct TargetPrivateSymbolRecord {".into(),
        "    pub name: &'static str,".into(),
        "    pub kind: &'static str,".into(),
        "    pub entry: usize,".into(),
        "    pub callable: usize,".into(),
        "    pub status: &'static str,".into(),
        "    pub policy: &'static str,".into(),
        "    pub prototype: &'static str,".into(),
        "}".into(),
        String::new(),
        format!("pub const TARGET_ID: &str = {};", rust_string(target_id)),
        format!(
            "pub const FIRMWARE_SHA256: &str = {};",
            rust_string(firmware_sha256)
        ),
        String::new(),
        "pub const RECORDS: &[TargetPrivateSymbolRecord] = &[".into(),
    ];
    for record in &active {
        let Some(entry) = parse_addr(record.get("entry_address").and_then(Value::as_str)) else {
            continue;
        };
        let callable = callable_of(record, entry);
        lines.push("    TargetPrivateSymbolRecord {".into());
        lines.push(format!("        name: {},", rust_string(str_field(record, "name"))));
        lines.push(format!("        kind: {},", rust_string(str_field(record, "kind"))));
        lines.push(format!("        entry: 0x{entry:x}usize,"));
        lines.push(format!("        callable: 0x{callable:x}usize,"));
        lines.push(format!("        status: {},", rust_string(str_field(record, "status"))));
        lines.push(format!("        policy: {},", rust_string(str_field(record, "policy"))));
        lines.push(format!(
            "        prototype: {},",
            rust_string(str_field(record, "prototype"))
        ));
        lines.push("    },".into());
    }
    lines.push("];".into());
    lines.push(String::new());
    for record in &active {
        let Some(entry) = parse_addr(record.get("entry_address").and_then(Value::as_str)) else {
            continue;
        };
        let name = rust_ident(str_field(record, "name"));
        let callable = callable_of(record, entry);
        lines.push(format!("pub const {name}_ENTRY: usize = 0x{entry:x}usize;"));
        if str_field(record, "kind") == "function" {
            lines.push(format!(
                "pub const {name}_CALLABLE: usize = 0x{callable:x}usize;"
            ));
        }
    }
    lines.push(String::new());
    let (raw_lines, raw_count) = render_raw_wrappers(records, &generated);
    lines.extend(raw_lines);
    (lines.join("\n"), raw_count)
}

fn rustfmt(text: &str) -> String {
    use std::io::Write;
    use std::process::{Command, Stdio};
    let child = Command::new("rustfmt")
        .args(["--edition", "2024", "--emit", "stdout"])
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .stderr(Stdio::null())
        .spawn();
    let Ok(mut child) = child else {
        return text.to_string();
    };
    if child
        .stdin
        .take()
        .map(|mut s| s.write_all(text.as_bytes()))
        .is_none()
    {
        return text.to_string();
    }
    match child.wait_with_output() {
        Ok(out) if out.status.success() => String::from_utf8_lossy(&out.stdout).into_owned(),
        _ => text.to_string(),
    }
}

/// Generates the formatted `generated_symbols_*.rs` source for a target.
pub fn generate(
    target_id: &str,
    target_dir: &Path,
    generated_rust: &str,
) -> Result<String> {
    let records = load_records(&target_dir.join("symbols"))?;
    if records.is_empty() {
        return Err(Error::other(format!(
            "no function/global records under {}",
            target_dir.join("symbols").display()
        )));
    }
    let sha = records
        .iter()
        .filter_map(|r| r["provenance"]["firmware_sha256"].as_str())
        .find(|s| !s.is_empty())
        .ok_or_else(|| Error::other("firmware SHA-256 is missing from symbol records"))?
        .to_string();
    let (text, _) = render(target_id, &records, &sha, generated_rust);
    Ok(rustfmt(&text))
}
