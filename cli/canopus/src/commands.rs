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
        TargetCmd::List { targets_dir } => {
            let packs = list_target_packs(&targets_dir)?;
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
        TargetCmd::GenerateVeneer {
            target,
            targets_dir,
            output,
        } => {
            let pack = find_target(&targets_dir, &target)?;
            let root = targets_dir.join(&target);
            let (symbols, types) = canopus_core::veneer::load_records(&root)?;
            let r#gen = canopus_core::veneer::VeneerGen {
                pack: &pack,
                symbols: &symbols,
                types: &types,
            };
            let text = r#gen.generate();
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
        TargetCmd::GenerateRustBindings {
            target,
            targets_dir,
            output,
        } => {
            let pack = find_target(&targets_dir, &target)?;
            let root = targets_dir.join(&target);
            let (symbols, types) = canopus_core::veneer::load_records(&root)?;
            let r#gen = canopus_core::rustgen::RustGen {
                pack: &pack,
                symbols: &symbols,
                types: &types,
            };
            let text = r#gen.generate();
            /* CAN-P2-011: the sdk/rust crate's generated.rs is the single
             * committed copy; the per-target generated/ directory holds the
             * C veneer only. */
            let out = output.unwrap_or_else(|| {
                root.join("../../sdk/rust/canopus-target-generated/src/generated.rs")
            });
            if let Some(parent) = out.parent() {
                std::fs::create_dir_all(parent)?;
            }
            std::fs::write(&out, text)?;
            println!(
                "wrote Rust bindings {} ({} callable, {} types, {} symbols)",
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
            out_dir,
        } => {
            let lang = match lang.as_str() {
                "c" => canopus_core::template::ModuleLang::C,
                "rust" => canopus_core::template::ModuleLang::Rust,
                other => anyhow::bail!("unknown language '{other}' (expected c or rust)"),
            };
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
            key,
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
            let signed = match key {
                Some(k) => canopus_package::sign_archive(&archive, &k)?,
                None => archive,
            };
            let len = signed.len();
            std::fs::write(&output, &signed)?;
            println!(
                "wrote {} ({} bytes, {} artifact(s))",
                output.display(),
                len,
                pkg.artifacts.len()
            );
            Ok(())
        }
        PackageCmd::Sign { pkg, key, output } => {
            let archive = std::fs::read(&pkg)?;
            let signed = canopus_package::sign_archive(&archive, &key)?;
            let out = output.unwrap_or(pkg);
            std::fs::write(&out, signed)?;
            println!("signed {} ({}-byte signature appended)", out.display(), 64);
            Ok(())
        }
        PackageCmd::Verify { pkg, pubkey } => {
            let archive = std::fs::read(&pkg)?;
            match canopus_package::verify_archive(&archive, &pubkey) {
                Ok(()) => {
                    println!("signature OK");
                    Ok(())
                }
                Err(e) => Err(anyhow::anyhow!("{e}")),
            }
        }
        PackageCmd::Keygen { output } => {
            let (secret, public) = canopus_package::keygen()?;
            if let Some(out) = output {
                std::fs::write(&out, format!("{secret}\n{public}\n"))?;
                println!("wrote key pair to {}", out.display());
            } else {
                println!("secret: {secret}");
                println!("public: {public}");
            }
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
        ReCmd::RevisionSign {
            target,
            revision,
            key,
            output,
            targets_dir,
        } => re_revision_sign(&target, revision, &key, &output, &targets_dir),
        ReCmd::RevisionVerify { manifest, pubkey } => {
            let data = std::fs::read(&manifest)?;
            let signed: serde_json::Value = serde_json::from_slice(&data)?;
            let m: canopus_re::revision::RevisionManifest =
                serde_json::from_value(signed["manifest"].clone())?;
            let sig = signed["signature"]
                .as_str()
                .ok_or_else(|| anyhow::anyhow!("no signature in manifest"))?;
            m.verify(&pubkey, sig)
                .map_err(|e| anyhow::anyhow!("verify failed: {e}"))?;
            println!(
                "revision {} of {}: signature VALID",
                m.revision, m.target_id
            );
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

/// Signs a target-pack revision manifest (CAN-RE-009).
fn re_revision_sign(
    target: &str,
    revision: u32,
    key: &str,
    output: &Option<PathBuf>,
    targets_dir: &Path,
) -> anyhow::Result<()> {
    let pack = find_target(targets_dir, target)?;
    let root = targets_dir.join(target);
    let (symbols, types) = canopus_core::veneer::load_records(&root)?;
    let evidence_dir = root.join("evidence");

    // content digest over symbols + types (sorted, stable).
    let mut entries: Vec<Vec<u8>> = Vec::new();
    for sub in ["symbols", "types"] {
        let d = root.join(sub);
        if !d.is_dir() {
            continue;
        }
        let mut files: Vec<_> = std::fs::read_dir(&d)?
            .filter_map(|e| e.ok())
            .map(|e| e.path())
            .filter(|p| p.extension().and_then(|e| e.to_str()) == Some("json"))
            .collect();
        files.sort();
        for f in files {
            entries.push(std::fs::read(&f)?);
        }
    }
    let content_sha256 = {
        use sha2::{Digest, Sha256};
        let mut h = Sha256::new();
        for e in &entries {
            h.update(e);
        }
        hex::encode(h.finalize())
    };
    let evidence_count = if evidence_dir.is_dir() {
        std::fs::read_dir(&evidence_dir)?.count() as u32
    } else {
        0
    };

    let m = canopus_re::revision::RevisionManifest {
        target_id: pack.target_id.clone(),
        revision,
        firmware_sha256: pack.firmware_sha256.clone(),
        content_sha256,
        symbol_count: symbols.len() as u32,
        type_count: types.len() as u32,
        evidence_count,
        schema_version: 1,
    };
    let signature = m
        .sign(key)
        .map_err(|e| anyhow::anyhow!("sign failed: {e}"))?;
    let doc = serde_json::json!({ "manifest": m, "signature": signature });
    let out = output.clone().unwrap_or_else(|| {
        root.join("generated")
            .join(format!("revision-v{revision}.json"))
    });
    if let Some(p) = out.parent() {
        std::fs::create_dir_all(p)?;
    }
    std::fs::write(&out, serde_json::to_vec_pretty(&doc)?)?;
    println!(
        "signed revision {revision} of {target} (digest {}) -> {}",
        m.canonical_digest(),
        out.display()
    );
    Ok(())
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

// ---------------------------------------------------------------- key

use crate::KeyCmd;
use canopus_package::keyroles::{KeyCert, KeyRole, RevocationList, certify, check_cert_revoked};

fn parse_role(s: &str) -> anyhow::Result<KeyRole> {
    Ok(match s {
        "dev" => KeyRole::Dev,
        "production" => KeyRole::Production,
        other => anyhow::bail!("unknown key role '{other}' (expected dev or production)"),
    })
}

pub fn key(cmd: KeyCmd) -> anyhow::Result<()> {
    match cmd {
        KeyCmd::RoleCert { role, public, note } => {
            let role = parse_role(&role)?;
            let cert = certify(role, &public, note.as_deref().unwrap_or(""));
            let text = serde_json::to_string_pretty(&cert)?;
            println!("{text}");
            Ok(())
        }
        KeyCmd::Revoke {
            fingerprint,
            role,
            key,
            list,
        } => {
            let role = parse_role(&role)?;
            let path = std::path::Path::new(&list);
            let mut rl = if path.exists() {
                RevocationList::load(path).map_err(|e| anyhow::anyhow!(e))?
            } else {
                RevocationList::new_empty(role)
            };
            rl.add(&fingerprint);
            rl.sign(&key).map_err(|e| anyhow::anyhow!("sign: {e}"))?;
            rl.save(path).map_err(|e| anyhow::anyhow!(e))?;
            println!(
                "revoked {} (list now has {} entries, signer={})",
                fingerprint,
                rl.revoked.len(),
                rl.signer_role.as_str()
            );
            Ok(())
        }
        KeyCmd::Check { cert, list } => {
            let path = std::path::Path::new(&list);
            if !path.exists() {
                anyhow::bail!("revocation list not found: {list}");
            }
            let rl = RevocationList::load(path).map_err(|e| anyhow::anyhow!(e))?;
            let cdata = std::fs::read_to_string(&cert)?;
            let c: KeyCert = serde_json::from_str(&cdata)?;
            rl.verify(&c).map_err(|e| anyhow::anyhow!(e))?;
            match check_cert_revoked(&c, &rl) {
                Ok(()) => println!(
                    "cert {} ({}): NOT revoked",
                    c.role.as_str(),
                    &c.fingerprint[..16.min(c.fingerprint.len())]
                ),
                Err(e) => {
                    println!("{}", e);
                    std::process::exit(2);
                }
            }
            Ok(())
        }
    }
}
