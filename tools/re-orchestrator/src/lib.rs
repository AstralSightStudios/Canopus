//! canopus-re — RE orchestrator (architecture §9, CAN-RE-001..009).
//!
//! Host-side workflow engine for firmware reverse engineering:
//!   - [`store`]   — immutable RE task/evidence state machine + append-only
//!     audit log, persisted as JSON (CAN-RE-001).
//!   - [`ida`]     — read-only IDA MCP tool allowlist + per-call audit
//!     (CAN-RE-002). Write tools can never be invoked.
//!   - [`workflow`]— function/type evidence bundles and signature-candidate
//!     generation (CAN-RE-003/004/005). Candidates only — never executed.
//!   - [`verify`]  — independent verifier stage + human promotion gate
//!     (CAN-RE-006/007).
//!   - [`revision`]— signed immutable target-pack revisions (CAN-RE-009).

pub mod ida;
pub mod revision;
pub mod store;
pub mod verify;
pub mod workflow;

pub use store::{EvidenceRecord, EvidenceState, ReStore, ReTask, ReTaskState};
