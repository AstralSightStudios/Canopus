//! Read-only IDA MCP adapter (CAN-RE-002).
//!
//! RE may only invoke a fixed allowlist of read-only IDA tools. Any write,
//! patch, or rename tool is refused with the reason recorded in the audit
//! log. This is the enforcement point for "默认禁止的自动操作"
//! (architecture §9.3): the orchestrator itself never patches firmware.

use std::collections::BTreeSet;

/// Read-only IDA tools the orchestrator may call. Names follow the ida-pro
/// idalib MCP server's tools.
pub const READ_TOOLS: &[&str] = &[
    "survey_binary",
    "list_funcs",
    "func_query",
    "func_profile",
    "analyze_function",
    "analyze_component",
    "analyze_batch",
    "decompile",
    "disasm",
    "basic_blocks",
    "stack_frame",
    "callgraph",
    "callers",
    "callees",
    "xref_query",
    "xrefs_to",
    "xrefs_to_field",
    "find_xref_signatures",
    "trace_data_flow",
    "imports_query",
    "imports",
    "list_globals",
    "get_global_value",
    "find",
    "find_bytes",
    "find_regex",
    "search_text",
    "get_bytes",
    "get_int",
    "get_string",
    "read_struct",
    "entity_query",
    "lookup_funcs",
    "make_signature",
    "make_signature_for_function",
    "type_query",
    "type_inspect",
    "search_structs",
    "int_convert",
];

/// Tools that mutate the database or firmware. Never allowed.
pub const WRITE_TOOLS: &[&str] = &[
    "patch",
    "patch_asm",
    "put_int",
    "rename",
    "set_type",
    "type_apply_batch",
    "declare_type",
    "declare_stack",
    "delete_stack",
    "enum_upsert",
    "set_comments",
    "append_comments",
    "define_code",
    "define_func",
    "undefine",
    "make_data",
    "set_op_type",
    "force_recompile",
    "idb_save",
    "infer_types",
];

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum IdaDecision {
    /// Allowed read-only invocation.
    Allowed,
    /// Refused: the tool is a known write/patch tool.
    DeniedWriteTool { reason: &'static str },
    /// Refused: unknown tool (not in either table — fail closed).
    DeniedUnknown,
}

/// Decides whether `tool` may be invoked by the orchestrator. Fail-closed:
/// anything not in [`READ_TOOLS`] is refused.
pub fn decide(tool: &str) -> IdaDecision {
    if READ_TOOLS.contains(&tool) {
        return IdaDecision::Allowed;
    }
    if WRITE_TOOLS.contains(&tool) {
        return IdaDecision::DeniedWriteTool {
            reason: "write/patch tool is not in the read-only allowlist",
        };
    }
    IdaDecision::DeniedUnknown
}

/// A single audited IDA invocation (append-only log, CAN-RE-002).
#[derive(Debug, Clone, serde::Serialize, serde::Deserialize)]
pub struct IdaCall {
    pub tool: String,
    pub args: String,
    pub decision: String, // "allowed" | "denied:write" | "denied:unknown"
    pub at: String,
}

/// Records a tool call decision in the audit log. Write/unknown calls are
/// recorded as denials (they must not silently disappear).
pub fn audit_call(call: IdaCall, log: &mut Vec<IdaCall>) {
    log.push(call);
}

/// Renders the audit log as text (for the CLI / evidence bundle).
pub fn render_audit(log: &[IdaCall]) -> String {
    let mut out = String::new();
    for c in log {
        out.push_str(&format!("{} {} {}({})\n", c.at, c.decision, c.tool, c.args));
    }
    out
}

pub fn read_toolset() -> BTreeSet<&'static str> {
    READ_TOOLS.iter().copied().collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn read_tools_allowed() {
        for t in ["decompile", "xrefs_to", "disasm", "callgraph", "search_text", "find_bytes", "get_bytes", "survey_binary"] {
            assert_eq!(decide(t), IdaDecision::Allowed, "{t}");
        }
    }

    #[test]
    fn write_tools_denied() {
        for t in ["patch", "patch_asm", "rename", "set_type", "put_int", "idb_save", "make_data"] {
            match decide(t) {
                IdaDecision::DeniedWriteTool { .. } => {}
                other => panic!("{t}: expected deny, got {other:?}"),
            }
        }
    }

    #[test]
    fn unknown_tools_fail_closed() {
        assert_eq!(decide("totally_made_up"), IdaDecision::DeniedUnknown);
    }

    #[test]
    fn allow_and_write_disjoint() {
        for r in READ_TOOLS {
            assert!(!WRITE_TOOLS.contains(r), "{r} in both tables");
        }
    }

    #[test]
    fn audit_roundtrip() {
        let mut log = Vec::new();
        audit_call(
            IdaCall {
                tool: "decompile".into(),
                args: "0x1234".into(),
                decision: "allowed".into(),
                at: "t0".into(),
            },
            &mut log,
        );
        audit_call(
            IdaCall {
                tool: "patch".into(),
                args: "0x0".into(),
                decision: "denied:write".into(),
                at: "t1".into(),
            },
            &mut log,
        );
        let text = render_audit(&log);
        assert!(text.contains("allowed decompile"));
        assert!(text.contains("denied:write patch"));
    }
}
