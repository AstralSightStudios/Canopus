//! 128 KiB module heap management for Canopus modules.

use canopus_abi::*;
use core::ffi::c_void;

pub fn alloc(ctx: *const CanopusContext, size: usize) -> *mut u8 {
    if ctx.is_null() {
        return core::ptr::null_mut();
    }
    let mem = unsafe { (*ctx).mem };
    if mem.is_null() {
        return core::ptr::null_mut();
    }
    unsafe { ((*mem).alloc)(ctx as *const ContextV1, size as u32) as *mut u8 }
}

pub fn realloc(ctx: *const CanopusContext, ptr: *mut u8, new_size: usize) -> *mut u8 {
    if ctx.is_null() {
        return core::ptr::null_mut();
    }
    let mem = unsafe { (*ctx).mem };
    if mem.is_null() {
        return core::ptr::null_mut();
    }
    unsafe { ((*mem).realloc)(ctx as *const ContextV1, ptr as *mut c_void, new_size as u32) as *mut u8 }
}

pub fn free(ctx: *const CanopusContext, ptr: *mut u8) {
    if ctx.is_null() || ptr.is_null() {
        return;
    }
    let mem = unsafe { (*ctx).mem };
    if !mem.is_null() {
        unsafe { ((*mem).free)(ctx as *const ContextV1, ptr as *mut c_void) };
    }
}

pub fn available(ctx: *const CanopusContext) -> usize {
    if ctx.is_null() {
        return 0;
    }
    let mem = unsafe { (*ctx).mem };
    if mem.is_null() {
        return 0;
    }
    unsafe { ((*mem).available)(ctx as *const ContextV1) as usize }
}

pub fn used(ctx: *const CanopusContext) -> usize {
    if ctx.is_null() {
        return 0;
    }
    let mem = unsafe { (*ctx).mem };
    if mem.is_null() {
        return 0;
    }
    unsafe { ((*mem).used)(ctx as *const ContextV1) as usize }
}
