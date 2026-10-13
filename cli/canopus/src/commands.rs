//! Subcommand implementations for the canopus CLI.

use canopus_core::error::Error as CoreError;
use canopus_core::model::{
    EvidenceBundle, ModuleManifest, PackageManifest, Symbol, TargetPack, TypeRecord,
};
use canopus_core::policy::symbol_policy_check;
use canopus_core::registry::{discover_target_dir, list_target_packs, load_target_pack};
use canopus_core::schema::{SchemaKind, validate};
use canopus_elf::Verifier;
use serde_json::Value;
use std::path::{Path, PathBuf};

use crate::{EvidenceCmd, ModuleCmd, PackageCmd, SymbolCmd, TargetCmd, TypeCmd};

// ---------------------------------------------------------------- target

pub fn target(cmd: TargetCmd) -> anyhow::Result<()> {
    match cmd {
        TargetCmd::Validate { path } => {
            let pack = load_target_pack(&path)?;
            println!("target OK: {}", canopus_core::registry::describe(&pack));
            Ok(())
        }
        TargetCmd::List {
            targets_dir,
            active,
            names,
        } => {
            let mut packs = list_target_packs(&targets_dir)?;
            if active {
                packs.retain(|p| p.status.as_deref() != Some("frozen"));
            }
            if names {
                for p in &packs {
                    println!("{}", p.target_id);
                }
                return Ok(());
            }
            if packs.is_empty() {
                println!("no target packs found under {}", targets_dir.display());
                return Ok(());
            }
            println!(
                "{} registered target(s) under {}:",
                packs.len(),
                targets_dir.display()
            );
            for p in &packs {
                println!("  {}", canopus_core::registry::describe(p));
            }
            Ok(())
        }
        TargetCmd::Generate {
            target,
            all,
            check,
            targets_dir,
        } => {
            let packs = match (target, all) {
                (Some(id), false) => vec![canopus_core::generate::pack_by_id(&targets_dir, &id)?],
                (None, true) => canopus_core::generate::active_targets(&targets_dir)?,
                _ => anyhow::bail!("specify either a target id or --all"),
            };
            let mut stale = Vec::new();
            for pack in &packs {
                let outputs = canopus_core::generate::outputs_for(&targets_dir, pack)?;
                for path in canopus_core::generate::apply(&outputs, check)? {
                    if check {
                        stale.push(path);
                    } else {
                        println!("wrote {}", path.display());
                    }
                }
            }
            if !stale.is_empty() {
                for path in &stale {
                    eprintln!("stale: {}", path.display());
                }
                anyhow::bail!(
                    "{} generated file(s) are stale; run `canopus target generate --all`",
                    stale.len()
                );
            }
            if check {
                println!("generated files up to date ({} target(s))", packs.len());
            }
            Ok(())
        }
        TargetCmd::Info {
            target,
            targets_dir,
            shell,
            json,
        } => {
            let pack = canopus_core::generate::pack_by_id(&targets_dir, &target)?;
            let family = pack
                .family
                .as_deref()
                .map(|f| canopus_core::family::load_family(&targets_dir, f))
                .transpose()?;
            if shell {
                let family = family.ok_or_else(|| {
                    anyhow::anyhow!("target {target} has no family; no build data")
                })?;
                let q = |s: &str| format!("'{}'", s.replace('\'', "'\\''"));
                println!("MANAGER_BACKEND={}", q(&family.manager_backend));
                println!("MAX_SIZE={}", family.supervisor_max_size);
                println!("LOADER_SRCS={}", q(&family.loader_sources.join(" ")));
                println!("TARGET_DEFINES={}", q(&family.defines.join(" ")));
                println!("PROD_FAMILY={}", q(&family.prod_family));
            } else if json {
                let mut value = serde_json::json!({ "pack": pack, "family": family });
                if family.as_ref().is_some_and(|f| f.native_config) {
                    let cfg = canopus_core::native_config::load(&targets_dir.join(&target), &pack)?;
                    value["native"] = serde_json::json!({
                        "profile": cfg.profile,
                        "addresses": cfg.addresses,
                    });
                }
                println!("{}", serde_json::to_string_pretty(&value)?);
            } else {
                println!("{}", canopus_core::registry::describe(&pack));
            }
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
            println!(
                "module OK: {} v{} lifecycle={}",
                m.module.id, m.module.version, m.module.lifecycle
            );
            Ok(())
        }
        ModuleCmd::New {
            name,
            lang,
            target,
            targets_dir,
            out_dir,
        } => {
            let lang = match lang.as_str() {
                "c" => canopus_core::template::ModuleLang::C,
                "rust" => canopus_core::template::ModuleLang::Rust,
                other => anyhow::bail!("unknown language '{other}' (expected c or rust)"),
            };
            find_target(&targets_dir, &target)?;
            let files = canopus_core::template::render(&name, &target, lang);
            let dir = out_dir.join(&name);
            std::fs::create_dir_all(&dir)?;
            let mut n = 0;
            for (path, contents) in &files {
                let full = dir.join(path);
                if let Some(parent) = full.parent() {
                    std::fs::create_dir_all(parent)?;
                }
                std::fs::write(&full, contents)?;
                n += 1;
            }
            // build.sh must be executable
            let sh = dir.join("build.sh");
            #[cfg(unix)]
            {
                use std::os::unix::fs::PermissionsExt;
                if sh.exists() {
                    let mut p = std::fs::metadata(&sh)?.permissions();
                    p.set_mode(0o755);
                    std::fs::set_permissions(&sh, p)?;
                }
            }
            println!(
                "scaffolded {} module '{}' in {} ({} files, target {})",
                match lang {
                    canopus_core::template::ModuleLang::C => "C",
                    canopus_core::template::ModuleLang::Rust => "Rust",
                },
                name,
                dir.display(),
                n,
                target
            );
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
        PackageCmd::Build {
            manifest,
            artifact,
            resource,
            output,
        } => {
            let value = load_json(&manifest)?;
            validate(SchemaKind::Package, &value)?;
            let pkg: PackageManifest = serde_json::from_value(value)?;

            // parse target_id=path pairs
            let mut files = std::collections::HashMap::new();
            for a in &artifact {
                let (tid, path) = a
                    .split_once('=')
                    .ok_or_else(|| anyhow::anyhow!("--artifact must be target_id=path: {a}"))?;
                files.insert(tid.to_string(), PathBuf::from(path));
            }
            // CAN-P1-013: declared_path=file pairs for native-app resources
            let mut resource_files = std::collections::HashMap::new();
            for a in resource {
                let (path, file) = a
                    .split_once('=')
                    .ok_or_else(|| anyhow::anyhow!("--resource must be path=file: {a}"))?;
                resource_files.insert(path.to_string(), PathBuf::from(file));
            }

            let pkg = canopus_package::manifest_with_real_hashes(&pkg, &files, &resource_files)?;
            let archive = canopus_package::build_archive(&pkg, &files, &resource_files)?;
            let len = archive.len();
            std::fs::write(&output, &archive)?;
            println!(
                "wrote {} ({} bytes, {} artifact(s))",
                output.display(),
                len,
                pkg.artifacts.len()
            );
            Ok(())
        }
        PackageCmd::Verify { pkg } => {
            let archive = std::fs::read(&pkg)?;
            let manifest = canopus_package::verify_archive(&archive)?;
            println!(
                "package OK: {} v{} ({} artifact(s))",
                manifest.package_id,
                manifest.version,
                manifest.artifacts.len()
            );
            Ok(())
        }
    }
}

// ---------------------------------------------------------------- build-plan

pub fn build_plan(manifest_path: &Path, targets_dir: &Path, json: bool) -> anyhow::Result<()> {
    let value = load_json(manifest_path)?;
    validate(SchemaKind::Module, &value)?;
    let module: ModuleManifest = serde_json::from_value(value)?;

    let registry = list_target_packs(targets_dir)?;
    let rows = canopus_core::planner::expand(&module, &registry)
        .map_err(|e| anyhow::anyhow!("build-plan: {e}"))?;
    let rows: Vec<serde_json::Value> = rows
        .iter()
        .map(|r| {
            serde_json::json!({
                "target_id": r.target_id,
                "target_pack_revision": r.target_pack_revision,
                "firmware_sha256": r.firmware_sha256,
                "module_id": r.module_id,
                "module_version": r.module_version,
                "lifecycle": module.module.lifecycle,
                "language": module.module.language,
            })
        })
        .collect();

    if json {
        println!("{}", serde_json::to_string_pretty(&rows)?);
    } else {
        println!(
            "build plan for {} v{}",
            module.module.id, module.module.version
        );
        for r in &rows {
            println!("  {}", serde_json::to_string(r)?);
        }
        println!("  {} target artifact(s)", rows.len());
    }
    Ok(())
}

// ---------------------------------------------------------------- verify

pub fn verify(
    elf_path: &Path,
    target_id: &str,
    targets_dir: &Path,
    json: bool,
) -> anyhow::Result<()> {
    let pack = find_target(targets_dir, target_id)?;
    let data = std::fs::read(elf_path)
        .map_err(|e| anyhow::anyhow!("cannot read {}: {e}", elf_path.display()))?;

    // CAN-P1-011: the absolute-address allowlist is the target's own symbol
    // table (entry + callable addresses). Embedded firmware-range addresses
    // in the module must all appear here.
    let allowed = build_allowed_addresses(targets_dir, target_id);
    let verifier = Verifier {
        target: &pack,
        allowed_addresses: &allowed,
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

/// CAN-P1-011: build the absolute-address allowlist from the target's own
/// symbol records (entry + callable addresses). Every firmware-range address
/// a module may legitimately reference must come from this table.
fn build_allowed_addresses(targets_dir: &Path, target_id: &str) -> Vec<u64> {
    let sym_dir = targets_dir.join(target_id).join("symbols");
    let mut addrs = Vec::new();
    let Ok(entries) = std::fs::read_dir(&sym_dir) else {
        return addrs;
    };
    for entry in entries.flatten() {
        let p = entry.path();
        if p.extension().and_then(|e| e.to_str()) != Some("json") {
            continue;
        }
        let Ok(text) = std::fs::read_to_string(&p) else {
            continue;
        };
        let Ok(sym) = serde_json::from_str::<canopus_core::model::Symbol>(&text) else {
            continue;
        };
        if let Some(a) = sym.entry_address.as_deref().and_then(parse_hex_addr) {
            addrs.push(a);
        }
        if let Some(a) = sym.callable_address.as_deref().and_then(parse_hex_addr) {
            addrs.push(a);
        }
    }
    addrs.sort_unstable();
    addrs.dedup();
    addrs
}

fn parse_hex_addr(s: &str) -> Option<u64> {
    let t = s.trim().trim_start_matches("0x").trim_start_matches("0X");
    u64::from_str_radix(t, 16).ok()
}

fn load_json(path: &Path) -> anyhow::Result<Value> {
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

fn find_target(targets_dir: &Path, target_id: &str) -> anyhow::Result<TargetPack> {
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

// ---------------------------------------------------------------- re

/// Default on-disk RE store path (append-only task/evidence audit).
const RE_STORE_PATH: &str = ".canopus-re-store.json";

use crate::ReCmd;

fn load_re_store() -> anyhow::Result<canopus_re::ReStore> {
    let p = std::path::Path::new(RE_STORE_PATH);
    if p.exists() {
        Ok(canopus_re::ReStore::load(p)?)
    } else {
        Ok(canopus_re::ReStore::new())
    }
}

fn save_re_store(s: &canopus_re::ReStore) -> anyhow::Result<()> {
    s.save(std::path::Path::new(RE_STORE_PATH))?;
    Ok(())
}

fn parse_task_state(s: &str) -> anyhow::Result<canopus_re::ReTaskState> {
    Ok(match s {
        "analyzing" => canopus_re::ReTaskState::Analyzing,
        "evidence-gathered" => canopus_re::ReTaskState::EvidenceGathered,
        "verifying" => canopus_re::ReTaskState::Verifying,
        "promoted" => canopus_re::ReTaskState::Promoted,
        "rejected" => canopus_re::ReTaskState::Rejected,
        "withdrawn" => canopus_re::ReTaskState::Withdrawn,
        other => anyhow::bail!("unknown task state '{other}'"),
    })
}

fn parse_evidence_state(s: &str) -> anyhow::Result<canopus_re::EvidenceState> {
    Ok(match s {
        "candidate" => canopus_re::EvidenceState::Candidate,
        "verified" => canopus_re::EvidenceState::Verified,
        "promoted" => canopus_re::EvidenceState::Promoted,
        "refuted" => canopus_re::EvidenceState::Refuted,
        "withdrawn" => canopus_re::EvidenceState::Withdrawn,
        other => anyhow::bail!("unknown evidence state '{other}'"),
    })
}

pub fn re(cmd: ReCmd) -> anyhow::Result<()> {
    match cmd {
        ReCmd::NewTask { id, target, desc } => {
            let mut store = load_re_store()?;
            store.add_task(&id, &desc, &target, "-", "cli");
            save_re_store(&store)?;
            println!("task {id} created on target {target}");
            Ok(())
        }
        ReCmd::TransitionTask { id, state } => {
            let mut store = load_re_store()?;
            let to = parse_task_state(&state)?;
            store.transition_task(&id, to, "cli")?;
            save_re_store(&store)?;
            println!("task {id} -> {state}");
            Ok(())
        }
        ReCmd::AddEvidence {
            task,
            id,
            kind,
            summary,
        } => {
            let mut store = load_re_store()?;
            let rec = canopus_re::EvidenceRecord {
                evidence_id: id.clone(),
                task_id: task.clone(),
                state: canopus_re::EvidenceState::Draft,
                kind,
                summary,
                artifact_uris: Vec::new(),
                reviews: Vec::new(),
            };
            store.add_evidence(rec, "cli")?;
            save_re_store(&store)?;
            println!("evidence {id} added to task {task}");
            Ok(())
        }
        ReCmd::TransitionEvidence { id, state } => {
            let mut store = load_re_store()?;
            let to = parse_evidence_state(&state)?;
            store.transition_evidence(&id, to, "cli")?;
            save_re_store(&store)?;
            println!("evidence {id} -> {state}");
            Ok(())
        }
        ReCmd::Gate { id, needed } => {
            let store = load_re_store()?;
            let rec = store
                .evidence
                .get(&id)
                .ok_or_else(|| anyhow::anyhow!("unknown evidence {id}"))?;
            let d = canopus_re::verify::evaluate_gate(&rec.reviews, needed, 1);
            println!("gate for {id}: {d:?}");
            Ok(())
        }
        ReCmd::Probe {
            target,
            symbol,
            targets_dir,
            output,
        } => re_probe(&target, &symbol, &targets_dir, &output),
    }
}

/// Emits a minimal, safe C probe module for a callable symbol (RE-008).
fn re_probe(
    target: &str,
    symbol: &str,
    targets_dir: &Path,
    output: &Option<PathBuf>,
) -> anyhow::Result<()> {
    let root = targets_dir.join(target);
    let (symbols, _types) = canopus_core::veneer::load_records(&root)?;
    let sym = symbols
        .iter()
        .find(|s| s.name == symbol)
        .ok_or_else(|| anyhow::anyhow!("symbol '{symbol}' not in target {target}"))?;
    if sym.status == "FORBIDDEN" || sym.status == "WITHDRAWN" {
        anyhow::bail!("symbol '{symbol}' is {}. no probe may call it.", sym.status);
    }
    if sym.kind != "function" {
        anyhow::bail!("symbol '{symbol}' is not a function");
    }
    let proto = sym.prototype.clone().unwrap_or_else(|| "unknown".into());

    // A dry probe: identity guard + status only. The recovered call is left
    // commented out until a device gate approves executing it.
    let text = format!(
        r#"/* probe_{name}.c — minimal probe for {name} (CAN-RE-008). GENERATED. */
/* Risk: read-only. Verifies identity + module status only; the recovered
 * call site is left commented until a device gate approves execution.
 * Recovery: rmmod probe_{name}. Recovery scope: none beyond the probe. */
#include "canopus_veneer.h"
#include "canopus_abi.h"

int probe_{name}_run(void)
{{
    if (canopus_identity_guard() != 0) {{
        return -1; /* wrong firmware: refuse */
    }}
    /* recovered prototype: {proto} */
    /* int rc = canopus_fw_{name}(/* args *\/); */
    return 0;
}}
"#,
        name = symbol
    );
    let out = output
        .clone()
        .unwrap_or_else(|| root.join("generated").join(format!("probe_{symbol}.c")));
    if let Some(p) = out.parent() {
        std::fs::create_dir_all(p)?;
    }
    std::fs::write(&out, text)?;
    println!("wrote probe {}", out.display());
    Ok(())
}
