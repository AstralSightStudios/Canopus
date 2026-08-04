//! no-heap-counter device build — staticlib root (CAN-RUST-004/007).
//!
//! Shares `module.rs` with the host-testable rlib package and adds the
//! device panic policy (CAN-RUST-009): a panic must never cross FFI, so the
//! handler fails closed by spinning forever; the firmware watchdog/health
//! monitor resets the device. Only present on the device target so the host
//! rlib stays linkable into the std test harness.

#![no_std]
#![deny(unsafe_op_in_unsafe_fn)]

#[path = "../../no-heap-counter/src/module.rs"]
pub mod module;

pub use module::*;

#[cfg(target_os = "none")]
#[panic_handler]
fn canopus_panic(_info: &core::panic::PanicInfo) -> ! {
    loop {}
}
