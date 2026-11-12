//! Native Features for QuickApps (ADR-CAN-014).
//! The Supervisor owns the firmware dispatch every QuickApp starts through and
//! registers each published description into QuickApps started afterwards.

use canopus_abi::*;
use core::ffi::c_void;

/// Publishes an exact-target Feature description to every QuickApp started
/// after the call. Publishing the same description again succeeds without a
/// second entry; there is no removal.
///
/// # Safety
/// `ctx` must be null or the live context passed by the Supervisor, and the
/// call must run on the serialized module/UI owner. `description` must have
/// the exact selected firmware's Feature description layout. It, everything
/// it references and its callbacks must remain valid until reboot, which is
/// why the module must be resident (a removable module gets ACCESS). The
/// firmware calls the callbacks on each QuickApp's own JS task.
pub unsafe fn register_feature(
    ctx: *const CanopusContext,
    description: *const c_void,
) -> Result<(), i32> {
    if ctx.is_null() {
        return Err(-22);
    }
    if !unsafe { services_has(ctx as *const ContextV1, SVC_QUICKAPP, 1) } {
        return Err(-38);
    }
    let object = unsafe { (*ctx).quickapp };
    if object.is_null() {
        return Err(-38);
    }
    let rc = unsafe { ((*object).register_feature)(ctx as *const ContextV1, description) };
    if rc < 0 { Err(rc) } else { Ok(()) }
}

#[cfg(test)]
mod tests {
    use super::*;

    unsafe extern "C" fn register(_: *const ContextV1, description: *const c_void) -> i32 {
        if description.is_null() { -22 } else { 0 }
    }

    #[test]
    fn registration_needs_the_service_and_carries_its_errors() {
        let mut ctx: CanopusContext = unsafe { core::mem::zeroed() };
        let description = [0u32; 7];
        let feature = description.as_ptr().cast::<c_void>();
        assert_eq!(unsafe { register_feature(core::ptr::null(), feature) }, Err(-22));
        assert_eq!(unsafe { register_feature(&ctx, feature) }, Err(-38));
        let mut services = ServicesV1Head {
            struct_size: core::mem::size_of::<ServicesV1Head>() as u32,
            group_version: [0; SVC_GROUP_COUNT as usize],
        };
        services.group_version[SVC_QUICKAPP as usize] = 1;
        ctx.struct_size = core::mem::size_of::<CanopusContext>() as u32;
        ctx.services = &services as *const _ as *const c_void;
        assert_eq!(unsafe { register_feature(&ctx, feature) }, Err(-38));
        let object = CanopusQuickappObj { register_feature: register };
        ctx.quickapp = &object;
        assert_eq!(unsafe { register_feature(&ctx, feature) }, Ok(()));
        assert_eq!(unsafe { register_feature(&ctx, core::ptr::null()) }, Err(-22));
    }
}
