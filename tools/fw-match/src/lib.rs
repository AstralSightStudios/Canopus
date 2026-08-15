//! canopus-fw-match — multi-layer firmware function matcher.
//!
//! Given a source corpus (functions of a known firmware, with a set of named
//! symbol addresses) and a target corpus (functions of another firmware), find
//! the target counterpart of each source symbol. Layers:
//!
//!   1. pattern  — relocation-masked entry bytes ([`thumb`])
//!   2. cfg      — normalized control-flow shape ([`score::cfg_score`])
//!   3. xref     — referenced strings + constants ([`score`])
//!   4. GA       — assignment search + global callee consistency ([`ga`])
//!
//! The result is a set of candidate matches with scores and margins; the
//! caller decides thresholds based on the verification report.

pub mod corpus;
pub mod engine;
pub mod ga;
pub mod score;
pub mod thumb;
