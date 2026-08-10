//! End-to-end package build / sign / verify tests.

use canopus_core::model::PackageManifest;
use canopus_package::{build_archive, keygen, sign_archive, validate_entry, verify_archive};
use std::collections::HashMap;
use std::path::PathBuf;
use tar::{Builder, EntryType, Header};

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
    files.insert(
        "xiaomi-band-10-pro-3.101.030".to_string(),
        artifact_file(tag),
    );
    let manifest =
        canopus_package::manifest_with_real_hashes(&manifest, &files, &HashMap::new()).unwrap();
    (manifest, files)
}

fn no_resources() -> HashMap<String, PathBuf> {
    HashMap::new()
}

#[test]
fn sign_then_verify_roundtrip() {
    let (manifest, files) = real_manifest("roundtrip");
    let (secret, public) = keygen().unwrap();
    let archive = build_archive(&manifest, &files, &no_resources()).unwrap();
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
    let archive = build_archive(&manifest, &files, &no_resources()).unwrap();
    let _ = secret;
    assert!(verify_archive(&archive, &public).is_err());
}

#[test]
fn tampered_archive_fails_verify() {
    let (secret, public) = keygen().unwrap();
    let (manifest, files) = real_manifest("tamper");
    let archive = build_archive(&manifest, &files, &no_resources()).unwrap();
    let mut signed = sign_archive(&archive, &secret).unwrap();

    // flip a byte in the artifact payload region
    let mid = signed.len() / 2;
    signed[mid] ^= 0xFF;

    assert!(verify_archive(&signed, &public).is_err());
}

#[test]
fn build_is_deterministic() {
    let (manifest, files) = real_manifest("determinism");
    let a = build_archive(&manifest, &files, &no_resources()).unwrap();
    let b = build_archive(&manifest, &files, &no_resources()).unwrap();
    assert_eq!(a, b, "same inputs must produce byte-identical archives");
}

// ---- CAN-P0-003: canonical archive entry boundary ----------------------

fn make_tar(entries: &[(String, Vec<u8>)]) -> Vec<u8> {
    let mut data = Vec::new();
    {
        let mut b = Builder::new(&mut data);
        for (path, bytes) in entries {
            let mut h = Header::new_gnu();
            h.set_size(bytes.len() as u64);
            h.set_mode(0o644);
            h.set_uid(0);
            h.set_gid(0);
            h.set_mtime(0);
            h.set_cksum();
            b.append_data(&mut h, path, bytes.as_slice()).unwrap();
        }
        b.finish().unwrap();
    }
    data
}

fn make_tar_symlink(path: &str, target: &str) -> Vec<u8> {
    let mut data = Vec::new();
    {
        let mut b = Builder::new(&mut data);
        let mut h = Header::new_gnu();
        h.set_entry_type(EntryType::Symlink);
        h.set_size(target.len() as u64);
        h.set_uid(0);
        h.set_gid(0);
        h.set_mtime(0);
        h.set_cksum();
        b.append_data(&mut h, path, target.as_bytes()).unwrap();
        b.finish().unwrap();
    }
    data
}

#[test]
fn validate_entry_rejects_traversal_and_absolute() {
    let r = EntryType::Regular;
    assert!(validate_entry("../x", &r).is_err());
    assert!(validate_entry("a/../../x", &r).is_err());
    assert!(validate_entry("/abs", &r).is_err());
    assert!(validate_entry("a/./b", &r).is_err());
    assert!(validate_entry("a//b", &r).is_err());
    assert!(validate_entry("a\\b", &r).is_err());
    assert!(validate_entry("ok/name.bin", &r).is_ok());
}

fn make_tar_dir(path: &str) -> Vec<u8> {
    let mut data = Vec::new();
    {
        let mut b = Builder::new(&mut data);
        let mut h = Header::new_gnu();
        h.set_entry_type(EntryType::Directory);
        h.set_uid(0);
        h.set_gid(0);
        h.set_mtime(0);
        h.set_cksum();
        b.append_data(&mut h, path, std::io::empty()).unwrap();
        b.finish().unwrap();
    }
    data
}

#[test]
fn malicious_archives_fail_verify() {
    let (_, public) = keygen().unwrap();
    // The tar crate refuses to even WRITE `..`/absolute paths (defense in
    // depth), so those cases are exercised via validate_entry above. Here we
    // cover what a real attacker can produce with the format:
    // symlink escape is rejected
    let sym = make_tar_symlink("link", "/etc/passwd");
    assert!(verify_archive(&sym, &public).is_err());
    // duplicate entries are rejected (one path verified, another unpacked)
    let dup = make_tar(&[
        ("manifest.json".to_string(), b"a".to_vec()),
        ("manifest.json".to_string(), b"b".to_vec()),
    ]);
    assert!(verify_archive(&dup, &public).is_err());
    // a directory entry is rejected (packages contain regular files only)
    let dir = make_tar_dir("subdir");
    assert!(verify_archive(&dir, &public).is_err());
}

#[test]
fn build_rejects_traversal_artifact_path() {
    let mut manifest = test_manifest();
    manifest.artifacts[0].path = "../evil.bin".to_string();
    let mut files = HashMap::new();
    files.insert(
        "xiaomi-band-10-pro-3.101.030".to_string(),
        artifact_file("traversal"),
    );
    let manifest =
        canopus_package::manifest_with_real_hashes(&manifest, &files, &HashMap::new()).unwrap();
    assert!(build_archive(&manifest, &files, &no_resources()).is_err());
}

// ---- CAN-P1-013: native-app resources ---------------------------------

fn manifest_with_resource(path: &str) -> PackageManifest {
    serde_json::from_str(&format!(
        r#"{{
            "schema": 1,
            "package_id": "org.example.hello",
            "module_id": "org.example.hello",
            "version": "0.1.0",
            "build_generation": 1,
            "canopus_abi": "1",
            "lifecycle": "removable",
            "artifacts": [{{
                "target_id": "xiaomi-band-10-pro-3.101.030",
                "target_pack_revision": 1,
                "firmware_sha256": "f701a84ffcafa67f4d4603ad8cd66a11e5442f27140f5af0982e0975dccd225b",
                "path": "artifacts/xiaomi-band-10-pro-3.101.030/module.elf",
                "sha256": "0000000000000000000000000000000000000000000000000000000000000000"
            }}],
            "target_pack_revision": 1,
            "signature": {{ "key_id": "test", "algorithm": "ed25519" }},
            "min_manager_version": "0.1.0",
            "native_app": {{
                "app_id": "com.canopus.manager",
                "name": "Manager",
                "icon": "resources/icon.png",
                "resources": [{{
                    "path": "{path}",
                    "sha256": "0000000000000000000000000000000000000000000000000000000000000000",
                    "media_type": "image/png",
                    "target_backend": "lvgl"
                }}]
            }}
        }}"#
    ))
    .unwrap()
}

#[test]
fn build_embeds_declared_resource() {
    let manifest = manifest_with_resource("resources/icon.png");
    let mut files = HashMap::new();
    files.insert(
        "xiaomi-band-10-pro-3.101.030".to_string(),
        artifact_file("res-artifact"),
    );
    let mut resource_files = HashMap::new();
    resource_files.insert("resources/icon.png".to_string(), artifact_file("icon"));
    let manifest =
        canopus_package::manifest_with_real_hashes(&manifest, &files, &resource_files).unwrap();
    let archive = build_archive(&manifest, &files, &resource_files).unwrap();
    // the resource is embedded alongside the artifact
    assert!(
        archive
            .windows(b"resources/icon.png".len())
            .any(|w| w == b"resources/icon.png")
    );
}

#[test]
fn build_rejects_missing_declared_resource() {
    let manifest = manifest_with_resource("resources/icon.png");
    let mut files = HashMap::new();
    files.insert(
        "xiaomi-band-10-pro-3.101.030".to_string(),
        artifact_file("missing-res"),
    );
    // declared but not supplied -> error
    assert!(build_archive(&manifest, &files, &no_resources()).is_err());
}

#[test]
fn build_rejects_undeclared_resource_file() {
    let manifest = manifest_with_resource("resources/icon.png");
    let mut files = HashMap::new();
    files.insert(
        "xiaomi-band-10-pro-3.101.030".to_string(),
        artifact_file("undeclared-res"),
    );
    let mut resource_files = HashMap::new();
    resource_files.insert("resources/icon.png".to_string(), artifact_file("icon"));
    resource_files.insert("resources/secret.bin".to_string(), artifact_file("secret"));
    let manifest =
        canopus_package::manifest_with_real_hashes(&manifest, &files, &resource_files).unwrap();
    // a supplied file that the manifest does not declare is rejected
    assert!(build_archive(&manifest, &files, &resource_files).is_err());
}

#[test]
fn resource_hash_mismatch_fails_build() {
    let manifest = manifest_with_resource("resources/icon.png");
    let mut files = HashMap::new();
    files.insert(
        "xiaomi-band-10-pro-3.101.030".to_string(),
        artifact_file("res-hash"),
    );
    let mut resource_files = HashMap::new();
    resource_files.insert("resources/icon.png".to_string(), artifact_file("icon"));
    let mut manifest =
        canopus_package::manifest_with_real_hashes(&manifest, &files, &resource_files).unwrap();
    // corrupt the declared resource hash after filling it
    if let Some(rs) = manifest
        .native_app
        .as_mut()
        .and_then(|a| a.resources.as_mut())
    {
        rs[0].sha256 =
            "0000000000000000000000000000000000000000000000000000000000000000".to_string();
    }
    assert!(build_archive(&manifest, &files, &resource_files).is_err());
}

#[test]
fn tampered_resource_fails_verify() {
    let manifest = manifest_with_resource("resources/icon.png");
    let mut files = HashMap::new();
    files.insert(
        "xiaomi-band-10-pro-3.101.030".to_string(),
        artifact_file("res-tamper-artifact"),
    );
    let mut resource_files = HashMap::new();
    resource_files.insert(
        "resources/icon.png".to_string(),
        artifact_file("res-tamper"),
    );
    let manifest =
        canopus_package::manifest_with_real_hashes(&manifest, &files, &resource_files).unwrap();
    let (secret, public) = keygen().unwrap();
    let archive = build_archive(&manifest, &files, &resource_files).unwrap();
    let mut signed = sign_archive(&archive, &secret).unwrap();
    // tamper the resource content bytes (the resource's payload string)
    let marker = b"res-tamper";
    let idx = signed
        .windows(marker.len())
        .position(|w| w == marker)
        .expect("resource payload present");
    signed[idx] ^= 0xFF;
    assert!(verify_archive(&signed, &public).is_err());
}
