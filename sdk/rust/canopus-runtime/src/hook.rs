//! Universal dynamic function hook engine for Canopus modules.

use canopus_abi::*;

pub struct Hook;

impl Hook {
    /// Register a Flash-function to RAM-dispatch-slot mapping for this module.
    /// The supervisor derives ownership from `ctx`; no owner slot is passed in
    /// the public C ABI. The dispatch storage must remain valid until module
    /// cleanup releases the mapping and any hooks using it.
    pub fn register_slot_map(
        ctx: *const CanopusContext,
        flash_fn: usize,
        ram_slot: usize,
    ) -> Result<(), i32> {
        if ctx.is_null() {
            return Err(-22);
        }
        let hook = unsafe { (*ctx).hook };
        if hook.is_null() {
            return Err(-38);
        }
        let rc =
            unsafe { ((*hook).register_slot_map)(ctx as *const ContextV1, flash_fn, ram_slot) };
        if rc < 0 { Err(rc) } else { Ok(()) }
    }

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
        if id < 0 { Err(id) } else { Ok(id) }
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
        if rc < 0 { Err(rc) } else { Ok(()) }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    unsafe extern "C" fn install(
        _: *const ContextV1,
        _: usize,
        _: usize,
        _: *mut usize,
        _: u32,
    ) -> i32 {
        -38
    }

    unsafe extern "C" fn uninstall(_: *const ContextV1, _: i32) -> i32 {
        -38
    }

    unsafe extern "C" fn is_hooked(_: *const ContextV1, _: usize) -> i32 {
        0
    }

    unsafe extern "C" fn register(ctx: *const ContextV1, flash_fn: usize, ram_slot: usize) -> i32 {
        // Verify the public three-argument ABI, including the exact context.
        if unsafe { (*ctx.cast::<CanopusContext>()).slot } != 7 {
            return -22;
        }
        if flash_fn == 0x0c00_1001 && ram_slot == 0x2000_1000 {
            0
        } else {
            -16
        }
    }

    #[test]
    fn mapping_wrapper_forwards_public_abi_and_errors() {
        let service = CanopusHookObj {
            install,
            uninstall,
            is_hooked,
            register_slot_map: register,
        };
        // Every field in CanopusContext is an integer or a nullable raw pointer.
        let mut ctx: CanopusContext = unsafe { core::mem::zeroed() };
        ctx.slot = 7;
        ctx.hook = &service;
        assert_eq!(
            Hook::register_slot_map(&ctx, 0x0c00_1001, 0x2000_1000),
            Ok(())
        );
        assert_eq!(Hook::register_slot_map(&ctx, 0x0c00_1001, 0), Err(-16));
        ctx.hook = core::ptr::null();
        assert_eq!(Hook::register_slot_map(&ctx, 1, 2), Err(-38));
        assert_eq!(Hook::register_slot_map(core::ptr::null(), 1, 2), Err(-22));
    }
}
