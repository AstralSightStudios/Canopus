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
// Service Objects & Context v2
// ---------------------------------------------------------------------------

pub const FS_READ: u32 = 0x0001;
pub const FS_WRITE: u32 = 0x0002;
pub const FS_APPEND: u32 = 0x0004;
pub const FS_CREATE: u32 = 0x0008;
pub const FS_TRUNC: u32 = 0x0010;
pub const FS_RDWR: u32 = FS_READ | FS_WRITE;

pub const FS_SEEK_SET: i32 = 0;
pub const FS_SEEK_CUR: i32 = 1;
pub const FS_SEEK_END: i32 = 2;

pub const MEM_BUDGET: u32 = 131072; // 128 KiB
pub const MEM_MAX_BLOCKS: u32 = 64;

#[repr(C)]
#[derive(Copy, Clone, Debug, Default, PartialEq, Eq)]
pub struct CanopusFsStat {
    pub size: u32,
    pub is_dir: u8,
    pub _pad: [u8; 3],
    pub mtime: u32,
}

#[repr(C)]
#[derive(Copy, Clone, Debug)]
pub struct CanopusFsDirent {
    pub name: [u8; 64],
    pub is_dir: u8,
    pub _pad: [u8; 3],
}

#[repr(u32)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum HookType {
    Auto = 0,
    Fpb = 1,
    Inline = 2,
    Pointer = 3,
}

#[repr(C)]
pub struct CanopusFsObj {
    pub open: unsafe extern "C" fn(ctx: *const ContextV1, path: *const c_char, flags: u32) -> i32,
    pub read: unsafe extern "C" fn(ctx: *const ContextV1, fd: i32, buf: *mut c_void, size: u32) -> i32,
    pub write: unsafe extern "C" fn(ctx: *const ContextV1, fd: i32, buf: *const c_void, size: u32) -> i32,
    pub seek: unsafe extern "C" fn(ctx: *const ContextV1, fd: i32, offset: i64, whence: i32) -> i64,
    pub tell: unsafe extern "C" fn(ctx: *const ContextV1, fd: i32) -> i64,
    pub size: unsafe extern "C" fn(ctx: *const ContextV1, fd: i32) -> i64,
    pub sync: unsafe extern "C" fn(ctx: *const ContextV1, fd: i32) -> i32,
    pub close: unsafe extern "C" fn(ctx: *const ContextV1, fd: i32) -> i32,
    pub remove: unsafe extern "C" fn(ctx: *const ContextV1, path: *const c_char) -> i32,
    pub rename: unsafe extern "C" fn(ctx: *const ContextV1, old_path: *const c_char, new_path: *const c_char) -> i32,
    pub stat: unsafe extern "C" fn(ctx: *const ContextV1, path: *const c_char, out_stat: *mut CanopusFsStat) -> i32,
    pub exists: unsafe extern "C" fn(ctx: *const ContextV1, path: *const c_char) -> i32,
    pub mkdir: unsafe extern "C" fn(ctx: *const ContextV1, path: *const c_char) -> i32,
    pub opendir: unsafe extern "C" fn(ctx: *const ContextV1, path: *const c_char) -> *mut c_void,
    pub readdir: unsafe extern "C" fn(ctx: *const ContextV1, dir: *mut c_void, out_entry: *mut CanopusFsDirent) -> i32,
    pub closedir: unsafe extern "C" fn(ctx: *const ContextV1, dir: *mut c_void) -> i32,
}

#[repr(C)]
pub struct CanopusMemObj {
    pub alloc: unsafe extern "C" fn(ctx: *const ContextV1, size: u32) -> *mut c_void,
    pub realloc: unsafe extern "C" fn(ctx: *const ContextV1, ptr: *mut c_void, new_size: u32) -> *mut c_void,
    pub free: unsafe extern "C" fn(ctx: *const ContextV1, ptr: *mut c_void),
    pub available: unsafe extern "C" fn(ctx: *const ContextV1) -> u32,
    pub used: unsafe extern "C" fn(ctx: *const ContextV1) -> u32,
}

#[repr(C)]
pub struct CanopusTimeObj {
    pub monotonic_ms: unsafe extern "C" fn(ctx: *const ContextV1) -> u32,
    pub realtime_ms: unsafe extern "C" fn(ctx: *const ContextV1) -> u64,
    pub sleep_ms: unsafe extern "C" fn(ctx: *const ContextV1, ms: u32),
}

#[repr(C)]
pub struct CanopusHookObj {
    pub install: unsafe extern "C" fn(ctx: *const ContextV1, target_fn: usize, hook_fn: usize, orig_fn: *mut usize, hook_type: u32) -> i32,
    pub uninstall: unsafe extern "C" fn(ctx: *const ContextV1, hook_id: i32) -> i32,
}

#[repr(C)]
pub struct CanopusLogObj {
    pub write: unsafe extern "C" fn(ctx: *const ContextV1, level: u32, message: *const c_char),
    pub info: unsafe extern "C" fn(ctx: *const ContextV1, message: *const c_char),
    pub warn: unsafe extern "C" fn(ctx: *const ContextV1, message: *const c_char),
    pub error: unsafe extern "C" fn(ctx: *const ContextV1, message: *const c_char),
    pub debug: unsafe extern "C" fn(ctx: *const ContextV1, message: *const c_char),
}

#[repr(C)]
pub struct CanopusNotifyObj {
    pub post: unsafe extern "C" fn(ctx: *const ContextV1, title: *const c_char, body: *const c_char) -> i32,
}

#[repr(C)]
pub struct CanopusSystemObj {
    pub target_id: unsafe extern "C" fn(ctx: *const ContextV1) -> *const c_char,
    pub framework_revision: unsafe extern "C" fn(ctx: *const ContextV1) -> u32,
    pub is_safe_mode: unsafe extern "C" fn(ctx: *const ContextV1) -> i32,
    pub reboot: unsafe extern "C" fn(ctx: *const ContextV1) -> i32,
}

#[repr(C)]
pub struct CanopusBluetoothObj {
    pub l2_send: unsafe extern "C" fn(ctx: *const ContextV1, channel: u8, payload: *const c_void, len: u16) -> i32,
    pub adapter_state: unsafe extern "C" fn(ctx: *const ContextV1) -> u32,
}

#[repr(C)]
pub struct CanopusContext {
    pub struct_size: u32,
    pub abi: u32,
    pub services: *const c_void,
    pub slot: u32,
    pub target_id: *const c_char,
    pub module_id: *const c_char,
    pub fs: *const CanopusFsObj,
    pub mem: *const CanopusMemObj,
    pub time: *const CanopusTimeObj,
    pub hook: *const CanopusHookObj,
    pub log: *const CanopusLogObj,
    pub notify: *const CanopusNotifyObj,
    pub system: *const CanopusSystemObj,
    pub bt: *const CanopusBluetoothObj,
}

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
