//! `canopus target generate`: every artifact derived from a target pack's
//! `target.toml` + `symbols/*.json` (+ family layer), in one place.

use crate::error::{Error, Result};
use crate::family::load_family;
use crate::model::TargetPack;
use crate::registry::{discover_target_dir, list_target_packs, load_target_pack};
use crate::rustgen::RustGen;
use crate::target_config::TargetConfigGen;
use crate::veneer::{VeneerGen, load_records};
use std::path::{Path, PathBuf};

/// One generated output file.
pub struct Output {
    pub path: PathBuf,
    pub content: String,
}

/// Rendered outputs for one target. Paths are relative to the repository root
/// (the parent of `targets_dir`).
pub fn outputs_for(targets_dir: &Path, pack: &TargetPack) -> Result<Vec<Output>> {
    let repo = targets_dir
        .parent()
        .map(Path::to_path_buf)
        .filter(|p| !p.as_os_str().is_empty())
        .unwrap_or_else(|| PathBuf::from("."));
    let root = targets_dir.join(&pack.target_id);
    let (symbols, types) = load_records(&root)?;
    let gen_dir = root.join("generated");
    let mut outputs = vec![
        Output {
            path: gen_dir.join("canopus_veneer.h"),
            content: VeneerGen { pack, symbols: &symbols, types: &types }.generate(),
        },
        Output {
            path: gen_dir.join("canopus_target_config.h"),
            content: TargetConfigGen { pack, symbols: &symbols }.generate(),
        },
    ];
    let Some(rust_module) = pack.rust_module.as_deref() else {
        return Ok(outputs);
    };
    let rust = RustGen { pack, symbols: &symbols, types: &types }.generate();
    let sdk = repo.join("sdk/rust");
    let family = pack
        .family
        .as_deref()
        .map(|f| load_family(targets_dir, f))
        .transpose()?;
    if family.as_ref().is_some_and(|f| f.private_symbols) {
        outputs.push(Output {
            path: sdk.join(format!(
                "canopus-target-private/src/generated_symbols_{rust_module}.rs"
            )),
            content: crate::private_symbols::generate(&pack.target_id, &root, &rust)?,
        });
    }
    outputs.push(Output {
        path: sdk.join(format!(
            "canopus-target-generated/src/generated_{rust_module}.rs"
        )),
        content: rust,
    });
    if family.as_ref().is_some_and(|f| f.native_config) {
        let config = crate::native_config::load(&root, pack)?;
        outputs.push(Output {
            path: gen_dir.join("canopus_band11_addresses.h"),
            content: crate::native_config::render(pack, &config),
        });
    }
    Ok(outputs)
}

/// Targets selected by `--all`: every non-frozen pack.
pub fn active_targets(targets_dir: &Path) -> Result<Vec<TargetPack>> {
    Ok(list_target_packs(targets_dir)?
        .into_iter()
        .filter(|p| p.status.as_deref() != Some("frozen"))
        .collect())
}

pub fn pack_by_id(targets_dir: &Path, id: &str) -> Result<TargetPack> {
    let td = discover_target_dir(&targets_dir.join(id))?;
    load_target_pack(&td.manifest)
}

/// Writes (or with `check`, compares) the outputs. Returns the stale or
/// written paths.
pub fn apply(outputs: &[Output], check: bool) -> Result<Vec<PathBuf>> {
    let mut touched = Vec::new();
    for out in outputs {
        let current = std::fs::read_to_string(&out.path).ok();
        if current.as_deref() == Some(out.content.as_str()) {
            continue;
        }
        if !check {
            if let Some(parent) = out.path.parent() {
                std::fs::create_dir_all(parent)?;
            }
            std::fs::write(&out.path, &out.content)
                .map_err(|e| Error::other(format!("write {}: {e}", out.path.display())))?;
        }
        touched.push(out.path.clone());
    }
    Ok(touched)
}
