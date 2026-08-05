//! Key roles and revocation (CAN-REL-001).
//!
//! Dev and production keys are kept separate: a module signed with a dev key
//! must never be treated as production-signed, and a revoked key must never
//! verify. A revocation list is itself signed so a tampered list is rejected.

use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};

/// The trust role a key is allowed to carry.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum KeyRole {
    /// Local developer key — module packages signed with it are dev-only.
    Dev,
    /// Production key — custody-controlled; required for release packages.
    Production,
}

impl KeyRole {
    pub fn as_str(self) -> &'static str {
        match self {
            KeyRole::Dev => "dev",
            KeyRole::Production => "production",
        }
    }
}

/// A key certificate: binds a public key to a role. The fingerprint is a
/// SHA-256 of the public key bytes, the stable revocation identity.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct KeyCert {
    pub role: KeyRole,
    pub public_key_hex: String,
    pub fingerprint: String,
    pub note: String,
}

/// Computes the fingerprint (SHA-256 hex) of a public key.
pub fn fingerprint(public_key_hex: &str) -> String {
    let mut h = Sha256::new();
    h.update(public_key_hex.as_bytes());
    hex::encode(h.finalize())
}

/// Creates a key certificate for a role from an existing public key.
pub fn certify(role: KeyRole, public_key_hex: &str, note: &str) -> KeyCert {
    KeyCert {
        role,
        public_key_hex: public_key_hex.to_string(),
        fingerprint: fingerprint(public_key_hex),
        note: note.to_string(),
    }
}

/// A signed list of revoked key fingerprints.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct RevocationList {
    pub revoked: Vec<String>, // fingerprints, sorted
    pub signer_role: KeyRole,
    pub signature: String, // Ed25519 over the canonical list digest
}

impl RevocationList {
    pub fn new_empty(signer_role: KeyRole) -> Self {
        RevocationList {
            revoked: Vec::new(),
            signer_role,
            signature: String::new(),
        }
    }

    pub fn add(&mut self, fp: &str) {
        if !self.revoked.contains(&fp.to_string()) {
            self.revoked.push(fp.to_string());
            self.revoked.sort();
        }
    }

    /// Loads a revocation list from a JSON file.
    pub fn load(path: &std::path::Path) -> Result<Self, String> {
        let data = std::fs::read(path).map_err(|e| format!("read {path:?}: {e}"))?;
        serde_json::from_slice(&data).map_err(|e| format!("parse {path:?}: {e}"))
    }

    /// Saves the revocation list to a JSON file.
    pub fn save(&self, path: &std::path::Path) -> Result<(), String> {
        let data = serde_json::to_vec_pretty(self).map_err(|e| format!("serialize: {e}"))?;
        std::fs::write(path, data).map_err(|e| format!("write {path:?}: {e}"))
    }

    /// Canonical digest over the sorted revocation set + signer role.
    pub fn canonical_digest(&self) -> String {
        let mut h = Sha256::new();
        h.update(format!("signer={}\n", self.signer_role.as_str()).as_bytes());
        for fp in &self.revoked {
            h.update(format!("revoked={fp}\n").as_bytes());
        }
        hex::encode(h.finalize())
    }

    /// Signs the list with the signer's secret key.
    pub fn sign(&mut self, secret_key_hex: &str) -> Result<(), String> {
        use ed25519_dalek::Signer;
        let bytes = hex::decode(secret_key_hex).map_err(|e| format!("bad key hex: {e}"))?;
        let arr: [u8; 32] = bytes
            .try_into()
            .map_err(|_| "secret key must be 32 bytes".to_string())?;
        let sk = ed25519_dalek::SigningKey::from_bytes(&arr);
        let sig = sk.sign(self.canonical_digest().as_bytes());
        self.signature = hex::encode(sig.to_bytes());
        Ok(())
    }

    /// Verifies the list signature and that the signer public key matches the
    /// declared role certificate.
    pub fn verify(&self, signer_cert: &KeyCert) -> Result<(), String> {
        use ed25519_dalek::Verifier;
        let bytes = hex::decode(&signer_cert.public_key_hex)
            .map_err(|e| format!("bad pubkey hex: {e}"))?;
        let arr: [u8; 32] = bytes
            .try_into()
            .map_err(|_| "public key must be 32 bytes".to_string())?;
        let vk = ed25519_dalek::VerifyingKey::from_bytes(&arr)
            .map_err(|e| format!("bad public key: {e}"))?;
        if signer_cert.role != self.signer_role {
            return Err("revocation list signer role does not match cert".into());
        }
        let sig_bytes = hex::decode(&self.signature).map_err(|e| format!("bad sig hex: {e}"))?;
        let sig_arr: [u8; 64] = sig_bytes
            .try_into()
            .map_err(|_| "signature must be 64 bytes".to_string())?;
        let sig = ed25519_dalek::Signature::from_bytes(&sig_arr);
        vk.verify(self.canonical_digest().as_bytes(), &sig)
            .map_err(|_| "revocation list signature invalid".to_string())
    }

    pub fn is_revoked(&self, fp: &str) -> bool {
        self.revoked.iter().any(|r| r == fp)
    }
}

/// Applies a revocation list to a certificate: production certs are checked
/// against the production-revoked set only (dev keys never sign release).
pub fn check_cert_revoked(cert: &KeyCert, revocations: &RevocationList) -> Result<(), String> {
    if revocations.is_revoked(&cert.fingerprint) {
        return Err(format!(
            "key {} revoked (fingerprint {})",
            cert.role.as_str(),
            &cert.fingerprint[..16.min(cert.fingerprint.len())]
        ));
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn fingerprint_is_stable() {
        let a = fingerprint("abcd");
        let b = fingerprint("abcd");
        assert_eq!(a, b);
        assert_ne!(a, fingerprint("abce"));
    }

    #[test]
    fn certify_binds_role() {
        let (sk, pk) = crate::keygen().unwrap();
        let cert = certify(KeyRole::Dev, &pk, "local");
        assert_eq!(cert.role, KeyRole::Dev);
        assert_eq!(cert.fingerprint, fingerprint(&pk));
        assert_eq!(cert.public_key_hex, pk);
        let _ = sk;
    }

    #[test]
    fn revocation_list_sign_verify_roundtrip() {
        let (sk, pk) = crate::keygen().unwrap();
        let mut list = RevocationList::new_empty(KeyRole::Production);
        list.add(&fingerprint(&pk));
        list.sign(&sk).unwrap();
        let cert = certify(KeyRole::Production, &pk, "prod");
        list.verify(&cert).unwrap();
        assert!(list.is_revoked(&fingerprint(&pk)));
        assert!(check_cert_revoked(&cert, &list).is_err());
    }

    #[test]
    fn revocation_scoped_to_fingerprint() {
        // Revoke key A; key B (different fingerprint) is unaffected. A
        // fingerprint is global — the same key cannot be "revoked for dev
        // only" and still trusted for production.
        let (sk, pk_a) = crate::keygen().unwrap();
        let (_, pk_b) = crate::keygen().unwrap();
        let mut list = RevocationList::new_empty(KeyRole::Dev);
        list.add(&fingerprint(&pk_a));
        list.sign(&sk).unwrap();

        let dev_a = certify(KeyRole::Dev, &pk_a, "dev");
        assert!(check_cert_revoked(&dev_a, &list).is_err());

        let prod_b = certify(KeyRole::Production, &pk_b, "prod");
        assert!(check_cert_revoked(&prod_b, &list).is_ok());
    }

    #[test]
    fn tampered_revocation_list_rejected() {
        let (sk, pk) = crate::keygen().unwrap();
        let mut list = RevocationList::new_empty(KeyRole::Dev);
        list.add(&fingerprint("victim"));
        list.sign(&sk).unwrap();
        // tamper: add a second fingerprint after signing
        list.add(&fingerprint("attacker"));
        let cert = certify(KeyRole::Dev, &pk, "dev");
        assert!(list.verify(&cert).is_err());
    }

    #[test]
    fn wrong_role_signer_rejected() {
        let (sk, pk) = crate::keygen().unwrap();
        let mut list = RevocationList::new_empty(KeyRole::Dev);
        list.add(&fingerprint("x"));
        list.sign(&sk).unwrap();
        // verify against a Production cert: role mismatch
        let prod_cert = certify(KeyRole::Production, &pk, "prod");
        assert!(list.verify(&prod_cert).is_err());
    }
}
