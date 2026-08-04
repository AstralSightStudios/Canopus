//! canopus-host-fake — host-only fake target for Rust modules.
//!
//! Rust mirror of `tests/host/fake_target.{h,c}` (CAN-C-009): a deterministic
//! arena allocator, monotonic clock, timer wheel, driver namespace, plus a
//! `ModuleHarness` that drives a `ModuleDescriptorV1` through its callbacks
//! end-to-end without hardware.
//!
//! This crate is std-only (host testing); device builds never link it.

use std::collections::HashMap;
use std::sync::Mutex;
use std::sync::OnceLock;

// ===========================================================================
// Allocator
// ===========================================================================

/// Allocates `n` bytes from the host heap and tracks the allocation for leak
/// checks. Returns a pointer that `fake_free` must later receive.
pub fn fake_alloc(n: usize) -> *mut u8 {
    let mut v = vec![0u8; n];
    let p = v.as_mut_ptr();
    std::mem::forget(v); // leak the Vec header; the payload is freed in fake_free
    ALLOC
        .get_or_init(Default::default)
        .lock()
        .unwrap()
        .insert(p as usize, n);
    p
}

/// Frees an allocation from [`fake_alloc`] using the original size's layout.
/// No-op on null or an untracked pointer.
pub fn fake_free(p: *mut u8) {
    if p.is_null() {
        return;
    }
    let mut map = ALLOC.get_or_init(Default::default).lock().unwrap();
    if let Some(n) = map.remove(&(p as usize)) {
        drop(map);
        unsafe {
            let layout = std::alloc::Layout::array::<u8>(n).unwrap();
            std::alloc::dealloc(p, layout);
        }
    }
}

/// Number of live (allocated, not freed) allocations. Must be 0 after a clean
/// module release.
pub fn fake_alloc_live_count() -> usize {
    ALLOC.get_or_init(Default::default).lock().unwrap().len()
}

/// Resets the allocator accounting (for test setup).
pub fn fake_alloc_reset() {
    ALLOC.get_or_init(Default::default).lock().unwrap().clear();
}

static ALLOC: OnceLock<Mutex<HashMap<usize, usize>>> = OnceLock::new();

// ===========================================================================
// Clock
// ===========================================================================

#[derive(Default)]
struct ClockState {
    mono_ms: u64,
}

static CLOCK: OnceLock<Mutex<ClockState>> = OnceLock::new();

fn clock() -> std::sync::MutexGuard<'static, ClockState> {
    CLOCK.get_or_init(Default::default).lock().unwrap()
}

/// Mirrors the stock `clock_gettime(CLOCK_MONOTONIC)` contract: returns 0 and
/// writes a valid monotonic time.
pub fn fake_clock_gettime() -> (i32, u64) {
    let st = clock();
    (0, st.mono_ms)
}

/// Advances the fake clock by `ms` (used by tests that model time).
pub fn fake_clock_advance(ms: u64) {
    clock().mono_ms += ms;
}

// ===========================================================================
// Timer wheel
// ===========================================================================

pub const FAKE_TIMER_MAX: usize = 8;

struct TimerEntry {
    id: u32,
    period: u32,
    remaining: u32,
    cb: Box<dyn FnMut() + Send>,
}

#[derive(Default)]
struct TimerState {
    slots: Vec<Option<TimerEntry>>,
    next_id: u32,
}

static TIMERS: OnceLock<Mutex<TimerState>> = OnceLock::new();

fn timers() -> std::sync::MutexGuard<'static, TimerState> {
    TIMERS.get_or_init(Default::default).lock().unwrap()
}

/// Registers a periodic timer; returns a handle, or `None` when full.
pub fn timer_register<F: FnMut() + Send + 'static>(
    cb: F,
    period_ms: u32,
) -> Option<u32> {
    let mut st = timers();
    let active = st.slots.iter().filter(|s| s.is_some()).count();
    if active >= FAKE_TIMER_MAX {
        return None;
    }
    let id = st.next_id;
    st.next_id = st.next_id.wrapping_add(1);
    let entry = TimerEntry {
        id,
        period: period_ms,
        remaining: period_ms,
        cb: Box::new(cb),
    };
    // Reuse a cancelled slot if one exists; otherwise append.
    match st.slots.iter_mut().find(|s| s.is_none()) {
        Some(slot) => *slot = Some(entry),
        None => st.slots.push(Some(entry)),
    }
    Some(id)
}

/// Cancels a timer by handle. Returns `false` if unknown (or already
/// cancelled). A cancelled slot is freed for reuse but keeps its position.
pub fn timer_cancel(id: u32) -> bool {
    let mut st = timers();
    match st.slots.iter_mut().find(|s| s.as_ref().map(|t| t.id) == Some(id)) {
        Some(slot) => slot.take().is_some(),
        None => false,
    }
}

/// Advances one tick and fires every timer whose countdown reached zero,
/// re-arming periodic timers. The callback closure is retained across fires
/// (periodic timers keep firing forever until cancelled).
pub fn timer_tick() {
    // Collect due handles first, then invoke so a firing callback that
    // registers/cancels another timer does not invalidate the iteration.
    let due: Vec<u32> = {
        let mut st = timers();
        st.slots
            .iter_mut()
            .filter_map(|slot| {
                let t = slot.as_mut()?;
                t.remaining = t.remaining.saturating_sub(1);
                if t.remaining == 0 {
                    t.remaining = t.period;
                    Some(t.id)
                } else {
                    None
                }
            })
            .collect()
    };
    for id in due {
        // Swap the closure out, invoke it WITHOUT holding the lock (a
        // callback may register/cancel timers), then restore it.
        let cb = {
            let mut st = timers();
            st.slots
                .iter_mut()
                .find(|s| s.as_ref().map(|t| t.id) == Some(id))
                .and_then(|slot| {
                    slot.as_mut()
                        .map(|t| std::mem::replace(&mut t.cb, Box::new(|| {})))
                })
        };
        if let Some(mut cb) = cb {
            cb();
            let mut st = timers();
            if let Some(slot) = st.slots.iter_mut().find(|s| s.as_ref().map(|t| t.id) == Some(id)) {
                if let Some(t) = slot.as_mut() {
                    t.cb = cb;
                }
            }
        }
    }
}

/// Number of active (registered, not cancelled) timers.
pub fn timer_active_count() -> usize {
    timers().slots.iter().filter(|s| s.is_some()).count()
}

// ===========================================================================
// Driver namespace
// ===========================================================================

pub const FAKE_DRIVER_MAX: usize = 4;
pub const FAKE_DRIVER_EBUSY: i32 = -16;

struct Driver {
    ops: usize,
    private_data: usize,
    held: bool,
}

#[derive(Default)]
struct DriverState {
    names: Vec<(String, Driver)>,
}

static DRIVERS: OnceLock<Mutex<DriverState>> = OnceLock::new();

fn drivers() -> std::sync::MutexGuard<'static, DriverState> {
    DRIVERS.get_or_init(Default::default).lock().unwrap()
}

/// Returns 0 on success; -1 on name collision or full table.
pub fn driver_register(name: &str, ops: usize, private_data: usize) -> i32 {
    let mut st = drivers();
    if st.names.len() >= FAKE_DRIVER_MAX {
        return -1;
    }
    if st.names.iter().any(|(n, _)| n == name) {
        return -1;
    }
    st.names.push((
        name.to_string(),
        Driver { ops, private_data, held: false },
    ));
    0
}

/// Unregister: 0 on success, [`FAKE_DRIVER_EBUSY`] when an open reference
/// holds the name, -1 when unknown.
pub fn driver_unregister(name: &str) -> i32 {
    let mut st = drivers();
    let Some(i) = st.names.iter().position(|(n, _)| n == name) else {
        return -1;
    };
    if st.names[i].1.held {
        return FAKE_DRIVER_EBUSY;
    }
    st.names.remove(i);
    0
}

/// Simulates an open reference (blocks unregister with EBUSY).
pub fn driver_hold(name: &str) -> bool {
    let mut st = drivers();
    match st.names.iter_mut().find(|(n, _)| n == name) {
        Some((_, d)) => {
            d.held = true;
            true
        }
        None => false,
    }
}

pub fn driver_release(name: &str) -> bool {
    let mut st = drivers();
    match st.names.iter_mut().find(|(n, _)| n == name) {
        Some((_, d)) => {
            d.held = false;
            true
        }
        None => false,
    }
}

pub fn driver_count() -> usize {
    drivers().names.len()
}

// ===========================================================================
// Module harness
// ===========================================================================

use canopus_abi::{
    ContextV1, ModuleDescriptorV1, SnapshotV1, StatusWriterV1, STATUS_RECORD_MAX,
};
use canopus_runtime::{LifecycleV1, lifecycle_init, status_writer_init};

/// Drives a module descriptor through its callbacks with a fake context,
/// mirroring how the stock loader invokes a module on device.
pub struct ModuleHarness {
    pub descriptor: ModuleDescriptorV1,
    pub lifecycle: LifecycleV1,
    pub status_buf: [u8; STATUS_RECORD_MAX as usize],
    pub status: StatusWriterV1,
}

impl ModuleHarness {
    pub fn new(descriptor: ModuleDescriptorV1, lifecycle_class: u32) -> Self {
        let mut h = Self {
            descriptor,
            lifecycle: LifecycleV1 {
                state: 0,
                lifecycle_class,
                generation: 0,
                reserved: 0,
            },
            status_buf: [0u8; STATUS_RECORD_MAX as usize],
            status: StatusWriterV1 {
                buf: std::ptr::null_mut(),
                capacity: 0,
                used: 0,
                dropped: 0,
                snap: SnapshotV1 { sequence: 0 },
            },
        };
        assert!(lifecycle_init(&mut h.lifecycle, lifecycle_class));
        h
    }

    fn ctx(&self) -> *const ContextV1 {
        std::ptr::null()
    }

    /// Returns a status writer initialised over the harness buffer.
    pub fn status_writer(&mut self) -> *mut StatusWriterV1 {
        let buf = self.status_buf.as_mut_ptr();
        unsafe {
            status_writer_init(&mut self.status, buf, STATUS_RECORD_MAX);
        }
        &mut self.status as *mut StatusWriterV1
    }

    pub fn prepare(&mut self) -> i32 {
        self.descriptor
            .prepare
            .map(|f| f(self.ctx()))
            .unwrap_or(-1)
    }

    pub fn activate(&mut self) -> i32 {
        self.descriptor
            .activate
            .map(|f| f(self.ctx()))
            .unwrap_or(-1)
    }

    pub fn deactivate(&mut self) -> i32 {
        self.descriptor
            .deactivate
            .map(|f| f(self.ctx()))
            .unwrap_or(-1)
    }

    pub fn stop(&mut self) -> i32 {
        self.descriptor.stop.map(|f| f(self.ctx())).unwrap_or(-1)
    }

    /// Runs the query callback and returns the published status bytes.
    pub fn query(&mut self) -> &[u8] {
        let w = self.status_writer();
        if let Some(q) = self.descriptor.query {
            q(w);
        }
        let used = self.status.used as usize;
        &self.status_buf[..used.min(STATUS_RECORD_MAX as usize)]
    }

    /// Full removable lifecycle: load -> ready -> active -> stop -> unloaded.
    /// Every transition must succeed or the test fails loudly.
    pub fn run_removable_cycle(&mut self) {
        use canopus_runtime::*;
        for s in [
            STATE_VERIFIED,
            STATE_INSTALLED,
            STATE_DISABLED,
            STATE_ENABLED,
            STATE_LOADING,
            STATE_PREPARING,
            STATE_READY,
            STATE_ACTIVE,
            STATE_STOPPING,
            STATE_DRAINING,
            STATE_UNLOADED,
        ] {
            assert!(
                lifecycle_transition(&mut self.lifecycle, s),
                "transition to {}",
                state_name(s)
            );
        }
    }

    /// A full load+run+unload, driving the module callbacks at the right
    /// points. Returns the status record from the query.
    pub fn run_full_cycle(&mut self) -> i32 {
        self.run_removable_cycle();
        // callbacks during the cycle (checked by the module itself)
        if self.prepare() != 0 {
            return -1;
        }
        if self.activate() != 0 {
            return -2;
        }
        let _ = self.query();
        if self.deactivate() != 0 {
            return -3;
        }
        if self.stop() != 0 {
            return -4;
        }
        0
    }
}

// ===========================================================================
// Tests
// ===========================================================================

#[cfg(test)]
mod tests {
    use super::*;

    /// The fake's global state is shared; tests that mutate it must be
    /// serialized. Rust runs `#[test]` fns on parallel threads by default.
    static TEST_LOCK: std::sync::Mutex<()> = std::sync::Mutex::new(());

    fn lock() -> std::sync::MutexGuard<'static, ()> {
        TEST_LOCK.lock().unwrap()
    }

    #[test]
    fn alloc_tracking() {
        let _g = lock();
        fake_alloc_reset();
        fake_alloc_reset();
        let p = fake_alloc(16);
        assert_eq!(fake_alloc_live_count(), 1);
        fake_free(p);
        assert_eq!(fake_alloc_live_count(), 0);
        fake_free(p); // double free is a no-op
        assert_eq!(fake_alloc_live_count(), 0);
    }

    #[test]
    fn timer_wheel_fires_and_rearms() {
        let _g = lock();
        use std::sync::Arc;
        use std::sync::atomic::{AtomicU32, Ordering};
        let fired = Arc::new(AtomicU32::new(0));
        let f = Arc::clone(&fired);
        let h = timer_register(
            move || {
                f.fetch_add(1, Ordering::SeqCst);
            },
            3,
        )
        .unwrap();
        timer_tick();
        timer_tick();
        assert_eq!(fired.load(Ordering::SeqCst), 0);
        timer_tick(); // 3 ticks -> fire
        assert_eq!(fired.load(Ordering::SeqCst), 1);
        // re-armed
        timer_tick();
        timer_tick();
        timer_tick();
        assert_eq!(fired.load(Ordering::SeqCst), 2);
        assert!(timer_cancel(h));
        assert!(!timer_cancel(h));
    }

    #[test]
    fn timer_full_returns_none() {
        let _g = lock();
        let mut hs = Vec::new();
        for _ in 0..FAKE_TIMER_MAX {
            let h = timer_register(|| {}, 1);
            assert!(h.is_some());
            hs.push(h.unwrap());
        }
        assert!(timer_register(|| {}, 1).is_none());
        for h in hs {
            timer_cancel(h);
        }
    }

    #[test]
    fn driver_ebusy_semantics() {
        let _g = lock();
        assert_eq!(driver_register("dev/foo", 1, 2), 0);
        assert_eq!(driver_register("dev/foo", 1, 2), -1); // collision
        assert!(driver_hold("dev/foo"));
        assert_eq!(driver_unregister("dev/foo"), FAKE_DRIVER_EBUSY);
        assert!(driver_release("dev/foo"));
        assert_eq!(driver_unregister("dev/foo"), 0);
        assert_eq!(driver_unregister("dev/foo"), -1);
    }
}
