//! Subcommand implementations for the canopus CLI.

use canopus_core::error::Error as CoreError;
use canopus_core::model::{ModuleManifest, PackageManifest, Symbol, TargetPack, TypeRecord, EvidenceBundle};
use canopus_core::policy::symbol_policy_check;
use canopus_core::registry::{discover_target_dir, list_target_packs, load_target_pack};
use canopus_core::schema::{validate, SchemaKind};
use canopus_elf::Verifier;
use serde_json::Value;
use std::path::PathBuf;

use crate::{EvidenceCmd, ModuleCmd, PackageCmd, SymbolCmd, TargetCmd, TypeCmd};

// ---------------------------------------------------------------- target

pub fn target(cmd: TargetCmd) -> anyhow::Result<()> {
    match cmd {
        TargetCmd::Validate { path } => {
            let pack = load_target_pack(&path)?;
            println!("target OK: {}", canopus_core::registry::describe(&pack));
            Ok(())
        }
        TargetCmd::List { targets_dir } => {
            let packs = list_target_packs(&targets_dir)?;
            if packs.is_empty() {
                println!("no target packs found under {}", targets_dir.display());
                return Ok(());
            }
            println!("{} registered target(s) under {}:", packs.len(), targets_dir.display());
            for p in &packs {
                println!("  {}", canopus_core::registry::describe(p));
            }
            Ok(())
        }
        TargetCmd::GenerateVeneer {
            target,
            targets_dir,
            output,
        } => {
            let pack = find_target(&targets_dir, &target)?;
            let root = targets_dir.join(&target);
            let (symbols, types) = canopus_core::veneer::load_records(&root)?;
            let gen = canopus_core::veneer::VeneerGen {
                pack: &pack,
                symbols: &symbols,
                types: &types,
            };
            let text = gen.generate();
            let out = output.unwrap_or_else(|| root.join("generated").join("canopus_veneer.h"));
            if let Some(parent) = out.parent() {
                std::fs::create_dir_all(parent)?;
            }
            std::fs::write(&out, text)?;
            println!(
                "wrote veneer header {} ({} callable, {} types, {} symbols)",
                out.display(),
                symbols
                    .iter()
                    .filter(|s| s.kind == "function"
                        && s.callable_address.is_some()
                        && s.status != "FORBIDDEN"
                        && s.status != "WITHDRAWN"
                        && s.policy != "restricted")
                    .count(),
                types.len(),
                symbols.len()
            );
            Ok(())
        }
    }
}

// ---------------------------------------------------------------- symbol

pub fn symbol(cmd: SymbolCmd) -> anyhow::Result<()> {
    match cmd {
        SymbolCmd::Validate { path } => {
            let value = load_json(&path)?;
            validate(SchemaKind::Symbol, &value)?;
            let sym: Symbol = serde_json::from_value(value)?;
            let findings = symbol_policy_check(&sym);
            if findings.is_empty() {
                println!("symbol OK: {}", sym.symbol_id);
            } else {
                for f in &findings {
                    println!("  policy: {f}");
                }
                anyhow::bail!("symbol policy check failed for {}", sym.symbol_id);
            }
            Ok(())
        }
    }
}

// ---------------------------------------------------------------- type

pub fn type_cmd(cmd: TypeCmd) -> anyhow::Result<()> {
    match cmd {
        TypeCmd::Validate { path } => {
            let value = load_json(&path)?;
            validate(SchemaKind::Type, &value)?;
            let t: TypeRecord = serde_json::from_value(value)?;
            println!("type OK: {}", t.type_id);
            Ok(())
        }
    }
}

// ---------------------------------------------------------------- evidence

pub fn evidence(cmd: EvidenceCmd) -> anyhow::Result<()> {
    match cmd {
        EvidenceCmd::Validate { path } => {
            let value = load_json(&path)?;
            validate(SchemaKind::Evidence, &value)?;
            let e: EvidenceBundle = serde_json::from_value(value)?;
            println!("evidence OK: {} verdict={}", e.evidence_id, e.verdict);
            Ok(())
        }
    }
}

// ---------------------------------------------------------------- module

pub fn module(cmd: ModuleCmd) -> anyhow::Result<()> {
    match cmd {
        ModuleCmd::Validate { path } => {
            let value = load_json(&path)?;
            validate(SchemaKind::Module, &value)?;
            let m: ModuleManifest = serde_json::from_value(value)?;
            println!("module OK: {} v{} lifecycle={}", m.module.id, m.module.version, m.module.lifecycle);
            Ok(())
        }
    }
}

// ---------------------------------------------------------------- package

pub fn package(cmd: PackageCmd) -> anyhow::Result<()> {
    match cmd {
        PackageCmd::Validate { path } => {
            let value = load_json(&path)?;
            validate(SchemaKind::Package, &value)?;
            let p: PackageManifest = serde_json::from_value(value)?;
            println!(
                "package OK: {} v{} ({} artifact(s))",
                p.package_id,
                p.version,
                p.artifacts.len()
            );
            Ok(())
        }
    }
}

// ---------------------------------------------------------------- build-plan

pub fn build_plan(manifest_path: &PathBuf, targets_dir: &PathBuf, json: bool) -> anyhow::Result<()> {
    let value = load_json(manifest_path)?;
    validate(SchemaKind::Module, &value)?;
    let module: ModuleManifest = serde_json::from_value(value)?;

    let registry = list_target_packs(targets_dir)?;
    let mut rows = Vec::new();

    for target_id in &module.targets.include {
        let pack = match registry.iter().find(|p| &p.target_id == target_id) {
            Some(p) => p,
            None => {
                anyhow::bail!(
                    "build-plan: target '{}' not registered under {}",
                    target_id,
                    targets_dir.display()
                )
            }
        };
        let req = module
            .capabilities
            .as_ref()
            .map(|c| &c.required)
            .cloned()
            .unwrap_or_default();
        let have = pack.capabilities.clone().unwrap_or_default();
        let missing: Vec<&String> = req.iter().filter(|c| !have.contains(c)).collect();
        if !missing.is_empty() {
            anyhow::bail!(
                "build-plan: target {} lacks required capabilities: {:?}",
                target_id,
                missing
            );
        }
        rows.push(serde_json::json!({
            "target_id": pack.target_id,
            "target_pack_revision": pack.revision,
            "firmware_sha256": pack.firmware_sha256,
            "module_id": module.module.id,
            "module_version": module.module.version,
            "lifecycle": module.module.lifecycle,
            "language": module.module.language,
        }));
    }

    if json {
        println!("{}", serde_json::to_string_pretty(&rows)?);
    } else {
        println!("build plan for {} v{}", module.module.id, module.module.version);
        for r in &rows {
            println!("  {}", serde_json::to_string(r)?);
        }
        println!("  {} target artifact(s)", rows.len());
    }
    Ok(())
}

// ---------------------------------------------------------------- verify

pub fn verify(elf_path: &PathBuf, target_id: &str, targets_dir: &PathBuf, json: bool) -> anyhow::Result<()> {
    let pack = find_target(targets_dir, target_id)?;
    let data = std::fs::read(elf_path)
        .map_err(|e| anyhow::anyhow!("cannot read {}: {e}", elf_path.display()))?;

    // allowed_addresses will be wired to the target symbol table in Phase 3;
    // for now the verifier runs format/policy checks with an empty allowlist.
    let verifier = Verifier {
        target: &pack,
        allowed_addresses: &[],
    };
    let report = verifier.verify(&data);

    if json {
        println!("{}", serde_json::to_string_pretty(&report)?);
    } else {
        println!("sha256: {}", report.sha256.as_deref().unwrap_or("-"));
        for e in &report.errors {
            println!("  ERROR: {e}");
        }
        for w in &report.warnings {
            println!("  warn:  {w}");
        }
        let s = &report.summary;
        println!(
            "summary: {} ({} sections, {} undefined, {} relocs, {} ctors, {} dtors)",
            if report.ok { "PASS" } else { "FAIL" },
            s.section_count,
            s.undefined_symbols,
            s.relocation_count,
            s.constructor_count,
            s.destructor_count
        );
    }
    if !report.ok {
        std::process::exit(1);
    }
    Ok(())
}

// ---------------------------------------------------------------- helpers

fn load_json(path: &PathBuf) -> anyhow::Result<Value> {
    let text = std::fs::read_to_string(path)
        .map_err(|e| anyhow::anyhow!("cannot read {}: {e}", path.display()))?;
    match path.extension().and_then(|e| e.to_str()) {
        Some("json") => Ok(serde_json::from_str(&text)?),
        Some("toml") | Some("tml") => {
            let t: toml::Value = toml::from_str(&text)?;
            Ok(serde_json::to_value(t)?)
        }
        other => anyhow::bail!(
            "unsupported file extension {other:?} for {} (expected .json or .toml)",
            path.display()
        ),
    }
}

fn find_target(targets_dir: &PathBuf, target_id: &str) -> anyhow::Result<TargetPack> {
    let dir = targets_dir.join(target_id);
    let td = discover_target_dir(&dir)?;
    load_target_pack(&td.manifest).map_err(|e| {
        let msg = match e {
            CoreError::SchemaValidation { message, .. } => message,
            other => other.to_string(),
        };
        anyhow::anyhow!("target {}: {msg}", target_id)
    })
}
