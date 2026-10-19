//! The module's own launcher entry (ADR-CAN-011). Choosing it opens the page the
//! module registered with `ui::set_entry`. The firmware cannot take an entry
//! back, so it stays until the next reboot.

use canopus_abi::*;
use core::mem::{offset_of, size_of};

/// Publishes the entry. Errors are the C service codes: `-38` where this
/// Supervisor predates launcher entries or the build has no Manager UI, `-22`
/// for a malformed entry, `-16` when the module has registered no page entry or
/// already published a different entry, `-12` when all `LAUNCHER_ENTRIES` are taken.
pub fn set_entry(ctx: *const CanopusContext, entry: &LauncherEntryV1) -> Result<(), i32> {
    if ctx.is_null() {
        return Err(-22);
    }
    let covered = unsafe { (*ctx).struct_size } as usize
        >= offset_of!(CanopusContext, launcher) + size_of::<*const CanopusLauncherObj>();
    let object = if covered { unsafe { (*ctx).launcher } } else { core::ptr::null() };
    if object.is_null() {
        return Err(-38);
    }
    let rc = unsafe { ((*object).set_entry)(ctx as *const ContextV1, entry) };
    if rc < 0 { Err(rc) } else { Ok(()) }
}
