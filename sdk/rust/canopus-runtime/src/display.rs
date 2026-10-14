//! Display metrics, brightness, and wake lock control for Canopus modules.

use canopus_abi::*;

pub fn get_brightness(ctx: *const CanopusContext) -> u32 {
    if ctx.is_null() { return 100; }
    let d = unsafe { (*ctx).display };
    if d.is_null() { return 100; }
    unsafe { ((*d).get_brightness)(ctx as *const ContextV1) }
}

pub fn set_brightness(ctx: *const CanopusContext, percent: u32) -> Result<(), i32> {
    if ctx.is_null() { return Err(-22); }
    let d = unsafe { (*ctx).display };
    if d.is_null() { return Err(-38); }
    let rc = unsafe { ((*d).set_brightness)(ctx as *const ContextV1, percent) };
    if rc < 0 { Err(rc) } else { Ok(()) }
}

pub fn is_screen_on(ctx: *const CanopusContext) -> bool {
    if ctx.is_null() { return true; }
    let d = unsafe { (*ctx).display };
    if d.is_null() { return true; }
    unsafe { ((*d).is_screen_on)(ctx as *const ContextV1) != 0 }
}

pub fn wake_screen(ctx: *const CanopusContext) -> Result<(), i32> {
    if ctx.is_null() { return Err(-22); }
    let d = unsafe { (*ctx).display };
    if d.is_null() { return Err(-38); }
    let rc = unsafe { ((*d).wake_screen)(ctx as *const ContextV1) };
    if rc < 0 { Err(rc) } else { Ok(()) }
}

pub fn get_resolution(ctx: *const CanopusContext) -> (u32, u32) {
    if ctx.is_null() { return (240, 240); }
    let d = unsafe { (*ctx).display };
    if d.is_null() { return (240, 240); }
    let mut w = 0u32;
    let mut h = 0u32;
    unsafe { ((*d).get_resolution)(ctx as *const ContextV1, &mut w, &mut h) };
    (w, h)
}
