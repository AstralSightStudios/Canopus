//! canopus-abi — Rust port of the public Canopus module ABI v1.
//!
//! The layout here MUST match `sdk/c/canopus_abi.h` field-for-field:
//! fixed-width fields, natural alignment, no `#[repr(packed)]`. Every struct
//! carries `#[repr(C)]` and the crate's `layout` test module asserts the exact
//! sizes/offsets that the C header's `CANOPUS_STATIC_ASSERT` guarantees, on
//! both 32-bit (device) and 64-bit (host) pointer widths.
//!
//! Only stable Canopus types live here. Firmware-private structs and calling
//! constraints belong to the target pack and generated code (architecture §3.5).

#![no_std]
#![deny(unsafe_op_in_unsafe_fn)]

use core::ffi::{c_char, c_void};

// ---------------------------------------------------------------------------
// Version
// ---------------------------------------------------------------------------

pub const ABI_MAJOR: u16 = 1;
pub const ABI_MINOR: u16 = 2;

// ---------------------------------------------------------------------------
// Module descriptor flags
// ---------------------------------------------------------------------------

pub const FLAG_HAS_NATIVE_APP: u32 = 1 << 0;
pub const FLAG_NATIVE_APP_INTEGRATED: u32 = 1 << 1;
pub const FLAG_NATIVE_APP_STANDALONE: u32 = 1 << 2;
pub const FLAG_REGISTERS_LAUNCHER_ENTRY: u32 = 1 << 3;
pub const FLAG_REQUIRES_UI_DISPATCHER: u32 = 1 << 4;
pub const FLAG_APP_UNREGISTER_REBOOT_REQUIRED: u32 = 1 << 5;

// ---------------------------------------------------------------------------
// Lifecycle classes
// ---------------------------------------------------------------------------

pub const LIFECYCLE_REMOVABLE: u32 = 0;
pub const LIFECYCLE_RESIDENT_AFTER_ACTIVATION: u32 = 1;
pub const LIFECYCLE_ALWAYS_RESIDENT: u32 = 2;
pub const LIFECYCLE_PATCH_REBOOT_REQUIRED: u32 = 3;

// ---------------------------------------------------------------------------
// Control plane result states
// ---------------------------------------------------------------------------

pub const RESULT_REJECTED: u32 = 1;
pub const RESULT_ACCEPTED: u32 = 2;
pub const RESULT_QUEUED: u32 = 3;
pub const RESULT_RUNNING: u32 = 4;
pub const RESULT_COMPLETED: u32 = 5;
pub const RESULT_FAILED: u32 = 6;
pub const RESULT_DISALLOWED: u32 = 7;
pub const RESULT_REBOOT_REQUIRED: u32 = 8;

// ---------------------------------------------------------------------------
// Status record
// ---------------------------------------------------------------------------

/// Max payload bytes per status record; readers must accept `<=` this.
pub const STATUS_RECORD_MAX: u32 = 128;

pub const CAP_MAGIC: u32 = 0x4341_5031; // "CAP1"

// ---------------------------------------------------------------------------
// Opaque context
// ---------------------------------------------------------------------------

/// Opaque firmware-side context passed to every module callback. Never
/// dereferenced by the module; only ever used behind a pointer.
pub enum ContextV1 {}

// ---------------------------------------------------------------------------
// Snapshot (sequence protocol, architecture §16)
// ---------------------------------------------------------------------------

/// Sequence snapshot: `sequence` is odd while writing, even when valid.
/// Access must go through the volatile helpers in `canopus-runtime`.
#[repr(C)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct SnapshotV1 {
    pub sequence: u32,
}

// ---------------------------------------------------------------------------
// Status writer
// ---------------------------------------------------------------------------

#[repr(C)]
#[derive(Copy, Clone, Debug)]
pub struct StatusWriterV1 {
    pub buf: *mut u8,
    pub capacity: u32,
    pub used: u32,
    pub dropped: u32,
    pub snap: SnapshotV1,
}

// ---------------------------------------------------------------------------
// Module descriptor
// ---------------------------------------------------------------------------

pub type CbPrepare = extern "C" fn(ctx: *const ContextV1) -> i32;
pub type CbActivate = extern "C" fn(ctx: *const ContextV1) -> i32;
pub type CbDeactivate = extern "C" fn(ctx: *const ContextV1) -> i32;
pub type CbStop = extern "C" fn(ctx: *const ContextV1) -> i32;
pub type CbQuery = extern "C" fn(writer: *mut StatusWriterV1) -> i32;
pub type CbPublishNativeApp = extern "C" fn(ctx: *const ContextV1) -> i32;
pub type CbPublishNativeAppStage = extern "C" fn(ctx: *const ContextV1, stage: u32) -> i32;

/// Public module descriptor (architecture §10.1).
///
/// The fixed-width prefix (`struct_size` .. `target_id`) is 124 bytes and is
/// pointer-independent; the five callbacks follow at pointer-aligned offsets.
#[repr(C)]
#[derive(Copy, Clone, Debug)]
pub struct ModuleDescriptorV1 {
    pub struct_size: u32,
    pub abi_major: u16,
    pub abi_minor: u16,
    pub flags: u32,
    pub module_id: [u8; 32],
    pub module_version: [u8; 16],
    pub build_id: [u8; 32],
    pub target_id: [u8; 32],
    pub prepare: Option<CbPrepare>,
    pub activate: Option<CbActivate>,
    pub deactivate: Option<CbDeactivate>,
    pub stop: Option<CbStop>,
    pub query: Option<CbQuery>,
    /// ABI 1.1 append-only callback. Called only from a UI-process bootstrap.
    pub publish_native_app: Option<CbPublishNativeApp>,
    /// ABI 1.2 append-only callback. Stage 1 registers app/pages; stage 2 adds
    /// the Launcher entry in a later UI event turn.
    pub publish_native_app_stage: Option<CbPublishNativeAppStage>,
}

// ---------------------------------------------------------------------------
// Control header
// ---------------------------------------------------------------------------

#[repr(C)]
#[derive(Copy, Clone, Debug)]
pub struct ControlHeaderV1 {
    pub struct_size: u32,
    pub abi_major: u16,
    pub abi_minor: u16,
    pub command: u32,
    pub request_id: u32,
    pub payload_size: u32,
    pub flags: u32,
}

// ---------------------------------------------------------------------------
// Capability query
// ---------------------------------------------------------------------------

pub type CbHasCapability = extern "C" fn(q: *const CapabilityQueryV1, name: *const c_char) -> i32;

#[repr(C)]
#[derive(Copy, Clone, Debug)]
pub struct CapabilityQueryV1 {
    pub magic: u32,
    pub struct_size: u32,
    pub abi_major: u16,
    pub abi_minor: u16,
    pub has: Option<CbHasCapability>,
    pub private_data: *mut c_void,
}

// ---------------------------------------------------------------------------
// Tests: C/Rust layout parity
// ---------------------------------------------------------------------------
//
// The numbers below are the values `sdk/c/canopus_abi.h` and
// `sdk/c/canopus_runtime.h` assert at compile time (32-bit) and that a host
// probe compiled with the same headers reports (64-bit). They are pinned here
// so a Rust layout drift fails loudly even when no C compiler is available in
// CI for the parity probe.

#[cfg(test)]
mod layout {
    use super::*;
    use core::mem::{align_of, offset_of, size_of};

    // --- descriptor (the C header pins both widths) ---
    #[test]
    fn descriptor_fixed_prefix_offsets() {
        assert_eq!(offset_of!(ModuleDescriptorV1, module_id), 12);
        assert_eq!(offset_of!(ModuleDescriptorV1, target_id), 92);
    }

    #[cfg(target_pointer_width = "64")]
    #[test]
    fn descriptor_64bit_layout() {
        assert_eq!(offset_of!(ModuleDescriptorV1, prepare), 128);
        assert_eq!(offset_of!(ModuleDescriptorV1, query), 160);
        assert_eq!(offset_of!(ModuleDescriptorV1, publish_native_app), 168);
        assert_eq!(
            offset_of!(ModuleDescriptorV1, publish_native_app_stage),
            176
        );
        assert_eq!(size_of::<ModuleDescriptorV1>(), 184);
        assert_eq!(align_of::<ModuleDescriptorV1>(), 8);
    }

    #[cfg(target_pointer_width = "32")]
    #[test]
    fn descriptor_32bit_layout() {
        assert_eq!(offset_of!(ModuleDescriptorV1, prepare), 124);
        assert_eq!(offset_of!(ModuleDescriptorV1, query), 140);
        assert_eq!(offset_of!(ModuleDescriptorV1, publish_native_app), 144);
        assert_eq!(
            offset_of!(ModuleDescriptorV1, publish_native_app_stage),
            148
        );
        assert_eq!(size_of::<ModuleDescriptorV1>(), 152);
        assert_eq!(align_of::<ModuleDescriptorV1>(), 4);
    }

    // --- control header (C: 24 bytes) ---
    #[test]
    fn control_header_layout() {
        assert_eq!(size_of::<ControlHeaderV1>(), 24);
        assert_eq!(offset_of!(ControlHeaderV1, payload_size), 16);
        assert_eq!(offset_of!(ControlHeaderV1, flags), 20);
    }

    // --- snapshot (C: 4 bytes) ---
    #[test]
    fn snapshot_layout() {
        assert_eq!(size_of::<SnapshotV1>(), 4);
        assert_eq!(size_of::<u32>(), 4);
    }

    // --- status writer ---
    #[cfg(target_pointer_width = "64")]
    #[test]
    fn status_writer_64bit_layout() {
        assert_eq!(size_of::<StatusWriterV1>(), 24);
        assert_eq!(offset_of!(StatusWriterV1, snap), 20);
    }

    #[cfg(target_pointer_width = "32")]
    #[test]
    fn status_writer_32bit_layout() {
        assert_eq!(size_of::<StatusWriterV1>(), 20);
        assert_eq!(offset_of!(StatusWriterV1, snap), 16);
    }

    // --- capability query ---
    #[cfg(target_pointer_width = "64")]
    #[test]
    fn capability_query_64bit_layout() {
        assert_eq!(size_of::<CapabilityQueryV1>(), 32);
        assert_eq!(offset_of!(CapabilityQueryV1, has), 16);
    }

    #[cfg(target_pointer_width = "32")]
    #[test]
    fn capability_query_32bit_layout() {
        assert_eq!(size_of::<CapabilityQueryV1>(), 20);
        assert_eq!(offset_of!(CapabilityQueryV1, has), 12);
    }

    // --- function pointer option is a null-pointer-sentinel (same size) ---
    #[test]
    fn fn_ptr_option_is_niche() {
        assert_eq!(
            size_of::<Option<CbPrepare>>(),
            size_of::<*const ContextV1>()
        );
        assert_eq!(size_of::<Option<CbHasCapability>>(), size_of::<usize>());
    }

    #[test]
    fn abi_version_constants() {
        assert_eq!(ABI_MAJOR, 1);
        assert_eq!(ABI_MINOR, 2);
    }
}
