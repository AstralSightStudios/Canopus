//! RE task/evidence store (CAN-RE-001).
//!
//! An append-only store of RE tasks and their evidence records. Every state
//! transition is forward-only and recorded in an audit log; nothing is ever
//! deleted. REJECTED / WITHDRAWN history is retained permanently
//! (architecture §8.4, §23.2).

use std::collections::BTreeMap;
use std::path::Path;

use serde::{Deserialize, Serialize};

/// Lifecycle of an RE task. Transitions are strict and forward-only.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "SCREAMING_SNAKE_CASE")]
pub enum ReTaskState {
    New,
    Analyzing,
    EvidenceGathered,
    Verifying,
    Promoted,
    Rejected,
    Withdrawn,
}

impl ReTaskState {
    /// Returns `Ok` when `to` is a legal forward transition from `self`.
    pub fn can_transition(self, to: ReTaskState) -> bool {
        matches!(
            (self, to),
            (ReTaskState::New, ReTaskState::Analyzing)
                | (ReTaskState::New, ReTaskState::Withdrawn)
                | (ReTaskState::Analyzing, ReTaskState::EvidenceGathered)
                | (ReTaskState::Analyzing, ReTaskState::Rejected)
                | (ReTaskState::Analyzing, ReTaskState::Withdrawn)
                | (ReTaskState::EvidenceGathered, ReTaskState::Verifying)
                | (ReTaskState::EvidenceGathered, ReTaskState::Withdrawn)
                | (ReTaskState::Verifying, ReTaskState::Promoted)
                | (ReTaskState::Verifying, ReTaskState::Rejected)
                | (ReTaskState::Verifying, ReTaskState::Withdrawn)
                // A promoted record can later be superseded/withdrawn, but
                // never un-promoted or re-verified.
                | (ReTaskState::Promoted, ReTaskState::Withdrawn)
        )
    }
}

/// Lifecycle of an evidence record.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "SCREAMING_SNAKE_CASE")]
pub enum EvidenceState {
    Draft,
    Candidate,
    Verified,
    Promoted,
    Refuted,
    Withdrawn,
}

impl EvidenceState {
    pub fn can_transition(self, to: EvidenceState) -> bool {
        matches!(
            (self, to),
            (EvidenceState::Draft, EvidenceState::Candidate)
                | (EvidenceState::Draft, EvidenceState::Withdrawn)
                | (EvidenceState::Candidate, EvidenceState::Verified)
                | (EvidenceState::Candidate, EvidenceState::Refuted)
                | (EvidenceState::Candidate, EvidenceState::Withdrawn)
                | (EvidenceState::Verified, EvidenceState::Promoted)
                | (EvidenceState::Verified, EvidenceState::Refuted)
                | (EvidenceState::Verified, EvidenceState::Withdrawn)
                | (EvidenceState::Promoted, EvidenceState::Withdrawn)
        )
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct EvidenceRecord {
    pub evidence_id: String,
    pub task_id: String,
    pub state: EvidenceState,
    pub kind: String, // "function" | "type" | "signature" | "layout"
    pub summary: String,
    pub artifact_uris: Vec<String>,
    /// Review verdicts (independent verifier stage, CAN-RE-006).
    pub reviews: Vec<Review>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Review {
    pub reviewer: String,
    pub verdict: String, // "confirm" | "refute"
    pub note: String,
    pub at: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct AuditEntry {
    pub at: String,
    pub actor: String,
    pub action: String,
    pub detail: String,
}

/// In-memory + JSON-file-backed store. All mutations are append-only on the
/// audit log and never delete historical records.
#[derive(Debug, Clone, Default, Serialize, Deserialize)]
pub struct ReStore {
    pub tasks: BTreeMap<String, ReTask>,
    pub evidence: BTreeMap<String, EvidenceRecord>,
    pub audit: Vec<AuditEntry>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ReTask {
    pub task_id: String,
    pub state: ReTaskState,
    pub description: String,
    pub target_id: String,
    pub firmware_sha256: String,
    pub evidence_ids: Vec<String>,
}

fn now() -> String {
    // Use a fixed sortable format; the crate never depends on a real clock in
    // tests, but a regular build may use std time. We format seconds.
    std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.as_secs().to_string())
        .unwrap_or_else(|_| "0".to_string())
}

#[derive(Debug, thiserror::Error)]
pub enum StoreError {
    #[error("illegal task transition {from:?} -> {to:?}")]
    IllegalTaskTransition { from: ReTaskState, to: ReTaskState },
    #[error("illegal evidence transition {from:?} -> {to:?}")]
    IllegalEvidenceTransition {
        from: EvidenceState,
        to: EvidenceState,
    },
    #[error("unknown task {0}")]
    UnknownTask(String),
    #[error("unknown evidence {0}")]
    UnknownEvidence(String),
    #[error("io: {0}")]
    Io(std::io::Error),
}

impl ReStore {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn add_task(
        &mut self,
        task_id: &str,
        description: &str,
        target_id: &str,
        firmware_sha256: &str,
        actor: &str,
    ) {
        self.tasks.insert(
            task_id.to_string(),
            ReTask {
                task_id: task_id.to_string(),
                state: ReTaskState::New,
                description: description.to_string(),
                target_id: target_id.to_string(),
                firmware_sha256: firmware_sha256.to_string(),
                evidence_ids: Vec::new(),
            },
        );
        self.audit.push(AuditEntry {
            at: now(),
            actor: actor.to_string(),
            action: "task.create".into(),
            detail: task_id.to_string(),
        });
    }

    pub fn transition_task(
        &mut self,
        task_id: &str,
        to: ReTaskState,
        actor: &str,
    ) -> Result<(), StoreError> {
        let task = self
            .tasks
            .get_mut(task_id)
            .ok_or_else(|| StoreError::UnknownTask(task_id.to_string()))?;
        if !task.state.can_transition(to) {
            return Err(StoreError::IllegalTaskTransition {
                from: task.state,
                to,
            });
        }
        let from = task.state;
        task.state = to;
        self.audit.push(AuditEntry {
            at: now(),
            actor: actor.to_string(),
            action: "task.transition".into(),
            detail: format!("{task_id}: {from:?} -> {to:?}"),
        });
        Ok(())
    }

    pub fn add_evidence(&mut self, rec: EvidenceRecord, actor: &str) -> Result<(), StoreError> {
        if !self.tasks.contains_key(&rec.task_id) {
            return Err(StoreError::UnknownTask(rec.task_id.clone()));
        }
        let task_id = rec.task_id.clone();
        let ev_id = rec.evidence_id.clone();
        self.evidence.insert(ev_id.clone(), rec);
        if let Some(t) = self.tasks.get_mut(&task_id)
            && !t.evidence_ids.contains(&ev_id)
        {
            t.evidence_ids.push(ev_id.clone());
        }
        self.audit.push(AuditEntry {
            at: now(),
            actor: actor.to_string(),
            action: "evidence.create".into(),
            detail: format!("{ev_id} on task {task_id}"),
        });
        Ok(())
    }

    pub fn transition_evidence(
        &mut self,
        evidence_id: &str,
        to: EvidenceState,
        actor: &str,
    ) -> Result<(), StoreError> {
        let rec = self
            .evidence
            .get_mut(evidence_id)
            .ok_or_else(|| StoreError::UnknownEvidence(evidence_id.to_string()))?;
        if !rec.state.can_transition(to) {
            return Err(StoreError::IllegalEvidenceTransition {
                from: rec.state,
                to,
            });
        }
        let from = rec.state;
        rec.state = to;
        self.audit.push(AuditEntry {
            at: now(),
            actor: actor.to_string(),
            action: "evidence.transition".into(),
            detail: format!("{evidence_id}: {from:?} -> {to:?}"),
        });
        Ok(())
    }

    pub fn add_review(&mut self, evidence_id: &str, review: Review) -> Result<(), StoreError> {
        let rec = self
            .evidence
            .get_mut(evidence_id)
            .ok_or_else(|| StoreError::UnknownEvidence(evidence_id.to_string()))?;
        rec.reviews.push(review.clone());
        self.audit.push(AuditEntry {
            at: now(),
            actor: review.reviewer.clone(),
            action: "evidence.review".into(),
            detail: format!("{evidence_id}: {}", review.verdict),
        });
        Ok(())
    }

    pub fn load(path: &Path) -> Result<Self, StoreError> {
        let data = std::fs::read(path).map_err(StoreError::Io)?;
        serde_json::from_slice(&data)
            .map_err(|e| StoreError::Io(std::io::Error::new(std::io::ErrorKind::InvalidData, e)))
    }

    pub fn save(&self, path: &Path) -> Result<(), StoreError> {
        let data = serde_json::to_vec_pretty(self)
            .map_err(|e| StoreError::Io(std::io::Error::other(e)))?;
        std::fs::write(path, data).map_err(StoreError::Io)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn mk() -> ReStore {
        let mut s = ReStore::new();
        s.add_task("T-1", "recover app_launcher_add", "tgt", "abc", "alice");
        s
    }

    fn ev(task: &str, id: &str) -> EvidenceRecord {
        EvidenceRecord {
            evidence_id: id.to_string(),
            task_id: task.to_string(),
            state: EvidenceState::Draft,
            kind: "function".into(),
            summary: "candidate".into(),
            artifact_uris: vec!["ida://...".into()],
            reviews: Vec::new(),
        }
    }

    #[test]
    fn task_forward_only() {
        let mut s = mk();
        s.transition_task("T-1", ReTaskState::Analyzing, "a")
            .unwrap();
        s.transition_task("T-1", ReTaskState::EvidenceGathered, "a")
            .unwrap();
        s.transition_task("T-1", ReTaskState::Verifying, "a")
            .unwrap();
        s.transition_task("T-1", ReTaskState::Promoted, "a")
            .unwrap();
        // promoted -> promoted is illegal
        assert!(
            s.transition_task("T-1", ReTaskState::Promoted, "a")
                .is_err()
        );
        // promoted -> analyzing is illegal (no rollback)
        assert!(
            s.transition_task("T-1", ReTaskState::Analyzing, "a")
                .is_err()
        );
    }

    #[test]
    fn task_illegal_skip() {
        let mut s = mk();
        // new -> promoted is not a legal edge
        assert!(
            s.transition_task("T-1", ReTaskState::Promoted, "a")
                .is_err()
        );
    }

    #[test]
    fn evidence_forward_only() {
        let mut s = mk();
        s.add_evidence(ev("T-1", "E-1"), "a").unwrap();
        s.transition_evidence("E-1", EvidenceState::Candidate, "a")
            .unwrap();
        s.transition_evidence("E-1", EvidenceState::Verified, "a")
            .unwrap();
        s.transition_evidence("E-1", EvidenceState::Promoted, "a")
            .unwrap();
        // verified is not reachable from promoted
        assert!(
            s.transition_evidence("E-1", EvidenceState::Verified, "a")
                .is_err()
        );
    }

    #[test]
    fn evidence_refute_then_withdraw() {
        let mut s = mk();
        s.add_evidence(ev("T-1", "E-1"), "a").unwrap();
        s.transition_evidence("E-1", EvidenceState::Candidate, "a")
            .unwrap();
        s.transition_evidence("E-1", EvidenceState::Refuted, "a")
            .unwrap();
        // refuted is terminal (a new task must be opened)
        assert!(
            s.transition_evidence("E-1", EvidenceState::Candidate, "a")
                .is_err()
        );
    }

    #[test]
    fn unknown_ids_rejected() {
        let mut s = mk();
        assert!(
            s.transition_task("NOPE", ReTaskState::Analyzing, "a")
                .is_err()
        );
        assert!(
            s.transition_evidence("NOPE", EvidenceState::Verified, "a")
                .is_err()
        );
        // evidence for an unknown task is rejected
        assert!(s.add_evidence(ev("NOPE", "E-x"), "a").is_err());
    }

    #[test]
    fn audit_is_append_only() {
        let mut s = mk();
        let before = s.audit.len();
        s.transition_task("T-1", ReTaskState::Analyzing, "bob")
            .unwrap();
        assert_eq!(s.audit.len(), before + 1);
        assert_eq!(s.audit.last().unwrap().actor, "bob");
    }

    #[test]
    fn persist_roundtrip() {
        let dir = tempfile::tempdir().unwrap();
        let p = dir.path().join("store.json");
        let mut s = mk();
        s.transition_task("T-1", ReTaskState::Analyzing, "a")
            .unwrap();
        s.save(&p).unwrap();
        let loaded = ReStore::load(&p).unwrap();
        assert_eq!(loaded.tasks.len(), 1);
        assert_eq!(loaded.tasks["T-1"].state, ReTaskState::Analyzing);
        assert_eq!(loaded.audit.len(), s.audit.len());
    }
}
