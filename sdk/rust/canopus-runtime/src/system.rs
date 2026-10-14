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
