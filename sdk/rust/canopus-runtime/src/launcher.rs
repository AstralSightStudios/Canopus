//! Supervisor-owned application registration (ADR-CAN-011).
//! One definition registers a managed UI entry or writable exact-target native
//! pages. Application identity persists; descriptors are republished each boot.

use canopus_abi::*;

/// Queues application registration and returns its stable assigned app ID.
/// Null `pages` and zero `page_count` open the page registered with
/// `ui::set_entry`. A nonempty page array registers native firmware pages
/// without that prerequisite. Repeating an identical definition returns the
/// same ID without another launcher entry; changing it returns BUSY.
///
/// # Safety
/// `ctx` must be null or the live context passed by the Supervisor, and the
/// call must run on the serialized module/UI owner. The label and icon must
/// satisfy the C entry's string contract. For native pages, `pages` must address
/// `page_count` pointers to writable descriptors with the exact selected
/// firmware layout, with a count in `1..=NATIVE_APP_PAGES_MAX`. Descriptors,
/// referenced data and callbacks must remain valid until reboot. The array
/// itself is copied before return and may live on the stack. The Supervisor
/// writes every native page's app ID on a later UI registration tick.
pub unsafe fn register_app(
    ctx: *const CanopusContext,
    entry: &AppEntryV1,
) -> Result<u8, i32> {
    if ctx.is_null() {
        return Err(-22);
    }
    if !unsafe { services_has(ctx as *const ContextV1, SVC_LAUNCHER, 1) } {
        return Err(-38);
    }
    let object = unsafe { (*ctx).launcher };
    if object.is_null() {
        return Err(-38);
    }
    let rc = unsafe { ((*object).register_app)(ctx as *const ContextV1, entry) };
    if rc < 0 { Err(rc) } else { Ok(rc as u8) }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn registration_rejects_null_context_or_absent_service() {
        let mut ctx: CanopusContext = unsafe { core::mem::zeroed() };
        let entry: AppEntryV1 = unsafe { core::mem::zeroed() };
        assert_eq!(unsafe { register_app(core::ptr::null(), &entry) }, Err(-22));
        assert_eq!(unsafe { register_app(&ctx, &entry) }, Err(-38));
        let mut services = ServicesV1Head {
            struct_size: core::mem::size_of::<ServicesV1Head>() as u32,
            group_version: [0; SVC_GROUP_COUNT as usize],
        };
        services.group_version[SVC_LAUNCHER as usize] = 1;
        ctx.struct_size = core::mem::size_of::<CanopusContext>() as u32;
        ctx.services = &services as *const _ as *const core::ffi::c_void;
        assert_eq!(unsafe { register_app(&ctx, &entry) }, Err(-38));
    }

    unsafe extern "C" fn register(_: *const ContextV1, entry: *const AppEntryV1) -> i32 {
        // Exercise managed and native success, plus service error transport.
        match unsafe { (*entry).page_count } {
            0 => 0xCC,
            1 => 0xD0,
            _ => -22,
        }
    }

    #[test]
    fn registration_preserves_assigned_id_and_service_errors() {
        let mut ctx: CanopusContext = unsafe { core::mem::zeroed() };
        let mut entry: AppEntryV1 = unsafe { core::mem::zeroed() };
        let mut services = ServicesV1Head {
            struct_size: core::mem::size_of::<ServicesV1Head>() as u32,
            group_version: [0; SVC_GROUP_COUNT as usize],
        };
        services.group_version[SVC_LAUNCHER as usize] = 1;
        let object = CanopusLauncherObj { register_app: register };
        ctx.struct_size = core::mem::size_of::<CanopusContext>() as u32;
        ctx.services = &services as *const _ as *const core::ffi::c_void;
        ctx.launcher = &object;
        assert_eq!(unsafe { register_app(&ctx, &entry) }, Ok(0xCC));
        entry.page_count = 1;
        assert_eq!(unsafe { register_app(&ctx, &entry) }, Ok(0xD0));
        entry.page_count = 2;
        assert_eq!(unsafe { register_app(&ctx, &entry) }, Err(-22));
    }
}
