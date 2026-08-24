//! C typed veneer and identity-guard generator (CAN-TGT-004/005).
//!
//! Reads a target pack's symbol and type records and emits a single
//! generated C header with:
//!   - typedefs for the recovered struct layouts (from types/);
//!   - a typed inline veneer per callable symbol (Thumb callable address
//!     baked in as a constant — no undefined imports);
//!   - an identity guard over the firmware version/build strings.
//!
//! FORBIDDEN / WITHDRAWN symbols never produce a veneer; they are emitted
//! only as comments so a reviewer can see they were deliberately excluded.

use crate::model::{Symbol, TargetPack, TypeField, TypeRecord};
use std::collections::BTreeSet;

pub const FORBIDDEN_STATUS: &[&str] = &["FORBIDDEN", "WITHDRAWN"];
pub const CALLABLE_POLICY: &[&str] = &["managed", "native-full-trust"];
pub const RESTRICTED_POLICY: &[&str] = &["restricted"];

pub struct VeneerGen<'a> {
    pub pack: &'a TargetPack,
    pub symbols: &'a [Symbol],
    pub types: &'a [TypeRecord],
}

/// Maps an ARM AAPCS C type name from a recovered prototype to a C type.
/// Returns None for types we cannot faithfully express; callers must not
/// emit a veneer then.
pub(crate) fn map_type(t: &str) -> Option<String> {
    let t = t.trim();
    if t.is_empty() {
        return Some("void".into());
    }
    match t {
        "void" => Some("void".into()),
        "int" => Some("int".into()),
        "int32" | "int32_t" => Some("int32_t".into()),
        "int64" | "int64_t" => Some("int64_t".into()),
        "int16" | "int16_t" => Some("int16_t".into()),
        "int8" | "int8_t" => Some("int8_t".into()),
        "uint32" | "uint32_t" => Some("uint32_t".into()),
        "uint16" | "uint16_t" => Some("uint16_t".into()),
        "uint8" | "uint8_t" => Some("uint8_t".into()),
        "char" => Some("char".into()),
        "size_t" => Some("uint32_t".into()),
        "mode_t" => Some("uint32_t".into()),
        "stock_timespec_t" => Some("stock_timespec_t".into()),
        "stock_timespec_t *" => Some("stock_timespec_t *".into()),
        "const char *" => Some("const char *".into()),
        "const void *" => Some("const void *".into()),
        "void *" => Some("void *".into()),
        "uintptr_t" => Some("uintptr_t".into()),
        _ => None,
    }
}

/// Parses `RET(ARG, ARG, ...)` into (ret, args), stripping any C parameter
/// name so `uint16_t app_id` becomes `uint16_t` and `void *object` becomes
/// `void *`. Function-pointer arguments (`void (*)(void *)`) are left intact.
pub(crate) fn parse_proto(p: &str) -> Option<(String, Vec<String>)> {
    let open = p.find('(')?;
    let close = p.rfind(')')?;
    if close <= open {
        return None;
    }
    let ret = p[..open].trim().to_string();
    let mut args: Vec<String> = p[open + 1..close]
        .split(',')
        .map(|s| strip_param_name(s.trim()))
        .filter(|s| !s.is_empty())
        .collect();
    // In a C prototype, a sole `void` means no parameters. It is not a unit
    // value argument and must not become `a0: ()` in generated Rust.
    if args.len() == 1 && args[0] == "void" {
        args.clear();
    }
    Some((ret, args))
}

/// Strips a trailing C parameter identifier from `uint16_t app_id` /
/// `void *object` / `const char *name`, leaving the type. Does nothing for
/// function-pointer types (`void (*)(void *)`) and untyped forms.
fn strip_param_name(arg: &str) -> String {
    let arg = arg.trim();
    if arg.is_empty() || arg == "void" || arg == "..." {
        return arg.to_string();
    }
    // Function-pointer args contain `(*)`; the name, if any, sits inside the
    // closing paren. Leave them untouched (callers resolve via typedef names).
    if arg.contains("(*") {
        return arg.to_string();
    }
    // A C parameter is `TYPE name` where `name` is the trailing identifier and
    // TYPE may itself contain spaces (`void *`, `const char *`, multi-level
    // pointers). Strip the trailing identifier and verify the remainder still
    // looks like a type. A bare scalar with no name (`uint32_t`, `int`) is
    // left untouched because its "identifier" occupies the whole string.
    if let Some((name_start, _name)) = last_identifier(arg) {
        let ty = arg[..name_start].trim();
        if is_plausible_c_type(ty) {
            return ty.to_string();
        }
    }
    arg.to_string()
}

/// Returns the byte index of the final identifier token and the identifier,
/// when the string ends with one (e.g. `void *object` -> (`5`, `object`)).
fn last_identifier(arg: &str) -> Option<(usize, &str)> {
    let bytes = arg.as_bytes();
    let mut end = bytes.len();
    while end > 0 && bytes[end - 1].is_ascii_whitespace() {
        end -= 1;
    }
    let mut start = end;
    while start > 0 && (bytes[start - 1].is_ascii_alphanumeric() || bytes[start - 1] == b'_') {
        start -= 1;
    }
    if start == end {
        return None;
    }
    // Must be preceded by a non-identifier boundary (whitespace or `*`), and
    // there must be a type prefix before it — a bare scalar (`uint32_t`) has
    // its whole string as the identifier with no prefix, so it is left alone.
    let before = if start == 0 {
        None
    } else {
        Some(bytes[start - 1])
    };
    if start == 0 || !matches!(before, Some(b' ') | Some(b'*')) {
        return None;
    }
    let name = &arg[start..end];
    if is_identifier(name) {
        Some((start, name))
    } else {
        None
    }
}

fn is_plausible_c_type(ty: &str) -> bool {
    ty.is_empty()
        || ty.contains('*')
        || ty.contains('[')
        || matches!(
            ty,
            "int"
                | "int8_t"
                | "int16_t"
                | "int32_t"
                | "int64_t"
                | "uint8_t"
                | "uint16_t"
                | "uint32_t"
                | "uint64_t"
                | "char"
                | "float"
                | "double"
                | "size_t"
                | "mode_t"
                | "void"
        )
        || ty.starts_with("const ")
        || ty.starts_with("enum ")
        || ty.starts_with("struct ")
        || ty.starts_with("union ")
}

fn is_identifier(s: &str) -> bool {
    let mut chars = s.chars();
    matches!(chars.next(), Some(c) if c.is_ascii_alphabetic() || c == '_')
        && chars.all(|c| c.is_ascii_alphanumeric() || c == '_')
}

/// Splits a typed pointer string into `(leading_const, inner_type, pointer_suffix)`.
/// Handles `X *`, `const X *`, `X *const *`, `const X *const *` where `X` is a
/// bare type name. Returns `None` when the string is not such a chain.
pub(crate) fn pointer_chain(t: &str) -> Option<(bool, &str, &str)> {
    let leading_const = t.starts_with("const ");
    let body = t.strip_prefix("const ").unwrap_or(t).trim();
    let star = body.find('*')?;
    let inner = body[..star].trim();
    if inner.is_empty() {
        return None;
    }
    // `inner` must be a single type token or a `struct`/`union`-prefixed name.
    if inner.split_whitespace().count() > 2 {
        return None;
    }
    Some((leading_const, inner, body[star..].trim()))
}

fn width_type(f: &TypeField) -> String {
    match f.signedness.as_str() {
        "signed" => match f.width {
            1 => "int8_t",
            2 => "int16_t",
            4 => "int32_t",
            8 => "int64_t",
            _ => "int32_t",
        }
        .to_string(),
        "pointer" => "void *".to_string(),
        "array" => {
            let n = f.array_length.unwrap_or(f.width);
            format!("uint8_t [{n}]")
        }
        _ => match f.width {
            1 => "uint8_t",
            2 => "uint16_t",
            4 => "uint32_t",
            8 => "uint64_t",
            _ => "uint32_t",
        }
        .to_string(),
    }
}

impl<'a> VeneerGen<'a> {
    /// CAN-P2-018: deterministic digest over the codegen inputs (symbol
    /// identity/policy/status/address and type identity/size), so a generated
    /// header carries reproducible provenance independent of the firmware hash.
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

    /// Generates the full header text.
    pub fn generate(&self) -> String {
        let id = &self.pack.target_id;
        let guard = id.to_uppercase().replace(['-', '.'], "_");
        let mut out = String::new();

        out.push_str("/*\n");
        out.push_str(" * Generated by `canopus target generate-veneer`. DO NOT EDIT.\n");
        out.push_str(&format!(" * target_id: {}\n", self.pack.target_id));
        out.push_str(&format!(
            " * firmware : {} ({})\n",
            self.pack.firmware_version, self.pack.firmware_build
        ));
        out.push_str(&format!(" * sha256   : {}\n", self.pack.firmware_sha256));
        out.push_str(&format!(" * revision : {}\n", self.pack.revision));
        out.push_str(&format!(" * input_digest: {}\n", self.input_digest()));
        out.push_str(" */\n");
        out.push_str("#ifndef CANOPUS_VENEER_");
        out.push_str(&guard);
        out.push_str("_H\n#define CANOPUS_VENEER_");
        out.push_str(&guard);
        out.push_str("_H\n\n");
        out.push_str("#include <stdint.h>\n\n");

        self.emit_types(&mut out);
        self.emit_identity(&mut out);
        self.emit_veneer_functions(&mut out);
        self.emit_notes(&mut out);

        out.push_str("\n#endif /* CANOPUS_VENEER_");
        out.push_str(&guard);
        out.push_str("_H */\n");
        out
    }

    fn emit_types(&self, out: &mut String) {
        if self.types.is_empty() {
            return;
        }
        out.push_str("/* ---- recovered type layouts ---- */\n");
        out.push_str("/* Explicit padding reproduces the exact recovered byte layout\n");
        out.push_str(" * even when fields are sparse (e.g. launcher_order_record). */\n");
        for t in self.types {
            if t.kind == "struct" || t.kind == "union" {
                let kw = if t.kind == "union" { "union" } else { "struct" };
                let name = t.name.clone().unwrap_or_else(|| t.type_id.clone());
                out.push_str(&format!("typedef {kw} {name} {name};\n"));
            }
        }
        for t in self.types {
            if t.kind == "typedef" {
                if let Some(proto) = &t.prototype {
                    if let Some((ret, args)) = parse_proto(proto) {
                        let ret_t = match map_type(&ret) {
                            Some(t) => t,
                            None => continue,
                        };
                        let mut arg_c = Vec::new();
                        let mut mappable = true;
                        for a in &args {
                            match self.resolve_c_type(a) {
                                Some(t) => arg_c.push(t),
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
                        let arg_list = if arg_c.is_empty() {
                            "void".to_string()
                        } else {
                            arg_c.join(", ")
                        };
                        out.push_str(&format!("typedef {} (*{})({});\n", ret_t, name, arg_list));
                    }
                }
                continue;
            }
            if t.kind != "struct" && t.kind != "union" {
                continue;
            }
            let kw = if t.kind == "union" { "union" } else { "struct" };
            let total = t.size;
            let name = t.name.clone().unwrap_or_else(|| t.type_id.clone());
            out.push_str(&format!("{kw} {name} {{\n"));
            let mut cursor = 0u64;
            let mut fields = t.fields.clone().unwrap_or_default();
            fields.sort_by_key(|f| f.offset);
            for f in &fields {
                if f.offset > cursor {
                    let gap = f.offset - cursor;
                    out.push_str(&format!("    uint8_t _pad_{:x}[{}];\n", cursor, gap));
                    cursor += gap;
                }
                if f.offset < cursor {
                    out.push_str(&format!(
                        "    /* skipped {}: overlaps at +0x{:x} */\n",
                        f.name, f.offset
                    ));
                    continue;
                }
                if f.signedness == "array" {
                    // C array syntax: the bracket suffix follows the name.
                    let n = f.array_length.unwrap_or(f.width);
                    out.push_str(&format!(
                        "    uint8_t {}[{}]; /* +0x{:x} */\n",
                        f.name, n, f.offset
                    ));
                } else {
                    out.push_str(&format!(
                        "    {} {}; /* +0x{:x} */\n",
                        width_type(f),
                        f.name,
                        f.offset
                    ));
                }
                cursor += f.width;
            }
            if total > cursor {
                out.push_str(&format!("    uint8_t _tail[{}];\n", total - cursor));
            }
            out.push_str("};\n");
        }
        out.push('\n');
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

        out.push_str("/* ---- runtime identity guard ---- */\n");
        out.push_str("static inline int canopus_str_neq(const char *a, const char *b)\n{\n");
        out.push_str("    while (*a && *b) { if (*a++ != *b++) return 1; }\n");
        out.push_str("    return *a != *b;\n}\n\n");
        match (ver, build) {
            (Some(v), Some(b)) => {
                let va = v.entry_address.as_deref().unwrap();
                let ba = b.entry_address.as_deref().unwrap();
                out.push_str("static inline int canopus_identity_guard(void)\n{\n");
                out.push_str(&format!(
                    "    const char *const expect_version = \"{}\";\n",
                    self.pack.firmware_version
                ));
                out.push_str(&format!(
                    "    const char *const expect_build = \"{}\";\n",
                    self.pack.firmware_build
                ));
                out.push_str(&format!(
                    "    const char *const actual_version = (const char *)(uintptr_t){};\n",
                    va
                ));
                out.push_str(&format!(
                    "    const char *const actual_build = (const char *)(uintptr_t){};\n",
                    ba
                ));
                out.push_str(
                    "    if (canopus_str_neq(actual_version, expect_version)) return -1;\n",
                );
                out.push_str("    if (canopus_str_neq(actual_build, expect_build)) return -1;\n");
                out.push_str("    return 0;\n}\n\n");
            }
            (Some(v), None) if self.pack.loader != "nuttx-modlib-elf32-rel" => {
                let va = v.entry_address.as_deref().unwrap();
                out.push_str("static inline int canopus_identity_guard(void)\n{\n");
                out.push_str(&format!(
                    "    const char *const expect_version = \"{}\";\n",
                    self.pack.firmware_version
                ));
                out.push_str(&format!(
                    "    const char *const actual_version = (const char *)(uintptr_t){};\n",
                    va
                ));
                out.push_str(
                    "    return canopus_str_neq(actual_version, expect_version) ? -1 : 0;\n}\n\n",
                );
            }
            _ => {
                out.push_str(
                    "/* identity strings not present in this pack; guard unavailable */\n\n",
                );
            }
        }
    }

    /// Resolves a prototype argument/return type to a C type.
    ///
    /// Falls back to [`map_type`] for the primitive types it knows, then
    /// recognizes recovered typedef names and typed pointers to recovered
    /// structs/unions (e.g. `canopus_interconnect_recv_cb`,
    /// `const canopus_interconnect_message *`). Returns `None` when the type
    /// cannot be expressed faithfully so the caller skips the symbol.
    fn resolve_c_type(&self, t: &str) -> Option<String> {
        let t = t.trim();
        if let Some(mapped) = map_type(t) {
            return Some(mapped);
        }
        // Typed pointer chain to a recovered type: `const X *`, `X *const *`,
        // `const X *const *`. Reconstruct the exact pointer decoration so the
        // generated call site matches the firmware ABI.
        if let Some((leading_const, inner, suffix)) = pointer_chain(t) {
            if self.type_names().contains(&inner) {
                let const_pref = if leading_const { "const " } else { "" };
                return Some(format!("{const_pref}{inner} {suffix}"));
            }
            return None;
        }
        // Bare recovered type name used by value (struct, union, typedef).
        if self.type_names().contains(&t) {
            return Some(t.to_string());
        }
        None
    }

    /// The set of recovered type names emitted by this pack.
    fn type_names(&self) -> std::collections::BTreeSet<&str> {
        self.types
            .iter()
            .filter_map(|t| t.name.as_deref())
            .collect()
    }

    fn emit_veneer_functions(&self, out: &mut String) {
        out.push_str("/* ---- typed veneers ---- */\n");
        for s in self.symbols {
            if s.kind == "global" || s.kind == "data" {
                // A fixed firmware data address, emitted as a constant so C
                // modules can read the slot (e.g. the connection-framework
                // loop handle) through the same generated surface.
                if s.status == "FORBIDDEN"
                    || s.status == "WITHDRAWN"
                    || s.policy == "restricted"
                    || !s.approved_for_codegen()
                {
                    continue; // handled in notes
                }
                if let Some(addr) = &s.entry_address {
                    let name = &s.name;
                    let proto = s.prototype.as_deref().unwrap_or("void *");
                    let mut t = proto.trim();
                    if t.is_empty() || t == "void" {
                        t = "void *";
                    }
                    let ctype = self
                        .resolve_c_type(t)
                        .unwrap_or_else(|| "void *".to_string());
                    out.push_str(&format!(
                        "/* Recovered global `{name}` at {addr}. */\n#define canopus_fw_{name} (({ctype})(uintptr_t){addr}u)\n\n"
                    ));
                }
                continue;
            }
            if s.kind != "function" {
                continue;
            }
            if s.status == "FORBIDDEN" || s.status == "WITHDRAWN" {
                continue; // handled in notes
            }
            if s.policy == "restricted" {
                continue; // handled in notes
            }
            /* CAN-P1-012: a callable requires an explicit APPROVED promotion
             * with at least one evidence id. PENDING/REJECTED never emits. */
            if !s.approved_for_codegen() {
                out.push_str(&format!(
                    "/* {}: not APPROVED (approval_state={}, evidence={}) - no veneer */\n",
                    s.name,
                    s.approval_state.as_deref().unwrap_or("PENDING"),
                    s.proof.evidence_ids.as_ref().map(|e| e.len()).unwrap_or(0)
                ));
                continue;
            }
            let callable = match &s.callable_address {
                Some(c) => c.clone(),
                None => continue,
            };
            let proto = match &s.prototype {
                Some(p) if !p.is_empty() && p != "unknown" => p.clone(),
                _ => continue, // cannot type the call safely
            };
            let (ret, args) = match parse_proto(&proto) {
                Some(x) => x,
                None => continue,
            };
            let ret_t = match self.resolve_c_type(&ret) {
                Some(t) => t,
                None => {
                    out.push_str(&format!(
                        "/* {}: skipped (return type '{}' not mappable) */\n",
                        s.name, ret
                    ));
                    continue;
                }
            };
            let mut arg_c = Vec::new();
            let mut mappable = true;
            for a in &args {
                match self.resolve_c_type(a) {
                    Some(t) => arg_c.push(t),
                    None => {
                        mappable = false;
                        break;
                    }
                }
            }
            if !mappable {
                out.push_str(&format!(
                    "/* {}: skipped (argument type not mappable) */\n",
                    s.name
                ));
                continue;
            }
            let arg_list = if arg_c.is_empty() {
                "void".to_string()
            } else {
                arg_c.join(", ")
            };
            let param_list: Vec<String> = arg_c
                .iter()
                .enumerate()
                .map(|(i, t)| format!("{t} a{i}"))
                .collect();
            let params = if param_list.is_empty() {
                "void".to_string()
            } else {
                param_list.join(", ")
            };
            let call_args = if arg_c.is_empty() {
                String::new()
            } else {
                (0..arg_c.len())
                    .map(|i| format!("a{i}"))
                    .collect::<Vec<_>>()
                    .join(", ")
            };
            let fn_name = format!("canopus_fw_{}", s.name);
            out.push_str(&format!(
                "typedef {} (*{}_fn)({});\n",
                ret_t, fn_name, arg_list
            ));
            out.push_str(&format!(
                "static inline {} {}({}) {{\n",
                ret_t, fn_name, params
            ));
            out.push_str(&format!(
                "    return (({}_fn)(uintptr_t){})({});\n",
                fn_name, callable, call_args
            ));
            out.push_str("}\n\n");
        }
    }

    fn emit_notes(&self, out: &mut String) {
        let mut skipped = BTreeSet::new();
        for s in self.symbols {
            if s.kind != "function" {
                continue;
            }
            if s.status == "FORBIDDEN" || s.status == "WITHDRAWN" {
                skipped.insert(format!(
                    "{}: {} - no veneer may ever be generated",
                    s.name, s.status
                ));
            } else if s.policy == "restricted" {
                skipped.insert(format!(
                    "{}: restricted - not exported until context/ownership approved",
                    s.name
                ));
            } else if !s.approved_for_codegen() {
                skipped.insert(format!(
                    "{}: not APPROVED - no veneer until approval_state=APPROVED with evidence",
                    s.name
                ));
            }
        }
        if !skipped.is_empty() {
            out.push_str("/* ---- excluded symbols ----\n");
            for line in skipped {
                out.push_str(&format!(" * {line}\n"));
            }
            out.push_str(" */\n\n");
        }
    }
}

/// Loads symbols and types from a target pack directory's `symbols/` and
/// `types/` folders.
pub fn load_records(
    root: &std::path::Path,
) -> crate::error::Result<(Vec<Symbol>, Vec<TypeRecord>)> {
    let mut syms = Vec::new();
    let mut types = Vec::new();

    let sym_dir = root.join("symbols");
    if sym_dir.is_dir() {
        for entry in std::fs::read_dir(&sym_dir)? {
            let p = entry?.path();
            if p.extension().and_then(|e| e.to_str()) != Some("json") {
                continue;
            }
            let text = std::fs::read_to_string(&p)?;
            syms.push(serde_json::from_str(&text)?);
        }
    }

    let type_dir = root.join("types");
    if type_dir.is_dir() {
        for entry in std::fs::read_dir(&type_dir)? {
            let p = entry?.path();
            if p.extension().and_then(|e| e.to_str()) != Some("json") {
                continue;
            }
            let text = std::fs::read_to_string(&p)?;
            types.push(serde_json::from_str(&text)?);
        }
    }

    Ok((syms, types))
}
