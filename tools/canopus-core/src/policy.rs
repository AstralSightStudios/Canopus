//! Cross-record policy checks (CAN-CLI-003).
//!
//! These complement JSON Schema validation with checks that span fields and
//! records: callability policy vs evidence status, DEVICE_PROVEN provenance
//! requirements, and blocking-context rules.

use crate::model::Symbol;

/// Consistency warnings/errors for a single symbol record.
///
/// Returns a list of human-readable findings. An empty list means the record
/// passes all policy checks.
pub fn symbol_policy_check(sym: &Symbol) -> Vec<String> {
    let mut out = Vec::new();

    // A symbol that is forbidden/withdrawn must never be promoted to a
    // callable policy.
    match sym.status.as_str() {
        "FORBIDDEN" => {
            if sym.policy == "managed" || sym.policy == "native-full-trust" {
                out.push(format!(
                    "{}: status FORBIDDEN but policy '{}' would permit a veneer",
                    sym.symbol_id, sym.policy
                ));
            }
        }
        "WITHDRAWN" => {
            if sym.provenance.withdrawal_reason.is_none() {
                out.push(format!(
                    "{}: status WITHDRAWN without withdrawal_reason (history must be permanent)",
                    sym.symbol_id
                ));
            }
            if sym.policy != "withdrawn" {
                out.push(format!(
                    "{}: status WITHDRAWN but policy is '{}' (must be 'withdrawn')",
                    sym.symbol_id, sym.policy
                ));
            }
        }
        "DEVICE_PROVEN" => {
            let proven = sym
                .proof
                .device
                .as_deref()
                .is_some_and(|d| d == "proven");
            let has_evidence = sym
                .proof
                .evidence_ids
                .as_ref()
                .is_some_and(|ids| !ids.is_empty());
            if !proven {
                out.push(format!(
                    "{}: status DEVICE_PROVEN but proof.device is not 'proven'",
                    sym.symbol_id
                ));
            }
            if !has_evidence {
                out.push(format!(
                    "{}: status DEVICE_PROVEN requires evidence_ids",
                    sym.symbol_id
                ));
            }
        }
        _ => {}
    }

    // Blocking calls must not be allowed from interrupt contexts.
    if let Some(ctx) = &sym.contexts {
        if ctx.blocking == Some(true) {
            if let Some(allowed) = &ctx.allowed {
                if allowed.iter().any(|a| a.contains("isr") || a.contains("ISR")) {
                    out.push(format!(
                        "{}: blocking call allowed in ISR context",
                        sym.symbol_id
                    ));
                }
            }
        }
    }

    // Functions on a thumb target require a callable address with the
    // Thumb bit set.
    if sym.kind == "function" && sym.instruction_set == "thumb" {
        if let Some(callable) = &sym.callable_address {
            let num = u64::from_str_radix(callable.trim_start_matches("0x"), 16);
            match num {
                Ok(n) if (n & 1) == 0 => out.push(format!(
                    "{}: callable_address {} lacks Thumb bit (bit 0)",
                    sym.symbol_id, callable
                )),
                _ => {}
            }
        }
    }

    out
}
