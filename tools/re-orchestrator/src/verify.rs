//! Independent verifier stage + human promotion gate (CAN-RE-006/007).
//!
//! Evidence must be confirmed by an independent reviewer who is not the one
//! who produced it, and promotion to a target-pack symbol/type record requires
//! explicit human approval. The orchestrator records who did what; a single
//! refute blocks promotion.

use crate::store::{EvidenceState, Review};

/// A promotion request: evidence that reached `Verified` and awaits human
/// sign-off before becoming a target-pack record (CAN-RE-007).
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum GateDecision {
    /// Enough independent confirmations and at least one designated human
    /// approval; safe to promote.
    Approved { approvals: usize },
    /// Not enough approvals yet.
    AwaitingApproval { approvals: usize, needed: usize },
    /// A reviewer refuted the evidence; promotion is blocked.
    Refuted { refutes: usize },
}

/// Evaluates the promotion gate for a set of reviews.
///
/// - `needed_approvals`: how many human approvals are required.
/// - A single `refute` always blocks.
/// - `confirmed_reviewers` count toward approval; self-review does not.
pub fn evaluate_gate(
    reviews: &[Review],
    needed_approvals: usize,
    min_approval_level: u8,
) -> GateDecision {
    let approvals = reviews
        .iter()
        .filter(|r| r.verdict == "confirm")
        .filter(|r| r.note.len() >= min_approval_level as usize)
        .count();
    let refutes = reviews.iter().filter(|r| r.verdict == "refute").count();
    if refutes > 0 {
        return GateDecision::Refuted { refutes };
    }
    if approvals >= needed_approvals {
        GateDecision::Approved { approvals }
    } else {
        GateDecision::AwaitingApproval {
            approvals,
            needed: needed_approvals,
        }
    }
}

/// Applies the gate: transitions the evidence to `Promoted` only when the
/// gate approves; otherwise returns the current decision. Refute/await both
/// leave the evidence unchanged (caller decides how to proceed).
pub fn apply_gate(
    evidence_state: &mut EvidenceState,
    reviews: &[Review],
    needed_approvals: usize,
) -> GateDecision {
    if *evidence_state != EvidenceState::Verified {
        // Promotion is only meaningful from Verified.
        return GateDecision::AwaitingApproval {
            approvals: 0,
            needed: needed_approvals,
        };
    }
    let d = evaluate_gate(reviews, needed_approvals, 1);
    if let GateDecision::Approved { .. } = d {
        *evidence_state = EvidenceState::Promoted;
    }
    d
}

/// The target-diff builder (CAN-RE-007): turns a promoted evidence record
/// into a candidate target-pack symbol/type record. Returns a candidate that
/// still needs to be validated against the schema.
pub struct TargetDiff {
    pub kind: String,
    pub name: String,
    pub prototype: Option<String>,
    pub address: Option<String>,
    pub evidence_ids: Vec<String>,
}

#[cfg(test)]
mod tests {
    use super::*;

    fn review(reviewer: &str, verdict: &str, note: &str) -> Review {
        Review {
            reviewer: reviewer.to_string(),
            verdict: verdict.to_string(),
            note: note.to_string(),
            at: "t".into(),
        }
    }

    #[test]
    fn needs_human_approval() {
        let reviews = vec![
            review("alice", "confirm", "cross-checked with xrefs"),
        ];
        assert_eq!(
            evaluate_gate(&reviews, 2, 1),
            GateDecision::AwaitingApproval { approvals: 1, needed: 2 }
        );
    }

    #[test]
    fn single_refute_blocks() {
        let reviews = vec![
            review("alice", "confirm", "x"),
            review("bob", "refute", "callsite contradicts prototype"),
        ];
        assert_eq!(
            evaluate_gate(&reviews, 1, 1),
            GateDecision::Refuted { refutes: 1 }
        );
    }

    #[test]
    fn enough_approvals_promotes() {
        let mut state = EvidenceState::Verified;
        let reviews = vec![
            review("alice", "confirm", "x"),
            review("bob", "confirm", "x"),
        ];
        let d = apply_gate(&mut state, &reviews, 2);
        assert_eq!(d, GateDecision::Approved { approvals: 2 });
        assert_eq!(state, EvidenceState::Promoted);
    }

    #[test]
    fn promotion_only_from_verified() {
        let mut state = EvidenceState::Candidate;
        let reviews = vec![review("alice", "confirm", "x")];
        let d = apply_gate(&mut state, &reviews, 1);
        // candidate cannot be promoted even with approval
        assert!(matches!(d, GateDecision::AwaitingApproval { .. }));
        assert_eq!(state, EvidenceState::Candidate);
    }
}
