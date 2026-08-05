//! Signed immutable target-pack revisions (CAN-RE-009).
//!
//! A revision is a canonical digest over the pack's meaningful content plus a
//! revision number. Signing it with the target maintainer key makes the
//! revision tamper-evident and auditable, reusing the same Ed25519 scheme as
//! package signing (architecture §19.3).

use ed25519_dalek::Signer;
use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};

/// Canonical, sortable, tamper-evident revision manifest.
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct RevisionManifest {
    pub target_id: String,
    pub revision: u32,
    pub firmware_sha256: String,
    pub content_sha256: String,
    pub symbol_count: u32,
    pub type_count: u32,
    pub evidence_count: u32,
    pub schema_version: u32,
}

impl RevisionManifest {
    /// Canonical SHA-256 over the manifest fields (sorted, fixed encoding).
    /// Both sign and verify use this, so field order never matters.
    pub fn canonical_digest(&self) -> String {
        let mut h = Sha256::new();
        // Use a compact canonical form: key=value lines sorted by key.
        let mut lines: Vec<String> = vec![
            format!("content_sha256={}", self.content_sha256),
            format!("evidence_count={}", self.evidence_count),
            format!("firmware_sha256={}", self.firmware_sha256),
            format!("revision={}", self.revision),
            format!("schema_version={}", self.schema_version),
            format!("symbol_count={}", self.symbol_count),
            format!("target_id={}", self.target_id),
            format!("type_count={}", self.type_count),
        ];
        lines.sort();
        for l in &lines {
            h.update(l.as_bytes());
            h.update(b"\n");
        }
        hex::encode(h.finalize())
    }

    /// Signs the canonical digest. Returns the 64-byte signature as hex.
    pub fn sign(&self, secret_key_hex: &str) -> Result<String, String> {
        let sk = decode_secret(secret_key_hex)?;
        let sig = sk.sign(self.canonical_digest().as_bytes());
        Ok(hex::encode(sig.to_bytes()))
    }

    /// Verifies the canonical digest against `signature_hex`.
    pub fn verify(&self, public_key_hex: &str, signature_hex: &str) -> Result<(), String> {
        let vk = decode_public(public_key_hex)?;
        let sig = decode_signature(signature_hex)?;
        vk.verify_strict(self.canonical_digest().as_bytes(), &sig)
            .map_err(|_| "signature verification failed".to_string())
    }
}

fn decode_secret(hex_str: &str) -> Result<ed25519_dalek::SigningKey, String> {
    let bytes = hex::decode(hex_str).map_err(|e| format!("bad key hex: {e}"))?;
    let arr: [u8; 32] = bytes
        .try_into()
        .map_err(|_| "secret key must be 32 bytes".to_string())?;
    Ok(ed25519_dalek::SigningKey::from_bytes(&arr))
}

fn decode_public(hex_str: &str) -> Result<ed25519_dalek::VerifyingKey, String> {
    let bytes = hex::decode(hex_str).map_err(|e| format!("bad key hex: {e}"))?;
    let arr: [u8; 32] = bytes
        .try_into()
        .map_err(|_| "public key must be 32 bytes".to_string())?;
    ed25519_dalek::VerifyingKey::from_bytes(&arr).map_err(|e| format!("bad public key: {e}"))
}

fn decode_signature(hex_str: &str) -> Result<ed25519_dalek::Signature, String> {
    let bytes = hex::decode(hex_str).map_err(|e| format!("bad signature hex: {e}"))?;
    let arr: [u8; 64] = bytes
        .try_into()
        .map_err(|_| "signature must be 64 bytes".to_string())?;
    Ok(ed25519_dalek::Signature::from_bytes(&arr))
}

#[cfg(test)]
mod tests {
    use super::*;

    fn sample() -> RevisionManifest {
        RevisionManifest {
            target_id: "xiaomi-band-10-pro-3.101.030".into(),
            revision: 2,
            firmware_sha256: "f701a84ffcafa67f4d4603ad8cd66a11e5442f27140f5af0982e0975dccd225b"
                .into(),
            content_sha256: hex_sha256(b"symbols+types"),
            symbol_count: 42,
            type_count: 6,
            evidence_count: 4,
            schema_version: 1,
        }
    }

    fn hex_sha256(data: &[u8]) -> String {
        hex::encode(Sha256::digest(data))
    }

    #[test]
    fn canonical_digest_is_stable() {
        let a = sample();
        let b = sample();
        assert_eq!(a.canonical_digest(), b.canonical_digest());
    }

    #[test]
    fn sign_verify_roundtrip() {
        let (sk, pk) = canopus_package::keygen().unwrap();
        let m = sample();
        let sig = m.sign(&sk).unwrap();
        m.verify(&pk, &sig).unwrap();
    }

    #[test]
    fn tamper_detected() {
        let (sk, pk) = canopus_package::keygen().unwrap();
        let mut m = sample();
        let sig = m.sign(&sk).unwrap();
        m.symbol_count += 1;
        assert!(m.verify(&pk, &sig).is_err());
    }

    #[test]
    fn wrong_key_detected() {
        let (sk, _) = canopus_package::keygen().unwrap();
        let (_, other_pk) = canopus_package::keygen().unwrap();
        let m = sample();
        let sig = m.sign(&sk).unwrap();
        assert!(m.verify(&other_pk, &sig).is_err());
    }

    #[test]
    fn revision_number_is_part_of_digest() {
        let (sk, pk) = canopus_package::keygen().unwrap();
        let mut a = sample();
        let b = sample();
        let sig = a.sign(&sk).unwrap();
        a.revision += 1;
        assert!(a.verify(&pk, &sig).is_err());
        assert_ne!(a.canonical_digest(), b.canonical_digest());
    }
}
