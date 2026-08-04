//! canopus-elf: generic ELF module verifier.

pub mod verifier;

pub use verifier::{hex_sha256, Verifier, VerifyReport};
