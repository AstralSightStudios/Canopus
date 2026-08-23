//! Global/data-object matching driven by matched function data flow.
//!
//! A global is never treated as a function and raw pointer-bearing bytes are
//! never sufficient evidence. Candidate retrieval starts from source xrefs,
//! maps their owner functions into the target firmware, then compares access
//! direction, instruction-relative position, segment class and object shape.

use crate::corpus::{Corpus, DataObjectRecord, FunctionRecord};
use crate::ga::MatchResult;
use serde::{Deserialize, Serialize};
use std::collections::{BTreeMap, BTreeSet, HashMap};

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct SourceGlobal {
    pub symbol_id: String,
    pub name: String,
    pub entry_address: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct GlobalCandidate {
    pub target_addr: String,
    pub score: f64,
    pub supporting_functions: Vec<String>,
    pub access_evidence: usize,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct GlobalMatchResult {
    pub symbol_id: String,
    pub name: String,
    pub source_addr: String,
    pub target_addr: Option<String>,
    pub score: Option<f64>,
    pub margin: Option<f64>,
    pub state: String,
    pub top_candidates: Vec<GlobalCandidate>,
    pub required_independent_evidence: Vec<String>,
}

#[derive(Default)]
struct CandidateScore {
    score: f64,
    supporters: BTreeSet<String>,
    accesses: usize,
}

fn parse_addr(value: &str) -> u64 {
    u64::from_str_radix(value.trim_start_matches("0x"), 16).unwrap_or(0)
}

fn ratio_score(a: u64, b: u64) -> f64 {
    if a == 0 || b == 0 {
        return 0.0;
    }
    let ratio = a.min(b) as f64 / a.max(b) as f64;
    if ratio >= 0.98 {
        2.0
    } else if ratio >= 0.90 {
        1.0
    } else {
        0.0
    }
}

fn set_overlap<T: Ord>(left: BTreeSet<T>, right: BTreeSet<T>) -> f64 {
    if left.is_empty() || right.is_empty() {
        return 0.0;
    }
    let intersection = left.intersection(&right).count() as f64;
    let union = left.union(&right).count() as f64;
    intersection / union
}

/// Infer an unnamed xref-owner function only when its structural best match is
/// decisive. This lets unique initializers participate without adding guessed
/// function symbols to a target pack.
fn infer_owner<'a>(source: &FunctionRecord, target: &'a Corpus) -> Option<&'a FunctionRecord> {
    let mut ranked = Vec::new();
    for candidate in &target.functions {
        if ratio_score(source.size, candidate.size) == 0.0
            || source.blocks.abs_diff(candidate.blocks) > 1
        {
            continue;
        }
        let cfg = if source.cfg_fingerprint() == candidate.cfg_fingerprint() {
            5.0
        } else {
            0.0
        };
        let strings = set_overlap(source.string_set(), candidate.string_set()) * 10.0;
        let constants = set_overlap(source.constant_set(), candidate.constant_set()) * 2.0;
        let score = ratio_score(source.size, candidate.size)
            + ratio_score(source.insn, candidate.insn)
            + cfg
            + strings
            + constants;
        ranked.push((score, candidate));
    }
    ranked.sort_by(|a, b| b.0.total_cmp(&a.0));
    let (best_score, best) = ranked.first().copied()?;
    let second_score = ranked.get(1).map_or(0.0, |item| item.0);
    (best_score >= 8.0 && best_score - second_score >= 1.5).then_some(best)
}

fn mapped_owners<'a>(
    source: &'a Corpus,
    target: &'a Corpus,
    function_matches: &[MatchResult],
) -> HashMap<u64, &'a FunctionRecord> {
    let mut out = HashMap::new();
    for item in function_matches {
        let (Some(score), Some(margin)) = (item.score, item.margin) else {
            continue;
        };
        // A speculative function assignment must not redirect global data flow.
        // Use the same decisive gate as the engine's monotonic anchors; weaker
        // owners may still enter through the independent structural inference
        // path below.
        if score < 11.0 || margin < 0.25 {
            continue;
        }
        let Some(target_addr) = item.target_addr.as_deref() else {
            continue;
        };
        if let Some(function) = target.function_at(parse_addr(target_addr)) {
            out.insert(parse_addr(&item.source_addr), function);
        }
    }
    // Unnamed initializer/consumer owners are admitted only through the strict
    // structural gate above. This is candidate retrieval, not symbol promotion.
    for function in &source.functions {
        if out.contains_key(&function.addr_u64()) || function.data_refs.is_empty() {
            continue;
        }
        if let Some(mapped) = infer_owner(function, target) {
            out.insert(function.addr_u64(), mapped);
        }
    }
    out
}

fn access_compatible(source: &str, target: &str) -> bool {
    source == target || source == "unknown" || target == "unknown"
}

fn property_score(source: &DataObjectRecord, target: &DataObjectRecord) -> f64 {
    let mut score = 0.0;
    if source.segment == target.segment {
        score += 2.0;
    }
    if source.writable == target.writable {
        score += 2.0;
    }
    score += ratio_score(source.size, target.size);
    if source.alignment != 0 && source.alignment == target.alignment {
        score += 0.5;
    }
    score
}

pub fn match_globals(
    symbols: &[SourceGlobal],
    source: &Corpus,
    target: &Corpus,
    function_matches: &[MatchResult],
) -> Vec<GlobalMatchResult> {
    let owner_map = mapped_owners(source, target, function_matches);
    let mut results = Vec::new();

    for symbol in symbols {
        let source_addr = parse_addr(&symbol.entry_address);
        let Some(source_object) = source.global_at(source_addr) else {
            results.push(GlobalMatchResult {
                symbol_id: symbol.symbol_id.clone(),
                name: symbol.name.clone(),
                source_addr: symbol.entry_address.clone(),
                target_addr: None,
                score: None,
                margin: None,
                state: "BLOCKED".into(),
                top_candidates: vec![],
                required_independent_evidence: vec![
                    "source corpus data-object record".into(),
                    "exact-target initializer/consumer data flow".into(),
                    "object ownership and pointer depth".into(),
                    "reviewer promotion".into(),
                ],
            });
            continue;
        };
        let mut scores: BTreeMap<u64, CandidateScore> = BTreeMap::new();

        for xref in &source_object.xrefs {
            let source_owner_addr = parse_addr(&xref.function);
            let Some(source_owner) = source.function_at(source_owner_addr) else {
                continue;
            };
            let Some(target_owner) = owner_map.get(&source_owner_addr).copied() else {
                continue;
            };
            let source_position = xref.off as f64 / source_owner.size.max(1) as f64;
            for target_ref in &target_owner.data_refs {
                if !access_compatible(&xref.access, &target_ref.access) {
                    continue;
                }
                let target_position = target_ref.off as f64 / target_owner.size.max(1) as f64;
                let distance = (source_position - target_position).abs();
                if distance > 0.08 && xref.off.abs_diff(target_ref.off) > 24 {
                    continue;
                }
                let candidate_addr = parse_addr(&target_ref.addr);
                let Some(target_object) = target.global_at(candidate_addr) else {
                    continue;
                };
                let positional = (1.0 - distance / 0.08).clamp(0.0, 1.0) * 4.0;
                let item = scores.entry(candidate_addr).or_default();
                item.score += positional + 1.0;
                item.score += property_score(source_object, target_object);
                item.supporters.insert(format!("0x{source_owner_addr:x}"));
                item.accesses += 1;
            }
        }

        let mut ranked: Vec<_> = scores
            .into_iter()
            .map(|(address, item)| GlobalCandidate {
                target_addr: format!("0x{address:x}"),
                score: item.score,
                supporting_functions: item.supporters.into_iter().collect(),
                access_evidence: item.accesses,
            })
            .collect();
        ranked.sort_by(|a, b| b.score.total_cmp(&a.score));
        ranked.truncate(8);
        let best = ranked.first();
        let margin = best.map(|item| item.score - ranked.get(1).map_or(0.0, |next| next.score));
        let state = match (best, margin) {
            (Some(item), Some(margin))
                if item.score >= 8.0 && margin >= 2.0 && !item.supporting_functions.is_empty() =>
            {
                "REVIEW_REQUIRED"
            }
            (Some(_), _) => "CANDIDATE",
            (None, _) => "BLOCKED",
        };
        results.push(GlobalMatchResult {
            symbol_id: symbol.symbol_id.clone(),
            name: symbol.name.clone(),
            source_addr: symbol.entry_address.clone(),
            target_addr: best.map(|item| item.target_addr.clone()),
            score: best.map(|item| item.score),
            margin,
            state: state.into(),
            top_candidates: ranked,
            required_independent_evidence: vec![
                "exact-target initializer/consumer data flow".into(),
                "object ownership and pointer depth".into(),
                "reviewer promotion".into(),
            ],
        });
    }
    results
}

pub fn load_source_globals(symbols_dir: &std::path::Path) -> Result<Vec<SourceGlobal>, String> {
    let mut out = Vec::new();
    for entry in std::fs::read_dir(symbols_dir)
        .map_err(|e| format!("cannot read {}: {e}", symbols_dir.display()))?
    {
        let path = entry.map_err(|e| e.to_string())?.path();
        if path.extension().and_then(|value| value.to_str()) != Some("json") {
            continue;
        }
        let value: serde_json::Value =
            serde_json::from_str(&std::fs::read_to_string(&path).map_err(|e| e.to_string())?)
                .map_err(|e| e.to_string())?;
        if value.get("kind").and_then(|item| item.as_str()) != Some("global") {
            continue;
        }
        let Some(entry_address) = value.get("entry_address").and_then(|item| item.as_str()) else {
            continue;
        };
        out.push(SourceGlobal {
            symbol_id: value
                .get("symbol_id")
                .and_then(|item| item.as_str())
                .unwrap_or("")
                .into(),
            name: value
                .get("name")
                .and_then(|item| item.as_str())
                .unwrap_or("")
                .into(),
            entry_address: entry_address.into(),
        });
    }
    out.sort_by(|a, b| a.entry_address.cmp(&b.entry_address));
    Ok(out)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::corpus::{BlockShape, DataObjectXref, DataReference};

    fn function(addr: &str, data_refs: Vec<DataReference>) -> FunctionRecord {
        FunctionRecord {
            addr: addr.into(),
            name: String::new(),
            size: 100,
            insn: 40,
            blocks: 2,
            block_offs: vec![
                BlockShape { off: 0, size: 40 },
                BlockShape { off: 40, size: 60 },
            ],
            succ: vec![(0, 1)],
            callees: vec![],
            callers: vec![],
            strings: vec!["unique theme resource".into()],
            constants: vec![12, 24],
            entry: "00be".into(),
            data_refs,
        }
    }

    fn object(addr: &str, xrefs: Vec<DataObjectXref>) -> DataObjectRecord {
        DataObjectRecord {
            addr: addr.into(),
            name: String::new(),
            segment: "ram".into(),
            writable: true,
            size: 12,
            alignment: 4,
            bytes: String::new(),
            readers: vec![],
            writers: vec![],
            xrefs,
        }
    }

    fn corpus(functions: Vec<FunctionRecord>, globals: Vec<DataObjectRecord>) -> Corpus {
        Corpus {
            schema: 2,
            target_id: String::new(),
            image_base: "0x0".into(),
            functions,
            globals,
        }
    }

    fn symbol() -> SourceGlobal {
        SourceGlobal {
            symbol_id: "source.style".into(),
            name: "style".into(),
            entry_address: "0x2000".into(),
        }
    }

    fn owner_match() -> MatchResult {
        MatchResult {
            name: "owner".into(),
            source_addr: "0x1000".into(),
            target_addr: Some("0x9000".into()),
            target_name: None,
            score: Some(12.0),
            margin: Some(1.0),
            pool_size: 1,
        }
    }

    #[test]
    fn matched_owner_retrieves_global_candidate() {
        let source = corpus(
            vec![function("0x1000", vec![])],
            vec![object(
                "0x2000",
                vec![DataObjectXref {
                    function: "0x1000".into(),
                    off: 20,
                    access: "write".into(),
                }],
            )],
        );
        let target = corpus(
            vec![function(
                "0x9000",
                vec![DataReference {
                    off: 20,
                    addr: "0xa000".into(),
                    access: "write".into(),
                }],
            )],
            vec![object("0xa000", vec![])],
        );

        let results = match_globals(&[symbol()], &source, &target, &[owner_match()]);
        assert_eq!(results.len(), 1);
        assert_eq!(results[0].target_addr.as_deref(), Some("0xa000"));
        assert_eq!(results[0].state, "REVIEW_REQUIRED");
    }

    #[test]
    fn incompatible_access_and_position_are_rejected() {
        let source = corpus(
            vec![function("0x1000", vec![])],
            vec![object(
                "0x2000",
                vec![DataObjectXref {
                    function: "0x1000".into(),
                    off: 20,
                    access: "write".into(),
                }],
            )],
        );
        let target = corpus(
            vec![function(
                "0x9000",
                vec![
                    DataReference {
                        off: 20,
                        addr: "0xa000".into(),
                        access: "read".into(),
                    },
                    DataReference {
                        off: 80,
                        addr: "0xb000".into(),
                        access: "write".into(),
                    },
                ],
            )],
            vec![object("0xa000", vec![]), object("0xb000", vec![])],
        );

        let results = match_globals(&[symbol()], &source, &target, &[owner_match()]);
        assert_eq!(results[0].state, "BLOCKED");
        assert!(results[0].top_candidates.is_empty());
    }

    #[test]
    fn tied_candidates_remain_candidates() {
        let source = corpus(
            vec![function("0x1000", vec![])],
            vec![object(
                "0x2000",
                vec![DataObjectXref {
                    function: "0x1000".into(),
                    off: 20,
                    access: "read".into(),
                }],
            )],
        );
        let target = corpus(
            vec![function(
                "0x9000",
                vec![
                    DataReference {
                        off: 20,
                        addr: "0xa000".into(),
                        access: "read".into(),
                    },
                    DataReference {
                        off: 20,
                        addr: "0xb000".into(),
                        access: "read".into(),
                    },
                ],
            )],
            vec![object("0xa000", vec![]), object("0xb000", vec![])],
        );

        let results = match_globals(&[symbol()], &source, &target, &[owner_match()]);
        assert_eq!(results[0].top_candidates.len(), 2);
        assert_eq!(results[0].margin, Some(0.0));
        assert_eq!(results[0].state, "CANDIDATE");
    }

    #[test]
    fn unique_unnamed_owner_can_be_inferred() {
        let source_owner = function(
            "0x1000",
            vec![DataReference {
                off: 20,
                addr: "0x2000".into(),
                access: "offset".into(),
            }],
        );
        let source = corpus(
            vec![source_owner],
            vec![object(
                "0x2000",
                vec![DataObjectXref {
                    function: "0x1000".into(),
                    off: 20,
                    access: "offset".into(),
                }],
            )],
        );
        let mut unrelated = function("0x9100", vec![]);
        unrelated.size = 400;
        unrelated.strings = vec!["other".into()];
        let target = corpus(
            vec![
                function(
                    "0x9000",
                    vec![DataReference {
                        off: 20,
                        addr: "0xa000".into(),
                        access: "offset".into(),
                    }],
                ),
                unrelated,
            ],
            vec![object("0xa000", vec![])],
        );

        let results = match_globals(&[symbol()], &source, &target, &[]);
        assert_eq!(results[0].target_addr.as_deref(), Some("0xa000"));
    }

    #[test]
    fn missing_source_object_is_reported_blocked() {
        let source = corpus(vec![], vec![]);
        let target = corpus(vec![], vec![]);
        let results = match_globals(&[symbol()], &source, &target, &[]);
        assert_eq!(results.len(), 1);
        assert_eq!(results[0].state, "BLOCKED");
        assert!(results[0].target_addr.is_none());
    }

    #[test]
    fn loads_only_addressed_global_symbols() {
        let dir = tempfile::tempdir().unwrap();
        std::fs::write(
            dir.path().join("global.json"),
            r#"{"kind":"global","symbol_id":"s.g","name":"g","entry_address":"0x2000"}"#,
        )
        .unwrap();
        std::fs::write(
            dir.path().join("function.json"),
            r#"{"kind":"function","symbol_id":"s.f","name":"f","entry_address":"0x1000"}"#,
        )
        .unwrap();
        std::fs::write(
            dir.path().join("missing.json"),
            r#"{"kind":"global","symbol_id":"s.m","name":"m"}"#,
        )
        .unwrap();

        let globals = load_source_globals(dir.path()).unwrap();
        assert_eq!(globals.len(), 1);
        assert_eq!(globals[0].entry_address, "0x2000");
    }
}
