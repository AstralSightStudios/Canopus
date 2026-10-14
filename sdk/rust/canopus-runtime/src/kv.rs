//! Persistent key-value configuration store for Canopus modules.

use canopus_abi::*;
use core::ffi::c_char;

pub fn get_int(ctx: *const CanopusContext, key: &str, default_val: i32) -> i32 {
    if ctx.is_null() { return default_val; }
    let kv = unsafe { (*ctx).kv };
    if kv.is_null() { return default_val; }
    let mut kb = [0u8; 32];
    if key.len() >= kb.len() { return default_val; }
    kb[..key.len()].copy_from_slice(key.as_bytes());
    unsafe { ((*kv).get_int)(ctx as *const ContextV1, kb.as_ptr() as *const c_char, default_val) }
}

pub fn set_int(ctx: *const CanopusContext, key: &str, val: i32) -> Result<(), i32> {
    if ctx.is_null() { return Err(-22); }
    let kv = unsafe { (*ctx).kv };
    if kv.is_null() { return Err(-38); }
    let mut kb = [0u8; 32];
    if key.len() >= kb.len() { return Err(-22); }
    kb[..key.len()].copy_from_slice(key.as_bytes());
    let rc = unsafe { ((*kv).set_int)(ctx as *const ContextV1, kb.as_ptr() as *const c_char, val) };
    if rc < 0 { Err(rc) } else { Ok(()) }
}

pub fn get_bool(ctx: *const CanopusContext, key: &str, default_val: bool) -> bool {
    if ctx.is_null() { return default_val; }
    let kv = unsafe { (*ctx).kv };
    if kv.is_null() { return default_val; }
    let mut kb = [0u8; 32];
    if key.len() >= kb.len() { return default_val; }
    kb[..key.len()].copy_from_slice(key.as_bytes());
    unsafe { ((*kv).get_bool)(ctx as *const ContextV1, kb.as_ptr() as *const c_char, default_val as i32) != 0 }
}

pub fn set_bool(ctx: *const CanopusContext, key: &str, val: bool) -> Result<(), i32> {
    if ctx.is_null() { return Err(-22); }
    let kv = unsafe { (*ctx).kv };
    if kv.is_null() { return Err(-38); }
    let mut kb = [0u8; 32];
    if key.len() >= kb.len() { return Err(-22); }
    kb[..key.len()].copy_from_slice(key.as_bytes());
    let rc = unsafe { ((*kv).set_bool)(ctx as *const ContextV1, kb.as_ptr() as *const c_char, val as i32) };
    if rc < 0 { Err(rc) } else { Ok(()) }
}

pub fn remove(ctx: *const CanopusContext, key: &str) -> Result<(), i32> {
    if ctx.is_null() { return Err(-22); }
    let kv = unsafe { (*ctx).kv };
    if kv.is_null() { return Err(-38); }
    let mut kb = [0u8; 32];
    if key.len() >= kb.len() { return Err(-22); }
    kb[..key.len()].copy_from_slice(key.as_bytes());
    let rc = unsafe { ((*kv).remove)(ctx as *const ContextV1, kb.as_ptr() as *const c_char) };
    if rc < 0 { Err(rc) } else { Ok(()) }
}

pub fn clear(ctx: *const CanopusContext) -> Result<(), i32> {
    if ctx.is_null() { return Err(-22); }
    let kv = unsafe { (*ctx).kv };
    if kv.is_null() { return Err(-38); }
    let rc = unsafe { ((*kv).clear)(ctx as *const ContextV1) };
    if rc < 0 { Err(rc) } else { Ok(()) }
}
