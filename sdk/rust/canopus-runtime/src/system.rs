//! System and hardware information for Canopus modules.

use canopus_abi::*;
use core::ffi::CStr;

pub fn target_id(ctx: *const CanopusContext) -> &'static str {
    if ctx.is_null() {
        return "";
    }
    let sys = unsafe { (*ctx).system };
    if sys.is_null() {
        return "";
    }
    let ptr = unsafe { ((*sys).target_id)(ctx as *const ContextV1) };
    if ptr.is_null() {
        return "";
    }
    unsafe { CStr::from_ptr(ptr) }.to_str().unwrap_or("")
}

pub fn framework_revision(ctx: *const CanopusContext) -> u32 {
    if ctx.is_null() {
        return 0;
    }
    let sys = unsafe { (*ctx).system };
    if sys.is_null() {
        return 0;
    }
    unsafe { ((*sys).framework_revision)(ctx as *const ContextV1) }
}

pub fn is_safe_mode(ctx: *const CanopusContext) -> bool {
    if ctx.is_null() {
        return false;
    }
    let sys = unsafe { (*ctx).system };
    if sys.is_null() {
        return false;
    }
    unsafe { ((*sys).is_safe_mode)(ctx as *const ContextV1) != 0 }
}

/// Battery percentage (0..=100); `None` until the firmware has reported it or
/// when this target cannot read it.
pub fn battery_level(ctx: *const CanopusContext) -> Option<u32> {
    if ctx.is_null() {
        return None;
    }
    let sys = unsafe { (*ctx).system };
    if sys.is_null() {
        return None;
    }
    let level = unsafe { ((*sys).battery_level)(ctx as *const ContextV1) };
    if level == SYSTEM_BATTERY_UNKNOWN { None } else { Some(level) }
}

/// `Err(-16)` before the firmware reported the charging state, `Err(-38)`
/// where this target has no battery source.
pub fn is_charging(ctx: *const CanopusContext) -> Result<bool, i32> {
    if ctx.is_null() {
        return Err(-22);
    }
    let sys = unsafe { (*ctx).system };
    if sys.is_null() {
        return Err(-38);
    }
    let rc = unsafe { ((*sys).is_charging)(ctx as *const ContextV1) };
    if rc < 0 { Err(rc) } else { Ok(rc != 0) }
}
