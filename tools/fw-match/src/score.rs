//! Per-pair layer scoring for the multi-layer matcher.
//!
//! Each layer produces a normalized score in 0..1:
//!   - pattern: masked entry-byte bit similarity ([`crate::thumb`])
//!   - cfg:     normalized control-flow block structure similarity
//!   - xref:    referenced-string Jaccard + small-constant Jaccard
//!   - size:    instruction-count proximity
//!
//! The GA layer additionally uses *callee consistency* (a global term) once
//! anchors exist; see [`crate::ga`].

use crate::corpus::FunctionRecord;
use crate::thumb::{entry_mask, masked_compare};
use std::collections::BTreeSet;

/// Layer scores for one source/target function pair.
#[derive(Debug, Clone, Copy, Default, PartialEq)]
pub struct LayerScores {
    pub pattern: f64,
    pub cfg: f64,
    pub strings: f64,
    pub constants: f64,
    pub size: f64,
    pub degree: f64,
}

impl LayerScores {
    /// Weighted composite (all layers, including size as a weak prior).
    pub fn composite(&self) -> f64 {
        4.0 * self.pattern
            + 4.0 * self.cfg
            + 3.0 * self.strings
            + 1.5 * self.constants
            + 1.0 * self.size
            + 2.0 * self.degree
    }

    /// Composite without the size prior (for final ranking clarity).
    pub fn structural(&self) -> f64 {
        4.0 * self.pattern
            + 4.0 * self.cfg
            + 3.0 * self.strings
            + 1.5 * self.constants
            + 2.0 * self.degree
    }
}

/// Similarity of two unordered multisets via normalized set overlap.
/// Empty-vs-empty is *weak* evidence (0.5), not a perfect match: unrelated
/// stringless functions must not be rewarded for sharing nothing.
fn jaccard<'a, T: Ord + Clone + 'a>(
    a: impl Iterator<Item = &'a T>,
    b: impl Iterator<Item = &'a T>,
) -> f64 {
    let sa: BTreeSet<T> = a.cloned().collect();
    let sb: BTreeSet<T> = b.cloned().collect();
    if sa.is_empty() && sb.is_empty() {
        return 0.5; // no evidence -> weak, neutral-positive
    }
    let inter = sa.intersection(&sb).count();
    let union = sa.len() + sb.len() - inter;
    if union == 0 {
        return 0.0;
    }
    inter as f64 / union as f64
}

/// Entry-pattern score: masked bit similarity over the longer of the two
/// entries (source mask applied to both, so both get the same relocation mask).
pub fn pattern_score(src: &FunctionRecord, dst: &FunctionRecord) -> f64 {
    let sa = src.entry_bytes();
    let sb = dst.entry_bytes();
    if sa.is_empty() || sb.is_empty() {
        return 0.0;
    }
    let mask = entry_mask(&sa);
    let n = sa.len().min(sb.len());
    if n == 0 {
        return 0.0;
    }
    // Compare the common prefix under the source-derived mask.
    let (matched, total) = masked_compare(&sa[..n], &sb[..n], &mask[..n]);
    if total == 0 {
        return 0.0;
    }
    let base = matched as f64 / total as f64;
    // Penalize unequal entry length: if one is a veneer (8 bytes) and the
    // other a full body, the match must be downgraded.
    let la = sa.len();
    let lb = sb.len();
    let len_penalty = 1.0 / (1.0 + (la as i64 - lb as i64).unsigned_abs() as f64 / 64.0);
    base * len_penalty
}

/// CFG score: compare normalized block structure.
///
/// We compare (a) the ratio of block counts, (b) the overlap of block *shape*
/// sizes (relative to function size) as a multiset, and (c) edge-count
/// proximity. This is robust to a relocation because the CFG shape is
/// position-independent.
pub fn cfg_score(src: &FunctionRecord, dst: &FunctionRecord) -> f64 {
    // Block-count ratio.
    let block_ratio = if src.blocks == 0 && dst.blocks == 0 {
        1.0
    } else if src.blocks == 0 || dst.blocks == 0 {
        0.0
    } else {
        let (a, b) = (
            src.blocks.min(dst.blocks) as f64,
            src.blocks.max(dst.blocks) as f64,
        );
        a / b
    };

    // Normalized block-size multiset overlap: sizes relative to function size.
    fn norm_sizes(f: &FunctionRecord) -> Vec<u64> {
        let total = f.size.max(1);
        let mut v: Vec<u64> = f
            .block_offs
            .iter()
            .map(|b| (b.size * 1024) / total)
            .collect();
        v.sort_unstable();
        v
    }
    let ns = norm_sizes(src);
    let nd = norm_sizes(dst);
    let shape = if ns.is_empty() && nd.is_empty() {
        1.0
    } else if ns.is_empty() || nd.is_empty() {
        0.0
    } else {
        let mut i = 0;
        let mut j = 0;
        let mut inter = 0usize;
        while i < ns.len() && j < nd.len() {
            match ns[i].cmp(&nd[j]) {
                std::cmp::Ordering::Less => i += 1,
                std::cmp::Ordering::Greater => j += 1,
                std::cmp::Ordering::Equal => {
                    inter += 1;
                    i += 1;
                    j += 1;
                }
            }
        }
        let union = ns.len() + nd.len() - inter;
        if union == 0 {
            0.0
        } else {
            inter as f64 / union as f64
        }
    };

    // Edge-count proximity.
    let edge = if src.succ.is_empty() && dst.succ.is_empty() {
        1.0
    } else if src.succ.is_empty() || dst.succ.is_empty() {
        0.0
    } else {
        let (a, b) = (
            src.succ.len().min(dst.succ.len()) as f64,
            src.succ.len().max(dst.succ.len()) as f64,
        );
        a / b
    };

    0.4 * block_ratio + 0.4 * shape + 0.2 * edge
}

/// Xref string score: Jaccard of referenced string contents.
pub fn string_score(src: &FunctionRecord, dst: &FunctionRecord) -> f64 {
    jaccard(src.strings.iter(), dst.strings.iter())
}

/// Constant score: Jaccard of small immediates.
pub fn constant_score(src: &FunctionRecord, dst: &FunctionRecord) -> f64 {
    jaccard(src.constants.iter(), dst.constants.iter())
}

/// Size score: instruction-count proximity (weak prior).
pub fn size_score(src: &FunctionRecord, dst: &FunctionRecord) -> f64 {
    if src.insn == 0 && dst.insn == 0 {
        return 1.0;
    }
    if src.insn == 0 || dst.insn == 0 {
        return 0.0;
    }
    let (a, b) = (src.insn.min(dst.insn) as f64, src.insn.max(dst.insn) as f64);
    // closeness in log-space: a/b, squared for a stronger falloff
    let r = a / b;
    r * r
}

/// Degree proximity: caller-count and callee-count closeness.
///
/// Tiny wrappers and veneers are structurally identical, but a function
/// called by 64 sites is a different function than one called by 7. Caller
/// and callee *counts* are position-independent (the addresses move, the
/// in-degree does not), so this layer discriminates where pattern/CFG tie.
pub fn degree_score(src: &FunctionRecord, dst: &FunctionRecord) -> f64 {
    let callers = ratio_closeness(src.callers.len(), dst.callers.len());
    let callees = ratio_closeness(src.callees.len(), dst.callees.len());
    0.5 * callers + 0.5 * callees
}

fn ratio_closeness(a: usize, b: usize) -> f64 {
    if a == 0 && b == 0 {
        return 1.0;
    }
    if a == 0 || b == 0 {
        return 0.0;
    }
    let (lo, hi) = (a.min(b) as f64, a.max(b) as f64);
    (lo / hi).powi(2)
}

/// Full layer scores for a pair.
pub fn score_pair(src: &FunctionRecord, dst: &FunctionRecord) -> LayerScores {
    LayerScores {
        pattern: pattern_score(src, dst),
        cfg: cfg_score(src, dst),
        strings: string_score(src, dst),
        constants: constant_score(src, dst),
        size: size_score(src, dst),
        degree: degree_score(src, dst),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::corpus::{BlockShape, FunctionRecord};

    fn rec(addr: &str, size: u64, insn: u64, blocks: u64, entry: &str) -> FunctionRecord {
        let block_offs: Vec<BlockShape> = (0..blocks)
            .map(|i| BlockShape {
                off: i * 8,
                size: 8,
            })
            .collect();
        let succ: Vec<(usize, usize)> = (0..blocks.saturating_sub(1))
            .map(|i| (i as usize, (i + 1) as usize))
            .collect();
        FunctionRecord {
            addr: addr.into(),
            name: String::new(),
            size,
            insn,
            blocks: blocks as usize,
            block_offs,
            succ,
            callees: vec![],
            callers: vec![],
            strings: vec![],
            constants: vec![],
            entry: entry.into(),
        }
    }

    #[test]
    fn identical_entries_score_high() {
        // PUSH {r4-r7, lr}; MOV r0, #1
        let a = rec("0x1000", 32, 16, 4, "f0b50120");
        let b = rec("0x5000", 32, 16, 4, "f0b50120");
        let s = score_pair(&a, &b);
        assert!(s.pattern > 0.9, "pattern {}", s.pattern);
        assert!(s.cfg > 0.9, "cfg {}", s.cfg);
    }

    #[test]
    fn different_opcodes_score_low() {
        // PUSH {r4-r7, lr}; MOVS r0, #1  vs  POP {r3-r7, pc}; BX lr
        // Byte-level similarity of unrelated Thumb code is still moderate
        // (register fields coincide), so assert the layer *discriminates*
        // rather than an absolute floor.
        let a = rec("0x1000", 32, 16, 4, "f0b50120");
        let b = rec("0x5000", 32, 16, 4, "f8bd7047");
        let c = rec("0x6000", 32, 16, 4, "f0b50120");
        let same = score_pair(&a, &c).pattern;
        let diff = score_pair(&a, &b).pattern;
        assert!(same > 0.9, "pattern {same}");
        assert!(diff < same - 0.1, "pattern {diff} vs same {same}");
    }

    #[test]
    fn relocated_branch_still_matches() {
        // Same body, different BL target: 0xF000 0xF800 vs 0xF000 0xF900
        let a = rec("0x1000", 32, 16, 4, "00f000f8");
        let b = rec("0x5000", 32, 16, 4, "00f000f9");
        let s = score_pair(&a, &b);
        assert!(
            s.pattern > 0.5,
            "pattern {} should tolerate BL offset",
            s.pattern
        );
    }

    #[test]
    fn string_overlap() {
        let mut a = rec("0x1000", 32, 16, 4, "");
        let mut b = rec("0x5000", 32, 16, 4, "");
        a.strings = vec!["abc".into(), "def".into()];
        b.strings = vec!["abc".into(), "ghi".into()];
        assert!((string_score(&a, &b) - 1.0 / 3.0).abs() < 1e-9);
    }

    #[test]
    fn empty_strings_neutral() {
        let a = rec("0x1000", 32, 16, 4, "");
        let b = rec("0x5000", 32, 16, 4, "");
        // No evidence on either side -> weak, not a perfect match.
        assert_eq!(string_score(&a, &b), 0.5);
    }

    #[test]
    fn size_score_proximity() {
        let a = rec("0x1000", 64, 32, 4, "");
        let b = rec("0x5000", 64, 30, 4, "");
        assert!(size_score(&a, &b) > 0.8);
        let c = rec("0x6000", 64, 4, 4, "");
        assert!(size_score(&a, &c) < 0.1);
    }
}
