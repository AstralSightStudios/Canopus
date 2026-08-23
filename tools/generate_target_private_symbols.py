#!/usr/bin/env python3
"""Generate target-private symbol metadata and raw wrappers.

Mechanical ABI data comes from exact target symbol records. Semantic adapters
remain hand-written: callback mirror/rollback, ownership, protocol
translation, lifecycle and fail-closed policy.
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
from pathlib import Path


def parse_addr(value: str | None) -> int | None:
    if not value:
        return None
    try:
        return int(value.strip().lower().removeprefix("0x"), 16)
    except ValueError:
        return None


def rust_string(value: str) -> str:
    return json.dumps(value, ensure_ascii=False)


def format_rust(text: str) -> str:
    try:
        result = subprocess.run(
            ["rustfmt", "--edition", "2024", "--emit", "stdout"],
            input=text,
            text=True,
            capture_output=True,
            check=True,
        )
    except (OSError, subprocess.CalledProcessError):
        return text
    return result.stdout


def rust_ident(value: str) -> str:
    value = re.sub(r"[^A-Za-z0-9]+", "_", value).strip("_").upper()
    if not value:
        value = "UNNAMED"
    if value[0].isdigit():
        value = "S_" + value
    return value


def load_records(symbols_dir: Path) -> list[dict]:
    records = []
    for path in sorted(symbols_dir.glob("*.json")):
        value = json.loads(path.read_text())
        if value.get("kind") not in {"function", "global"}:
            continue
        if not value.get("entry_address"):
            continue
        records.append(value)
    return records


def parse_prototype(prototype: str | None) -> tuple[str, list[str]] | None:
    if not prototype or "..." in prototype or " via " in prototype:
        return None
    left = prototype.find("(")
    right = prototype.rfind(")")
    if left <= 0 or right < left:
        return None
    ret = prototype[:left].strip()
    args_text = prototype[left + 1:right].strip()
    if args_text in {"", "void"}:
        return ret, []
    args = [part.strip() for part in args_text.split(",")]
    if any(not part for part in args):
        return None
    return ret, args


def map_c_type(c_type: str, generated_types: set[str]) -> str | None:
    c_type = " ".join(c_type.strip().split())
    primitive = {
        "void": "()",
        "int": "i32",
        "int32_t": "i32",
        "uint8_t": "u8",
        "uint16_t": "u16",
        "uint32_t": "u32",
        "int64_t": "i64",
        "uint64_t": "u64",
        "uintptr_t": "usize",
        "size_t": "usize",
        "bool": "bool",
    }
    if c_type in primitive:
        return primitive[c_type]
    if c_type == "void *":
        return "*mut core::ffi::c_void"
    if c_type == "const void *":
        return "*const core::ffi::c_void"
    if c_type.endswith(" *"):
        base = c_type[:-2].strip()
        is_const = base.startswith("const ")
        if is_const:
            base = base[6:].strip()
        if base == "char":
            rust_base = "u8"
        elif base in primitive and primitive[base] != "()":
            rust_base = primitive[base]
        elif base in generated_types:
            rust_base = f"canopus_target_generated::{base}"
        else:
            return None
        return f"*{'const' if is_const else 'mut'} {rust_base}"
    if c_type in generated_types:
        return f"canopus_target_generated::{c_type}"
    return None


def generated_type_names(path: Path | None) -> set[str]:
    if path is None or not path.exists():
        return set()
    text = path.read_text()
    return set(re.findall(r"pub (?:struct|type|union) ([A-Za-z_][A-Za-z0-9_]*)", text))


def render_raw_wrappers(records: list[dict], generated_types: set[str]) -> tuple[list[str], int]:
    lines = [
        "// ---- generated target-private raw wrappers ----",
        "// Mechanical ABI only; ownership and policy stay in the handwritten facade.",
        "",
    ]
    count = 0
    for record in records:
        if record.get("kind") != "function":
            continue
        if record.get("status") in {"FORBIDDEN", "WITHDRAWN"} or record.get("policy") in {"forbidden", "withdrawn"}:
            continue
        parsed = parse_prototype(record.get("prototype"))
        if parsed is None or not record.get("callable_address"):
            continue
        ret, args = parsed
        rust_ret = map_c_type(ret, generated_types)
        rust_args = [map_c_type(arg, generated_types) for arg in args]
        if rust_ret is None or any(arg is None for arg in rust_args):
            continue
        name = record.get("name", "")
        raw_name = f"raw_{name}"
        const_name = f"CANOPUS_FW_{name.upper()}_CALLABLE"
        params = ", ".join(f"a{i}: {arg}" for i, arg in enumerate(rust_args))
        call_args = ", ".join(f"a{i}" for i in range(len(rust_args)))
        lines.extend([
            f"pub unsafe fn {raw_name}({params}) -> {rust_ret} {{",
            f"    type F = unsafe extern \"C\" fn({', '.join(rust_args)}) -> {rust_ret};",
            f"    let f: F = unsafe {{ core::mem::transmute(canopus_target_generated::{const_name}) }};",
            f"    unsafe {{ f({call_args}) }}",
            "}",
            "",
        ])
        count += 1
    lines.append(f"// generated target-private raw wrapper count: {count}")
    lines.append("")
    return lines, count


def render(target_id: str, records: list[dict], firmware_sha256: str, generated_types: set[str]) -> tuple[str, int]:
    active = [
        record for record in records
        if record.get("status") not in {"FORBIDDEN", "WITHDRAWN"}
        and record.get("policy") != "forbidden"
    ]
    lines = [
        "// Generated by tools/generate_target_private_symbols.py. DO NOT EDIT.",
        "//",
        f"// target_id: {target_id}",
        f"// firmware_sha256: {firmware_sha256}",
        "// Source: targets/<target-id>/symbols/*.json.",
        "",
        "#![allow(dead_code)]",
        "#![allow(clippy::missing_safety_doc)]",
        "",
        "#[derive(Copy, Clone, Debug, Eq, PartialEq)]",
        "pub struct TargetPrivateSymbolRecord {",
        "    pub name: &'static str,",
        "    pub kind: &'static str,",
        "    pub entry: usize,",
        "    pub callable: usize,",
        "    pub status: &'static str,",
        "    pub policy: &'static str,",
        "    pub prototype: &'static str,",
        "}",
        "",
        f"pub const TARGET_ID: &str = {rust_string(target_id)};",
        f"pub const FIRMWARE_SHA256: &str = {rust_string(firmware_sha256)};",
        "",
        "pub const RECORDS: &[TargetPrivateSymbolRecord] = &[",
    ]
    for record in active:
        entry = parse_addr(record.get("entry_address"))
        callable = parse_addr(record.get("callable_address"))
        if entry is None:
            continue
        if callable is None:
            callable = entry | 1 if record.get("kind") == "function" else entry
        lines.extend([
            "    TargetPrivateSymbolRecord {",
            f"        name: {rust_string(record.get('name', ''))},",
            f"        kind: {rust_string(record.get('kind', ''))},",
            f"        entry: 0x{entry:x}usize,",
            f"        callable: 0x{callable:x}usize,",
            f"        status: {rust_string(record.get('status', ''))},",
            f"        policy: {rust_string(record.get('policy', ''))},",
            f"        prototype: {rust_string(record.get('prototype', ''))},",
            "    },",
        ])
    lines.extend(["];"])
    lines.append("")
    for record in active:
        entry = parse_addr(record.get("entry_address"))
        if entry is None:
            continue
        name = rust_ident(record.get("name", ""))
        callable = parse_addr(record.get("callable_address"))
        if callable is None:
            callable = entry | 1 if record.get("kind") == "function" else entry
        lines.append(f"pub const {name}_ENTRY: usize = 0x{entry:x}usize;")
        if record.get("kind") == "function":
            lines.append(f"pub const {name}_CALLABLE: usize = 0x{callable:x}usize;")
    lines.append("")
    raw_lines, raw_count = render_raw_wrappers(records, generated_types)
    lines.extend(raw_lines)
    return "\n".join(lines), raw_count


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--target-id", required=True)
    parser.add_argument("--symbols-dir", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--generated-rust", type=Path)
    parser.add_argument("--firmware-sha256")
    args = parser.parse_args()
    records = load_records(args.symbols_dir)
    if not records:
        raise SystemExit(f"no function/global records under {args.symbols_dir}")
    firmware_sha256 = args.firmware_sha256
    if not firmware_sha256:
        firmware_sha256 = next(
            (
                record.get("provenance", {}).get("firmware_sha256", "")
                for record in records
                if record.get("provenance", {}).get("firmware_sha256")
            ),
            "",
        )
    if not firmware_sha256:
        raise SystemExit("firmware SHA-256 is missing from records; pass --firmware-sha256")
    text, raw_count = render(
        args.target_id,
        records,
        firmware_sha256,
        generated_type_names(args.generated_rust),
    )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(format_rust(text))
    print(f"wrote {args.output} ({len(records)} records, {raw_count} target-private raw wrappers)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
