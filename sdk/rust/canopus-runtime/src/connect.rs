//! CanopusConnect: text/binary messages between a module and the remote
//! plugins (AstroBox) that listen to its module id. One message is one frame of
//! at most `CONNECT_DATA_MAX` bytes; see `canopus_services.h`.

use canopus_abi::*;
use core::ffi::c_void;

/// Outcome of a `send_*` that did not fail.
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum Delivery {
    /// The message was queued for the phone.
    Queued,
    /// No remote plugin listens to this module in the current phone session;
    /// nothing was queued.
    NotListening,
}

/// The `connect` object, or `-22` for a null context and `-38` when this
/// target has no L2 backend.
fn object(ctx: *const CanopusContext) -> Result<*const CanopusConnectObj, i32> {
    if ctx.is_null() {
        return Err(-22);
    }
    let object = unsafe { (*ctx).connect };
    if object.is_null() { Err(-38) } else { Ok(object) }
}

fn send(ctx: *const CanopusContext, kind: u32, data: &[u8]) -> Result<Delivery, i32> {
    let object = object(ctx)?;
    let rc = unsafe {
        ((*object).send)(ctx as *const ContextV1, kind, data.as_ptr() as *const c_void, data.len() as u32)
    };
    match rc {
        0 => Ok(Delivery::Queued),
        CONNECT_NOT_LISTENING => Ok(Delivery::NotListening),
        _ => Err(rc),
    }
}

pub fn send_text(ctx: *const CanopusContext, text: &str) -> Result<Delivery, i32> {
    send(ctx, CONNECT_TEXT, text.as_bytes())
}

pub fn send_binary(ctx: *const CanopusContext, data: &[u8]) -> Result<Delivery, i32> {
    send(ctx, CONNECT_BINARY, data)
}

/// Receives the messages remote plugins send to this module. The callback runs
/// on the module/UI task and must return promptly; for text the data is
/// NUL-terminated and the length excludes the NUL.
pub fn listen(ctx: *const CanopusContext, cb: CbConnect, user_data: *mut c_void) -> Result<(), i32> {
    let object = object(ctx)?;
    let rc = unsafe { ((*object).listen)(ctx as *const ContextV1, Some(cb), user_data) };
    if rc < 0 { Err(rc) } else { Ok(()) }
}

/// Removes the callback registered with `listen`.
pub fn unlisten(ctx: *const CanopusContext) -> Result<(), i32> {
    let object = object(ctx)?;
    let rc = unsafe { ((*object).listen)(ctx as *const ContextV1, None, core::ptr::null_mut()) };
    if rc < 0 { Err(rc) } else { Ok(()) }
}

/// Whether a remote plugin listens to this module in the current phone session.
pub fn remote_listening(ctx: *const CanopusContext) -> Result<bool, i32> {
    let object = object(ctx)?;
    let rc = unsafe { ((*object).remote_listening)(ctx as *const ContextV1) };
    if rc < 0 { Err(rc) } else { Ok(rc != 0) }
}
