//! Inter-module and system publish/subscribe event bus for Canopus modules.

use canopus_abi::*;
use core::ffi::{c_char, c_void};

pub fn publish(ctx: *const CanopusContext, topic: &str, payload: &[u8]) -> Result<i32, i32> {
    if ctx.is_null() { return Err(-22); }
    let b = unsafe { (*ctx).bus };
    if b.is_null() { return Err(-38); }
    let mut tb = [0u8; 32];
    if topic.len() >= tb.len() { return Err(-22); }
    tb[..topic.len()].copy_from_slice(topic.as_bytes());
    let rc = unsafe {
        ((*b).publish)(
            ctx as *const ContextV1,
            tb.as_ptr() as *const c_char,
            payload.as_ptr() as *const c_void,
            payload.len() as u32,
        )
    };
    if rc < 0 { Err(rc) } else { Ok(rc) }
}

pub fn subscribe(
    ctx: *const CanopusContext,
    topic: &str,
    cb: CbBus,
    user_data: *mut c_void,
) -> Result<i32, i32> {
    if ctx.is_null() { return Err(-22); }
    let b = unsafe { (*ctx).bus };
    if b.is_null() { return Err(-38); }
    let mut tb = [0u8; 32];
    if topic.len() >= tb.len() { return Err(-22); }
    tb[..topic.len()].copy_from_slice(topic.as_bytes());
    let sub_id = unsafe {
        ((*b).subscribe)(
            ctx as *const ContextV1,
            tb.as_ptr() as *const c_char,
            cb,
            user_data,
        )
    };
    if sub_id < 0 { Err(sub_id) } else { Ok(sub_id) }
}

pub fn unsubscribe(ctx: *const CanopusContext, sub_id: i32) -> Result<(), i32> {
    if ctx.is_null() { return Err(-22); }
    let b = unsafe { (*ctx).bus };
    if b.is_null() { return Err(-38); }
    let rc = unsafe { ((*b).unsubscribe)(ctx as *const ContextV1, sub_id) };
    if rc < 0 { Err(rc) } else { Ok(()) }
}
