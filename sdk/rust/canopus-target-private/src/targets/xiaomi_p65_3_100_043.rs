//! Experimental exact-P65 VFS/native UI facade, with the Band11 API names
//! where prototypes match. Bluetooth/L2CAP/SDP and unknown methods remain
//! fail-closed stubs; API presence is not implementation or device evidence.
//! Caller must pass the exact identity guard before invoking any unsafe call.
#![allow(non_camel_case_types, dead_code)]
use core::ffi::c_void;
#[path = "static_candidate.rs"]
mod unsupported;
pub use unsupported::*;
pub use canopus_target_generated::canopus_identity_guard;
mod addresses {
    #![allow(dead_code)]
    include!(concat!(env!("OUT_DIR"), "/p65_addresses.rs"));
}
macro_rules! call {
    ($address:ident, $ty:ty $(, $arg:expr)*) => {{
        let function: $ty = unsafe { core::mem::transmute(addresses::$address) };
        unsafe { function($($arg),*) }
    }};
}
pub const TARGET_ID: &str = "xiaomi-p65-3.100.043";
pub const SELECTED_TARGET_ID: &str = TARGET_ID;
pub const EXPERIMENTAL: bool = true;
pub fn capabilities() -> &'static [&'static str] {
    &["identity-guard", "experimental-vfs", "experimental-native-ui", "experimental-notification-center"]
}

/* Unlike Band10, P65 dispatches UI destruction at +100. */
#[repr(C, packed(4))]
#[derive(Copy, Clone, Debug)]
pub struct firmware_page_descriptor {
    pub parent_descriptor: *mut c_void,
    pub _pad_4: [u8; 12],
    pub page_name: *mut c_void,
    pub page_id: u16,
    pub app_id: u16,
    pub flags: u16,
    pub _pad_1a: [u8; 2],
    pub scheduler_deadline: i32,
    pub scheduler_priority: i32,
    pub async_destroy_state: u32,
    pub lifecycle_state: u8,
    pub layer: u8,
    pub page_kind: u8,
    pub _pad_2b: u8,
    pub activity_context: *mut c_void,
    pub root_object: *mut c_void,
    pub on_signal: *mut c_void,
    pub runtime_default_56: *mut c_void,
    pub user_data: *mut c_void,
    pub registry_prev: *mut c_void,
    pub registry_next: *mut c_void,
    pub runtime_parent: *mut c_void,
    pub on_create: *mut c_void,
    pub on_resume: *mut c_void,
    pub on_foreground_data: *mut c_void,
    pub on_intermediate: *mut c_void,
    pub on_pause: *mut c_void,
    pub on_stop: *mut c_void,
    pub on_ui_destroy: *mut c_void,
    pub extensions: [*mut c_void; 4],
}
#[cfg(target_pointer_width = "32")]
const _: () = {
    assert!(core::mem::size_of::<launcher_app_descriptor>() == 64);
    assert!(core::mem::offset_of!(firmware_page_descriptor, on_create) == 76);
    assert!(core::mem::offset_of!(firmware_page_descriptor, on_ui_destroy) == 100);
    assert!(core::mem::size_of::<firmware_page_descriptor>() == 120);
    assert!(core::mem::size_of::<firmware_notification_message>() == 88);
    assert!(core::mem::size_of::<file_operations>() == 48);
};

pub unsafe fn nuttx_open(path: *const u8, flags: i32) -> i32 {
    call!(CANOPUS_SUP_NUTTX_OPEN, unsafe extern "C" fn(*const u8, i32) -> i32, path, flags)
}
pub unsafe fn nuttx_create(path: *const u8, flags: i32, mode: u32) -> i32 {
    call!(CANOPUS_SUP_NUTTX_OPEN, unsafe extern "C" fn(*const u8, i32, u32) -> i32, path, flags, mode)
}
pub unsafe fn nuttx_close(fd: i32) -> i32 {
    call!(CANOPUS_SUP_NUTTX_CLOSE, unsafe extern "C" fn(i32) -> i32, fd)
}
pub unsafe fn nuttx_read(fd: i32, buffer: *mut c_void, count: u32) -> i32 {
    call!(CANOPUS_SUP_NUTTX_READ, unsafe extern "C" fn(i32, *mut c_void, u32) -> i32, fd, buffer, count)
}
pub unsafe fn nuttx_write(fd: i32, buffer: *const c_void, count: u32) -> i32 {
    call!(CANOPUS_SUP_NUTTX_WRITE, unsafe extern "C" fn(i32, *const c_void, u32) -> i32, fd, buffer, count)
}
pub unsafe fn get_errno() -> i32 {
    let pointer = call!(CANOPUS_SUP_NUTTX_ERRNO_LOCATION, unsafe extern "C" fn() -> *const i32);
    if pointer.is_null() { 0 } else { unsafe { pointer.read_volatile() } }
}
pub unsafe fn nuttx_unlink(path: *const u8) -> i32 {
    call!(CANOPUS_SUP_NUTTX_UNLINK, unsafe extern "C" fn(*const u8) -> i32, path)
}
pub unsafe fn nuttx_rename(old: *const u8, new: *const u8) -> i32 {
    call!(CANOPUS_SUP_NUTTX_RENAME, unsafe extern "C" fn(*const u8, *const u8) -> i32, old, new)
}
pub unsafe fn canopus_fw_register_driver(path: *const u8, fops: *const c_void,
                                         mode: u32, private: *mut c_void) -> i32 {
    call!(P65_REGISTER_DRIVER, unsafe extern "C" fn(*const u8, *const c_void, u32, *mut c_void) -> i32,
          path, fops, mode, private)
}
pub unsafe fn lvx_label_create(parent: *mut c_void) -> *mut c_void {
    call!(P65_LABEL_CREATE, unsafe extern "C" fn(*mut c_void) -> *mut c_void, parent)
}
pub unsafe fn lvx_label_set_text(label: *mut c_void, text: *const u8) {
    call!(P65_LABEL_TEXT, unsafe extern "C" fn(*mut c_void, *const u8), label, text)
}
pub unsafe fn lvx_object_set_size(object: *mut c_void, width: i32, height: i32) {
    call!(P65_OBJECT_SIZE, unsafe extern "C" fn(*mut c_void, i32, i32), object, width, height)
}
pub unsafe fn lvx_object_align(object: *mut c_void, align: u32, x: i32, y: i32) {
    call!(P65_OBJECT_ALIGN, unsafe extern "C" fn(*mut c_void, u32, i32, i32), object, align, x, y)
}
pub unsafe fn lvx_object_add_flag(object: *mut c_void, flags: u32) {
    call!(P65_OBJECT_ADD_FLAG, unsafe extern "C" fn(*mut c_void, u32), object, flags)
}
pub unsafe fn lvx_event_add(object: *mut c_void, callback: LvxEventCallback,
                             event: u32, context: *mut c_void) {
    call!(P65_EVENT_ADD, unsafe extern "C" fn(*mut c_void, LvxEventCallback, u32, *mut c_void),
          object, callback, event, context)
}
pub unsafe fn lvx_event_get_user_data(event: *mut c_void) -> usize {
    if event.is_null() { return 0; }
    unsafe { event.cast::<u8>().add(12).cast::<u32>().read_unaligned() as usize }
}
pub unsafe fn lvx_event_get_code(event: *mut c_void) -> u32 {
    if event.is_null() { return 0; }
    unsafe { u32::from(event.cast::<u8>().add(8).cast::<u16>().read_unaligned() & 0x7fff) }
}
pub unsafe fn notification_insert(message: *const firmware_notification_message) -> i32 {
    if message.is_null() { return -1; }
    call!(P65_NOTIFICATION_INSERT, unsafe extern "C" fn(*const firmware_notification_message) -> i32, message)
}

/* P65 app lookup uses a package-name string, not Band11's numeric ID. */
pub unsafe fn app_lookup_package(package: *const u8) -> *mut c_void {
    if package.is_null() { return core::ptr::null_mut(); }
    call!(P65_APP_LOOKUP, unsafe extern "C" fn(*const u8) -> *mut c_void, package)
}
pub unsafe fn app_install(app: *const launcher_app_descriptor,
                           pages: *const *mut firmware_page_descriptor, count: u32) -> i32 {
    if app.is_null() || pages.is_null() || count == 0 { return -1; }
    let package = unsafe { (*app).package_name }.cast::<u8>();
    if package.is_null() { return -1; }
    if !unsafe { app_lookup_package(package) }.is_null() { return -17; }
    call!(P65_APP_INSTALL,
          unsafe extern "C" fn(*const launcher_app_descriptor, *const *mut firmware_page_descriptor, u32),
          app, pages, count);
    if unsafe { app_lookup_package(package) }.is_null() { -1 } else { 0 }
}
pub unsafe fn launcher_refresh() {
    call!(P65_LAUNCHER_REFRESH, unsafe extern "C" fn())
}
pub unsafe fn activity_finish(_: *mut firmware_page_descriptor) -> i32 { ERR_UNSUPPORTED }
/// Unrecovered P65 flag-removal hook: no firmware call is performed.
/// Check capabilities rather than assuming the void API is implemented.
pub unsafe fn lvx_object_clear_flag(_: *mut c_void, _: u32) {}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn unsupported_subsystems_never_call_firmware() {
        assert_eq!(TARGET_ID, "xiaomi-p65-3.100.043");
        assert!(EXPERIMENTAL);
        assert!(!capabilities().contains(&"bluetooth"));
        unsafe {
            assert!(bt_adapter_get_state(core::ptr::null_mut()) < 0);
            assert_eq!(canopus_fw_unregister_driver(core::ptr::null()), ERR_UNSUPPORTED);
            assert_eq!(activity_finish(core::ptr::null_mut()), ERR_UNSUPPORTED);
        }
    }
    #[test]
    fn event_accessors_use_the_p65_layout() {
        let mut bytes = [0u8; 16];
        bytes[8..10].copy_from_slice(&0x8007u16.to_le_bytes());
        bytes[12..16].copy_from_slice(&0x12345678u32.to_le_bytes());
        unsafe {
            assert_eq!(lvx_event_get_code(bytes.as_mut_ptr().cast()), 7);
            assert_eq!(lvx_event_get_user_data(bytes.as_mut_ptr().cast()), 0x12345678);
        }
    }
}
