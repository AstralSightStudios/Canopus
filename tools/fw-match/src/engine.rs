//! End-to-end matching engine: source symbols -> target candidates.
//!
//! A source symbol set is a list of (symbol_id, name, entry_address) — exactly
//! what a target pack's `symbols/` directory records. The engine reads the
//! source corpus, indexes each source function by address, builds candidate
//! pools in the target corpus, runs the GA, and reports matches.

use crate::corpus::{Corpus, FunctionRecord};
use crate::ga::{
    build_pools, finalize, run_ga, Anchor, ConfirmedMatch, GaParams, Individual, MatchResult,
};

/// A source symbol to match (typically read from a target pack symbol record).
#[derive(Debug, Clone, serde::Serialize, serde::Deserialize)]
pub struct SourceSymbol {
    pub symbol_id: String,
    pub name: String,
    pub entry_address: String,
}

/// Engine configuration.
#[derive(Debug, Clone)]
pub struct EngineConfig {
    pub ga: GaParams,
    /// Seed for the deterministic GA.
    pub seed: u64,
    /// Pool cap per source symbol.
    pub max_pool: usize,
}

impl Default for EngineConfig {
    fn default() -> Self {
        EngineConfig {
            ga: GaParams::default(),
            seed: 0x5EED_CAFE,
            max_pool: 24,
        }
    }
}

/// Run the match: source symbols -> target candidates.
///
/// Iterative anchoring: round 1 runs with no anchors; confident matches from
/// each round become anchors for the next, letting callee-overlap (xref)
/// bootstrap from strong pairs to weaker ones.
pub fn match_symbols(
    symbols: &[SourceSymbol],
    src_corpus: &Corpus,
    dst_corpus: &Corpus,
    cfg: &EngineConfig,
) -> (Vec<MatchResult>, Option<Individual>) {
    // Resolve each symbol to a source corpus index, carrying the semantic name.
    // Only symbols found in the source corpus become pool problems; the pool
    // index for a symbol is its position in `source_indices`.
    let mut source_indices: Vec<(usize, String)> = Vec::new();
    for sym in symbols.iter() {
        let addr = u64::from_str_radix(sym.entry_address.trim_start_matches("0x"), 16)
            .unwrap_or(0);
        if let Some(idx) = src_corpus.functions.iter().position(|f| f.addr_u64() == addr) {
            source_indices.push((idx, sym.name.clone()));
        }
    }

    // Anchor rounds: grow the anchor set until stable (max 4 rounds).
    let mut anchors: Vec<Anchor> = Vec::new();
    let mut best: Option<Individual> = None;
    let mut results = Vec::new();
    let rounds = 4;
    for round in 0..rounds {
        let pools = build_pools(&source_indices, src_corpus, dst_corpus, cfg.max_pool, &anchors);
        // GA resolves assignment collisions over the (structurally-ranked,
        // xref-tiebroken) pools.
        let ind = run_ga(&pools, &cfg.ga, cfg.seed + round as u64 * 0x9E37);
        results = finalize(&ind, &pools, src_corpus, dst_corpus);
        let prev = anchors.len();
        anchors = extract_anchors(&results, &anchors);
        if anchors.len() == prev {
            break; // converged
        }
        best = Some(ind);
    }

    (results, best)
}

/// Turn confident matches into anchors for the next round. An anchor needs a
/// high absolute score and a decisive margin over the runner-up. Anchors that
/// contradict an existing anchor (same source, different target) are dropped.
fn extract_anchors(results: &[MatchResult], existing: &[Anchor]) -> Vec<Anchor> {
    let mut out: Vec<Anchor> = existing.to_vec();
    for r in results {
        let (Some(target_addr), Some(score), Some(margin)) =
            (r.target_addr.as_deref(), r.score, r.margin)
        else {
            continue;
        };
        if score < 11.0 || margin < 0.25 {
            continue;
        }
        let source_addr = u64::from_str_radix(r.source_addr.trim_start_matches("0x"), 16)
            .unwrap_or(0);
        let target_addr = u64::from_str_radix(target_addr.trim_start_matches("0x"), 16)
            .unwrap_or(0);
        // Drop any existing anchor claiming the same source addr with a
        // different target (round-over-round conflict).
        out.retain(|a| a.source_addr != source_addr || a.target_addr == target_addr);
        out.push(Anchor {
            source_addr,
            target_addr,
        });
    }
    out
}

/// Filter matched results to those passing a composite-score threshold and a
/// margin floor, then emit stable, address-carrying records.
pub fn confirm(
    results: &[MatchResult],
    min_score: f64,
    min_margin: f64,
) -> Vec<ConfirmedMatch> {
    results
        .iter()
        .filter_map(|r| {
            let score = r.score?;
            let margin = r.margin?;
            let target_addr = r.target_addr.clone()?;
            if score < min_score || margin < min_margin {
                return None;
            }
            Some(ConfirmedMatch {
                name: r.name.clone(),
                source_addr: r.source_addr.clone(),
                target_addr,
                score,
                margin,
                target_name: r.target_name.clone().unwrap_or_default(),
            })
        })
        .collect()
}

/// Load a source symbol list from the `symbols/` directory of a target pack.
/// Only function-kind records with a recovered `entry_address` are kept.
pub fn load_source_symbols(symbols_dir: &std::path::Path) -> Result<Vec<SourceSymbol>, String> {
    let mut out = Vec::new();
    if !symbols_dir.is_dir() {
        return Err(format!("{} is not a directory", symbols_dir.display()));
    }
    for entry in std::fs::read_dir(symbols_dir)
        .map_err(|e| format!("cannot read {}: {e}", symbols_dir.display()))?
    {
        let p = entry.map_err(|e| e.to_string())?.path();
        if p.extension().and_then(|e| e.to_str()) != Some("json") {
            continue;
        }
        let text = std::fs::read_to_string(&p).map_err(|e| e.to_string())?;
        let value: serde_json::Value = serde_json::from_str(&text).map_err(|e| e.to_string())?;
        if value.get("kind").and_then(|k| k.as_str()) != Some("function") {
            continue;
        }
        let Some(entry_address) = value.get("entry_address").and_then(|a| a.as_str()) else {
            continue;
        };
        let symbol_id = value
            .get("symbol_id")
            .and_then(|s| s.as_str())
            .unwrap_or("")
            .to_string();
        let name = value.get("name").and_then(|n| n.as_str()).unwrap_or("").to_string();
        out.push(SourceSymbol {
            symbol_id,
            name,
            entry_address: entry_address.to_string(),
        });
    }
    out.sort_by(|a, b| a.entry_address.cmp(&b.entry_address));
    Ok(out)
}

/// Fetch a target function record by address.
pub fn target_function<'c>(corpus: &'c Corpus, addr: u64) -> Option<&'c FunctionRecord> {
    corpus.function_at(addr)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::corpus::{BlockShape, FunctionRecord};

    fn rec(addr: &str, size: u64, insn: u64, entry: &str, name: &str) -> FunctionRecord {
        let blocks = 2;
        FunctionRecord {
            addr: addr.into(),
            name: name.into(),
            size,
            insn,
            blocks,
            block_offs: vec![
                BlockShape { off: 0, size: 8 },
                BlockShape { off: 8, size: size.saturating_sub(8).max(8) },
            ],
            succ: vec![(0, 1)],
            callees: vec![],
            callers: vec![],
            strings: vec![],
            constants: vec![],
            entry: entry.into(),
        }
    }

    #[test]
    fn load_source_symbols_filters() {
        let dir = tempfile::tempdir().unwrap();
        let good = serde_json::json!({
            "schema": 1,
            "symbol_id": "t.ui.foo",
            "target_id": "t",
            "name": "foo",
            "kind": "function",
            "instruction_set": "thumb",
            "entry_address": "0x1000",
            "callable_address": "0x1001",
            "policy": "restricted",
            "status": "STATIC_RECOVERED",
            "proof": {"static": "recovered"},
            "provenance": {"firmware_sha256": "a".repeat(64)}
        });
        let data = serde_json::json!({
            "schema": 1, "symbol_id": "t.data", "target_id": "t", "name": "g",
            "kind": "global", "instruction_set": "thumb", "policy": "restricted",
            "status": "STATIC_RECOVERED", "proof": {"static": "recovered"},
            "provenance": {"firmware_sha256": "a".repeat(64)}
        });
        std::fs::write(dir.path().join("a.json"), serde_json::to_string(&good).unwrap()).unwrap();
        std::fs::write(dir.path().join("b.json"), serde_json::to_string(&data).unwrap()).unwrap();
        let syms = load_source_symbols(dir.path()).unwrap();
        assert_eq!(syms.len(), 1);
        assert_eq!(syms[0].name, "foo");
        assert_eq!(syms[0].entry_address, "0x1000");
    }

    #[test]
    fn engine_matches_duplicate_into_target() {
        let src = Corpus {
            schema: 1,
            target_id: "s".into(),
            image_base: "0x0".into(),
            functions: vec![rec("0x1000", 64, 32, "00f0b500be00bd", "foo")],
        };
        let dst = Corpus {
            schema: 1,
            target_id: "d".into(),
            image_base: "0x0".into(),
            functions: vec![
                rec("0x9000", 64, 32, "00f0b500be00bd", "sub_9000"),
                rec("0xA000", 200, 90, "00e40000f0b5", "sub_A000"),
                rec("0xB000", 16, 4, "f8bd7047", "sub_B000"), // tiny unrelated
            ],
        };
        let syms = vec![SourceSymbol {
            symbol_id: "s.ui.foo".into(),
            name: "foo".into(),
            entry_address: "0x1000".into(),
        }];
        let (results, _) = match_symbols(&syms, &src, &dst, &EngineConfig::default());
        assert_eq!(results.len(), 1);
        assert_eq!(results[0].target_addr.as_deref(), Some("0x9000"));
        let confirmed = confirm(&results, 5.0, 0.2);
        assert_eq!(confirmed.len(), 1);
        assert_eq!(confirmed[0].target_addr, "0x9000");
    }

    #[test]
    fn confirm_filters_low_confidence() {
        let results = vec![MatchResult {
            name: "foo".into(),
            source_addr: "0x1000".into(),
            target_addr: Some("0x9000".into()),
            target_name: Some("sub_9000".into()),
            score: Some(2.0),
            pool_size: 1,
            margin: Some(0.1),
        }];
        assert!(confirm(&results, 5.0, 0.2).is_empty());
    }
}
