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

// Present on both the device and host staticlib builds: a no_std archive
// needs the panic handler symbol even under panic=abort. Excluded under
// `cfg(test)` so the cargo-test harness (which provides std's panic_impl)
// does not see a duplicate lang item.
#[cfg(not(test))]
#[panic_handler]
fn canopus_panic(_info: &core::panic::PanicInfo) -> ! {
    loop {}
}
