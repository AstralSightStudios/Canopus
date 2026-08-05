//! Host lifecycle tests for the no-heap-counter module (CAN-RUST-006/007).
//!
//! Drives the module through the Rust fake target's `ModuleHarness`: full
//! removable lifecycle, prepare/activate/query/stop callbacks, status-record
//! content, and a leak check (the module must allocate nothing).

use canopus_abi::{ABI_MAJOR, ContextV1, ModuleDescriptorV1, StatusWriterV1};
use canopus_host_fake::{ModuleHarness, fake_alloc_live_count};
use canopus_runtime::LIFECYCLE_REMOVABLE;
use no_heap_counter as module;

fn harness() -> ModuleHarness {
    // ModuleHarness copies the descriptor, so it is safe to pass it by value.
    let d = unsafe { *module::canopus_module_descriptor_ptr() };
    ModuleHarness::new(d, LIFECYCLE_REMOVABLE)
}

/// The module's global state (COUNT/STARTED) is shared, so tests that drive it
/// must be serialized (cargo runs #[test] fns on parallel threads).
static TEST_LOCK: std::sync::Mutex<()> = std::sync::Mutex::new(());
fn lock() -> std::sync::MutexGuard<'static, ()> {
    TEST_LOCK.lock().unwrap()
}

#[test]
fn full_removable_cycle_succeeds() {
    let _g = lock();
    let mut h = harness();
    h.run_removable_cycle(); // discovered -> ... -> unloaded, all legal
    assert_eq!(h.prepare(), 0);
    assert_eq!(h.activate(), 0);
    assert_eq!(h.deactivate(), 0);
    assert_eq!(h.stop(), 0);
    assert_eq!(fake_alloc_live_count(), 0);
}

#[test]
fn query_reports_counter_after_increment() {
    let _g = lock();
    let mut h = harness();
    assert_eq!(h.prepare(), 0);
    assert_eq!(h.activate(), 0);
    module::canopus_mod_increment();
    module::canopus_mod_increment();
    module::canopus_mod_increment();
    let status = h.query();
    // magic "CNTR" + count 3, little-endian.
    assert_eq!(status.len(), 8);
    assert_eq!(status[0], 0x52);
    assert_eq!(status[1], 0x54);
    assert_eq!(status[2], 0x4E);
    assert_eq!(status[3], 0x43);
    assert_eq!(u32::from_le_bytes([status[4], status[5], status[6], status[7]]), 3);
    assert_eq!(fake_alloc_live_count(), 0);
}

#[test]
fn prepare_resets_counter() {
    let _g = lock();
    let mut h = harness();
    assert_eq!(h.prepare(), 0);
    assert_eq!(h.activate(), 0);
    module::canopus_mod_increment();
    assert_eq!(h.query()[4..8], [1, 0, 0, 0]);
    assert_eq!(h.stop(), 0);
    assert_eq!(h.prepare(), 0); // re-prepare after stop: reset
    assert_eq!(h.query()[4..8], [0, 0, 0, 0]);
    assert_eq!(fake_alloc_live_count(), 0);
}

#[test]
fn null_query_writer_rejected() {
    let _g = lock();
    // Direct callback contract check: null writer -> -1.
    assert_eq!(module::canopus_mod_query(core::ptr::null_mut()), -1);
}

#[test]
fn descriptor_abi_fields_valid() {
    let _g = lock();
    let d = unsafe { *module::canopus_module_descriptor_ptr() };
    assert_eq!(d.struct_size, core::mem::size_of::<ModuleDescriptorV1>() as u32);
    assert_eq!(d.abi_major, ABI_MAJOR);
    assert!(d.prepare.is_some());
    assert!(d.activate.is_some());
    assert!(d.deactivate.is_some());
    assert!(d.stop.is_some());
    assert!(d.query.is_some());
    // target_id is a fixed 32-byte NUL-padded field (28-char id).
    assert_eq!(&d.target_id[..28], b"xiaomi-band-10-pro-3.101.030");
    assert_eq!(d.target_id[28], 0);
}

// Compile-time check that the callbacks are valid `extern "C"` fns.
const _: Option<canopus_abi::CbPrepare> = Some(module::canopus_mod_prepare);
const _: Option<canopus_abi::CbQuery> = Some(module::canopus_mod_query as extern "C" fn(*mut StatusWriterV1) -> i32);
const _: extern "C" fn(*const ContextV1) -> i32 = module::canopus_mod_activate;
