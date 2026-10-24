//! {{id}} - Canopus no_std Rust module. The C shim `canopus build` links in
//! registers `canopus_module_descriptor` with the Supervisor
//! (sdk/c/canopus_module_entry.h) and calls `canopus_mod_stop` on unload.
#![no_std]

use canopus_abi::*;
use canopus_runtime::*;
use core::ffi::c_char;

const MAGIC: u32 = 0x4D4F4455; // "MODU"

/// The context with its service objects (`struct canopus_context_v1`).
fn context<'a>(ctx: *const ContextV1) -> Option<&'a CanopusContext> {
    unsafe { (ctx as *const CanopusContext).as_ref() }
}

#[no_mangle]
pub extern "C" fn canopus_mod_prepare(_ctx: *const ContextV1) -> i32 {
    0
}

#[no_mangle]
pub extern "C" fn canopus_mod_stop(_ctx: *const ContextV1) -> i32 {
    0
}

extern "C" fn activate(ctx: *const ContextV1) -> i32 {
    if let Some(c) = context(ctx) {
        if unsafe { services_has(ctx, SVC_LOG, 1) } && !c.log.is_null() {
            unsafe { ((*c.log).info)(ctx, b"{{id}} active\0".as_ptr() as *const c_char) };
        }
    }
    0
}

extern "C" fn deactivate(_ctx: *const ContextV1) -> i32 {
    0
}

extern "C" fn query(w: *mut StatusWriterV1) -> i32 {
    let Some(w) = (unsafe { w.as_mut() }) else {
        return -1;
    };
    if !unsafe { status_put_u32(w, MAGIC) } {
        return -1;
    }
    status_writer_publish(w);
    0
}

const fn pack<const N: usize>(s: &[u8]) -> [u8; N] {
    let mut out = [0u8; N];
    let mut i = 0;
    while i < s.len() && i < N {
        out[i] = s[i];
        i += 1;
    }
    out
}

#[no_mangle]
pub static canopus_module_descriptor: ModuleDescriptorV1 = ModuleDescriptorV1 {
    struct_size: core::mem::size_of::<ModuleDescriptorV1>() as u32,
    abi_major: ABI_MAJOR,
    abi_minor: ABI_MINOR,
    flags: 0,
    module_id: pack(b"{{id}}"),
    module_version: pack(b"0.1.0"),
    build_id: pack(b"{{id}}-0.1.0"),
    // An exact target id, or "portable-thumbv8m"; set by `canopus build`.
    target_id: pack(env!("CANOPUS_TARGET_ID").as_bytes()),
    prepare: Some(canopus_mod_prepare),
    activate: Some(activate),
    deactivate: Some(deactivate),
    stop: Some(canopus_mod_stop),
    query: Some(query),
    publish_native_app: None,
    publish_native_app_stage: None,
};

#[panic_handler]
fn panic(_info: &core::panic::PanicInfo) -> ! {
    loop {}
}
