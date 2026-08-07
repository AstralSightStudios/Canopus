//! no-heap-counter module logic.
//!
//! Shared by two crate roots:
//!   - `src/lib.rs`    (rlib)      — host-testable, no panic handler
//!   - `src/device.rs` (staticlib) — device build, adds the panic handler
//!
//! Demonstrates the Rust module shape end-to-end:
//!   - `#![no_std]`, `panic = "abort"`, no allocator, no threads, no TLS;
//!   - a fixed `#[no_mangle]` module descriptor exposed to the stock loader;
//!   - the identity guard (device builds only) failing closed on a firmware
//!     mismatch;
//!   - a fixed status record written through `canopus-runtime`;
//!   - host lifecycle tests via `canopus-host-fake`'s `ModuleHarness`.

use canopus_abi::*;
use canopus_runtime::*;
use core::sync::atomic::{AtomicBool, AtomicU32, Ordering};

#[cfg(feature = "device")]
use canopus_target_generated::canopus_identity_guard;

const COUNTER_MAGIC: u32 = 0x434E_5452; // "CNTR"

/// Packs a byte string into a fixed-width NUL-padded id field.
const fn pack<const N: usize>(s: &[u8]) -> [u8; N] {
    let mut out = [0u8; N];
    let mut i = 0;
    while i < s.len() && i < N {
        out[i] = s[i];
        i += 1;
    }
    out
}

// ---------------------------------------------------------------------------
// Module state: atomics only — no heap, no `static mut`, no Cell (must be
// `Sync` for a `static`). The device is a single-threaded event loop, so
// Relaxed orderings are sufficient.
// ---------------------------------------------------------------------------

static COUNT: AtomicU32 = AtomicU32::new(0);
static STARTED: AtomicBool = AtomicBool::new(false);

// ---------------------------------------------------------------------------
// Descriptor callbacks (extern "C", never panic across FFI)
// ---------------------------------------------------------------------------

#[unsafe(no_mangle)]
pub extern "C" fn canopus_mod_prepare(_ctx: *const ContextV1) -> i32 {
    COUNT.store(0, Ordering::Relaxed);
    STARTED.store(false, Ordering::Relaxed);
    0
}

#[unsafe(no_mangle)]
pub extern "C" fn canopus_mod_activate(_ctx: *const ContextV1) -> i32 {
    #[cfg(feature = "device")]
    {
        // Fail closed: never activate on the wrong firmware.
        if canopus_identity_guard() != 0 {
            return -1;
        }
    }
    STARTED.store(true, Ordering::Relaxed);
    0
}

#[unsafe(no_mangle)]
pub extern "C" fn canopus_mod_deactivate(_ctx: *const ContextV1) -> i32 {
    STARTED.store(false, Ordering::Relaxed);
    0
}

#[unsafe(no_mangle)]
pub extern "C" fn canopus_mod_stop(_ctx: *const ContextV1) -> i32 {
    STARTED.store(false, Ordering::Relaxed);
    0
}

// The stock loader calls query as a plain extern "C" fn pointer, so it cannot
// be `unsafe fn`; the raw-pointer dereference is the module's only contract
// with the loader-supplied writer.
#[allow(clippy::not_unsafe_ptr_arg_deref)]
#[unsafe(no_mangle)]
pub extern "C" fn canopus_mod_query(w: *mut StatusWriterV1) -> i32 {
    if w.is_null() {
        return -1;
    }
    let w = unsafe { &mut *w };
    unsafe {
        if !status_put_u32(w, COUNTER_MAGIC) {
            return -1;
        }
        if !status_put_u32(w, COUNT.load(Ordering::Relaxed)) {
            return -1;
        }
        status_writer_publish(w);
    }
    0
}

/// Test/control entry: bumps the counter. Called by host tests.
#[unsafe(no_mangle)]
pub extern "C" fn canopus_mod_increment() {
    COUNT.fetch_add(1, Ordering::Relaxed);
}

// ---------------------------------------------------------------------------
// Module descriptor + loader entry
// ---------------------------------------------------------------------------

#[unsafe(no_mangle)]
pub static canopus_module_descriptor: ModuleDescriptorV1 = ModuleDescriptorV1 {
    struct_size: core::mem::size_of::<ModuleDescriptorV1>() as u32,
    abi_major: ABI_MAJOR,
    abi_minor: ABI_MINOR,
    flags: 0,
    module_id: pack(b"org.canopus.no-heap-counter"),
    module_version: pack(b"0.1.0"),
    build_id: pack(b"no-heap-counter-0.1.0"),
    target_id: pack(b"xiaomi-band-10-pro-3.101.030"),
    prepare: Some(canopus_mod_prepare),
    activate: Some(canopus_mod_activate),
    deactivate: Some(canopus_mod_deactivate),
    stop: Some(canopus_mod_stop),
    query: Some(canopus_mod_query),
    publish_native_app: None,
    publish_native_app_stage: None,
};

/// The C constructor shim calls this to let the loader discover the descriptor.
#[unsafe(no_mangle)]
pub extern "C" fn canopus_module_descriptor_ptr() -> *const ModuleDescriptorV1 {
    &canopus_module_descriptor
}
