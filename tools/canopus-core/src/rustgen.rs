//! Rust binding generator (CAN-RUST-002 / TGT-006).
//!
//! Emits a `no_std` target crate module from a target pack, mirroring the C
//! veneer generator (CAN-TGT-005): struct layouts for recovered types, an
//! `unsafe fn` binding per callable symbol with the Thumb callable address
//! baked in as a constant, and audit comments for FORBIDDEN/WITHDRAWN and
//! restricted symbols (which never produce a callable binding).
//!
//! Safety layering (architecture §12.1): every firmware call is `unsafe`;
//! safe wrappers are only added elsewhere once the ABI and ownership are
//! proven. The identity guard is the one safe entry point.

use crate::model::{Symbol, TargetPack, TypeRecord};
use crate::veneer::{FORBIDDEN_STATUS, parse_proto};
use std::collections::BTreeSet;

pub struct RustGen<'a> {
    pub pack: &'a TargetPack,
    pub symbols: &'a [Symbol],
    pub types: &'a [TypeRecord],
}

/// Maps a C type name from a recovered prototype to a Rust type.
/// `None` means the type cannot be expressed faithfully — no binding is
/// emitted for that symbol.
fn map_rust_type(t: &str) -> Option<String> {
    let t = t.trim();
    if t.is_empty() {
        return Some("()".into());
    }
    match t {
        "void" => Some("()".into()),
        "int" => Some("i32".into()),
        "int32" | "int32_t" => Some("i32".into()),
        "int16" | "int16_t" => Some("i16".into()),
        "int8" | "int8_t" => Some("i8".into()),
        "uint32" | "uint32_t" => Some("u32".into()),
        "uint16" | "uint16_t" => Some("u16".into()),
        "uint8" | "uint8_t" => Some("u8".into()),
        "char" => Some("u8".into()),
        "size_t" | "mode_t" => Some("u32".into()),
        "uintptr_t" => Some("usize".into()),
        "const char *" => Some("*const u8".into()),
        "const void *" => Some("*const core::ffi::c_void".into()),
        "void *" => Some("*mut core::ffi::c_void".into()),
        // Typed pointers use the recursively mapped Rust spelling of their
        // pointee, so standard C primitives never leak into generated Rust
        // (`int *` becomes `*const i32`, not the invalid `*const int`).
        other => {
            if let Some(inner) = other.strip_suffix(" *") {
                let inner = inner.strip_prefix("const ").unwrap_or(inner);
                if inner.contains('*') {
                    return None; // nested pointers not yet expressible
                }
                let mapped = map_rust_type(inner)?;
                if mapped.starts_with('*') || mapped == "()" {
                    return None;
                }
                return Some(format!("*const {mapped}"));
            }
            if other.chars().all(|c| c.is_ascii_alphanumeric() || c == '_') {
                // A bare struct name by value.
                return Some(other.to_string());
            }
            None
        }
    }
}

/// Escapes a recovered C field name that collides with a Rust keyword (e.g.
/// the interconnect message `type` field) so the generated struct compiles.
fn rust_field_name(name: &str) -> String {
    if matches!(name, "type" | "ref" | "match" | "move" | "fn" | "impl") {
        format!("r#{name}")
    } else {
        name.to_string()
    }
}

fn width_rust_type(f: &crate::model::TypeField) -> String {
    match f.signedness.as_str() {
        "signed" => match f.width {
            1 => "i8",
            2 => "i16",
            4 => "i32",
            8 => "i64",
            _ => "i32",
        }
        .to_string(),
        "pointer" => "*mut core::ffi::c_void".to_string(),
        "array" => {
            let n = f.array_length.unwrap_or(f.width);
            format!("[u8; {n}]")
        }
        _ => match f.width {
            1 => "u8",
            2 => "u16",
            4 => "u32",
            8 => "u64",
            _ => "u32",
        }
        .to_string(),
    }
}

impl<'a> RustGen<'a> {
    /// Generates the full module text.
    /// CAN-P2-018: deterministic digest over the codegen inputs, emitted in
    /// the generated header for reproducible provenance.
    fn input_digest(&self) -> String {
        use std::collections::hash_map::DefaultHasher;
        use std::hash::{Hash, Hasher};
        let mut records: Vec<String> = self
            .symbols
            .iter()
            .map(|s| {
                format!(
                    "sym:{}:{}:{}:{}",
                    s.symbol_id,
                    s.policy,
                    s.status,
                    s.callable_address.as_deref().unwrap_or("")
                )
            })
            .collect();
        records.extend(
            self.types
                .iter()
                .map(|t| format!("type:{}:{}:{}", t.type_id, t.kind, t.size)),
        );
        records.sort();
        let mut h = DefaultHasher::new();
        records.hash(&mut h);
        format!("{:016x}", h.finish())
    }

    pub fn generate(&self) -> String {
        let mut out = String::new();

        out.push_str("// Generated by `canopus target generate-rust-bindings`. DO NOT EDIT.\n");
        out.push_str("//\n");
        out.push_str(&format!("// target_id: {}\n", self.pack.target_id));
        out.push_str(&format!(
            "// firmware : {} ({})\n",
            self.pack.firmware_version, self.pack.firmware_build
        ));
        out.push_str(&format!("// sha256   : {}\n", self.pack.firmware_sha256));
        out.push_str(&format!("// revision : {}\n", self.pack.revision));
        out.push_str(&format!("// input_digest: {}\n", self.input_digest()));
        out.push_str("//\n");
        out.push_str("// All firmware calls are `unsafe`; safe wrappers exist only\n");
        out.push_str("// where the ABI and ownership have been proven (architecture §12.1).\n");
        out.push('\n');
        out.push_str(
            "/// Converts a recovered Thumb entry or callable address into the only valid\n",
        );
        out.push_str(
            "/// indirect-call address. Target-private ABI adapters must use this instead\n",
        );
        out.push_str("/// of transmuting raw firmware addresses directly.\n");
        out.push_str("#[inline(always)]\n");
        out.push_str("pub const fn canopus_thumb_callable(entry_or_callable: usize) -> usize {\n");
        out.push_str("    entry_or_callable | 1usize\n");
        out.push_str("}\n\n");

        self.emit_types(&mut out);
        self.emit_identity(&mut out);
        self.emit_bindings(&mut out);
        self.emit_notes(&mut out);

        out
    }

    fn emit_types(&self, out: &mut String) {
        if self.types.is_empty() {
            return;
        }
        out.push_str("// ---- recovered type layouts ----\n");
        out.push_str("// `#[repr(C, packed(N))]` + explicit padding reproduces the exact\n");
        out.push_str("// recovered byte layout and its target-declared alignment.\n");
        for t in self.types {
            if t.kind == "typedef" {
                if let Some(proto) = &t.prototype {
                    if let Some((ret, args)) = parse_proto(proto) {
                        let ret_t = match self.resolve_rust_type(&ret) {
                            Some(t) => t,
                            None => continue,
                        };
                        let mut arg_r = Vec::new();
                        let mut mappable = true;
                        for a in &args {
                            match self.resolve_rust_type(a) {
                                Some(t) => arg_r.push(t),
                                None => {
                                    mappable = false;
                                    break;
                                }
                            }
                        }
                        if !mappable {
                            continue;
                        }
                        let name = t.name.clone().unwrap_or_else(|| t.type_id.clone());
                        let arg_list = if arg_r.is_empty() {
                            "()".to_string()
                        } else {
                            arg_r.join(", ")
                        };
                        out.push_str(&format!(
                            "pub type {name} = extern \"C\" fn({arg_list}) -> {ret_t};\n"
                        ));
                    }
                }
                continue;
            }
            if t.kind != "struct" && t.kind != "union" {
                continue;
            }
            let name = t.name.clone().unwrap_or_else(|| t.type_id.clone());
            let kw = if t.kind == "union" { "union" } else { "struct" };
            let total = t.size;
            out.push_str(&format!(
                "#[repr(C, packed({alignment}))]\n#[derive(Copy, Clone, Debug)]\npub {kw} {name} {{\n",
                alignment = t.alignment,
            ));
            let mut cursor = 0u64;
            let mut fields = t.fields.clone().unwrap_or_default();
            fields.sort_by_key(|f| f.offset);
            for f in &fields {
                if f.offset > cursor {
                    let gap = f.offset - cursor;
                    out.push_str(&format!(
                        "    pub _pad_{:x}: [u8; 0x{:x}], // {}\n",
                        cursor, gap, gap
                    ));
                    cursor += gap;
                }
                if f.offset < cursor {
                    out.push_str(&format!(
                        "    // skipped {}: overlaps at +0x{:x}\n",
                        f.name, f.offset
                    ));
                    continue;
                }
                out.push_str(&format!(
                    "    pub {}: {}, // +0x{:x}\n",
                    rust_field_name(&f.name),
                    width_rust_type(f),
                    f.offset
                ));
                cursor += f.width;
            }
            if total > cursor {
                let gap = total - cursor;
                out.push_str(&format!("    pub _tail: [u8; 0x{:x}], // {}\n", gap, gap));
            }
            out.push_str("}\n\n");
        }
    }

    fn emit_identity(&self, out: &mut String) {
        let ver = self
            .symbols
            .iter()
            .find(|s| s.name == "firmware_version_string" && s.entry_address.is_some());
        let build = self
            .symbols
            .iter()
            .find(|s| s.name == "firmware_build_string" && s.entry_address.is_some());

        out.push_str("// ---- runtime identity guard ----\n");
        match (ver, build) {
            (Some(v), Some(b)) => {
                let va = v.entry_address.as_deref().unwrap();
                let ba = b.entry_address.as_deref().unwrap();
                out.push_str("/// Returns 0 when the running firmware matches this pack's\n");
                out.push_str("/// version + build identity, -1 otherwise. Call once before use.\n");
                out.push_str("pub fn canopus_identity_guard() -> i32 {\n");
                out.push_str("    const EXPECT_VERSION: &[u8] = ");
                out.push_str(&format!("b\"{}\"", self.pack.firmware_version));
                out.push_str(";\n");
                out.push_str("    const EXPECT_BUILD: &[u8] = ");
                out.push_str(&format!("b\"{}\"", self.pack.firmware_build));
                out.push_str(";\n");
                out.push_str(&format!(
                    "    if !c_str_eq({va}, EXPECT_VERSION) {{ return -1; }}\n"
                ));
                out.push_str(&format!(
                    "    if !c_str_eq({ba}, EXPECT_BUILD) {{ return -1; }}\n"
                ));
                out.push_str("    0\n");
                out.push_str("}\n\n");
                out.push_str("/// Compares a NUL-terminated string at `addr` to `expected`.\n");
                out.push_str("fn c_str_eq(addr: usize, expected: &[u8]) -> bool {\n");
                out.push_str("    let mut i = 0usize;\n");
                out.push_str("    while i <= expected.len() && i < 64 {\n");
                out.push_str("        let b = unsafe { *((addr + i) as *const u8) };\n");
                out.push_str("        if i < expected.len() {\n");
                out.push_str("            if b != expected[i] { return false; }\n");
                out.push_str("        } else {\n");
                out.push_str("            return b == 0;\n");
                out.push_str("        }\n");
                out.push_str("        i += 1;\n");
                out.push_str("    }\n");
                out.push_str("    false\n");
                out.push_str("}\n\n");
            }
            _ => {
                out.push_str("// identity strings not present in this pack; guard unavailable\n\n");
            }
        }
    }

    /// Resolves a prototype argument/return type to a Rust type.
    ///
    /// Falls back to [`map_rust_type`] for primitive types, then recognizes
    /// recovered typedef names (emitted as `pub type` aliases) and typed
    /// pointers to recovered structs/unions. Returns `None` for anything that
    /// cannot be expressed faithfully so the caller skips the symbol.
    fn resolve_rust_type(&self, t: &str) -> Option<String> {
        let t = t.trim();
        // Typed pointer to a recovered type: `const X *` / `X *`. Handle this
        // before the primitive fallback so the `const` qualifier is dropped
        // (map_rust_type's suffix fallback would otherwise emit `*const const
        // X`, which is not valid Rust).
        if let Some((leading_const, inner, suffix)) = crate::veneer::pointer_chain(t) {
            if self.type_names().contains(&inner) {
                let _ = leading_const;
                // `X *` -> `*const X`; `X *const *` -> `*const *const X`
                // (outer pointer is the trailing `*`).
                let levels = suffix.matches('*').count();
                let stars = "*const ".repeat(levels);
                return Some(format!("{stars}{inner}"));
            }
        }
        if let Some(mapped) = map_rust_type(t) {
            return Some(mapped);
        }
        if self.type_names().contains(&t) {
            // By value (rare) or typedef alias — the name is emitted elsewhere.
            return Some(t.to_string());
        }
        None
    }

    fn type_names(&self) -> std::collections::BTreeSet<&str> {
        self.types
            .iter()
            .filter_map(|t| t.name.as_deref())
            .collect()
    }

    fn emit_bindings(&self, out: &mut String) {
        out.push_str("// ---- typed firmware bindings (unsafe) ----\n");
        for s in self.symbols {
            if s.kind == "global" || s.kind == "data" {
                // Every evidence-backed, non-forbidden data symbol gets a
                // mechanical address constant. Approval controls callable
                // wrappers, not whether target-private code can name a recovered
                // address through the generated source of truth.
                if FORBIDDEN_STATUS.contains(&s.status.as_str()) {
                    continue; // audit comment in emit_notes
                }
                if let Some(addr) = &s.entry_address {
                    let name = &s.name;
                    out.push_str(&format!(
                        "/// Recovered global `{name}` at {addr}.\npub const canopus_fw_{name}: usize = {addr}usize;\n\n"
                    ));
                }
                continue;
            }
            if s.kind != "function" {
                continue;
            }
            if FORBIDDEN_STATUS.contains(&s.status.as_str()) {
                continue; // audit comment in emit_notes
            }
            let callable = match &s.callable_address {
                Some(c) => c.clone(),
                None => continue,
            };
            let callable_name = format!("CANOPUS_FW_{}_CALLABLE", s.name.to_ascii_uppercase());
            out.push_str(&format!(
                "/// Recovered `{}` at {}. Thumb callable address {}.\n",
                s.name,
                s.entry_address.as_deref().unwrap_or("?"),
                callable
            ));
            out.push_str(&format!(
                "pub const {callable_name}: usize = canopus_thumb_callable({callable}usize);\n"
            ));

            // Constants are available to target-private ABI adapters once static
            // evidence exists. A typed callable wrapper remains gated by explicit
            // codegen approval and public policy.
            if s.policy == "restricted" || !s.approved_for_codegen() {
                out.push('\n');
                continue;
            }
            let proto = match &s.prototype {
                Some(p) if !p.is_empty() && p != "unknown" => p.clone(),
                _ => continue,
            };
            let (ret, args) = match parse_proto(&proto) {
                Some(x) => x,
                None => continue,
            };
            let ret_t = match self.resolve_rust_type(&ret) {
                Some(t) => t,
                None => {
                    out.push_str(&format!(
                        "// {}: skipped (return type '{}' not mappable)\n",
                        s.name, ret
                    ));
                    continue;
                }
            };
            let mut arg_r = Vec::new();
            let mut mappable = true;
            for a in &args {
                match self.resolve_rust_type(a) {
                    Some(t) => arg_r.push(t),
                    None => {
                        mappable = false;
                        break;
                    }
                }
            }
            if !mappable {
                out.push_str(&format!(
                    "// {}: skipped (argument type not mappable)\n",
                    s.name
                ));
                continue;
            }
            let fn_name = format!("canopus_fw_{}", s.name);
            let callable_name = format!("CANOPUS_FW_{}_CALLABLE", s.name.to_ascii_uppercase());
            let param_list = if arg_r.is_empty() {
                String::new()
            } else {
                (0..arg_r.len())
                    .map(|i| format!("a{i}: {}", arg_r[i]))
                    .collect::<Vec<_>>()
                    .join(", ")
            };
            let call_args = (0..arg_r.len())
                .map(|i| format!("a{i}"))
                .collect::<Vec<_>>()
                .join(", ");

            // The return type for () is spelled `()`; the extern fn pointer
            // type must be spelled exactly.
            let fn_ty = format!("extern \"C\" fn({}) -> {ret_t}", arg_r.join(", "));
            out.push_str(&format!(
                "#[allow(non_snake_case)]\n#[allow(clippy::missing_safety_doc)]\npub unsafe fn {fn_name}({param_list}) -> {ret_t} {{\n"
            ));
            out.push_str(&format!(
                "    let f: {fn_ty} = unsafe {{ core::mem::transmute({callable_name}) }};\n"
            ));
            if ret_t == "()" {
                out.push_str(&format!("    f({call_args});\n"));
            } else {
                out.push_str(&format!("    f({call_args})\n"));
            }
            out.push_str("}\n\n");
        }
    }

    fn emit_notes(&self, out: &mut String) {
        let mut skipped = BTreeSet::new();
        for s in self.symbols {
            if s.kind != "function" {
                continue;
            }
            if FORBIDDEN_STATUS.contains(&s.status.as_str()) {
                skipped.insert(format!(
                    "// {name}: {status} - no binding may ever be generated",
                    name = s.name,
                    status = s.status
                ));
            } else if s.policy == "restricted" {
                skipped.insert(format!(
                    "// {name}: restricted - not exported until context/ownership approved",
                    name = s.name
                ));
            } else if !s.approved_for_codegen() {
                skipped.insert(format!(
                    "// {name}: not APPROVED - no binding until approval_state=APPROVED with evidence",
                    name = s.name
                ));
            }
        }
        if !skipped.is_empty() {
            out.push_str("// ---- excluded symbols ----\n");
            for line in skipped {
                out.push_str(&line);
                out.push('\n');
            }
            out.push('\n');
        }
    }
}

#[cfg(test)]
mod tests {
    use super::map_rust_type;
    use crate::veneer::parse_proto;

    #[test]
    fn c_void_parameter_list_is_empty() {
        let (return_type, args) = parse_proto("int *(void)").unwrap();
        assert_eq!(return_type, "int *");
        assert!(args.is_empty());
    }

    #[test]
    fn primitive_c_pointers_use_rust_pointee_types() {
        assert_eq!(map_rust_type("int *").as_deref(), Some("*const i32"));
        assert_eq!(map_rust_type("int32_t *").as_deref(), Some("*const i32"));
        assert_eq!(map_rust_type("uint32_t *").as_deref(), Some("*const u32"));
    }
}
