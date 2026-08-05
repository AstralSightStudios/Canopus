//! canopus-package: deterministic .canopus package assembly and Ed25519
//! signing (CAN-PKG-001/002/003).
//!
//! Layout inside the tar (deterministic: sorted paths, mtime=0):
//!   manifest.json           canonical manifest (validated against schema)
//!   artifacts/<target_id>/module.elf   per-target payload
//!   reports/elf-verifier.json          verifier output (if provided)
//!   signature.ed25519                  64-byte signature appended by sign()
//!
//! The signature covers sha256(archive) where archive excludes the
//! signature.ed25519 entry itself, so verify() strips it and re-hashes.

use canopus_core::error::{Error, Result};
use canopus_core::model::PackageManifest;
use ed25519_dalek::{Signature, Signer, SigningKey, Verifier, VerifyingKey};
use sha2::{Digest, Sha256};
use std::collections::HashMap;
use std::path::PathBuf;
use tar::{Builder, Header};

pub mod keyroles;

pub const SIGNATURE_ENTRY: &str = "signature.ed25519";

/// Assembles a deterministic tar archive from a manifest and the on-disk
/// artifacts it references. Verifies each artifact SHA-256 against the
/// manifest before embedding.
pub fn build_archive(
    manifest: &PackageManifest,
    files: &HashMap<String, PathBuf>, // target_id -> source file
) -> Result<Vec<u8>> {
    let mut entries: Vec<(String, Vec<u8>)> = Vec::new();

    // manifest.json
    let manifest_bytes = serde_json::to_vec_pretty(manifest)?;
    entries.push(("manifest.json".to_string(), manifest_bytes));

    // per-target artifacts (hash-verified before embedding)
    for art in &manifest.artifacts {
        let src = files.get(&art.target_id).ok_or_else(|| {
            Error::other(format!(
                "package build: no artifact file supplied for target '{}'",
                art.target_id
            ))
        })?;
        let bytes = std::fs::read(src)
            .map_err(|e| Error::other(format!("cannot read {}: {e}", src.display())))?;
        let actual = hex_sha256(&bytes);
        if !actual.eq_ignore_ascii_case(&art.sha256) {
            return Err(Error::other(format!(
                "package build: artifact {} sha256 mismatch (expected {}, got {})",
                art.target_id, art.sha256, actual
            )));
        }
        entries.push((art.path.clone(), bytes));
    }

    write_tar(&entries)
}

/// Appends a signature.ed25519 entry over the canonical digest of the
/// archive's entries (sorted path+content, signature excluded) and returns
/// the new archive.
pub fn sign_archive(archive: &[u8], secret_key_hex: &str) -> Result<Vec<u8>> {
    let secret = hex::decode(secret_key_hex.trim())
        .map_err(|_| Error::other("secret key is not valid hex"))?;
    let secret: [u8; 32] = secret
        .as_slice()
        .try_into()
        .map_err(|_| Error::other("secret key must be 32 bytes"))?;
    let signing = SigningKey::from_bytes(&secret);

    let mut entries = read_tar(archive)?;
    let digest = canonical_digest(&entries);
    let sig = signing.sign(&digest);
    let sig_bytes = sig.to_bytes();

    entries.push((SIGNATURE_ENTRY.to_string(), sig_bytes.to_vec()));
    write_tar(&entries)
}

/// Replaces the placeholder artifact hashes in a manifest with real ones
/// computed from the supplied artifact files, so callers do not need to
/// pre-compute SHA-256 by hand.
pub fn manifest_with_real_hashes(
    manifest: &PackageManifest,
    files: &HashMap<String, PathBuf>,
) -> Result<PackageManifest> {
    let mut m = manifest.clone();
    for art in &mut m.artifacts {
        if let Some(src) = files.get(&art.target_id) {
            let bytes = std::fs::read(src)?;
            art.sha256 = hex_sha256(&bytes);
        }
    }
    Ok(m)
}

/// Verifies a signed archive against a public key. The signature entry is
/// stripped, the remaining payload is re-hashed, and the Ed25519 signature
/// is checked.
pub fn verify_archive(archive: &[u8], public_key_hex: &str) -> Result<()> {
    let pub_bytes = hex::decode(public_key_hex.trim())
        .map_err(|_| Error::other("public key is not valid hex"))?;
    let vk = VerifyingKey::from_bytes(
        pub_bytes
            .as_slice()
            .try_into()
            .map_err(|_| Error::other("public key must be 32 bytes"))?,
    )
    .map_err(|e| Error::other(format!("bad public key: {e}")))?;

    let entries = read_tar(archive)?;
    let mut sig: Option<Vec<u8>> = None;
    for (name, bytes) in &entries {
        if name == SIGNATURE_ENTRY {
            sig = Some(bytes.clone());
        }
    }
    let sig_bytes = sig.ok_or_else(|| Error::other("package has no signature.ed25519 entry"))?;
    let sig = Signature::from_bytes(
        sig_bytes
            .as_slice()
            .try_into()
            .map_err(|_| Error::other("signature must be 64 bytes"))?,
    );

    let digest = canonical_digest(&entries);
    vk.verify(&digest, &sig)
        .map_err(|_| Error::other("Ed25519 signature verification FAILED"))
}

/// sha256 over (path, 0x00, content) for every entry except the signature,
/// sorted by path. This is the canonical signed payload: binding path to
/// content and independent of tar encoding details.
fn canonical_digest(entries: &[(String, Vec<u8>)]) -> Vec<u8> {
    let mut sorted: Vec<&(String, Vec<u8>)> = entries
        .iter()
        .filter(|(n, _)| n != SIGNATURE_ENTRY)
        .collect();
    sorted.sort_by(|a, b| a.0.cmp(&b.0));
    let mut h = Sha256::new();
    for (name, bytes) in sorted {
        h.update(name.as_bytes());
        h.update([0u8]);
        h.update(bytes);
    }
    h.finalize().to_vec()
}

/// Parses a tar into sorted (path, bytes) entries. Order does not matter for
/// parsing; write_tar re-sorts for determinism.
fn read_tar(archive: &[u8]) -> Result<Vec<(String, Vec<u8>)>> {
    let mut out = Vec::new();
    let mut ar = tar::Archive::new(archive);
    for entry in ar.entries()? {
        let mut entry = entry?;
        let name = entry.path()?.to_string_lossy().into_owned();
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

/// Generates a fresh Ed25519 key pair from OS entropy.
pub fn keygen() -> Result<(String, String)> {
    let mut seed = [0u8; 32];
    getrandom::getrandom(&mut seed)
        .map_err(|e| Error::other(format!("entropy source failed: {e}")))?;
    let signing = SigningKey::from_bytes(&seed);
    let secret = hex::encode(signing.to_bytes());
    let public = hex::encode(signing.verifying_key().to_bytes());
    Ok((secret, public))
}

fn sha256(data: &[u8]) -> Vec<u8> {
    let mut h = Sha256::new();
    h.update(data);
    h.finalize().to_vec()
}

pub fn hex_sha256(data: &[u8]) -> String {
    hex::encode(sha256(data))
}
