//! canopus-runtime — portable Canopus module runtime in Rust.
//!
//! Behavior-for-behavior port of `runtime/*/canopus_*.c` + `canopus_runtime.h`
//! (host-testable, target-independent). All state lives in the caller-provided
//! `#[repr(C)]` structs from `canopus-abi`; nothing here allocates, and every
//! function can be called from a `no_std` module under `panic = "abort"`.
//!
//! The C counterpart is the reference; where a name or constant exists there,
//! it is kept verbatim so host tests can cross-check both implementations.

#![no_std]
#![deny(unsafe_op_in_unsafe_fn)]

pub use canopus_abi::*;

pub mod allocator;

use core::ptr::{read_volatile, write_volatile};

// ===========================================================================
// Lifecycle state machine (architecture §10.3)
// ===========================================================================

pub const STATE_DISCOVERED: u32 = 1;
pub const STATE_VERIFIED: u32 = 2;
pub const STATE_INSTALLED: u32 = 3;
pub const STATE_DISABLED: u32 = 4;
pub const STATE_ENABLED: u32 = 5;
pub const STATE_LOADING: u32 = 6;
pub const STATE_PREPARING: u32 = 7;
pub const STATE_READY: u32 = 8;
pub const STATE_ACTIVE: u32 = 9;
pub const STATE_STOPPING: u32 = 10;
pub const STATE_DRAINING: u32 = 11;
pub const STATE_UNLOADED: u32 = 12;
pub const STATE_BOOT_RESIDENT: u32 = 13;
pub const STATE_DISABLED_NEXT_BOOT: u32 = 14;
pub const STATE_FAILED: u32 = 15;
pub const STATE_FAIL_STOP: u32 = 16;
pub const STATE_QUARANTINED_NEXT_BOOT: u32 = 17;
pub const STATE_UPDATE_STAGED: u32 = 18;
pub const STATE_REBOOT_REQUIRED: u32 = 19;
pub const STATE_REMOVE_PENDING: u32 = 20;

/// `#[repr(C)]` mirror of `struct canopus_lifecycle_v1`.
#[repr(C)]
#[derive(Copy, Clone, Debug)]
pub struct LifecycleV1 {
    pub state: u32,
    pub lifecycle_class: u32,
    pub generation: u32,
    pub reserved: u32,
}

const CLASS_REMOVABLE: u32 = 1 << LIFECYCLE_REMOVABLE;
const CLASS_RESIDENT: u32 = 1 << LIFECYCLE_RESIDENT_AFTER_ACTIVATION;
const CLASS_ALWAYS: u32 = 1 << LIFECYCLE_ALWAYS_RESIDENT;
const CLASS_PATCH: u32 = 1 << LIFECYCLE_PATCH_REBOOT_REQUIRED;
const CLASS_ALL: u32 = CLASS_REMOVABLE | CLASS_RESIDENT | CLASS_ALWAYS | CLASS_PATCH;

struct Transition {
    from: u32,
    to: u32,
    classes: u32,
}

// Table must stay identical to `runtime/lifecycle/canopus_lifecycle.c`.
const TRANSITIONS: &[Transition] = &[
    Transition { from: STATE_DISCOVERED, to: STATE_VERIFIED, classes: CLASS_ALL },
    Transition { from: STATE_VERIFIED, to: STATE_INSTALLED, classes: CLASS_ALL },
    Transition { from: STATE_INSTALLED, to: STATE_DISABLED, classes: CLASS_ALL },
    Transition { from: STATE_DISABLED, to: STATE_ENABLED, classes: CLASS_ALL },
    Transition { from: STATE_ENABLED, to: STATE_DISABLED, classes: CLASS_ALL },
    Transition { from: STATE_ENABLED, to: STATE_LOADING, classes: CLASS_ALL },
    Transition { from: STATE_LOADING, to: STATE_PREPARING, classes: CLASS_ALL },
    Transition { from: STATE_PREPARING, to: STATE_READY, classes: CLASS_ALL },
    Transition { from: STATE_READY, to: STATE_ACTIVE, classes: CLASS_ALL },
    // removable unload path
    Transition { from: STATE_ACTIVE, to: STATE_STOPPING, classes: CLASS_REMOVABLE },
    Transition { from: STATE_STOPPING, to: STATE_DRAINING, classes: CLASS_REMOVABLE },
    Transition { from: STATE_DRAINING, to: STATE_UNLOADED, classes: CLASS_REMOVABLE },
    Transition { from: STATE_READY, to: STATE_STOPPING, classes: CLASS_REMOVABLE },
    Transition { from: STATE_DISABLED, to: STATE_UNLOADED, classes: CLASS_REMOVABLE },
    // resident barrier
    Transition { from: STATE_ACTIVE, to: STATE_BOOT_RESIDENT, classes: CLASS_RESIDENT | CLASS_ALWAYS | CLASS_PATCH },
    Transition { from: STATE_BOOT_RESIDENT, to: STATE_DISABLED_NEXT_BOOT, classes: CLASS_RESIDENT | CLASS_ALWAYS | CLASS_PATCH },
    // failure paths
    Transition { from: STATE_LOADING, to: STATE_FAILED, classes: CLASS_ALL },
    Transition { from: STATE_PREPARING, to: STATE_FAILED, classes: CLASS_ALL },
    Transition { from: STATE_READY, to: STATE_FAILED, classes: CLASS_ALL },
    Transition { from: STATE_ACTIVE, to: STATE_FAIL_STOP, classes: CLASS_RESIDENT | CLASS_ALWAYS | CLASS_PATCH },
    Transition { from: STATE_BOOT_RESIDENT, to: STATE_FAIL_STOP, classes: CLASS_RESIDENT | CLASS_ALWAYS | CLASS_PATCH },
    Transition { from: STATE_FAIL_STOP, to: STATE_QUARANTINED_NEXT_BOOT, classes: CLASS_RESIDENT | CLASS_ALWAYS | CLASS_PATCH },
    // resident update/remove
    Transition { from: STATE_BOOT_RESIDENT, to: STATE_UPDATE_STAGED, classes: CLASS_RESIDENT | CLASS_ALWAYS | CLASS_PATCH },
    Transition { from: STATE_UPDATE_STAGED, to: STATE_REBOOT_REQUIRED, classes: CLASS_ALL },
    Transition { from: STATE_BOOT_RESIDENT, to: STATE_REMOVE_PENDING, classes: CLASS_RESIDENT | CLASS_ALWAYS | CLASS_PATCH },
    Transition { from: STATE_REMOVE_PENDING, to: STATE_REBOOT_REQUIRED, classes: CLASS_ALL },
];

fn class_mask(lifecycle_class: u32) -> u32 {
    if lifecycle_class >= 4 {
        0
    } else {
        1 << lifecycle_class
    }
}

/// Returns `true` when `from -> to` is allowed for the lifecycle class.
pub fn lifecycle_allow(from: u32, to: u32, lifecycle_class: u32) -> bool {
    let mask = class_mask(lifecycle_class);
    if mask == 0 {
        return false;
    }
    TRANSITIONS
        .iter()
        .any(|t| t.from == from && t.to == to && (t.classes & mask) != 0)
}

/// Initialises the lifecycle into DISCOVERED. Returns `false` on a bad class.
pub fn lifecycle_init(lc: &mut LifecycleV1, lifecycle_class: u32) -> bool {
    if lifecycle_class >= 4 {
        return false;
    }
    lc.state = STATE_DISCOVERED;
    lc.lifecycle_class = lifecycle_class;
    lc.generation = 1;
    lc.reserved = 0;
    true
}

/// Attempts `state -> to_state`, bumping the generation on success.
pub fn lifecycle_transition(lc: &mut LifecycleV1, to_state: u32) -> bool {
    if !lifecycle_allow(lc.state, to_state, lc.lifecycle_class) {
        return false;
    }
    lc.state = to_state;
    lc.generation = lc.generation.wrapping_add(1);
    true
}

/// Canonical human-readable state name.
pub fn state_name(state: u32) -> &'static str {
    match state {
        STATE_DISCOVERED => "discovered",
        STATE_VERIFIED => "verified",
        STATE_INSTALLED => "installed",
        STATE_DISABLED => "disabled",
        STATE_ENABLED => "enabled",
        STATE_LOADING => "loading",
        STATE_PREPARING => "preparing",
        STATE_READY => "ready",
        STATE_ACTIVE => "active",
        STATE_STOPPING => "stopping",
        STATE_DRAINING => "draining",
        STATE_UNLOADED => "unloaded",
        STATE_BOOT_RESIDENT => "boot-resident",
        STATE_DISABLED_NEXT_BOOT => "disabled-next-boot",
        STATE_FAILED => "failed",
        STATE_FAIL_STOP => "fail-stop",
        STATE_QUARANTINED_NEXT_BOOT => "quarantined-next-boot",
        STATE_UPDATE_STAGED => "update-staged",
        STATE_REBOOT_REQUIRED => "reboot-required",
        STATE_REMOVE_PENDING => "remove-pending",
        _ => "unknown",
    }
}

// ===========================================================================
// Sequence snapshot (architecture §16)
// ===========================================================================

/// Marks a snapshot as being written (odd sequence). No-op if already odd.
pub fn snapshot_begin(snap: &mut SnapshotV1) {
    unsafe { write_volatile(&mut snap.sequence, snap.sequence | 1) };
}

/// Publishes the snapshot as valid (even sequence). No-op if already even.
pub fn snapshot_commit(snap: &mut SnapshotV1) {
    let cur = unsafe { read_volatile(&snap.sequence) };
    if (cur & 1) != 0 {
        unsafe { write_volatile(&mut snap.sequence, cur.wrapping_add(1)) };
    }
}

/// Readers must only accept the record when the sequence is even.
pub fn snapshot_ready(snap: &SnapshotV1) -> bool {
    (unsafe { read_volatile(&snap.sequence) } & 1) == 0
}

// ===========================================================================
// Status writer (fixed-width append-only record)
// ===========================================================================

/// Initialises a writer over `buf`. The buffer must stay alive and writable
/// for the writer's lifetime. Returns `false` when `capacity` exceeds
/// [`STATUS_RECORD_MAX`] or is zero.
///
/// # Safety
/// `buf` must point to `capacity` writable bytes for as long as the writer is
/// used, and must not alias anything read by the module.
pub unsafe fn status_writer_init(
    w: &mut StatusWriterV1,
    buf: *mut u8,
    capacity: u32,
) -> bool {
    if capacity == 0 || capacity > STATUS_RECORD_MAX {
        return false;
    }
    w.buf = buf;
    w.capacity = capacity;
    w.used = 0;
    w.dropped = 0;
    w.snap.sequence = 0; // even == valid immediately
    true
}

/// Appends a byte. On overflow increments `dropped` and returns `false`.
///
/// # Safety
/// Must only be called on a writer with a live buffer (see `status_writer_init`).
pub unsafe fn status_put_u8(w: &mut StatusWriterV1, v: u8) -> bool {
    unsafe { put_bytes(w, &[v]) }
}

/// See [`status_put_u8`]. Little-endian on device (ARM) and host.
pub unsafe fn status_put_u16(w: &mut StatusWriterV1, v: u16) -> bool {
    unsafe { put_bytes(w, &v.to_le_bytes()) }
}

/// See [`status_put_u8`]. Little-endian on device (ARM) and host.
pub unsafe fn status_put_u32(w: &mut StatusWriterV1, v: u32) -> bool {
    unsafe { put_bytes(w, &v.to_le_bytes()) }
}

/// Appends `len` bytes from `src`. See [`status_put_u8`].
pub unsafe fn status_put_bytes(w: &mut StatusWriterV1, src: &[u8]) -> bool {
    unsafe { put_bytes(w, src) }
}

unsafe fn put_bytes(w: &mut StatusWriterV1, src: &[u8]) -> bool {
    let room = w.capacity as usize - w.used as usize;
    if src.len() > room {
        w.dropped = w.dropped.wrapping_add(1);
        return false;
    }
    unsafe {
        let dst = w.buf.add(w.used as usize);
        core::ptr::copy_nonoverlapping(src.as_ptr(), dst, src.len());
    }
    w.used += src.len() as u32;
    true
}

/// Marks the record valid (publishes an even sequence).
pub fn status_writer_publish(w: &mut StatusWriterV1) {
    snapshot_commit(&mut w.snap);
}

// ===========================================================================
// Resource tracker (architecture §10.4)
// ===========================================================================

pub const RESOURCE_MAX: u32 = 32;

pub const RES_CHAR_DEVICE: u32 = 1;
pub const RES_HEAP: u32 = 2;
pub const RES_CALLBACK_TABLE: u32 = 3;
pub const RES_TIMER: u32 = 4;
pub const RES_WORKER: u32 = 5;
pub const RES_SERVICE: u32 = 6;
pub const RES_PROTOCOL: u32 = 7;
pub const RES_FD: u32 = 8;
pub const RES_HOOK: u32 = 9;
pub const RES_OPEN_REF: u32 = 10;
pub const RES_INFLIGHT_CALLBACK: u32 = 11;

pub const RES_ACTIVE: u32 = 1;
pub const RES_DRAINING: u32 = 2;
pub const RES_DETACHED: u32 = 3;
pub const RES_RELEASED: u32 = 4;
pub const RES_RETAINED_UNTIL_REBOOT: u32 = 5;

/// `#[repr(C)]` mirror of `struct canopus_resource_v1`.
#[repr(C)]
#[derive(Copy, Clone, Debug)]
pub struct ResourceV1 {
    pub kind: u32,
    pub state: u32,
    pub generation: u32,
    pub handle: *mut core::ffi::c_void,
    pub on_release: Option<extern "C" fn(*mut ResourceV1)>,
}

/// `#[repr(C)]` mirror of `struct canopus_resource_tracker_v1`.
#[repr(C)]
#[derive(Copy, Clone, Debug)]
pub struct ResourceTrackerV1 {
    pub count: u32,
    pub generation: u32,
    pub slots: [ResourceV1; RESOURCE_MAX as usize],
}

impl ResourceTrackerV1 {
    /// Mirrors `canopus_tracker_init`.
    pub fn init(&mut self) {
        self.count = 0;
        self.generation = 1;
        // We deliberately do not zero every slot here: `init` + `add` always
        // initialises a slot before it is read, and zeroing 32 resources would
        // cost a lot of flash on the device. Slots become meaningful only
        // through `add`.
    }

    pub fn generation(&self) -> u32 {
        self.generation
    }

    fn find(&self, handle: *const core::ffi::c_void) -> Option<usize> {
        self.slots[..self.count as usize]
            .iter()
            .position(|r| r.handle == handle as *mut core::ffi::c_void)
    }

    /// Mirrors `canopus_tracker_add`. Returns `false` on full table or a
    /// duplicate handle.
    pub fn add(&mut self, res: &ResourceV1) -> bool {
        if self.count >= RESOURCE_MAX {
            return false;
        }
        if res.state == RES_RELEASED {
            return false;
        }
        if self.find(res.handle).is_some() {
            return false;
        }
        let mut slot = *res;
        slot.generation = 1;
        self.slots[self.count as usize] = slot;
        self.count += 1;
        self.generation = self.generation.wrapping_add(1);
        true
    }

    /// Marks ACTIVE -> DRAINING.
    pub fn drain(&mut self, handle: *const core::ffi::c_void) -> bool {
        self.transition(handle, RES_DRAINING, &[RES_ACTIVE])
    }

    /// Marks the namespace unlinked; must not be retried.
    pub fn detach(&mut self, handle: *const core::ffi::c_void) -> bool {
        self.transition(handle, RES_DETACHED, &[RES_ACTIVE, RES_DRAINING])
    }

    /// Runs `on_release` and marks RELEASED. Double release fails.
    pub fn release(&mut self, handle: *const core::ffi::c_void) -> bool {
        let Some(i) = self.find(handle) else {
            return false;
        };
        let res = &mut self.slots[i];
        match res.state {
            RES_RELEASED => return false,
            RES_RETAINED_UNTIL_REBOOT => return false,
            // namespace already gone; must not release or retry (mirrors C)
            RES_DETACHED => return false,
            _ => {}
        }
        if let Some(cb) = res.on_release {
            let p = res as *mut ResourceV1;
            cb(p);
        }
        res.state = RES_RELEASED;
        res.generation = res.generation.wrapping_add(1);
        self.generation = self.generation.wrapping_add(1);
        true
    }

    /// Marks RETAINED_UNTIL_REBOOT (not releasable this boot).
    pub fn retain_until_reboot(&mut self, handle: *const core::ffi::c_void) -> bool {
        self.transition(handle, RES_RETAINED_UNTIL_REBOOT, &[RES_ACTIVE, RES_DRAINING])
    }

    /// Releases everything still releasable (rollback on init failure).
    pub fn release_all(&mut self) {
        for i in 0..self.count as usize {
            let res = &mut self.slots[i];
            match res.state {
                RES_RELEASED | RES_RETAINED_UNTIL_REBOOT => continue,
                _ => {}
            }
            if let Some(cb) = res.on_release {
                let p = res as *mut ResourceV1;
                cb(p);
            }
            res.state = RES_RELEASED;
            res.generation = res.generation.wrapping_add(1);
            self.generation = self.generation.wrapping_add(1);
        }
    }

    fn transition(
        &mut self,
        handle: *const core::ffi::c_void,
        to: u32,
        allowed_from: &[u32],
    ) -> bool {
        let Some(i) = self.find(handle) else {
            return false;
        };
        let res = &mut self.slots[i];
        if !allowed_from.contains(&res.state) {
            return false;
        }
        res.state = to;
        res.generation = res.generation.wrapping_add(1);
        self.generation = self.generation.wrapping_add(1);
        true
    }
}

// ===========================================================================
// Callback generation guards (architecture §10.5)
// ===========================================================================

/// `#[repr(C)]` mirror of `struct canopus_generation_v1`.
#[repr(C)]
#[derive(Copy, Clone, Debug)]
pub struct GenerationV1 {
    pub value: u32,
}

pub fn generation_init(g: &mut GenerationV1) {
    g.value = 1;
}

pub fn generation_bump(g: &mut GenerationV1) {
    g.value = g.value.wrapping_add(1);
}

pub fn generation_get(g: &GenerationV1) -> u32 {
    g.value
}

/// Returns `true` when the callback's captured generation is still current.
pub fn generation_valid(g: &GenerationV1, captured: u32) -> bool {
    g.value == captured
}

// ===========================================================================
// Event log / diagnostics (architecture §17.3)
// ===========================================================================

pub const EVENT_LOG_ENTRIES: usize = 16;

/// `#[repr(C)]` mirror of `struct canopus_event_v1`.
#[repr(C)]
#[derive(Copy, Clone, Debug, Default)]
pub struct EventV1 {
    pub sequence: u32,
    pub boot_id: u32,
    pub module_gen: u32,
    pub state_before: u32,
    pub state_after: u32,
    pub result: u32,
    pub flags: u32,
}

/// `#[repr(C)]` mirror of `struct canopus_event_log_v1`.
#[repr(C)]
#[derive(Copy, Clone, Debug)]
pub struct EventLogV1 {
    pub head: u32,
    pub next_sequence: u32,
    pub dropped: u32,
    pub entries: [EventV1; EVENT_LOG_ENTRIES],
}

/// Mirrors `canopus_event_log_init`: zeroes the log, then stamps every slot
/// with the boot id. `next_sequence` starts at 0, so the first append returns
/// sequence 0 (matching the C reference exactly).
pub fn event_log_init(log: &mut EventLogV1, boot_id: u32) {
    log.head = 0;
    log.next_sequence = 0;
    log.dropped = 0;
    for e in log.entries.iter_mut() {
        *e = EventV1 {
            sequence: 0,
            boot_id,
            module_gen: 0,
            state_before: 0,
            state_after: 0,
            result: 0,
            flags: 0,
        };
    }
}

/// Appends an event and returns its sequence number. The ring always has room
/// (oldest entries are overwritten); `dropped` is never incremented, matching
/// the C reference. Never blocks.
pub fn event_log_append(
    log: &mut EventLogV1,
    module_gen: u32,
    state_before: u32,
    state_after: u32,
    result: u32,
) -> u32 {
    let seq = log.next_sequence;
    let idx = (log.head as usize) % EVENT_LOG_ENTRIES;
    log.entries[idx] = EventV1 {
        sequence: seq,
        boot_id: log.entries[idx].boot_id,
        module_gen,
        state_before,
        state_after,
        result,
        flags: 0,
    };
    log.next_sequence = log.next_sequence.wrapping_add(1);
    log.head = (log.head + 1) % EVENT_LOG_ENTRIES as u32;
    seq
}

/// Total number of events ever appended (monotonic, unbounded).
pub fn event_log_count(log: &EventLogV1) -> u32 {
    log.next_sequence
}

pub fn event_log_dropped(log: &EventLogV1) -> u32 {
    log.dropped
}

// ===========================================================================
// Buffer helper + descriptor validation
// ===========================================================================

/// Copies `src` into `dst` (NUL-terminated) bounded by `capacity`. Returns the
/// number of bytes copied excluding the NUL, or `None` on truncation.
pub fn buf_copy(dst: &mut [u8], src: &str) -> Option<usize> {
    if dst.is_empty() {
        return None;
    }
    let room = dst.len() - 1;
    let n = src.len().min(room);
    dst[..n].copy_from_slice(&src.as_bytes()[..n]);
    dst[n] = 0;
    if n < src.len() {
        None
    } else {
        Some(n)
    }
}

/// Validates the descriptor header + ABI version. Returns `false` on a null
/// or malformed descriptor.
///
/// # Safety
/// `d` must point to at least `size_of::<ModuleDescriptorV1>()` readable bytes.
pub unsafe fn module_descriptor_check(d: *const ModuleDescriptorV1) -> bool {
    if d.is_null() {
        return false;
    }
    let d = unsafe { &*d };
    if d.struct_size != size_of::<ModuleDescriptorV1>() as u32 {
        return false;
    }
    if d.abi_major != ABI_MAJOR {
        return false;
    }
    if d.abi_minor > ABI_MINOR {
        return false;
    }
    true
}

// ===========================================================================
// Tests
// ===========================================================================

#[cfg(test)]
mod tests {
    extern crate std;
    use super::*;
    use core::ffi::c_void;
    use core::mem::{offset_of, size_of};

    // --- layout parity with the C host probe ---
    #[test]
    fn lifecycle_layout() {
        assert_eq!(size_of::<LifecycleV1>(), 16);
        assert_eq!(offset_of!(LifecycleV1, generation), 8);
    }

    #[cfg(target_pointer_width = "64")]
    #[test]
    fn resource_64bit_layout() {
        assert_eq!(size_of::<ResourceV1>(), 32);
        assert_eq!(offset_of!(ResourceV1, handle), 16);
        assert_eq!(offset_of!(ResourceV1, on_release), 24);
        assert_eq!(size_of::<ResourceTrackerV1>(), 1032);
        assert_eq!(offset_of!(ResourceTrackerV1, slots), 8);
    }

    #[cfg(target_pointer_width = "32")]
    #[test]
    fn resource_32bit_layout() {
        assert_eq!(size_of::<ResourceV1>(), 20);
        assert_eq!(offset_of!(ResourceV1, handle), 12);
        assert_eq!(offset_of!(ResourceV1, on_release), 16);
        assert_eq!(size_of::<ResourceTrackerV1>(), 648);
        assert_eq!(offset_of!(ResourceTrackerV1, slots), 8);
    }

    #[cfg(target_pointer_width = "64")]
    #[test]
    fn event_layout() {
        assert_eq!(size_of::<EventV1>(), 28);
        assert_eq!(size_of::<EventLogV1>(), 460);
    }

    // --- lifecycle ---
    #[test]
    fn removable_full_lifecycle() {
        let mut lc = LifecycleV1 {
            state: 0,
            lifecycle_class: 0,
            generation: 0,
            reserved: 0,
        };
        assert!(lifecycle_init(&mut lc, LIFECYCLE_REMOVABLE));
        assert_eq!(lc.generation, 1);
        assert_eq!(lc.state, STATE_DISCOVERED);

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
            assert!(lifecycle_transition(&mut lc, s), "to {}", state_name(s));
        }
        assert_eq!(lc.generation, 1 + 11);
    }

    #[test]
    fn illegal_transition_rejected() {
        let mut lc = LifecycleV1 {
            state: 0,
            lifecycle_class: 0,
            generation: 0,
            reserved: 0,
        };
        assert!(lifecycle_init(&mut lc, LIFECYCLE_REMOVABLE));
        // discovered -> active is not a direct edge
        assert!(!lifecycle_transition(&mut lc, STATE_ACTIVE));
        // jump over verified
        assert!(!lifecycle_transition(&mut lc, STATE_INSTALLED));
        assert_eq!(lc.generation, 1);
    }

    #[test]
    fn resident_barrier_blocks_unload() {
        let mut lc = LifecycleV1 {
            state: 0,
            lifecycle_class: 0,
            generation: 0,
            reserved: 0,
        };
        assert!(lifecycle_init(&mut lc, LIFECYCLE_ALWAYS_RESIDENT));
        for s in [
            STATE_VERIFIED,
            STATE_INSTALLED,
            STATE_DISABLED,
            STATE_ENABLED,
            STATE_LOADING,
            STATE_PREPARING,
            STATE_READY,
            STATE_ACTIVE,
            STATE_BOOT_RESIDENT,
        ] {
            assert!(lifecycle_transition(&mut lc, s));
        }
        // no unload path for resident
        assert!(!lifecycle_transition(&mut lc, STATE_STOPPING));
        assert!(!lifecycle_transition(&mut lc, STATE_DRAINING));
        assert!(!lifecycle_transition(&mut lc, STATE_UNLOADED));
        // but next-boot disable / update / remove are
        assert!(lifecycle_transition(&mut lc, STATE_DISABLED_NEXT_BOOT));
    }

    #[test]
    fn bad_class_rejected() {
        let mut lc = LifecycleV1 {
            state: 0,
            lifecycle_class: 0,
            generation: 0,
            reserved: 0,
        };
        assert!(!lifecycle_init(&mut lc, 4));
        assert!(!lifecycle_allow(STATE_DISCOVERED, STATE_VERIFIED, 4));
    }

    #[test]
    fn state_names_cover_all() {
        let mut seen = std::collections::HashSet::new();
        for s in 1..=STATE_REMOVE_PENDING {
            let n = state_name(s);
            assert!(!n.is_empty() && n != "unknown");
            seen.insert(n);
        }
        assert_eq!(seen.len(), STATE_REMOVE_PENDING as usize);
        assert_eq!(state_name(999), "unknown");
    }

    // --- snapshot ---
    #[test]
    fn snapshot_roundtrip() {
        let mut s = SnapshotV1 { sequence: 0 };
        assert!(snapshot_ready(&s));
        snapshot_begin(&mut s);
        assert!(!snapshot_ready(&s));
        snapshot_commit(&mut s);
        assert!(snapshot_ready(&s));
        assert_eq!(s.sequence, 2);
    }

    #[test]
    fn snapshot_commit_idempotent() {
        let mut s = SnapshotV1 { sequence: 0 };
        snapshot_commit(&mut s);
        snapshot_commit(&mut s);
        assert_eq!(s.sequence, 0);
        assert!(snapshot_ready(&s));
    }

    // --- status writer ---
    #[test]
    fn status_writer_basic() {
        let mut buf = [0u8; 32];
        let mut w = StatusWriterV1 {
            buf: core::ptr::null_mut(),
            capacity: 0,
            used: 0,
            dropped: 0,
            snap: SnapshotV1 { sequence: 0 },
        };
        unsafe {
            assert!(status_writer_init(&mut w, buf.as_mut_ptr(), 32));
            assert!(status_put_u8(&mut w, 0xAA));
            assert!(status_put_u16(&mut w, 0xBBCC));
            assert!(status_put_u32(&mut w, 0xDDEE_FF01));
            status_writer_publish(&mut w);
        }
        assert_eq!(w.used, 7);
        assert_eq!(w.dropped, 0);
        assert!(snapshot_ready(&w.snap));
        assert_eq!(buf[0], 0xAA);
        assert_eq!(u16::from_le_bytes([buf[1], buf[2]]), 0xBBCC);
        assert_eq!(u32::from_le_bytes([buf[3], buf[4], buf[5], buf[6]]), 0xDDEE_FF01);
    }

    #[test]
    fn status_writer_overflow_counts_dropped() {
        let mut buf = [0u8; 4];
        let mut w = StatusWriterV1 {
            buf: core::ptr::null_mut(),
            capacity: 0,
            used: 0,
            dropped: 0,
            snap: SnapshotV1 { sequence: 0 },
        };
        unsafe {
            assert!(status_writer_init(&mut w, buf.as_mut_ptr(), 4));
            assert!(status_put_u16(&mut w, 1));
            assert!(status_put_u16(&mut w, 2));
            assert!(!status_put_u16(&mut w, 3)); // no room
            assert!(!status_put_u8(&mut w, 9));
        }
        assert_eq!(w.used, 4);
        assert_eq!(w.dropped, 2);
    }

    #[test]
    fn status_writer_rejects_bad_capacity() {
        let mut w = StatusWriterV1 {
            buf: core::ptr::null_mut(),
            capacity: 0,
            used: 0,
            dropped: 0,
            snap: SnapshotV1 { sequence: 0 },
        };
        unsafe {
            assert!(!status_writer_init(&mut w, core::ptr::null_mut(), 0));
            assert!(!status_writer_init(&mut w, core::ptr::null_mut(), STATUS_RECORD_MAX + 1));
        }
    }

    // --- resource tracker ---
    fn track() -> (ResourceTrackerV1, ResourceV1) {
        let mut t = ResourceTrackerV1 {
            count: 0,
            generation: 1,
            slots: [ResourceV1 {
                kind: 0,
                state: 0,
                generation: 0,
                handle: core::ptr::null_mut(),
                on_release: None,
            }; RESOURCE_MAX as usize],
        };
        t.init();
        let res = ResourceV1 {
            kind: RES_HEAP,
            state: RES_ACTIVE,
            generation: 0,
            handle: 0x10 as *mut c_void,
            on_release: None,
        };
        (t, res)
    }

    #[test]
    fn tracker_lifecycle() {
        let (mut t, res) = track();
        assert!(t.add(&res));
        assert!(!t.add(&res)); // duplicate handle
        assert!(t.drain(res.handle));
        assert!(t.release(res.handle));
        assert!(!t.release(res.handle)); // double free
        assert_eq!(t.count, 1); // slot remains for audit, state RELEASED
    }

    #[test]
    fn tracker_detach_prevents_retry() {
        let (mut t, res) = track();
        assert!(t.add(&res));
        assert!(t.detach(res.handle));
        assert!(!t.drain(res.handle)); // detached must not be retried
        assert!(!t.release(res.handle)); // detached: namespace gone, no release
    }

    #[test]
    fn tracker_retain_until_reboot_blocks_release() {
        let (mut t, res) = track();
        assert!(t.add(&res));
        assert!(t.retain_until_reboot(res.handle));
        assert!(!t.release(res.handle));
    }

    #[test]
    fn tracker_release_all_runs_callbacks() {
        static RELEASED: core::sync::atomic::AtomicU32 = core::sync::atomic::AtomicU32::new(0);
        extern "C" fn on_rel(_r: *mut ResourceV1) {
            RELEASED.fetch_add(1, core::sync::atomic::Ordering::SeqCst);
        }
        let (mut t, mut res) = track();
        res.on_release = Some(on_rel);
        assert!(t.add(&res));
        t.release_all();
        assert_eq!(RELEASED.load(core::sync::atomic::Ordering::SeqCst), 1);
    }

    #[test]
    fn tracker_full_table() {
        let mut t = ResourceTrackerV1 {
            count: 0,
            generation: 1,
            slots: [ResourceV1 {
                kind: 0,
                state: 0,
                generation: 0,
                handle: core::ptr::null_mut(),
                on_release: None,
            }; RESOURCE_MAX as usize],
        };
        t.init();
        let mut added = 0;
        for i in 1..=RESOURCE_MAX + 2 {
            let res = ResourceV1 {
                kind: RES_FD,
                state: RES_ACTIVE,
                generation: 0,
                handle: i as *mut c_void,
                on_release: None,
            };
            if t.add(&res) {
                added += 1;
            }
        }
        assert_eq!(added, RESOURCE_MAX);
        assert_eq!(t.count, RESOURCE_MAX);
    }

    // --- generation guards ---
    #[test]
    fn generation_stale_detection() {
        let mut g = GenerationV1 { value: 0 };
        generation_init(&mut g);
        let captured = generation_get(&g);
        assert!(generation_valid(&g, captured));
        generation_bump(&mut g);
        assert!(!generation_valid(&g, captured));
        assert!(generation_valid(&g, generation_get(&g)));
    }

    // --- event log ---
    #[test]
    fn event_log_append_monotonic() {
        let mut log = EventLogV1 {
            head: 0,
            next_sequence: 0,
            dropped: 0,
            entries: [EventV1::default(); EVENT_LOG_ENTRIES],
        };
        event_log_init(&mut log, 0xBE);
        assert_eq!(event_log_count(&log), 0);
        let s1 = event_log_append(&mut log, 1, 2, 3, 0);
        let s2 = event_log_append(&mut log, 1, 3, 4, 0);
        assert!(s1 < s2);
        assert_eq!(event_log_count(&log), 2);
        assert_eq!(event_log_dropped(&log), 0);
    }

    #[test]
    fn event_log_wraps_and_keeps_sequence() {
        let mut log = EventLogV1 {
            head: 0,
            next_sequence: 0,
            dropped: 0,
            entries: [EventV1::default(); EVENT_LOG_ENTRIES],
        };
        event_log_init(&mut log, 7);
        let mut last = 0;
        for _ in 0..EVENT_LOG_ENTRIES + 5 {
            last = event_log_append(&mut log, 0, 0, 0, 0);
        }
        // count is the total ever appended; dropped stays 0 (ring always
        // has room, oldest entries are overwritten) — mirrors the C.
        assert_eq!(event_log_count(&log), (EVENT_LOG_ENTRIES + 5) as u32);
        assert_eq!(event_log_dropped(&log), 0);
        assert_eq!(last, (EVENT_LOG_ENTRIES + 4) as u32);
        // the ring really wrapped: the second pass overwrote entries 0..4
        let n = EVENT_LOG_ENTRIES as u32;
        assert_eq!(log.entries[0].sequence, n); // append #n
        assert_eq!(log.entries[4].sequence, n + 4); // append #n+4 (latest)
        // first-pass entries beyond the wrap are untouched
        assert_eq!(log.entries[n as usize - 1].sequence, n - 1);
        // every slot carries the boot id
        assert!(log.entries.iter().all(|e| e.boot_id == 7));
    }

    // --- buf_copy ---
    #[test]
    fn buf_copy_fits_and_truncates() {
        let mut dst = [0u8; 8];
        assert_eq!(buf_copy(&mut dst, "hi").unwrap(), 2);
        assert_eq!(&dst[..3], b"hi\0");
        assert_eq!(buf_copy(&mut dst, "0123456789"), None);
        assert_eq!(&dst[..], b"0123456\0");
    }

    #[test]
    fn buf_copy_zero_capacity() {
        let mut dst: [u8; 0] = [];
        assert_eq!(buf_copy(&mut dst, "x"), None);
    }

    // --- descriptor check ---
    #[test]
    fn descriptor_check_ok_and_reject() {
        let mut d = ModuleDescriptorV1 {
            struct_size: size_of::<ModuleDescriptorV1>() as u32,
            abi_major: ABI_MAJOR,
            abi_minor: ABI_MINOR,
            flags: 0,
            module_id: [0; 32],
            module_version: [0; 16],
            build_id: [0; 32],
            target_id: [0; 32],
            prepare: None,
            activate: None,
            deactivate: None,
            stop: None,
            query: None,
        };
        unsafe {
            assert!(module_descriptor_check(&d));
            d.struct_size = 0;
            assert!(!module_descriptor_check(&d));
            d.struct_size = size_of::<ModuleDescriptorV1>() as u32;
            d.abi_major = ABI_MAJOR + 1;
            assert!(!module_descriptor_check(&d));
            assert!(!module_descriptor_check(core::ptr::null()));
        }
    }
}
