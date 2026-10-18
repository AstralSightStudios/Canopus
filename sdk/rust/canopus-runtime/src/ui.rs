//! Module UI pages hosted by the Manager (ADR-CAN-010): register the page entry
//! the Manager offers on the module's detail page, and close the open page.
//! The callbacks and the tree builder are the C ABI of `canopus_services.h`
//! (`canopus-ui-core` mirrors the tree in `no_std` Rust).

use canopus_abi::*;
use core::mem::{offset_of, size_of};

/// The `ui_ext` object, or `-22` for a null context and `-38` when this
/// Supervisor predates module pages.
fn object(ctx: *const CanopusContext) -> Result<*const CanopusUiExtObj, i32> {
    if ctx.is_null() {
        return Err(-22);
    }
    let covered = unsafe { (*ctx).struct_size } as usize
        >= offset_of!(CanopusContext, ui_ext) + size_of::<*const CanopusUiExtObj>();
    let object = if covered { unsafe { (*ctx).ui_ext } } else { core::ptr::null() };
    if object.is_null() { Err(-38) } else { Ok(object) }
}

/// Registers (or replaces) the module's page entry. `-38` where the build has no
/// Manager UI, `-22` for a malformed entry.
pub fn set_entry(ctx: *const CanopusContext, entry: &UiEntryV1) -> Result<(), i32> {
    let object = object(ctx)?;
    let rc = unsafe { ((*object).set_entry)(ctx as *const ContextV1, entry) };
    if rc < 0 { Err(rc) } else { Ok(()) }
}

/// Removes the entry; an open page of this module is closed first.
pub fn clear_entry(ctx: *const CanopusContext) -> Result<(), i32> {
    let object = object(ctx)?;
    let rc = unsafe { ((*object).set_entry)(ctx as *const ContextV1, core::ptr::null()) };
    if rc < 0 { Err(rc) } else { Ok(()) }
}

/// Finishes the caller's open page; `Err(-16)` when it has none.
pub fn close_page(ctx: *const CanopusContext) -> Result<(), i32> {
    let object = object(ctx)?;
    let rc = unsafe { ((*object).close_page)(ctx as *const ContextV1) };
    if rc < 0 { Err(rc) } else { Ok(()) }
}
