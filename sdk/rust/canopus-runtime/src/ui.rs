//! Module UI pages hosted by the Manager (ADR-CAN-010), plus UI-thread tasks.
//! The callbacks and tree builder use the C ABI in `canopus_services.h`.

use canopus_abi::*;
use core::ffi::c_void;

/// The UI object, or `-22` for a null context and `-38` when UI is absent.
fn object(ctx: *const CanopusContext) -> Result<*const CanopusUiObj, i32> {
    if ctx.is_null() {
        return Err(-22);
    }
    let object = unsafe { (*ctx).ui };
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

/// Queues a task on the UI heartbeat. Repeating the same task coalesces;
/// another pending callback returns `-16`. Unloading cancels queued work.
pub fn post(ctx: *const CanopusContext, callback: CbUiTask, user_data: *mut c_void) -> Result<(), i32> {
    let object = object(ctx)?;
    let rc = unsafe { ((*object).post)(ctx as *const ContextV1, callback, user_data) };
    if rc < 0 { Err(rc) } else { Ok(()) }
}

/// Applies a generation-checked native UI control operation.
///
/// # Safety
/// `ui` must be the live page context and `out` must match the operation's
/// output record (or be null for operations with no output).
pub unsafe fn control(
    ctx: *const CanopusContext,
    ui: *mut c_void,
    generation: u32,
    key: u32,
    op: u32,
    a: i32,
    b: i32,
    out: *mut c_void,
) -> Result<(), i32> {
    let object = object(ctx)?;
    let rc = unsafe { ((*object).control)(ui, generation, key, op, a, b, out) };
    if rc < 0 { Err(rc) } else { Ok(()) }
}
