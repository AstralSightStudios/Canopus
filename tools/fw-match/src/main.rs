//! canopus-fw-match CLI: match a source target-pack's symbols into a target
//! firmware corpus.
//!
//! Usage:
//!   fw-match --symbols <source-target>/symbols \
//!            --source-corpus <source-corpus.json> \
//!            --target-corpus <target-corpus.json> \
//!            --output matches.json \
//!            [--seed N] [--pool N] [--pop N] [--gens N]

use std::path::PathBuf;

use canopus_fw_match::corpus::load_corpus;
use canopus_fw_match::engine::{EngineConfig, confirm, load_source_symbols, match_symbols};
use canopus_fw_match::ga::GaParams;
use canopus_fw_match::globals::{load_source_globals, match_globals};

fn main() -> anyhow::Result<()> {
    let args: Vec<String> = std::env::args().collect();
    let mut symbols: Option<PathBuf> = None;
    let mut src_corpus: Option<PathBuf> = None;
    let mut dst_corpus: Option<PathBuf> = None;
    let mut output: Option<PathBuf> = None;
    let mut seed: u64 = 0x5EED_CAFE;
    let mut pool = 24usize;
    let mut pop = 96usize;
    let mut gens = 240usize;
    let mut min_score = 5.0f64;
    let mut min_margin = 0.15f64;

    let mut i = 1;
    while i < args.len() {
        match args[i].as_str() {
            "--symbols" => {
                symbols = Some(PathBuf::from(args[i + 1].clone()));
                i += 2;
            }
            "--source-corpus" => {
                src_corpus = Some(PathBuf::from(args[i + 1].clone()));
                i += 2;
            }
            "--target-corpus" => {
                dst_corpus = Some(PathBuf::from(args[i + 1].clone()));
                i += 2;
            }
            "--output" => {
                output = Some(PathBuf::from(args[i + 1].clone()));
                i += 2;
            }
            "--seed" => {
                seed = args[i + 1].parse()?;
                i += 2;
            }
            "--pool" => {
                pool = args[i + 1].parse()?;
                i += 2;
            }
            "--pop" => {
                pop = args[i + 1].parse()?;
                i += 2;
            }
            "--gens" => {
                gens = args[i + 1].parse()?;
                i += 2;
            }
            "--min-score" => {
                min_score = args[i + 1].parse()?;
                i += 2;
            }
            "--min-margin" => {
                min_margin = args[i + 1].parse()?;
                i += 2;
            }
            other => {
                anyhow::bail!("unknown argument: {other}");
            }
        }
    }

    let symbols = symbols.ok_or_else(|| anyhow::anyhow!("--symbols is required"))?;
    let src_corpus = src_corpus.ok_or_else(|| anyhow::anyhow!("--source-corpus is required"))?;
    let dst_corpus = dst_corpus.ok_or_else(|| anyhow::anyhow!("--target-corpus is required"))?;
    let output = output.unwrap_or_else(|| PathBuf::from("matches.json"));

    let syms = load_source_symbols(&symbols).map_err(anyhow::Error::msg)?;
    let global_syms = load_source_globals(&symbols).map_err(anyhow::Error::msg)?;
    let src = load_corpus(&src_corpus).map_err(anyhow::Error::msg)?;
    let dst = load_corpus(&dst_corpus).map_err(anyhow::Error::msg)?;

    let cfg = EngineConfig {
        ga: GaParams {
            population: pop,
            generations: gens,
            ..GaParams::default()
        },
        seed,
        max_pool: pool,
    };

    eprintln!(
        "matching {} function and {} global symbols ({} source fns) -> {} target fns",
        syms.len(),
        global_syms.len(),
        src.functions.len(),
        dst.functions.len()
    );

    let (results, _best) = match_symbols(&syms, &src, &dst, &cfg);
    let confirmed = confirm(&results, min_score, min_margin);
    let global_results = match_globals(&global_syms, &src, &dst, &results);

    let report = serde_json::json!({
        "schema": 2,
        "source_target_id": src.target_id,
        "target_target_id": dst.target_id,
        "params": {
            "seed": seed,
            "pool": pool,
            "pop": pop,
            "gens": gens,
            "min_score": min_score,
            "min_margin": min_margin,
        },
        "num_symbols": syms.len(),
        "num_matched": results.iter().filter(|r| r.target_addr.is_some()).count(),
        "num_confirmed": confirmed.len(),
        "num_globals": global_syms.len(),
        "num_global_candidates": global_results.iter().filter(|item| item.target_addr.is_some()).count(),
        "matches": results,
        "confirmed": confirmed,
        "global_matches": global_results,
    });

    std::fs::write(&output, serde_json::to_string_pretty(&report)?)?;
    eprintln!(
        "wrote {} ({} functions confirmed, {} global candidates)",
        output.display(),
        confirmed.len(),
        global_results
            .iter()
            .filter(|item| item.target_addr.is_some())
            .count()
    );
    Ok(())
}
