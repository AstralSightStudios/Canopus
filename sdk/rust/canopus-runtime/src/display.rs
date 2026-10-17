//! Display metrics, brightness, and wake lock control for Canopus modules.
//!
//! Getters report what the firmware says: `None` / `Err` mean "this target
//! cannot tell right now", never a placeholder value.

use canopus_abi::*;

/// Brightness percentage of the active override or the stored manual setting;
/// `None` while auto-brightness decides or when this target cannot read it.
pub fn get_brightness(ctx: *const CanopusContext) -> Option<u32> {
    if ctx.is_null() { return None; }
    let d = unsafe { (*ctx).display };
    if d.is_null() { return None; }
    let percent = unsafe { ((*d).get_brightness)(ctx as *const ContextV1) };
    if percent == DISPLAY_BRIGHTNESS_UNKNOWN { None } else { Some(percent) }
}

pub fn set_brightness(ctx: *const CanopusContext, percent: u32) -> Result<(), i32> {
    if ctx.is_null() { return Err(-22); }
    let d = unsafe { (*ctx).display };
    if d.is_null() { return Err(-38); }
    let rc = unsafe { ((*d).set_brightness)(ctx as *const ContextV1, percent) };
    if rc < 0 { Err(rc) } else { Ok(()) }
}

/// `Ok(true)` only while the screen is on (the always-on display is `false`).
pub fn is_screen_on(ctx: *const CanopusContext) -> Result<bool, i32> {
    if ctx.is_null() { return Err(-22); }
    let d = unsafe { (*ctx).display };
    if d.is_null() { return Err(-38); }
    let rc = unsafe { ((*d).is_screen_on)(ctx as *const ContextV1) };
    if rc < 0 { Err(rc) } else { Ok(rc != 0) }
}

pub fn wake_screen(ctx: *const CanopusContext) -> Result<(), i32> {
    if ctx.is_null() { return Err(-22); }
    let d = unsafe { (*ctx).display };
    if d.is_null() { return Err(-38); }
    let rc = unsafe { ((*d).wake_screen)(ctx as *const ContextV1) };
    if rc < 0 { Err(rc) } else { Ok(()) }
}

/// Acquires a module-owned wake lock and returns its id (always positive).
pub fn acquire_wake_lock(ctx: *const CanopusContext, flags: u32) -> Result<i32, i32> {
    if ctx.is_null() { return Err(-22); }
    let d = unsafe { (*ctx).display };
    if d.is_null() { return Err(-38); }
    let rc = unsafe { ((*d).acquire_wake_lock)(ctx as *const ContextV1, flags) };
    if rc < 0 { Err(rc) } else { Ok(rc) }
}

pub fn release_wake_lock(ctx: *const CanopusContext, lock_id: i32) -> Result<(), i32> {
    if ctx.is_null() { return Err(-22); }
    let d = unsafe { (*ctx).display };
    if d.is_null() { return Err(-38); }
    let rc = unsafe { ((*d).release_wake_lock)(ctx as *const ContextV1, lock_id) };
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
