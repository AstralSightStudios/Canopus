//! Firmware ABI test entrypoints linked against the actual .139 Rust backend.
#![no_std]
use canopus_target_private::*;
use core::ffi::c_void;
#[panic_handler]
fn panic(_: &core::panic::PanicInfo) -> ! {
    loop {}
}
#[unsafe(no_mangle)]
pub extern "C" fn test_identity() -> i32 {
    canopus_identity_guard()
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn test_buffer(n: u16, h: u16) -> *mut StockBuffer {
    unsafe { bt_buffer_new(n, h) }
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn test_timer(owner: *mut c_void, cb: *mut c_void, arg: *mut c_void) -> u32 {
    unsafe { bt_timer_add(owner, 17, 3, cb, arg, core::ptr::null(), 1) }
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn test_cancel(p: *mut u32) -> i32 {
    unsafe { bt_timer_cancel(p) }
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn test_pair(addr: *const u8) -> i32 {
    unsafe { bt_create_bond(addr, 1) }
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn test_remove(addr: *const u8) -> i32 {
    unsafe { bt_remove_bond(addr, 1) }
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn test_pair_state(addr: *const u8) -> u32 {
    unsafe { bt_get_pairing_state(addr, 1) }
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn test_scan(mode: i32, bondable: i32) -> i32 {
    unsafe { bt_adapter_set_scan_mode(mode, bondable) }
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn test_hook(h: BtGapTransportReceive) -> bool {
    unsafe { bt_gap_install_receive_hook(h) }
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn test_forward(s: *mut c_void, p: *mut u8, n: i32) -> i32 {
    unsafe { bt_gap_stock_receive(s, p, n) }
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn test_queue(owner: *mut c_void, run: QueueWork, arg: *mut c_void) -> i32 {
    unsafe { bt_queue_external(owner, run, bt_queue_free_addr(), arg, 7) }
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn test_row(
    parent: *mut c_void,
    a: *const u8,
    b: *const u8,
    t: u8,
) -> *mut c_void {
    unsafe { lvx_list_row_create(parent, a, b, t) }
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn test_row_update(row: *mut c_void, a: *const u8, b: *const u8) -> i32 {
    unsafe { lvx_list_row_update(row, core::ptr::null(), a, b, 0, 1) }
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn test_event(event: *mut c_void) -> u32 {
    unsafe { (lvx_event_get_user_data(event) as u32) << 16 | lvx_event_get_code(event) }
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn test_sdp() -> *mut c_void {
    unsafe { sdp_builder_create(0, 0x110a, 0x103, 0, core::ptr::null()) }
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn test_sdp_attr(
    p: *mut c_void,
    id: u16,
    n: u16,
    v: *const c_void,
) -> *mut u8 {
    unsafe { sdp_set_raw_attribute(p, id, 0, n, v) }
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn test_sdp_commit(p: *mut c_void) -> u32 {
    unsafe { sdp_commit(p) }
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn test_sdp_unregister(h: u32) -> i32 {
    unsafe { sdp_unregister(h) }
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn test_free(p: *mut c_void) {
    unsafe { bt_free(p) }
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn test_get_scan() -> i32 {
    unsafe { bt_adapter_get_scan_mode() }
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn test_clock(clock: u32, time: *const stock_timespec_t) -> i32 {
    unsafe { canopus_fw_clock_gettime(clock, time) }
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn test_connect(p: *mut u8) -> u32 {
    unsafe {
        configure_avdtp_connect_request(p);
        bt_l2cap_connect(p.cast())
    }
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn test_channel(p: *const u8) -> u32 {
    unsafe {
        ((p.add(EVENT_COMPLETE_CID_OFFSET).cast::<u16>().read() as u32) << 16)
            | p.add(EVENT_COMPLETE_MTU_OFFSET).cast::<u16>().read() as u32
    }
}
