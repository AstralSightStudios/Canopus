//! Target registry: discover, parse and validate target packs.

use crate::error::{Error, Result};
use crate::model::TargetPack;
use crate::schema::{SchemaKind, validate};
use serde_json::Value;
use std::path::{Path, PathBuf};

/// Load a target pack from either a `.toml` or `.json` file.
///
/// TOML and JSON map to the same object shape for the target schema.
pub fn load_target_pack(path: &Path) -> Result<TargetPack> {
    let text = std::fs::read_to_string(path)
        .map_err(|e| Error::other(format!("cannot read {}: {e}", path.display())))?;
    let value: Value = match path.extension().and_then(|e| e.to_str()) {
        Some("json") => serde_json::from_str(&text)?,
        Some("toml") | Some("tml") => {
            let t: toml::Value = toml::from_str(&text)?;
            serde_json::to_value(t).map_err(Error::Json)?
        }
        other => {
            return Err(Error::other(format!(
                "unsupported target file extension {other:?} (expected .json or .toml)"
            )));
        }
    };
    validate(SchemaKind::Target, &value)?;
    let pack: TargetPack = serde_json::from_value(value)?;
    Ok(pack)
}

/// The canonical layout of a target pack directory.
#[derive(Debug, Clone)]
pub struct TargetPackDir {
    pub root: PathBuf,
    pub manifest: PathBuf,
}

/// Find the manifest file in a target pack directory.
/// Prefers `target.toml` over `target.json`.
pub fn discover_target_dir(dir: &Path) -> Result<TargetPackDir> {
    for name in ["target.toml", "target.json"] {
        let p = dir.join(name);
        if p.is_file() {
            return Ok(TargetPackDir {
                root: dir.to_path_buf(),
                manifest: p,
            });
        }
    }
    Err(Error::other(format!(
        "no target.toml/target.json in {}",
        dir.display()
    )))
}

/// Enumerate all target packs under `targets_root` (one subdir per target_id).
pub fn list_target_packs(targets_root: &Path) -> Result<Vec<TargetPack>> {
    let mut out = Vec::new();
    if !targets_root.is_dir() {
        return Ok(out);
    }
    for entry in std::fs::read_dir(targets_root)? {
        let entry = entry?;
        if !entry.file_type()?.is_dir() {
            continue;
        }
        match discover_target_dir(&entry.path()) {
            Ok(td) => match load_target_pack(&td.manifest) {
                Ok(pack) => out.push(pack),
                Err(e) => {
                    eprintln!("warn: target dir {} skipped: {e}", entry.path().display())
                }
            },
            Err(_) => continue,
        }
    }
    out.sort_by(|a, b| a.target_id.cmp(&b.target_id));
    Ok(out)
}

/// Check whether `firmware_sha256` (full hash) matches the pack identity.
pub fn identity_matches(pack: &TargetPack, firmware_sha256: &str) -> bool {
    pack.firmware_sha256.eq_ignore_ascii_case(firmware_sha256)
}

/// Human-readable one-line description of a target pack.
pub fn describe(pack: &TargetPack) -> String {
    format!(
        "{} ({} {}) sha256={} rev={}",
        pack.target_id,
        pack.device_model,
        pack.firmware_version,
        &pack.firmware_sha256[..16],
        pack.revision
    )
}
