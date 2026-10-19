//! canopus-package: deterministic .canopus package assembly and integrity
//! verification (CAN-PKG-001/002/003). Packages carry no signature (K9);
//! integrity is the per-artifact SHA-256 recorded in the manifest.
//!
//! Layout inside the tar (deterministic: sorted paths, mtime=0):
//!   manifest.json           canonical manifest (validated against schema)
//!   artifacts/<target_id>/module.elf   per-target payload
//!   reports/elf-verifier.json          verifier output (if provided)

use canopus_core::error::{Error, Result};
use canopus_core::model::{PackageManifest, PackageResource};
use sha2::{Digest, Sha256};
use std::collections::HashMap;
use std::path::PathBuf;
use tar::{Builder, EntryType, Header};

/// Recursively removes `null` object members so an in-memory manifest (with
/// `None` Options serialized as null) validates like a hand-written one.
fn strip_nulls(v: &mut serde_json::Value) {
    match v {
        serde_json::Value::Object(map) => {
            map.retain(|_, val| !val.is_null());
            for val in map.values_mut() {
                strip_nulls(val);
            }
        }
        serde_json::Value::Array(arr) => {
            for val in arr.iter_mut() {
                strip_nulls(val);
            }
        }
        _ => {}
    }
}

/// CAN-P0-003: canonical, security-bounded archive entry path. Rejects
/// absolute paths, `..` / `.` / empty components, backslash confusion, NUL
/// and any non-regular file type (symlink, hardlink, device node, FIFO,
/// directory). Every entry path is validated at build, at extract and at
/// verify so the signed payload and the unpacked tree can never diverge.
pub fn validate_entry(name: &str, entry_type: &EntryType) -> Result<()> {
    if name.is_empty() {
        return Err(Error::other("package archive: empty entry path"));
    }
    if name.contains('\0') {
        return Err(Error::other("package archive: NUL in entry path"));
    }
    if name.starts_with('/') {
        return Err(Error::other(format!(
            "package archive: absolute path '{name}'"
        )));
    }
    if name.contains('\\') {
        return Err(Error::other(format!(
            "package archive: backslash in path '{name}'"
        )));
    }
    for comp in name.split('/') {
        if comp.is_empty() {
            return Err(Error::other(format!(
                "package archive: empty path component in '{name}'"
            )));
        }
        if comp == "." || comp == ".." {
            return Err(Error::other(format!(
                "package archive: traversal component in '{name}'"
            )));
        }
    }
    if *entry_type != EntryType::Regular {
        return Err(Error::other(format!(
            "package archive: non-regular entry type for '{name}'"
        )));
    }
    Ok(())
}

/// Adds an entry; an identical duplicate path is stored once, conflicting
/// content for one path is an error.
fn push_entry(entries: &mut Vec<(String, Vec<u8>)>, path: String, bytes: Vec<u8>) -> Result<()> {
    match entries.iter().find(|(p, _)| *p == path) {
        Some((_, existing)) if *existing == bytes => Ok(()),
        Some(_) => Err(Error::other(format!(
            "package build: conflicting content for '{path}'"
        ))),
        None => {
            entries.push((path, bytes));
            Ok(())
        }
    }
}

/// Assembles a deterministic tar archive from a manifest and the on-disk
/// artifacts it references. Verifies each artifact SHA-256 against the
/// manifest before embedding. `resource_files` maps each declared
/// native-app resource path to its on-disk source (CAN-P1-013); every
/// declared resource must be supplied, and a supplied-but-undeclared file
/// is rejected so the signed payload can never diverge from the manifest.
pub fn build_archive(
    manifest: &PackageManifest,
    files: &HashMap<String, PathBuf>, // target_id -> source file
    resource_files: &HashMap<String, PathBuf>, // resource path -> source file
) -> Result<Vec<u8>> {
    build_archive_with_receipts(manifest, files, resource_files, &HashMap::new())
}

/// Like [`build_archive`], plus the per-target install receipts the manifest
/// declares (`receipt_path` / `receipt_sha256`, keyed by target id). Portable
/// modules point several targets at one artifact path; identical content is
/// stored once.
pub fn build_archive_with_receipts(
    manifest: &PackageManifest,
    files: &HashMap<String, PathBuf>,
    resource_files: &HashMap<String, PathBuf>,
    receipts: &HashMap<String, Vec<u8>>,
) -> Result<Vec<u8>> {
    // CAN-P2-009: the library enforces the package schema itself (not just
    // the CLI), so build and CLI contracts cannot drift apart. Absent
    // Option fields serialize as `null`, which the schema treats as absent;
    // strip them so an in-memory manifest validates like a hand-written one.
    let mut value = serde_json::to_value(manifest)?;
    strip_nulls(&mut value);
    canopus_core::schema::validate(canopus_core::schema::SchemaKind::Package, &value)?;

    let mut entries: Vec<(String, Vec<u8>)> = Vec::new();

    // manifest.json
    let manifest_bytes = serde_json::to_vec_pretty(&value)?;
    entries.push(("manifest.json".to_string(), manifest_bytes));

    // per-target artifacts (hash-verified before embedding)
    for art in &manifest.artifacts {
        let src = files.get(&art.target_id).ok_or_else(|| {
            Error::other(format!(
                "package build: no artifact file supplied for target '{}'",
                art.target_id
            ))
        })?;
        // CAN-P0-003: every embedded path is canonical before it is written
        validate_entry(&art.path, &EntryType::Regular)?;
        let bytes = std::fs::read(src)
            .map_err(|e| Error::other(format!("cannot read {}: {e}", src.display())))?;
        let actual = hex_sha256(&bytes);
        if !actual.eq_ignore_ascii_case(&art.sha256) {
            return Err(Error::other(format!(
                "package build: artifact {} sha256 mismatch (expected {}, got {})",
                art.target_id, art.sha256, actual
            )));
        }
        push_entry(&mut entries, art.path.clone(), bytes)?;
        if let (Some(path), Some(sha)) = (&art.receipt_path, &art.receipt_sha256) {
            validate_entry(path, &EntryType::Regular)?;
            let receipt = receipts.get(&art.target_id).ok_or_else(|| {
                Error::other(format!(
                    "package build: no receipt supplied for target '{}'",
                    art.target_id
                ))
            })?;
            if !hex_sha256(receipt).eq_ignore_ascii_case(sha) {
                return Err(Error::other(format!(
                    "package build: receipt for {} sha256 mismatch",
                    art.target_id
                )));
            }
            push_entry(&mut entries, path.clone(), receipt.clone())?;
        }
    }

    // CAN-P1-013: declared module & native-app resources (hash-verified, canonical
    // path). A supplied file that is not declared is rejected so nothing
    // unverified can ride along in the package.
    let empty_resources = Vec::new();
    let all_resources: Vec<&PackageResource> = manifest
        .resources
        .as_deref()
        .unwrap_or(&empty_resources)
        .iter()
        .chain(
            manifest
                .native_app
                .as_ref()
                .and_then(|a| a.resources.as_deref())
                .unwrap_or(&empty_resources)
                .iter(),
        )
        .collect();

    for r in &all_resources {
        validate_entry(&r.path, &EntryType::Regular)?;
        let src = resource_files.get(&r.path).ok_or_else(|| {
            Error::other(format!(
                "package build: no resource file supplied for declared '{}'",
                r.path
            ))
        })?;
        let bytes = std::fs::read(src)
            .map_err(|e| Error::other(format!("cannot read {}: {e}", src.display())))?;
        let actual = hex_sha256(&bytes);
        if !actual.eq_ignore_ascii_case(&r.sha256) {
            return Err(Error::other(format!(
                "package build: resource {} sha256 mismatch (expected {}, got {})",
                r.path, r.sha256, actual
            )));
        }
        entries.push((r.path.clone(), bytes));
    }
    // a supplied resource that the manifest does not declare is a reject
    for path in resource_files.keys() {
        let declared = all_resources.iter().any(|r| &r.path == path);
        if !declared {
            return Err(Error::other(format!(
                "package build: resource file '{path}' not declared in the manifest"
            )));
        }
    }

    write_tar(&entries)
}

/// Replaces the placeholder artifact hashes in a manifest with real ones
/// computed from the supplied artifact files, so callers do not need to
/// pre-compute SHA-256 by hand. Declared native-app resources are filled
/// the same way from `resource_files` (CAN-P1-013).
pub fn manifest_with_real_hashes(
    manifest: &PackageManifest,
    files: &HashMap<String, PathBuf>,
    resource_files: &HashMap<String, PathBuf>,
) -> Result<PackageManifest> {
    let mut m = manifest.clone();
    for art in &mut m.artifacts {
        if let Some(src) = files.get(&art.target_id) {
            let bytes = std::fs::read(src)?;
            art.sha256 = hex_sha256(&bytes);
        }
    }
    if let Some(resources) = m.resources.as_mut() {
        for r in resources {
            if let Some(src) = resource_files.get(&r.path) {
                let bytes = std::fs::read(src)?;
                r.sha256 = hex_sha256(&bytes);
            }
        }
    }
    if let Some(resources) = m.native_app.as_mut().and_then(|a| a.resources.as_mut()) {
        for r in resources {
            if let Some(src) = resource_files.get(&r.path) {
                let bytes = std::fs::read(src)?;
                r.sha256 = hex_sha256(&bytes);
            }
        }
    }
    Ok(m)
}

/// Verifies archive structure and integrity: canonical entry paths, a schema-valid
/// manifest, and every declared artifact and resource present with its recorded
/// SHA-256.
pub fn verify_archive(archive: &[u8]) -> Result<PackageManifest> {
    let entries: HashMap<String, Vec<u8>> = read_tar(archive)?.into_iter().collect();
    let manifest_bytes = entries
        .get("manifest.json")
        .ok_or_else(|| Error::other("package has no manifest.json entry"))?;
    let value: serde_json::Value = serde_json::from_slice(manifest_bytes)?;
    canopus_core::schema::validate(canopus_core::schema::SchemaKind::Package, &value)?;
    let manifest: PackageManifest = serde_json::from_value(value)?;
    let declared = manifest
        .artifacts
        .iter()
        .map(|a| (&a.path, &a.sha256))
        .chain(
            manifest
                .artifacts
                .iter()
                .filter_map(|a| a.receipt_path.as_ref().zip(a.receipt_sha256.as_ref())),
        )
        .chain(
            manifest
                .resources
                .iter()
                .flatten()
                .map(|r| (&r.path, &r.sha256)),
        )
        .chain(
            manifest
                .native_app
                .iter()
                .filter_map(|a| a.resources.as_ref())
                .flatten()
                .map(|r| (&r.path, &r.sha256)),
        );
    for (path, sha256) in declared {
        let bytes = entries
            .get(path)
            .ok_or_else(|| Error::other(format!("package is missing '{path}'")))?;
        if !hex_sha256(bytes).eq_ignore_ascii_case(sha256) {
            return Err(Error::other(format!(
                "package entry '{path}' sha256 mismatch"
            )));
        }
    }
    Ok(manifest)
}

/// Parses a tar into sorted (path, bytes) entries. Every entry is validated
/// against the canonical path rules and duplicates are rejected (so a
/// archive can never be fooled by "one path verified, another unpacked").
fn read_tar(archive: &[u8]) -> Result<Vec<(String, Vec<u8>)>> {
    let mut out = Vec::new();
    let mut seen = std::collections::HashSet::new();
    let mut ar = tar::Archive::new(archive);
    for entry in ar.entries()? {
        let mut entry = entry?;
        let name = entry.path()?.to_string_lossy().into_owned();
        validate_entry(&name, &entry.header().entry_type())?;
        if !seen.insert(name.clone()) {
            return Err(Error::other(format!(
                "package archive: duplicate entry '{name}'"
            )));
        }
        let mut bytes = Vec::new();
        std::io::Read::read_to_end(&mut entry, &mut bytes)?;
        out.push((name, bytes));
    }
    Ok(out)
}

/// Writes a deterministic tar: entries sorted by path, mtime=0, uid/gid=0.
fn write_tar(entries: &[(String, Vec<u8>)]) -> Result<Vec<u8>> {
    let mut sorted: Vec<&(String, Vec<u8>)> = entries.iter().collect();
    sorted.sort_by(|a, b| a.0.cmp(&b.0));
    let mut data = Vec::new();
    {
        let mut b = Builder::new(&mut data);
        for (path, bytes) in sorted {
            validate_entry(path, &EntryType::Regular)?;
            let mut h = Header::new_gnu();
            h.set_size(bytes.len() as u64);
            h.set_mode(0o644);
            h.set_uid(0);
            h.set_gid(0);
            h.set_mtime(0);
            h.set_cksum();
            b.append_data(&mut h, path, bytes.as_slice())
                .map_err(|e| Error::other(format!("tar append {path}: {e}")))?;
        }
        b.finish()?;
    }
    Ok(data)
}

fn sha256(data: &[u8]) -> Vec<u8> {
    let mut h = Sha256::new();
    h.update(data);
    h.finalize().to_vec()
}

pub fn hex_sha256(data: &[u8]) -> String {
    hex::encode(sha256(data))
}
