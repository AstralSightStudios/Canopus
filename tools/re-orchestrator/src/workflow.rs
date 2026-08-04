//! Evidence workflows (CAN-RE-003/004/005).
//!
//! Builds structured evidence bundles for functions, types/layouts and
//! signature candidates. All of this is pure host-side analysis — nothing is
//! executed and no firmware is patched.

use serde::{Deserialize, Serialize};

/// Function evidence bundle (CAN-RE-003): decompile + xrefs + callgraph.
#[derive(Debug, Clone, Default, Serialize, Deserialize)]
pub struct FunctionEvidence {
    pub address: String,
    pub symbol_name: Option<String>,
    pub decompile: Option<String>,
    pub disasm: Vec<String>,
    pub xrefs_to: Vec<String>,
    pub callers: Vec<String>,
    pub callees: Vec<String>,
    pub strings: Vec<String>,
}

/// Type/layout evidence bundle (CAN-RE-004): caller/callee + field layout.
#[derive(Debug, Clone, Default, Serialize, Deserialize)]
pub struct TypeEvidence {
    pub type_name: String,
    pub size: Option<u64>,
    pub alignment: Option<u64>,
    pub fields: Vec<Field>,
    pub used_by: Vec<String>, // functions that reference the type
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Field {
    pub name: String,
    pub offset: u64,
    pub width: u64,
    pub signedness: String,
}

/// Signature candidates (CAN-RE-005): only candidates, never executed.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct SignatureCandidate {
    pub function: String,
    pub prototype: String,
    pub source: String, // which decompile/xref evidence produced it
    pub confidence: u8, // 0..100, reviewer-assigned, never auto-asserted
}

/// Renders a function evidence bundle as a human-readable record that can be
/// embedded into the evidence store's `summary` / `artifact_uris`.
pub fn render_function_evidence(e: &FunctionEvidence) -> String {
    let mut out = String::new();
    out.push_str(&format!("## function evidence {}\n\n", e.address));
    if let Some(name) = &e.symbol_name {
        out.push_str(&format!("name: `{name}`\n\n"));
    }
    if let Some(d) = &e.decompile {
        out.push_str("### decompile\n```c\n");
        out.push_str(d);
        out.push_str("\n```\n\n");
    }
    out.push_str("### xrefs\n");
    for x in &e.xrefs_to {
        out.push_str(&format!("- {x}\n"));
    }
    out.push_str("\n### callers\n");
    for c in &e.callers {
        out.push_str(&format!("- {c}\n"));
    }
    out.push_str("\n### callees\n");
    for c in &e.callees {
        out.push_str(&format!("- {c}\n"));
    }
    if !e.strings.is_empty() {
        out.push_str("\n### strings\n");
        for s in &e.strings {
            out.push_str(&format!("- `{s}`\n"));
        }
    }
    out
}

/// Assembles a function bundle from raw IDA-returned pieces.
pub fn assemble_function_bundle(
    address: &str,
    symbol_name: Option<String>,
    decompile: Option<String>,
    disasm: Vec<String>,
    xrefs_to: Vec<String>,
    callers: Vec<String>,
    callees: Vec<String>,
    strings: Vec<String>,
) -> FunctionEvidence {
    FunctionEvidence {
        address: address.to_string(),
        symbol_name,
        decompile,
        disasm,
        xrefs_to,
        callers,
        callees,
        strings,
    }
}

/// Extracts signature candidates from decompiled text (CAN-RE-005).
///
/// Heuristic only: looks for a `RET name(ARGS)` signature line. The candidate
/// is never executed; a human reviewer must confirm it (RE-007). Returns
/// candidates with a low default confidence so nothing is auto-promoted.
pub fn signature_candidates_from_decompile(decompile: &str) -> Vec<SignatureCandidate> {
    let mut out = Vec::new();
    for line in decompile.lines() {
        let line = line.trim();
        if line.is_empty() || line.starts_with("//") || line.starts_with('{') {
            continue;
        }
        // Hext (often `__int64 __cdecl name(args) {`) — take up to the first `{`.
        let sig = line.split('{').next().unwrap_or(line).trim();
        if !sig.contains('(') || !sig.ends_with(')') {
            continue;
        }
        // Find the identifier right before '(' as the function name.
        let open = sig.rfind('(').unwrap();
        let before = sig[..open].trim();
        let name = before.split_whitespace().last().unwrap_or("");
        if name.is_empty() || name == "return" || name.starts_with('*') {
            continue;
        }
        let args = sig[open + 1..].trim_end_matches(')').trim();
        // Normalize empty args to "void".
        let args_norm = if args.is_empty() { "void".to_string() } else { args.to_string() };
        let ret = if name.is_empty() { "int".into() } else { before.split_whitespace().next().unwrap_or("int").to_string() };
        out.push(SignatureCandidate {
            function: name.to_string(),
            prototype: format!("{ret}({args_norm})"),
            source: "decompile signature line".into(),
            confidence: 10, // low: never auto-promote
        });
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn function_bundle_renders() {
        let e = assemble_function_bundle(
            "0x1234",
            Some("foo".into()),
            Some("int foo(int a) { return a; }".into()),
            vec!["MOV R0, #1".into()],
            vec!["0x2000 calls 0x1234".into()],
            vec!["0x3000".into()],
            vec!["0x4000".into()],
            vec!["hello".into()],
        );
        let text = render_function_evidence(&e);
        assert!(text.contains("## function evidence 0x1234"));
        assert!(text.contains("name: `foo`"));
        assert!(text.contains("0x2000 calls 0x1234"));
        assert!(text.contains("hello"));
    }

    #[test]
    fn signature_candidates_parsed() {
        let src = "int __cdecl app_launcher_add(app_record_t *a1)\n{\n  return 0;\n}\n";
        let cands = signature_candidates_from_decompile(src);
        assert_eq!(cands.len(), 1);
        assert_eq!(cands[0].function, "app_launcher_add");
        assert_eq!(cands[0].prototype, "int(app_record_t *a1)");
        assert_eq!(cands[0].confidence, 10);
    }

    #[test]
    fn signature_candidates_skip_bodies() {
        let src = "{\n  int x = 0;\n  return x;\n}\n";
        assert!(signature_candidates_from_decompile(src).is_empty());
    }

    #[test]
    fn signature_candidates_empty_args_void() {
        let src = "int data_init(void)\n{\n return 0;\n}\n";
        let cands = signature_candidates_from_decompile(src);
        assert_eq!(cands.len(), 1);
        assert_eq!(cands[0].function, "data_init");
        assert_eq!(cands[0].prototype, "int(void)");
    }
}
