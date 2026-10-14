//! Universal dynamic function hook engine for Canopus modules.

use canopus_abi::*;

pub struct Hook;

impl Hook {
    pub fn install(
        ctx: *const CanopusContext,
        target_fn: usize,
        hook_fn: usize,
        orig_fn: &mut usize,
        hook_type: HookType,
    ) -> Result<i32, i32> {
        if ctx.is_null() {
            return Err(-22);
        }
        let hook = unsafe { (*ctx).hook };
        if hook.is_null() {
            return Err(-38);
        }
        let id = unsafe {
            ((*hook).install)(
                ctx as *const ContextV1,
                target_fn,
                hook_fn,
                orig_fn as *mut usize,
                hook_type as u32,
            )
        };
        if id < 0 {
            Err(id)
        } else {
            Ok(id)
        }
    }

    pub fn uninstall(ctx: *const CanopusContext, hook_id: i32) -> Result<(), i32> {
        if ctx.is_null() {
            return Err(-22);
        }
        let hook = unsafe { (*ctx).hook };
        if hook.is_null() {
            return Err(-38);
        }
        let rc = unsafe { ((*hook).uninstall)(ctx as *const ContextV1, hook_id) };
        if rc < 0 {
            Err(rc)
        } else {
            Ok(())
        }
    }
}
