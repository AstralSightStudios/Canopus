//! Vibration and haptic feedback for Canopus modules.

use canopus_abi::*;

pub fn vibrate(ctx: *const CanopusContext, duration_ms: u32) -> Result<(), i32> {
    if ctx.is_null() { return Err(-22); }
    let h = unsafe { (*ctx).haptics };
    if h.is_null() { return Err(-38); }
    let rc = unsafe { ((*h).vibrate)(ctx as *const ContextV1, duration_ms) };
    if rc < 0 { Err(rc) } else { Ok(()) }
}

pub fn vibrate_preset(ctx: *const CanopusContext, preset: u32) -> Result<(), i32> {
    if ctx.is_null() { return Err(-22); }
    let h = unsafe { (*ctx).haptics };
    if h.is_null() { return Err(-38); }
    let rc = unsafe { ((*h).vibrate_preset)(ctx as *const ContextV1, preset) };
    if rc < 0 { Err(rc) } else { Ok(()) }
}

pub fn cancel(ctx: *const CanopusContext) -> Result<(), i32> {
    if ctx.is_null() { return Err(-22); }
    let h = unsafe { (*ctx).haptics };
    if h.is_null() { return Err(-38); }
    let rc = unsafe { ((*h).cancel)(ctx as *const ContextV1) };
    if rc < 0 { Err(rc) } else { Ok(()) }
}
