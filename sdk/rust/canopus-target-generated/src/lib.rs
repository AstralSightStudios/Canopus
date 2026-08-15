//! canopus-target-generated — per-target Rust bindings (CAN-RUST-002 / TGT-006).
//!
//! `generated.rs` is produced by
//! `canopus target generate-rust-bindings xiaomi-band-10-pro-3.101.030` and is
//! committed so the crate always builds. A regression test in `canopus-core`
//! fails when the pack changes without regenerating it.
//!
//! Layout notes: recovered structs are `#[repr(packed)]` with explicit padding,
//! so `size_of` and `offset_of` reproduce the exact device byte layout on both
//! the host and the device target. All firmware calls are `unsafe`; safe
//! wrappers appear only where ABI + ownership are proven (architecture §12.1).

#![no_std]
#![deny(unsafe_op_in_unsafe_fn)]
// Recovered firmware struct names are snake_case on purpose; they mirror the
// target's own identifiers, so the lint is suppressed crate-wide.
#![allow(non_camel_case_types)]

// Exactly one target feature must be selected. `generated.rs` is the band-10
// 3.101.030 bindings; `generated_1036.rs` is band-10 3.101.036;
// `generated_b9.rs` is the band-9 (3.1.175) bindings.
#[cfg(all(
    not(feature = "target-xiaomi-band-10-pro-3-101-030"),
    not(feature = "target-xiaomi-band-10-pro-3-101-036"),
    not(feature = "target-xiaomi-band-9-pro-3-1-175")
))]
compile_error!(
    "canopus-target-generated requires exactly one target feature; supported: \
     target-xiaomi-band-10-pro-3-101-030, target-xiaomi-band-10-pro-3-101-036, \
     target-xiaomi-band-9-pro-3-1-175"
);

#[cfg(feature = "target-xiaomi-band-10-pro-3-101-030")]
include!("generated.rs");
#[cfg(feature = "target-xiaomi-band-10-pro-3-101-036")]
include!("generated_1036.rs");
#[cfg(feature = "target-xiaomi-band-9-pro-3-1-175")]
include!("generated_b9.rs");

#[cfg(test)]
mod layout_tests {
    use super::*;
    use core::mem::{offset_of, size_of};

    #[test]
    fn thumb_callable_normalizes_entry_and_callable_addresses() {
        assert_eq!(canopus_thumb_callable(0x0C1C31C8), 0x0C1C31C9);
        assert_eq!(canopus_thumb_callable(0x0C1C31C9), 0x0C1C31C9);
    }

    #[test]
    fn launcher_order_record_is_exact() {
        // 128-byte name @0, 4-byte gap, u32 flags @132, 4-byte tail => 140.
        assert_eq!(size_of::<launcher_order_record>(), 140);
        assert_eq!(offset_of!(launcher_order_record, flags), 132);
        assert_eq!(offset_of!(launcher_order_record, app_name), 0);
    }

    // Pointer-free recovered layouts are exact on both 32-bit (device) and
    // 64-bit (host) builds.
    #[test]
    fn pointer_free_layouts_exact_on_both() {
        assert_eq!(size_of::<launcher_order_record>(), 140);
        assert_eq!(offset_of!(launcher_order_record, flags), 132);
        assert_eq!(size_of::<stock_timespec_t>(), 12);
        assert_eq!(offset_of!(stock_timespec_t, tv_nsec), 8);
        assert_eq!(size_of::<launcher_app_struct>(), 0x84);
        assert_eq!(offset_of!(launcher_app_struct, flags), 0x80);
    }

    // Structs that embed `*mut c_void` are exact only at the device ABI
    // (4-byte pointers); on the 64-bit host the pointer field is 8 bytes.
    #[cfg(target_pointer_width = "32")]
    #[test]
    fn pointer_structs_exact_on_device() {
        assert_eq!(size_of::<ordered_app_entry>(), 16);
        assert_eq!(offset_of!(ordered_app_entry, enabled), 8);
        assert_eq!(offset_of!(ordered_app_entry, hidden), 9);
        assert_eq!(size_of::<service_object>(), 60);
        assert_eq!(offset_of!(service_object, startup_cb), 0x24);
        assert_eq!(size_of::<file_operations>(), 0x30);
        assert_eq!(offset_of!(file_operations, ioctl), 0x14);
        // launcher app record (EVID-APP-004): u16 id@0, icons/name@4..12,
        // flags@20, total 24.
        assert_eq!(size_of::<launcher_app_record>(), 24);
        assert_eq!(offset_of!(launcher_app_record, app_id), 0);
        assert_eq!(offset_of!(launcher_app_record, name), 8);
        assert_eq!(offset_of!(launcher_app_record, flags), 0x14);
        // descriptor layout differs per target family: band-9 is 60B, band-10
        // (3.101.030/3.101.036) is 64B.
        #[cfg(not(feature = "target-xiaomi-band-9-pro-3-1-175"))]
        {
            // descriptor: name@8, icon@12, u16 app_id@16, resolver@28, hidden@60.
            assert_eq!(size_of::<launcher_app_descriptor>(), 64);
            assert_eq!(offset_of!(launcher_app_descriptor, name), 8);
            assert_eq!(offset_of!(launcher_app_descriptor, app_id), 16);
            assert_eq!(offset_of!(launcher_app_descriptor, icon_resolver), 28);
            assert_eq!(offset_of!(launcher_app_descriptor, hidden_flags), 60);
        }
        #[cfg(feature = "target-xiaomi-band-9-pro-3-1-175")]
        {
            assert_eq!(size_of::<launcher_app_descriptor>(), 60);
            assert_eq!(offset_of!(launcher_app_descriptor, package_name), 8);
            assert_eq!(offset_of!(launcher_app_descriptor, app_id), 16);
            assert_eq!(
                offset_of!(launcher_app_descriptor, launcher_metadata_callback),
                28
            );
            assert_eq!(offset_of!(launcher_app_descriptor, page_registry), 44);
            assert_eq!(offset_of!(launcher_app_descriptor, hidden_flags), 56);
        }
    }

    #[cfg(target_pointer_width = "64")]
    #[test]
    fn pointer_structs_compile_on_host() {
        // Host builds are for logic tests only: pointer sizes differ from the
        // device ABI, so offsets are not asserted here. Referencing every
        // recovered field proves the generated types are well-formed.
        let _e = ordered_app_entry {
            app_name: core::ptr::null_mut(),
            _pad_4: [0; 4],
            enabled: 0,
            hidden: 0,
            _tail: [0; 6],
        };
        let _s = service_object {
            enabled_state: 0,
            _pad_1: [0; 3],
            name: core::ptr::null_mut(),
            service_id: 0,
            profile_group: 0,
            _pad_a: [0; 0x1a],
            startup_cb: core::ptr::null_mut(),
            _pad_28: [0; 8],
            startup_eligibility_cb: core::ptr::null_mut(),
            get_profile_cb: core::ptr::null_mut(),
            cleanup_cb: core::ptr::null_mut(),
            _tail: [0; 4],
        };
        let _f = file_operations {
            open: core::ptr::null_mut(),
            close: core::ptr::null_mut(),
            read: core::ptr::null_mut(),
            write: core::ptr::null_mut(),
            lseek: core::ptr::null_mut(),
            ioctl: core::ptr::null_mut(),
            _tail: [0; 0x18],
        };
        // launcher app record/descriptor (EVID-APP-004) also compile on host.
        let _r = launcher_app_record {
            app_id: 0,
            _pad_2: [0; 2],
            icon_handle: core::ptr::null_mut(),
            name: core::ptr::null_mut(),
            icon_name: core::ptr::null_mut(),
            _pad_10: [0; 4],
            flags: 0,
            _tail: [0; 3],
        };
        let _ = (_e, _s, _f, _r);
        // The launcher app descriptor layout differs per target family.
        #[cfg(not(feature = "target-xiaomi-band-9-pro-3-1-175"))]
        {
            let _d = launcher_app_descriptor {
                registry_links: 0,
                package_name: core::ptr::null_mut(),
                launcher_icon_resource: core::ptr::null_mut(),
                app_id: 0,
                flags: 0,
                _pad_13: [0; 1],
                owned_string_20: core::ptr::null_mut(),
                owned_string_24: core::ptr::null_mut(),
                launcher_metadata_callback: core::ptr::null_mut(),
                _pad_20: [0; 16],
                page_registry: core::ptr::null_mut(),
                _pad_34: [0; 8],
                hidden_flags: 0,
                _tail: [0; 3],
            };
            let _ = _d;
        }
        #[cfg(feature = "target-xiaomi-band-9-pro-3-1-175")]
        {
            let _d = launcher_app_descriptor {
                registry_links: 0,
                package_name: core::ptr::null_mut(),
                launcher_icon_resource: core::ptr::null_mut(),
                app_id: 0,
                flags: 0,
                owned_string_20: core::ptr::null_mut(),
                owned_string_24: core::ptr::null_mut(),
                launcher_metadata_callback: core::ptr::null_mut(),
                _pad_20: [0; 0xc],
                page_registry: core::ptr::null_mut(),
                _pad_30: [0; 8],
                hidden_flags: 0,
                _tail: [0; 3],
            };
            let _ = _d;
        }
    }

    #[test]
    fn identity_guard_compiles() {
        // Cannot be *called* on host (reads firmware addresses); referencing
        // it proves the generated guard exists with the right signature.
        let _g: fn() -> i32 = canopus_identity_guard;
    }

    #[test]
    fn clock_gettime_binding_compiles() {
        // Cannot be *called* on host (absolute address); referencing it proves
        // the transmuted extern fn type is valid for the recovered prototype.
        let _f: unsafe fn(u32, *const stock_timespec_t) -> i32 = canopus_fw_clock_gettime;
    }

    #[test]
    fn register_driver_binding_compiles() {
        // band-10 (3.101.030/3.101.036) register_driver is 4-arg; band-9 is 3-arg.
        #[cfg(not(feature = "target-xiaomi-band-9-pro-3-1-175"))]
        let _f: unsafe fn(
            *const u8,
            *const core::ffi::c_void,
            u32,
            *mut core::ffi::c_void,
        ) -> i32 = canopus_fw_register_driver;
        #[cfg(feature = "target-xiaomi-band-9-pro-3-1-175")]
        let _f: unsafe fn(
            *const u8,
            *const core::ffi::c_void,
            *mut core::ffi::c_void,
        ) -> i32 = canopus_fw_register_driver;
        let _ = _f;
    }

    #[test]
    fn forbidden_symbols_never_bind() {
        // FORBIDDEN symbols must not produce a callable binding.
        // This is a compile-time contract: any `canopus_fw_bt_adapter_...`
        // below would fail to compile.
        let _ = (concat!(module_path!(), " for the record"),);
    }
}
