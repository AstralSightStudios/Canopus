//! Quick-app container (.rpk) for delivering Canopus modules through the
//! phone's stock quick-app install path (refactor-plan P5, O1 decision).
//!
//! The band's installer extracts a signed .rpk to `/data/app/<package>/` and
//! lists it (package, versionCode, fingerprint) to the phone, so installing,
//! listing and updating need no new device channel. The platform requires an
//! APK Signature Scheme v2 block; any publisher key is accepted (the app
//! fingerprint is derived from its certificate, so keep one key per publisher).

use canopus_core::error::{Error, Result};
use rsa::pkcs1v15::SigningKey;
use rsa::pkcs8::{DecodePrivateKey, EncodePublicKey};
use rsa::signature::{SignatureEncoding, Signer};
use rsa::RsaPrivateKey;
use sha2::{Digest, Sha256};
use std::io::{Cursor, Write};

pub const PACKAGE_PREFIX: &str = "com.canopus.pkg.";

pub struct RpkInput<'a> {
    pub module_id: &'a str,
    pub version_name: &'a str,
    pub version_code: u32,
    /// Extra files, path relative to the package root.
    pub files: &'a [(String, Vec<u8>)],
}

pub fn package_name(module_id: &str) -> String {
    format!("{PACKAGE_PREFIX}{module_id}")
}

/// Zip with a fixed timestamp and sorted entries (deterministic).
fn build_zip(input: &RpkInput) -> Result<Vec<u8>> {
    let manifest = serde_json::json!({
        "package": package_name(input.module_id),
        "name": input.module_id,
        "versionName": input.version_name,
        "versionCode": input.version_code,
        "minPlatformVersion": 1000,
        "icon": "/Common/logo.png",
        "features": [],
        "config": { "logLevel": "off" },
        "router": { "entry": "pages/index", "pages": { "pages/index": { "component": "index" } } },
        "display": {}
    });
    let mut files: Vec<(String, Vec<u8>)> = vec![
        ("manifest.json".into(), serde_json::to_vec_pretty(&manifest)?),
        ("pages/index/index.ux".into(), b"<template><div></div></template>\n".to_vec()),
        ("Common/logo.png".into(), b"x".to_vec()),
    ];
    files.extend(input.files.iter().cloned());
    files.sort_by(|a, b| a.0.cmp(&b.0));
    let mut out = Vec::new();
    {
        let mut zip = zip::ZipWriter::new(Cursor::new(&mut out));
        let options = zip::write::FileOptions::default()
            .compression_method(zip::CompressionMethod::Deflated)
            .last_modified_time(zip::DateTime::default());
        for (name, data) in &files {
            zip.start_file(name.as_str(), options)
                .map_err(|e| Error::other(format!("zip: {e}")))?;
            zip.write_all(data)?;
        }
        zip.finish().map_err(|e| Error::other(format!("zip: {e}")))?;
    }
    Ok(out)
}

fn u32le(v: u32) -> [u8; 4] {
    v.to_le_bytes()
}

fn len_prefixed(data: &[u8]) -> Vec<u8> {
    let mut v = u32le(data.len() as u32).to_vec();
    v.extend_from_slice(data);
    v
}

fn sequence(items: &[Vec<u8>]) -> Vec<u8> {
    let body: Vec<u8> = items.iter().flat_map(|i| len_prefixed(i)).collect();
    len_prefixed(&body)
}

/// APK v2 content digest over entries, central directory and EOCD (with the
/// central-directory offset pointing where the signing block starts).
fn content_digest(entries: &[u8], central: &[u8], eocd: &[u8]) -> Vec<u8> {
    const CHUNK: usize = 1 << 20;
    let mut chunk_digests: Vec<[u8; 32]> = Vec::new();
    for section in [entries, central, eocd] {
        for chunk in section.chunks(CHUNK) {
            let mut h = Sha256::new();
            h.update([0xa5]);
            h.update(u32le(chunk.len() as u32));
            h.update(chunk);
            chunk_digests.push(h.finalize().into());
        }
    }
    let mut top = Sha256::new();
    top.update([0x5a]);
    top.update(u32le(chunk_digests.len() as u32));
    for d in &chunk_digests {
        top.update(d);
    }
    top.finalize().to_vec()
}

const ALGO_RSA_PKCS1_SHA256: u32 = 0x0103;
const V2_BLOCK_ID: u32 = 0x7109_871a;

/// Signs `zip` (a plain zip) with an APK v2 signing block.
fn sign_v2(zip: &[u8], key: &RsaPrivateKey, cert_der: &[u8]) -> Result<Vec<u8>> {
    // Locate the end-of-central-directory record (no archive comment).
    let eocd_pos = zip
        .len()
        .checked_sub(22)
        .filter(|p| zip[*p..*p + 4] == [0x50, 0x4b, 0x05, 0x06])
        .ok_or_else(|| Error::other("zip: unexpected end-of-central-directory layout"))?;
    let cd_offset = u32::from_le_bytes(zip[eocd_pos + 16..eocd_pos + 20].try_into().unwrap()) as usize;
    let entries = &zip[..cd_offset];
    let central = &zip[cd_offset..eocd_pos];
    let eocd = &zip[eocd_pos..];
    let digest = content_digest(entries, central, eocd);

    let digests = sequence(&[{
        let mut d = u32le(ALGO_RSA_PKCS1_SHA256).to_vec();
        d.extend_from_slice(&len_prefixed(&digest));
        d
    }]);
    let certs = sequence(&[cert_der.to_vec()]);
    let attrs = len_prefixed(&[]);
    let mut signed_data = Vec::new();
    signed_data.extend_from_slice(&digests);
    signed_data.extend_from_slice(&certs);
    signed_data.extend_from_slice(&attrs);
    let signed_data = len_prefixed(&signed_data);

    let signer = SigningKey::<Sha256>::new(key.clone());
    // The signature covers the signed-data field *content* (without its length prefix).
    let signature = signer.sign(&signed_data[4..]).to_vec();
    let mut sig_entry = u32le(ALGO_RSA_PKCS1_SHA256).to_vec();
    sig_entry.extend_from_slice(&len_prefixed(&signature));
    let signatures = sequence(&[sig_entry]);
    let public_key = key
        .to_public_key()
        .to_public_key_der()
        .map_err(|e| Error::other(format!("public key: {e}")))?;

    let mut signer_block = Vec::new();
    signer_block.extend_from_slice(&signed_data);
    signer_block.extend_from_slice(&signatures);
    signer_block.extend_from_slice(&len_prefixed(public_key.as_bytes()));
    let signer_seq = sequence(&[signer_block]);

    let pair_len = 4 + signer_seq.len() as u64;
    let mut block = Vec::new();
    let block_size = 8 + pair_len + 8 + 16; // pair + trailing size + magic
    block.extend_from_slice(&block_size.to_le_bytes());
    block.extend_from_slice(&pair_len.to_le_bytes());
    block.extend_from_slice(&V2_BLOCK_ID.to_le_bytes());
    block.extend_from_slice(&signer_seq);
    block.extend_from_slice(&block_size.to_le_bytes());
    block.extend_from_slice(b"APK Sig Block 42");

    // The digest above must be over the EOCD as the verifier sees it: the
    // central-directory offset of the *original* layout is what the verifier
    // reconstructs by treating the signing block as absent.
    let mut out = Vec::with_capacity(zip.len() + block.len());
    out.extend_from_slice(entries);
    out.extend_from_slice(&block);
    out.extend_from_slice(central);
    let mut eocd = eocd.to_vec();
    let new_offset = (cd_offset + block.len()) as u32;
    eocd[16..20].copy_from_slice(&new_offset.to_le_bytes());
    out.extend_from_slice(&eocd);
    Ok(out)
}

/// Builds the signed .rpk. `key_pem` is a PKCS#8 RSA private key, `cert_der`
/// its self-signed X.509 certificate.
pub fn build_rpk(input: &RpkInput, key_pem: &str, cert_der: &[u8]) -> Result<Vec<u8>> {
    if !(input.module_id.len() <= 16
        && !input.module_id.is_empty()
        && input.module_id.bytes().all(|c| c.is_ascii_lowercase() || c.is_ascii_digit() || c == b'_'))
    {
        return Err(Error::other("module id does not match ADR-CAN-003"));
    }
    let key = RsaPrivateKey::from_pkcs8_pem(key_pem)
        .map_err(|e| Error::other(format!("rpk signing key must be PKCS#8 RSA PEM: {e}")))?;
    sign_v2(&build_zip(input)?, &key, cert_der)
}
