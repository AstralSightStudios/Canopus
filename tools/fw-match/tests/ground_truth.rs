//! Ground-truth integration test for the fw-match engine.
//!
//! The 036 target pack is the verified ground truth (the user's stated
//! confidence: "大部分函数正确"). Its symbols are matched into the 030 corpus
//! (same device, adjacent firmware) and compared against 030's own symbol
//! records for the same semantic names.
//!
//! The corpora are derived artifacts (gitignored, regenerated from IDBs), so
//! this test skips with a clear message when they are absent rather than
//! failing CI on a machine that has not run the extraction.

use canopus_fw_match::corpus::load_corpus;
use canopus_fw_match::engine::{confirm, load_source_symbols, match_symbols, EngineConfig};
use std::collections::HashMap;
use std::path::PathBuf;

fn repo() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../..")
}

fn syms_by_name(dir: &std::path::Path) -> HashMap<String, u64> {
    let mut out = HashMap::new();
    for entry in std::fs::read_dir(dir).unwrap() {
        let p = entry.unwrap().path();
        if p.extension().and_then(|e| e.to_str()) != Some("json") {
            continue;
        }
        let v: serde_json::Value =
            serde_json::from_str(&std::fs::read_to_string(&p).unwrap()).unwrap();
        if v.get("kind").and_then(|k| k.as_str()) != Some("function") {
            continue;
        }
        let Some(name) = v.get("name").and_then(|n| n.as_str()) else { continue };
        let Some(addr) = v.get("entry_address").and_then(|a| a.as_str()) else { continue };
        if let Ok(a) = u64::from_str_radix(addr.trim_start_matches("0x"), 16) {
            out.insert(name.to_string(), a);
        }
    }
    out
}

fn corpora_path() -> PathBuf {
    repo().join("targets/fw-corpus")
}

#[test]
fn match_036_into_030_against_ground_truth() {
    let corpus_dir = corpora_path();
    let src_path = corpus_dir.join("xiaomi-band-10-pro-3.101.036.json");
    let dst_path = corpus_dir.join("xiaomi-band-10-pro-3.101.030.json");
    if !src_path.exists() || !dst_path.exists() {
        eprintln!(
            "SKIP: corpus files missing (run tools/fw-match/extract_corpus.py); \
             skipping ground-truth match test"
        );
        return;
    }
    let root = repo();
    let symbols_dir = root.join("targets/xiaomi-band-10-pro-3.101.036/symbols");
    let target_syms_dir = root.join("targets/xiaomi-band-10-pro-3.101.030/symbols");

    let syms = load_source_symbols(&symbols_dir).unwrap();
    let src = load_corpus(&src_path).unwrap();
    let dst = load_corpus(&dst_path).unwrap();
    let ground_truth = syms_by_name(&target_syms_dir);

    let (results, _) = match_symbols(&syms, &src, &dst, &EngineConfig::default());

    // Best-candidate recall against 030's own records (same semantic name).
    let mut correct = 0;
    let mut shared = 0;
    let by_name: HashMap<&str, &canopus_fw_match::ga::MatchResult> =
        results.iter().map(|r| (r.name.as_str(), r)).collect();
    for (name, gt) in &ground_truth {
        let Some(r) = by_name.get(name.as_str()) else { continue };
        shared += 1;
        if let Some(pred) = &r.target_addr {
            if let Ok(p) = u64::from_str_radix(pred.trim_start_matches("0x"), 16) {
                if p == *gt {
                    correct += 1;
                }
            }
        }
    }
    let recall = if shared == 0 {
        0.0
    } else {
        correct as f64 / shared as f64
    };
    eprintln!(
        "036->030 ground truth: {correct}/{shared} best-candidate recall = {:.1}%",
        recall * 100.0
    );

    // Confirmed matches must be exact: precision on confirmed = 100%.
    let confirmed = confirm(&results, 5.0, 0.15);
    let mut confirmed_correct = 0;
    for c in &confirmed {
        if let Ok(p) = u64::from_str_radix(c.target_addr.trim_start_matches("0x"), 16)
            && ground_truth.get(&c.name) == Some(&p)
        {
            confirmed_correct += 1;
        }
    }
    let confirmed_precision = if confirmed.is_empty() {
        1.0
    } else {
        confirmed_correct as f64 / confirmed.len() as f64
    };
    eprintln!(
        "confirmed: {confirmed_correct}/{} precise = {:.1}%",
        confirmed.len(),
        confirmed_precision * 100.0
    );

    // Assertions: recall should be strong on same-model firmware, and
    // confirmed matches must never be wrong.
    assert!(
        confirmed_precision >= 0.99,
        "confirmed matches must be precise, got {:.1}%",
        confirmed_precision * 100.0
    );
    assert!(
        recall >= 0.55,
        "best-candidate recall too low: {:.1}% (expected >= 55%)",
        recall * 100.0
    );
    assert!(
        confirmed.len() >= 50,
        "expected >= 50 confirmed matches, got {}",
        confirmed.len()
    );
}
