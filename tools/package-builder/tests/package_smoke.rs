//! End-to-end package build / sign / verify tests.

use canopus_core::model::PackageManifest;
use canopus_package::{build_archive, keygen, sign_archive, verify_archive};
use std::collections::HashMap;
use std::path::PathBuf;

fn test_manifest() -> PackageManifest {
    serde_json::from_str(
        r#"{
            "schema": 1,
            "package_id": "org.example.hello",
            "module_id": "org.example.hello",
            "version": "0.1.0",
            "build_generation": 1,
            "canopus_abi": "1",
            "lifecycle": "removable",
            "artifacts": [{
                "target_id": "xiaomi-band-10-pro-3.101.030",
                "target_pack_revision": 1,
                "firmware_sha256": "f701a84ffcafa67f4d4603ad8cd66a11e5442f27140f5af0982e0975dccd225b",
                "path": "artifacts/xiaomi-band-10-pro-3.101.030/module.elf",
                "sha256": "0000000000000000000000000000000000000000000000000000000000000000"
            }],
            "target_pack_revision": 1,
            "signature": { "key_id": "test", "algorithm": "ed25519" },
            "min_manager_version": "0.1.0"
        }"#,
    )
    .unwrap()
}

fn artifact_file(tag: &str) -> PathBuf {
    let dir = std::env::temp_dir().join("canopus-pkg-test");
    std::fs::create_dir_all(&dir).unwrap();
    let p = dir.join(format!("module-{tag}.elf"));
    std::fs::write(&p, format!("hello module bytes {tag}")).unwrap();
    p
}

/// Fills real artifact hashes into the manifest, mirroring the CLI flow.
fn real_manifest(tag: &str) -> (PackageManifest, HashMap<String, PathBuf>) {
    let manifest = test_manifest();
    let mut files = HashMap::new();
    files.insert("xiaomi-band-10-pro-3.101.030".to_string(), artifact_file(tag));
    let manifest = canopus_package::manifest_with_real_hashes(&manifest, &files).unwrap();
    (manifest, files)
}

#[test]
fn sign_then_verify_roundtrip() {
    let (manifest, files) = real_manifest("roundtrip");
    let (secret, public) = keygen().unwrap();
    let archive = build_archive(&manifest, &files).unwrap();
    let signed = sign_archive(&archive, &secret).unwrap();
    assert!(signed.len() > archive.len());

    // correct key verifies
    verify_archive(&signed, &public).expect("signature must verify");

    // wrong key fails
    let (_, wrong_public) = keygen().unwrap();
    assert!(verify_archive(&signed, &wrong_public).is_err());
}

#[test]
fn unsigned_package_fails_verify() {
    let (secret, public) = keygen().unwrap();
    let (manifest, files) = real_manifest("unsigned");
    let archive = build_archive(&manifest, &files).unwrap();
    let _ = secret;
    assert!(verify_archive(&archive, &public).is_err());
}

#[test]
fn tampered_archive_fails_verify() {
    let (secret, public) = keygen().unwrap();
    let (manifest, files) = real_manifest("tamper");
    let archive = build_archive(&manifest, &files).unwrap();
    let mut signed = sign_archive(&archive, &secret).unwrap();

    // flip a byte in the artifact payload region
    let mid = signed.len() / 2;
    signed[mid] ^= 0xFF;

    assert!(verify_archive(&signed, &public).is_err());
}

#[test]
fn build_is_deterministic() {
    let (manifest, files) = real_manifest("determinism");
    let a = build_archive(&manifest, &files).unwrap();
    let b = build_archive(&manifest, &files).unwrap();
    assert_eq!(a, b, "same inputs must produce byte-identical archives");
}
