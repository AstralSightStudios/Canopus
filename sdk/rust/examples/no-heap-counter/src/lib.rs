//! no-heap-counter — Canopus no-heap removable Rust example module
//! (CAN-RUST-007). Host-testable rlib root; see `module.rs` for the logic and
//! `device.rs` for the device staticlib build.

#![no_std]

pub mod module;

pub use module::*;
