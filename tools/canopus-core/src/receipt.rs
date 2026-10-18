//! Unsigned CMI2 install receipt (176 bytes), the device-side binding of a
//! module artifact to an exact target/firmware (ADR-CAN-003/004). Replaces
//! `scripts/build-module-installer-receipt.py`.

use crate::error::{Error, Result};
use sha2::{Digest, Sha256};

pub const MAGIC: u32 = 0x3249_4D43; // "CMI2"
pub const VERSION: u32 = 2;
pub const SIZE: usize = 176;
pub const ARTIFACT_MAX: usize = 1024 * 1024;

pub fn lifecycle_code(name: &str) -> Result<u32> {
    Ok(match name {
        "removable" => 0,
        "resident-after-activation" => 1,
        "always-resident" => 2,
        "patch-reboot-required" => 3,
        other => return Err(Error::other(format!("unknown lifecycle '{other}'"))),
    })
}

/// ADR-CAN-003 module id: `^[a-z][a-z0-9_]{0,15}$`.
pub fn module_id_valid(id: &str) -> bool {
    let b = id.as_bytes();
    !b.is_empty()
        && b.len() <= 16
        && b[0].is_ascii_lowercase()
        && b.iter().all(|c| c.is_ascii_lowercase() || c.is_ascii_digit() || *c == b'_')
}

fn put_fixed(out: &mut Vec<u8>, value: &[u8], size: usize) -> Result<()> {
    if value.is_empty() || value.len() >= size {
        return Err(Error::other(format!("value must be 1..{} bytes", size - 1)));
    }
    out.extend_from_slice(value);
    out.resize(out.len() + size - value.len(), 0);
    Ok(())
}

pub fn build(
    module_id: &str,
    version_code: u32,
    lifecycle: u32,
    target_id: &str,
    firmware_sha256_hex: &str,
    artifact: &[u8],
) -> Result<Vec<u8>> {
    if !module_id_valid(module_id) {
        return Err(Error::other(format!(
            "module id '{module_id}' does not match ^[a-z][a-z0-9_]{{0,15}}$"
        )));
    }
    let firmware = hex::decode(firmware_sha256_hex)
        .ok()
        .filter(|f| f.len() == 32)
        .ok_or_else(|| Error::other("firmware SHA-256 must be 64 hexadecimal characters"))?;
    if !artifact.starts_with(b"\x7fELF") || artifact.is_empty() || artifact.len() > ARTIFACT_MAX {
        return Err(Error::other("artifact must be a non-empty bounded ELF"));
    }
    if lifecycle > 3 {
        return Err(Error::other("lifecycle class out of range"));
    }
    let mut out = Vec::with_capacity(SIZE);
    for word in [
        MAGIC,
        VERSION,
        SIZE as u32,
        0,
        lifecycle,
        version_code,
        artifact.len() as u32,
        0,
    ] {
        out.extend_from_slice(&word.to_le_bytes());
    }
    put_fixed(&mut out, module_id.as_bytes(), 32)?;
    put_fixed(&mut out, target_id.as_bytes(), 48)?;
    out.extend_from_slice(&firmware);
    out.extend_from_slice(&Sha256::digest(artifact));
    debug_assert_eq!(out.len(), SIZE);
    Ok(out)
}

#[cfg(test)]
mod tests {
    use super::*;

    const FW: &str = "f701a84ffcafa67f4d4603ad8cd66a11e5442f27140f5af0982e0975dccd225b";

    #[test]
    fn receipt_layout_matches_the_device_struct() {
        let elf = b"\x7fELF-module-bytes".to_vec();
        let r = build("hello", 3, 0, "xiaomi-band-10-pro-3.101.030", FW, &elf).unwrap();
        assert_eq!(r.len(), SIZE);
        assert_eq!(&r[0..4], b"CMI2");
        assert_eq!(u32::from_le_bytes(r[4..8].try_into().unwrap()), 2);
        assert_eq!(u32::from_le_bytes(r[8..12].try_into().unwrap()), 176);
        assert_eq!(u32::from_le_bytes(r[20..24].try_into().unwrap()), 3); // module_version
        assert_eq!(u32::from_le_bytes(r[24..28].try_into().unwrap()), elf.len() as u32);
        assert_eq!(&r[32..37], b"hello");
        assert_eq!(&r[64..64 + 28], b"xiaomi-band-10-pro-3.101.030");
        assert_eq!(hex::encode(&r[112..144]), FW);
        assert_eq!(&r[144..176], Sha256::digest(&elf).as_slice());
    }

    #[test]
    fn rejects_bad_identity_and_artifacts() {
        let elf = b"\x7fELFx".to_vec();
        assert!(build("Hello", 1, 0, "t", FW, &elf).is_err());
        assert!(build("a_very_long_module_id", 1, 0, "t", FW, &elf).is_err());
        assert!(build("hello", 1, 0, "t", "00", &elf).is_err());
        assert!(build("hello", 1, 0, "t", FW, b"MZ").is_err());
        assert!(build("hello", 1, 4, "t", FW, &elf).is_err());
    }
}
