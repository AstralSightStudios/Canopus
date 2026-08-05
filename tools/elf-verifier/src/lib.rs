//! canopus-elf: generic ELF module verifier.

pub mod verifier;

pub use verifier::{Verifier, VerifyReport, hex_sha256};
