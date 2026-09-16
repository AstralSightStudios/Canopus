//! Exact .139 Bluetooth, VFS and LVGL v9 backend. See EVID-BLUETOOTH-4139-006.
//! Call only after the identity guard; published callbacks require boot residency.
//! Interconnect and notifications are not provided by this backend.
#![allow(non_camel_case_types)]
macro_rules! call {
    ($symbol:ident, $ty:ty $(, $arg:expr)*) => {{
        let f: $ty = unsafe { core::mem::transmute(fw::canopus_thumb_callable(fw::$symbol)) };
        unsafe { f($($arg),*) }
    }};
}

use canopus_target_generated as fw;
use core::ffi::c_void;
pub use fw::{
    canopus_identity_guard, file_operations, firmware_notification_message,
    firmware_page_descriptor, launcher_app_descriptor, stock_timespec_t,
};
#[cfg(feature = "target-xiaomi-band-11-4-100-139")]
pub const TARGET_ID: &str = "xiaomi-band-11-4.100.139";
#[cfg(feature = "target-xiaomi-band-11-4-100-155")]
pub const TARGET_ID: &str = "xiaomi-band-11-4.100.155";
pub const SELECTED_TARGET_ID: &str = TARGET_ID;
pub const ERR_UNSUPPORTED: i32 = -38;
pub fn capabilities() -> &'static [&'static str] {
    &[
        "identity-guard",
        "bluetooth",
        "l2cap",
        "sdp",
        "native-ui",
        "vfs",
    ]
}
#[repr(C, packed(4))]
#[derive(Copy, Clone, Debug)]
pub struct canopus_interconnect_message {
    pub r#type: u8,
    pub _pad_1: [u8; 3],
    pub length: u32,
    pub _pad_8: [u8; 8],
    pub value: *mut core::ffi::c_void,
}

#[repr(C, packed(4))]
#[derive(Copy, Clone, Debug)]
pub struct canopus_interconnect_app_info {
    pub package_name: *mut core::ffi::c_void,
    pub display_name: *mut core::ffi::c_void,
    pub icon_file: *mut core::ffi::c_void,
    pub extra: *mut core::ffi::c_void,
    pub fingerprint: [u8; 20],
}

#[repr(C, packed(4))]
#[derive(Copy, Clone, Debug)]
pub struct canopus_thirdparty_message_content {
    pub message_id: u32,
    pub message_kind: u16,
    pub _pad_6: [u8; 10],
    pub package_name: *mut core::ffi::c_void,
    pub fingerprint_blob: *mut core::ffi::c_void,
    pub payload_blob: *mut core::ffi::c_void,
    pub _tail: [u8; 0x10c],
}

pub type canopus_interconnect_recv_cb = extern "C" fn(
    *mut core::ffi::c_void,
    i32,
    *const canopus_interconnect_message,
    *const u8,
) -> ();
pub type canopus_interconnect_send_done = extern "C" fn(
    *mut core::ffi::c_void,
    i32,
    *const canopus_interconnect_message,
    *mut core::ffi::c_void,
) -> ();
pub type InterconnectConnMessage = canopus_interconnect_message;
pub type QuickAppInfo = canopus_interconnect_app_info;
pub type InterconnectRecvCb = canopus_interconnect_recv_cb;
pub type InterconnectSendDone = canopus_interconnect_send_done;

#[repr(C)]
#[derive(Copy, Clone, Debug, Default)]
pub struct DiscoveryResult {
    pub address: [u8; 6],
    pub rssi: i8,
    pub reserved: u8,
    pub class_of_device: u32,
}

pub unsafe fn discovery_name<'a>(result: *const DiscoveryResult, capacity: usize) -> &'a [u8] {
    let header = core::mem::size_of::<DiscoveryResult>();
    if result.is_null() || capacity <= header {
        return &[];
    }
    let name = unsafe { result.cast::<u8>().add(header) };
    let mut length = 0;
    while length < capacity - header && unsafe { *name.add(length) } != 0 {
        length += 1;
    }
    unsafe { core::slice::from_raw_parts(name, length) }
}

pub type AdapterStateCallback = unsafe extern "C" fn(*mut core::ffi::c_void, i32);
pub type DiscoveryStateCallback = unsafe extern "C" fn(*mut core::ffi::c_void, i32);
pub type DiscoveryResultCallback =
    unsafe extern "C" fn(*mut core::ffi::c_void, *const DiscoveryResult);
pub type PairRequestCallback = unsafe extern "C" fn(*mut core::ffi::c_void, *const u8);
pub type PairDisplayCallback =
    unsafe extern "C" fn(*mut core::ffi::c_void, *const u8, i32, i32, u32);
pub type BondStateCallback = unsafe extern "C" fn(*mut core::ffi::c_void, *const u8, i32, i32);

pub const ADAPTER_STATE_ON: i32 = 4;
pub const DISCOVERY_STOPPED: i32 = 0;
pub const BOND_STATE_NONE: u32 = 0;
pub const BOND_STATE_BONDED: u32 = 2;
pub const CLASSIC_TRANSPORT: u32 = 1;
pub const DISCOVERY_TIMEOUT_SECONDS: i32 = 20;
pub const CALLBACK_WORDS: usize = 17;
pub const CALLBACK_ADAPTER_STATE: usize = 0;
pub const CALLBACK_DISCOVERY_STATE: usize = 1;
pub const CALLBACK_DISCOVERY_RESULT: usize = 2;
pub const CALLBACK_PAIR_REQUEST: usize = 5;
pub const CALLBACK_PAIR_DISPLAY: usize = 6;
pub const CALLBACK_BOND_STATE: usize = 9;
pub const STOCK_CALLBACK_WORDS: usize = 17;
pub const STOCK_CALLBACK_PAIR_REQUEST_SLOT: usize = 5;
pub const CREATE_BOND_ADAPTER_NOT_READY: i32 = 2;

#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub enum PairRequestFilterError {
    Policy,
    DescriptorUnavailable,
    DescriptorMismatch,
    PairSlotMismatch,
    Allocation,
    Registration,
}

#[derive(Copy, Clone, Debug)]
pub struct PairRequestFilter {
    pub allocation: usize,
    pub registration: u32,
}

pub unsafe fn bt_adapter_get_instance() -> *mut c_void {
    unsafe { core::ptr::read_volatile(fw::canopus_fw_bt_shared_adapter as *const *mut c_void) }
}
pub unsafe fn bt_adapter_register(adapter: *mut c_void, callbacks: *const u32) -> u32 {
    call!(
        CANOPUS_FW_BT_ADAPTER_REGISTER_CALLABLE,
        unsafe extern "C" fn(*mut c_void, *const u32) -> u32,
        adapter,
        callbacks
    )
}
pub unsafe fn bt_adapter_unregister(adapter: *mut c_void, registration: u32) -> i32 {
    call!(
        CANOPUS_FW_BT_ADAPTER_UNREGISTER_CALLABLE,
        unsafe extern "C" fn(*mut c_void, u32) -> i32,
        adapter,
        registration
    )
}
pub unsafe fn bt_adapter_get_state(adapter: *mut c_void) -> i32 {
    call!(
        CANOPUS_FW_BT_ADAPTER_GET_STATE_CALLABLE,
        unsafe extern "C" fn(*mut c_void) -> i32,
        adapter
    )
}
pub unsafe fn bt_discovery_start(adapter: *mut c_void, timeout: i32) -> i32 {
    call!(
        CANOPUS_FW_BT_DISCOVERY_START_CALLABLE,
        unsafe extern "C" fn(*mut c_void, i32) -> i32,
        adapter,
        timeout
    )
}
pub unsafe fn bt_discovery_stop(adapter: *mut c_void) -> i32 {
    call!(
        CANOPUS_FW_BT_DISCOVERY_STOP_CALLABLE,
        unsafe extern "C" fn(*mut c_void) -> i32,
        adapter
    )
}
pub unsafe fn bt_adapter_set_scan_mode(scan_mode: i32, bondable: i32) -> i32 {
    unsafe { adapter_request(37, &[scan_mode as u8, (bondable != 0) as u8]) }
}
pub unsafe fn bt_adapter_get_scan_mode() -> i32 {
    unsafe { adapter_request(38, &[]) }
}
pub unsafe fn bt_pair_request_reply(adapter: *mut c_void, address: *const u8, accept: i32) -> i32 {
    call!(
        CANOPUS_FW_BT_PAIR_REQUEST_REPLY_CALLABLE,
        unsafe extern "C" fn(*mut c_void, *const u8, i32) -> i32,
        adapter,
        address,
        accept
    )
}
pub unsafe fn bt_pair_display_reply(
    adapter: *mut c_void,
    address: *const u8,
    transport: i32,
    accept: i32,
) -> i32 {
    call!(
        CANOPUS_FW_BT_PAIR_DISPLAY_REPLY_CALLABLE,
        unsafe extern "C" fn(*mut c_void, *const u8, i32, i32) -> i32,
        adapter,
        address,
        transport,
        accept
    )
}
pub unsafe fn bt_get_bond_state(address: *const u8) -> u32 {
    call!(
        CANOPUS_FW_BT_GET_BOND_STATE_CALLABLE,
        unsafe extern "C" fn(*mut c_void, *const u8) -> u32,
        bt_adapter_get_instance(),
        address
    )
}
pub unsafe fn bt_get_pairing_state(address: *const u8, transport: u32) -> u32 {
    unsafe { device_request(92, address, transport) as u32 }
}
pub unsafe fn bt_create_bond(address: *const u8, transport: u32) -> i32 {
    unsafe { device_request(94, address, transport) as i32 }
}
pub unsafe fn bt_remove_bond(address: *const u8, transport: u32) -> i32 {
    unsafe { device_request(95, address, transport) as i32 }
}
pub unsafe fn bt_install_pair_request_filter(
    _replacement: PairRequestCallback,
) -> Result<Option<PairRequestFilter>, PairRequestFilterError> {
    // The .139 system callback accepts pairing. It must keep its registration.
    Ok(None)
}
pub unsafe fn bt_forward_pair_request(_cookie: *mut c_void, address: *const u8) -> i32 {
    unsafe { bt_pair_request_reply(bt_adapter_get_instance(), address, 1) }
}

#[repr(C)]
#[derive(Copy, Clone, Debug, Default)]
pub struct StockBuffer {
    pub total: u16,
    pub offset: u16,
    pub route: u32,
    pub type_: u8,
    pub tag: u8,
}
pub unsafe fn stock_buffer_payload_mut(buffer: *mut StockBuffer) -> *mut u8 {
    if buffer.is_null() {
        return core::ptr::null_mut();
    }
    unsafe { buffer.cast::<u8>().add(4 + (*buffer).offset as usize) }
}
pub unsafe fn stock_buffer_payload(buffer: *const StockBuffer) -> *const u8 {
    if buffer.is_null() {
        return core::ptr::null();
    }
    unsafe { buffer.cast::<u8>().add(4 + (*buffer).offset as usize) }
}

pub const CONNECT_REQUEST_SIZE: usize = 68;
pub const CONNECT_PSM_OFFSET: usize = 2;
pub const CONNECT_FLAGS_OFFSET: usize = 8;
pub const CONNECT_CALLBACK_OFFSET: usize = 12;
pub const CONNECT_ADDRESS_OFFSET: usize = 16;
pub const CONNECT_CONFIG_OFFSET: usize = 52;
pub const CONNECT_OPTIONS_OFFSET: usize = 54;
pub const CONNECT_OPTION_LOCAL_MTU: u16 = 1;
pub const AVDTP_SIGNALING_PSM: u16 = 0x0019;
pub const AVCTP_CONTROL_PSM: u16 = 0x0017;
pub const AVCTP_LOCAL_RX_MTU: u16 = 0x0200;
pub const AVDTP_LOCAL_RX_MTU: u16 = 0x0400;

#[repr(C)]
#[derive(Copy, Clone, Debug, Default)]
pub struct DisconnectRequest {
    pub private_cid: u16,
    pub caller_tag: u16,
}
#[repr(C)]
#[derive(Copy, Clone, Debug, Default)]
pub struct MediaTimerToken {
    pub generation: u32,
    pub timer_generation: u32,
}
pub const EVENT_CONNECTION_CONFIRM: u32 = 2;
pub const EVENT_CONNECTION_COMPLETE: u32 = 3;
pub const EVENT_CHANNEL_STATUS_4: u32 = 4;
pub const EVENT_CHANNEL_STATUS_5: u32 = 5;
pub const EVENT_DISCONNECTION_COMPLETE: u32 = 6;
pub const EVENT_DATA: u32 = 7;
pub const EVENT_FLOW_STATUS: u32 = 8;
// 0xc86eb0c allocates a separate 120-byte completion event. Remote config
// is copied to +44 (MTU at +28); CID is copied from channel+116 to event+108.
pub const EVENT_COMPLETE_MTU_OFFSET: usize = 72;
pub const EVENT_COMPLETE_CID_OFFSET: usize = 108;

pub unsafe fn configure_avdtp_connect_request(request: *mut u8) {
    unsafe {
        core::ptr::write_unaligned(
            request.add(CONNECT_PSM_OFFSET).cast::<u16>(),
            AVDTP_SIGNALING_PSM.to_le(),
        );
        core::ptr::write_unaligned(
            request.add(CONNECT_CONFIG_OFFSET).cast::<u16>(),
            AVDTP_LOCAL_RX_MTU.to_le(),
        );
        core::ptr::write_unaligned(
            request.add(CONNECT_OPTIONS_OFFSET).cast::<u16>(),
            CONNECT_OPTION_LOCAL_MTU.to_le(),
        );
    }
}

/// Installs the exact-target AVCTP policy in a zeroed stock connect request.
///
/// # Safety
/// `request` must point to a writable [`CONNECT_REQUEST_SIZE`]-byte allocation.
pub unsafe fn configure_avctp_connect_request(request: *mut u8) {
    unsafe {
        core::ptr::write_unaligned(
            request.add(CONNECT_PSM_OFFSET).cast::<u16>(),
            AVCTP_CONTROL_PSM.to_le(),
        );
        core::ptr::write_unaligned(
            request.add(CONNECT_CONFIG_OFFSET).cast::<u16>(),
            AVCTP_LOCAL_RX_MTU.to_le(),
        );
        core::ptr::write_unaligned(
            request.add(CONNECT_OPTIONS_OFFSET).cast::<u16>(),
            CONNECT_OPTION_LOCAL_MTU.to_le(),
        );
    }
}
pub unsafe fn bt_buffer_new(length: u16, headroom: u16) -> *mut StockBuffer {
    let Some(offset) = headroom.checked_add(4) else {
        return core::ptr::null_mut();
    };
    let Some(total) = length.checked_add(offset) else {
        return core::ptr::null_mut();
    };
    let p = unsafe { bt_alloc(u32::from(total) + 6) }.cast::<u8>();
    if p.is_null() {
        return core::ptr::null_mut();
    }
    unsafe {
        core::ptr::write_bytes(p, 0, usize::from(total) + 6);
        p.cast::<u16>().write(total);
        p.add(2).cast::<u16>().write(offset);
        if offset > 7 {
            p.add(8).write(3);
        }
    }
    p.cast()
}
pub unsafe fn bt_l2cap_connect(request: *const c_void) -> u32 {
    call!(
        CANOPUS_FW_BT_L2CAP_CONNECT_CALLABLE,
        unsafe extern "C" fn(*const c_void) -> u32,
        request
    )
}
pub unsafe fn bt_l2cap_disconnect(request: *const DisconnectRequest) -> i32 {
    call!(
        CANOPUS_FW_BT_L2CAP_DISCONNECT_CALLABLE,
        unsafe extern "C" fn(*const DisconnectRequest) -> i32,
        request
    )
}
pub unsafe fn bt_l2cap_submit_cid(buffer: *mut StockBuffer, cid: u16) -> i32 {
    call!(
        CANOPUS_FW_BT_L2CAP_SUBMIT_CID_CALLABLE,
        unsafe extern "C" fn(*mut StockBuffer, u16) -> i32,
        buffer,
        cid
    )
}
pub unsafe fn bt_alloc(size: u32) -> *mut c_void {
    call!(
        CANOPUS_FW_BT_ALLOC_CALLABLE,
        unsafe extern "C" fn(u32) -> *mut c_void,
        size
    )
}
pub unsafe fn bt_free(allocation: *mut c_void) {
    call!(
        CANOPUS_FW_BT_FREE_CALLABLE,
        unsafe extern "C" fn(*mut c_void) -> (),
        allocation
    )
}
pub unsafe fn bt_timer_add(
    owner: *mut c_void,
    delay_ms: u32,
    event: u8,
    run: *mut c_void,
    argument: *mut c_void,
    _tag: *const u8,
    flags: u32,
) -> u32 {
    call!(
        CANOPUS_FW_BT_TIMER_ADD_CALLABLE,
        unsafe extern "C" fn(*mut c_void, u32, u8, *mut c_void, *mut c_void, u32) -> u32,
        owner,
        delay_ms,
        event,
        run,
        argument,
        flags
    )
}
pub unsafe fn bt_timer_cancel(handle: *mut u32) -> i32 {
    call!(
        CANOPUS_FW_BT_TIMER_CANCEL_OWNED_CALLABLE,
        unsafe extern "C" fn(*mut u32) -> (),
        handle
    );
    0
}
pub type QueueWork = extern "C" fn(i32, i32, *mut core::ffi::c_void) -> i32;
pub unsafe fn bt_queue_external(
    owner: *mut c_void,
    run: QueueWork,
    cancel: *mut c_void,
    argument: *mut c_void,
    event: u8,
) -> i32 {
    call!(
        CANOPUS_FW_BT_QUEUE_EXTERNAL_CALLABLE,
        unsafe extern "C" fn(*mut c_void, QueueWork, *mut c_void, *mut c_void, u8) -> i32,
        owner,
        run,
        cancel,
        argument,
        event
    )
}
pub fn bt_queue_free_addr() -> *mut c_void {
    fw::CANOPUS_FW_BT_QUEUE_FREE_CALLABLE as *mut c_void
}
pub unsafe fn bt_l2cap_owner() -> *mut c_void {
    unsafe { core::ptr::read_volatile(fw::canopus_fw_bt_l2cap_owner as *const *mut c_void) }
}
pub type BtGapTransportReceive = extern "C" fn(*mut core::ffi::c_void, *mut u8, i32) -> i32;
pub unsafe fn bt_gap_install_receive_hook(hook: BtGapTransportReceive) -> bool {
    let slot = fw::canopus_fw_gap_host_receive_slot as *mut u32;
    let replacement = hook as usize as u32;
    let current = unsafe { core::ptr::read_volatile(slot) };
    if current != replacement && current as usize != fw::CANOPUS_FW_GAP_HOST_STOCK_RECEIVE_CALLABLE
    {
        return false;
    }
    unsafe { core::ptr::write_volatile(slot, replacement) };
    unsafe { core::ptr::read_volatile(slot) == replacement }
}
pub unsafe fn bt_gap_stock_receive(state: *mut c_void, packet: *mut u8, length: i32) -> i32 {
    call!(
        CANOPUS_FW_GAP_HOST_STOCK_RECEIVE_CALLABLE,
        unsafe extern "C" fn(*mut c_void, *mut u8, i32) -> i32,
        state,
        packet,
        length
    )
}

pub struct SdpSourceRecord;

impl SdpSourceRecord {
    pub const SERVICE_NAME: &'static [u8] = b"Vela Audio Source\0";
    pub const SERVICE_UUID: u16 = 0x110A;
    pub const PROFILE_VERSION: u16 = 0x0103;

    pub const ATTRIBUTES: [(u16, &'static [u8]); 6] = [
        // ServiceClassIDList: AudioSource
        (0x0001, ALIGNED_SERVICE_CLASS.as_slice()),
        // ProtocolDescriptorList: L2CAP(0x0100) + AVDTP(0x0019, 0x0103)
        (0x0004, ALIGNED_PROTOCOL.as_slice()),
        // BrowseGroupList: PublicBrowseRoot
        (0x0005, ALIGNED_BROWSE.as_slice()),
        // BluetoothProfileDescriptorList: AVDTP 1.3
        (0x0009, ALIGNED_PROFILE.as_slice()),
        // ServiceName
        (0x0100, ALIGNED_SERVICE_NAME.as_slice()),
        // SupportedFeatures: streaming
        (0x0311, ALIGNED_FEATURES.as_slice()),
    ];
}

/// AVRCP Controller/Target SDP record advertised alongside the A2DP source.
pub struct SdpAvrcpControllerRecord;

impl SdpAvrcpControllerRecord {
    pub const SERVICE_NAME: &'static [u8] = b"Vela Media Controller\0";
    pub const SERVICE_UUID: u16 = 0x110E;
    pub const PROFILE_VERSION: u16 = 0x0106;

    pub const ATTRIBUTES: [(u16, &'static [u8]); 6] = [
        (0x0001, ALIGNED_AVRCP_SERVICE_CLASS.as_slice()),
        (0x0004, ALIGNED_AVRCP_PROTOCOL.as_slice()),
        (0x0005, ALIGNED_BROWSE.as_slice()),
        (0x0009, ALIGNED_AVRCP_PROFILE.as_slice()),
        (0x0100, ALIGNED_AVRCP_SERVICE_NAME.as_slice()),
        (0x0311, ALIGNED_AVRCP_FEATURES.as_slice()),
    ];
}

// Each SDP encoded value lives in a 4-byte-aligned static. With function
// sections merged (lean module link), separate byte arrays otherwise abut inside
// one `.rodata`, and a window spanning the tail of one value and the `0x25 0x0c`
// ServiceName header forms an accidental 0x0cXXXXXX word that the verifier
// (CAN-P1-011) flags as an unapproved embedded address. Alignment inserts zero
// padding between values, keeping every aligned 4-byte word safe.
#[repr(align(4))]
struct AlignedValue<const N: usize>([u8; N]);
impl<const N: usize> AlignedValue<N> {
    const fn as_slice(&self) -> &[u8] {
        &self.0
    }
}

const ALIGNED_SERVICE_CLASS: AlignedValue<5> = AlignedValue([0x35, 0x03, 0x19, 0x11, 0x0a]);
const ALIGNED_PROTOCOL: AlignedValue<18> = AlignedValue([
    0x35, 0x10, 0x35, 0x06, 0x19, 0x01, 0x00, 0x09, 0x00, 0x19, 0x35, 0x06, 0x19, 0x11, 0x0d, 0x09,
    0x01, 0x03,
]);
const ALIGNED_BROWSE: AlignedValue<5> = AlignedValue([0x35, 0x03, 0x19, 0x10, 0x02]);
const ALIGNED_PROFILE: AlignedValue<10> =
    AlignedValue([0x35, 0x08, 0x35, 0x06, 0x19, 0x11, 0x0d, 0x09, 0x01, 0x03]);
const ALIGNED_SERVICE_NAME: AlignedValue<14> = AlignedValue([
    0x25, 0x0c, b'A', b'u', b'd', b'i', b'o', b' ', b'S', b'o', b'u', b'r', b'c', b'e',
]);
const ALIGNED_FEATURES: AlignedValue<3> = AlignedValue([0x09, 0x00, 0x01]);
const ALIGNED_AVRCP_SERVICE_CLASS: AlignedValue<11> = AlignedValue([
    0x35, 0x09, 0x19, 0x11, 0x0e, 0x19, 0x11, 0x0f, 0x19, 0x11, 0x0c,
]);
const ALIGNED_AVRCP_PROTOCOL: AlignedValue<18> = AlignedValue([
    0x35, 0x10, 0x35, 0x06, 0x19, 0x01, 0x00, 0x09, 0x00, 0x17, 0x35, 0x06, 0x19, 0x00, 0x17, 0x09,
    0x01, 0x04,
]);
const ALIGNED_AVRCP_PROFILE: AlignedValue<10> =
    AlignedValue([0x35, 0x08, 0x35, 0x06, 0x19, 0x11, 0x0e, 0x09, 0x01, 0x06]);
const ALIGNED_AVRCP_SERVICE_NAME: AlignedValue<23> = AlignedValue([
    0x25, 0x15, b'V', b'e', b'l', b'a', b' ', b'M', b'e', b'd', b'i', b'a', b' ', b'C', b'o', b'n',
    b't', b'r', b'o', b'l', b'l', b'e', b'r',
]);
const ALIGNED_AVRCP_FEATURES: AlignedValue<3> = AlignedValue([0x09, 0x00, 0x01]);

pub unsafe fn sdp_builder_create(
    old_handle: u32,
    _uuid: u16,
    _version: u16,
    selector: u8,
    _name: *const u8,
) -> *mut c_void {
    // Caller supplies every encoded attribute before commit. The firmware's
    // list builder has a four-byte ownership prefix, followed by two words.
    if old_handle != 0 || selector != 0 {
        return core::ptr::null_mut();
    }
    let allocation = unsafe { bt_alloc(12) }.cast::<u8>();
    if allocation.is_null() {
        return core::ptr::null_mut();
    }
    unsafe {
        core::ptr::write_bytes(allocation, 0, 12);
        allocation.add(4).cast()
    }
}
pub unsafe fn sdp_set_raw_attribute(
    builder: *mut c_void,
    id: u16,
    prefix: u16,
    length: u16,
    value: *const c_void,
) -> *mut u8 {
    call!(
        CANOPUS_FW_SDP_SET_RAW_ATTRIBUTE_CALLABLE,
        unsafe extern "C" fn(*mut c_void, u16, u16, u16, *const c_void) -> *mut u8,
        builder,
        id,
        prefix,
        length,
        value
    )
}
pub unsafe fn sdp_commit(builder: *mut c_void) -> u32 {
    call!(
        CANOPUS_FW_SDP_COMMIT_CALLABLE,
        unsafe extern "C" fn(*mut c_void) -> u32,
        builder
    )
}
pub unsafe fn sdp_unregister(handle: u32) -> i32 {
    call!(
        CANOPUS_FW_SDP_UNREGISTER_CALLABLE,
        unsafe extern "C" fn(u32) -> i32,
        handle
    )
}

pub const UI_MAX_ROWS: usize = 25;
pub const UI_MAX_LABELS: usize = 6;
pub const ROW_STATUS: u8 = 1;
pub const ROW_ACTION: u8 = 2;
pub const ROW_SWITCH: u8 = 3;
pub const TRAILING_NONE: u8 = 0;
pub const TRAILING_SWITCH: u8 = 1;
pub const TRAILING_FORWARD: u8 = 3;
pub const ALIGN_TOP_MID: u32 = 2;
pub const ALIGN_OUT_BOTTOM_MID: u32 = 14;
pub const EVENT_ALL: u32 = 0;
pub const EVENT_CLICKED: u32 = 7;
pub const EVENT_VALUE_CHANGED: u32 = 30;
pub const CONTENT_TOP_OFFSET: i32 = 68;
pub const CONTENT_WIDTH: i32 = 212;
pub const CONTENT_HEIGHT: i32 = 444;
pub const ROW_GAP: i32 = 8;
pub const LV_STYLE_BG_OPA: u32 = 29;
pub const LV_STYLE_RADIUS: u32 = 44;
pub const LV_STYLE_CLIP_CORNER: u32 = 45;
pub const LV_STYLE_BORDER_WIDTH: u32 = 48;
pub const LV_STYLE_BORDER_OPA: u32 = 50;
pub const LV_STYLE_TEXT_OPA: u32 = 89;
pub const LV_STYLE_TEXT_ALIGN: u32 = 94;
pub const LV_OBJ_FLAG_CLICKABLE: u32 = 0x20;
pub const STYLE_MISANS_REGULAR_24: usize = fw::canopus_fw_style_misans_medium_24;
pub const STYLE_MISANS_DEMIBOLD_32: usize = fw::canopus_fw_style_misans_medium_28;
pub type LvxTimerCallback = extern "C" fn(*mut core::ffi::c_void);
pub type LvxEventCallback = extern "C" fn(*mut core::ffi::c_void);
pub unsafe fn lvx_timer_create(
    callback: LvxTimerCallback,
    period: u32,
    data: *mut c_void,
) -> *mut c_void {
    call!(
        CANOPUS_FW_LV_TIMER_CREATE_CALLABLE,
        unsafe extern "C" fn(LvxTimerCallback, u32, *mut c_void) -> *mut c_void,
        callback,
        period,
        data
    )
}
pub unsafe fn lvx_timer_delete(timer: *mut c_void) {
    call!(
        CANOPUS_FW_LV_TIMER_DEL_CALLABLE,
        unsafe extern "C" fn(*mut c_void) -> (),
        timer
    )
}
pub unsafe fn lvx_list_row_create(
    parent: *mut c_void,
    primary: *const u8,
    secondary: *const u8,
    trailing: u8,
) -> *mut c_void {
    let row = call!(
        CANOPUS_FW_LVX_LIST_ITEM_CREATE_CALLABLE,
        unsafe extern "C" fn(*mut c_void) -> *mut c_void,
        parent
    );
    if row.is_null() {
        return row;
    }
    call!(
        CANOPUS_FW_LV_OBJ_CLEAR_FLAG_CALLABLE,
        unsafe extern "C" fn(*mut c_void, u32),
        row,
        16
    );
    call!(
        CANOPUS_FW_LVX_LIST_ITEM_ADD_CONTENT_CALLABLE,
        unsafe extern "C" fn(*mut c_void, *const c_void, *const u8, *const u8, i32, u8, u8),
        row,
        core::ptr::null(),
        primary,
        secondary,
        0,
        0,
        trailing
    );
    row
}
pub unsafe fn lvx_list_row_update(
    row: *mut c_void,
    icon: *const u8,
    primary: *const u8,
    secondary: *const u8,
    value: i32,
    selected: u8,
) -> i32 {
    call!(
        CANOPUS_FW_LVX_LIST_ITEM_UPDATE_CALLABLE,
        unsafe extern "C" fn(*mut c_void, *const u8, *const u8, *const u8, i32, u8) -> (),
        row,
        icon,
        primary,
        secondary,
        value,
        selected
    );
    0
}
pub unsafe fn lvx_list_row_trailing(row: *mut c_void) -> *mut c_void {
    if row.is_null() {
        return row;
    }
    unsafe { row.cast::<u8>().add(0x54).cast::<*mut c_void>().read() }
}
pub unsafe fn lvx_image_create(parent: *mut c_void) -> *mut c_void {
    call!(
        CANOPUS_FW_LV_IMAGE_CREATE_CALLABLE,
        unsafe extern "C" fn(*mut c_void) -> *mut c_void,
        parent
    )
}
pub unsafe fn lvx_image_set_src(image: *mut c_void, source: *const c_void) {
    call!(
        CANOPUS_FW_LV_IMAGE_SET_SRC_CALLABLE,
        unsafe extern "C" fn(*mut c_void, *const c_void) -> (),
        image,
        source
    )
}
pub unsafe fn lvx_image_set_scale(image: *mut c_void, x: i32, y: i32) {
    call!(
        CANOPUS_FW_LVX_IMAGE_SET_SCALE_CALLABLE,
        unsafe extern "C" fn(*mut c_void, i32, i32) -> (),
        image,
        x,
        y
    )
}
pub unsafe fn lvx_bar_create(parent: *mut c_void) -> *mut c_void {
    call!(
        CANOPUS_FW_LV_BAR_CREATE_CALLABLE,
        unsafe extern "C" fn(*mut c_void) -> *mut c_void,
        parent
    )
}
pub unsafe fn lvx_bar_set_range(bar: *mut c_void, min: i32, max: i32) {
    call!(
        CANOPUS_FW_LV_BAR_SET_RANGE_CALLABLE,
        unsafe extern "C" fn(*mut c_void, i32, i32) -> (),
        bar,
        min,
        max
    )
}
pub unsafe fn lvx_bar_set_value(bar: *mut c_void, value: i32) {
    call!(
        CANOPUS_FW_LV_BAR_SET_VALUE_CALLABLE,
        unsafe extern "C" fn(*mut c_void, i32, u32) -> (),
        bar,
        value,
        0
    )
}
pub unsafe fn lvx_label_create(parent: *mut c_void) -> *mut c_void {
    call!(
        CANOPUS_FW_LVX_LABEL_CREATE_CALLABLE,
        unsafe extern "C" fn(*mut c_void) -> *mut c_void,
        parent
    )
}
pub unsafe fn lvx_label_set_text(label: *mut c_void, text: *const u8) {
    call!(
        CANOPUS_FW_LVX_LABEL_SET_TEXT_CALLABLE,
        unsafe extern "C" fn(*mut c_void, *const u8) -> (),
        label,
        text
    )
}
pub unsafe fn lvx_label_set_text_align_center(label: *mut c_void) {
    unsafe { lvx_object_set_local_style_u32(label, LV_STYLE_TEXT_ALIGN, 2, 0) }
}
// LVGL v9 long-mode value, with the .139 setter recovered at 0xc3b3f5c.
pub const LV_LABEL_LONG_SCROLL_CIRCULAR: u32 = 3;
pub unsafe fn lvx_label_set_long_mode(label: *mut c_void, mode: u32) {
    call!(
        CANOPUS_FW_LVX_LABEL_SET_LONG_MODE_CALLABLE,
        unsafe extern "C" fn(*mut c_void, u32) -> (),
        label,
        mode
    )
}
pub unsafe fn lvx_content_create(parent: *mut c_void) -> *mut c_void {
    let object = call!(
        CANOPUS_FW_LVX_OBJECT_CREATE_CALLABLE,
        unsafe extern "C" fn(*mut c_void) -> *mut c_void,
        parent
    );
    if !object.is_null() {
        call!(
            CANOPUS_FW_LV_OBJ_REMOVE_STYLE_ALL_CALLABLE,
            unsafe extern "C" fn(*mut c_void),
            object
        );
        unsafe { lvx_object_set_content_pad_bottom(object, 32, 0) };
    }
    object
}
pub unsafe fn lvx_object_set_size(object: *mut c_void, width: i32, height: i32) {
    call!(
        CANOPUS_FW_LVX_OBJECT_SET_SIZE_CALLABLE,
        unsafe extern "C" fn(*mut c_void, i32, i32) -> (),
        object,
        width,
        height
    )
}
pub unsafe fn lvx_object_align(object: *mut c_void, align: u32, x: i32, y: i32) {
    call!(
        CANOPUS_FW_LVX_OBJECT_ALIGN_CALLABLE,
        unsafe extern "C" fn(*mut c_void, u32, i32, i32) -> (),
        object,
        align,
        x,
        y
    )
}
pub unsafe fn lvx_object_set_content_pad_bottom(object: *mut c_void, value: i32, selector: u32) {
    call!(
        CANOPUS_FW_LVX_CONTENT_PAD_BOTTOM_CALLABLE,
        unsafe extern "C" fn(*mut c_void, i32, u32) -> (),
        object,
        value,
        selector
    )
}
pub unsafe fn lvx_object_move_to_index(object: *mut c_void, index: i32) {
    call!(
        CANOPUS_FW_LV_OBJ_MOVE_TO_INDEX_CALLABLE,
        unsafe extern "C" fn(*mut c_void, i32) -> (),
        object,
        index
    )
}
pub unsafe fn lvx_object_set_local_style_u32(
    object: *mut c_void,
    property: u32,
    value: u32,
    selector: u32,
) {
    call!(
        CANOPUS_FW_LV_OBJ_SET_LOCAL_STYLE_PROP_CALLABLE,
        unsafe extern "C" fn(*mut c_void, u32, u32, u32) -> (),
        object,
        property,
        value,
        selector
    )
}
pub unsafe fn lvx_object_set_background_opacity(object: *mut c_void, opacity: u32, selector: u32) {
    unsafe { lvx_object_set_local_style_u32(object, LV_STYLE_BG_OPA, opacity, selector) }
}
pub unsafe fn lvx_page_title_create(
    parent: *mut c_void,
    title: *const u8,
    mode: u32,
    callback: *const (),
    _data: *mut c_void,
) -> *mut c_void {
    call!(
        CANOPUS_FW_LVX_PAGE_TITLE_CREATE_CALLABLE,
        unsafe extern "C" fn(*mut c_void, *const u8, u32, *const ()) -> *mut c_void,
        parent,
        title,
        mode,
        callback
    )
}
pub unsafe fn lvx_style_apply(object: *mut c_void, style: *const c_void, a: u32, b: u32) -> i32 {
    call!(
        CANOPUS_FW_LVX_STYLE_APPLY_CALLABLE,
        unsafe extern "C" fn(*mut c_void, *const c_void, u32, u32) -> (),
        object,
        style,
        a,
        b
    );
    0
}
pub unsafe fn lvx_event_add(
    object: *mut c_void,
    callback: LvxEventCallback,
    event: u32,
    data: *mut c_void,
) {
    call!(
        CANOPUS_FW_LV_OBJ_ADD_EVENT_CB_CALLABLE,
        unsafe extern "C" fn(*mut c_void, LvxEventCallback, u32, *mut c_void) -> *mut c_void,
        object,
        callback,
        event,
        data
    );
}
pub unsafe fn lvx_event_get_user_data(event: *mut c_void) -> usize {
    if event.is_null() {
        return 0;
    }
    unsafe { event.cast::<u8>().add(12).cast::<u32>().read() as usize }
}
pub unsafe fn lvx_event_get_code(event: *mut c_void) -> u32 {
    if event.is_null() {
        return 0;
    }
    unsafe { event.cast::<u8>().add(8).cast::<u16>().read() as u32 }
}
pub unsafe fn lvx_object_add_flag(object: *mut c_void, flags: u32) {
    call!(
        CANOPUS_FW_LVX_OBJECT_ADD_FLAG_CALLABLE,
        unsafe extern "C" fn(*mut c_void, u32) -> (),
        object,
        flags
    )
}
pub unsafe fn lvx_set_hidden(object: *mut c_void, hidden: u32) {
    if hidden != 0 {
        unsafe { lvx_object_add_flag(object, 1) };
    } else {
        call!(
            CANOPUS_FW_LV_OBJ_CLEAR_FLAG_CALLABLE,
            unsafe extern "C" fn(*mut c_void, u32),
            object,
            1
        );
    }
}
pub unsafe fn lvx_align_to(object: *mut c_void, base: *mut c_void, align: u32, x: i32, y: i32) {
    call!(
        CANOPUS_FW_LV_OBJ_ALIGN_TO_CALLABLE,
        unsafe extern "C" fn(*mut c_void, *mut c_void, u32, i32, i32) -> (),
        object,
        base,
        align,
        x,
        y
    )
}

pub const O_RDONLY: i32 = 1;
pub const O_RDWR: i32 = 3;
pub unsafe fn nuttx_open(path: *const u8, flags: i32) -> i32 {
    call!(
        CANOPUS_FW_OPEN_CALLABLE,
        unsafe extern "C" fn(*const u8, i32) -> i32,
        path,
        flags
    )
}
pub unsafe fn nuttx_create(path: *const u8, flags: i32, mode: u32) -> i32 {
    call!(
        CANOPUS_FW_OPEN_CALLABLE,
        unsafe extern "C" fn(*const u8, i32, u32) -> i32,
        path,
        flags,
        mode
    )
}
pub unsafe fn nuttx_close(fd: i32) -> i32 {
    call!(
        CANOPUS_FW_CLOSE_CALLABLE,
        unsafe extern "C" fn(i32) -> i32,
        fd
    )
}
pub unsafe fn nuttx_read(fd: i32, buffer: *mut c_void, count: u32) -> i32 {
    call!(
        CANOPUS_FW_READ_CALLABLE,
        unsafe extern "C" fn(i32, *mut c_void, u32) -> i32,
        fd,
        buffer,
        count
    )
}
pub unsafe fn nuttx_write(fd: i32, buffer: *const c_void, count: u32) -> i32 {
    call!(
        CANOPUS_FW_WRITE_CALLABLE,
        unsafe extern "C" fn(i32, *const c_void, u32) -> i32,
        fd,
        buffer,
        count
    )
}
pub unsafe fn nuttx_lseek(fd: i32, offset: i64, whence: i32) -> i64 {
    call!(
        CANOPUS_FW_LSEEK_CALLABLE,
        unsafe extern "C" fn(i32, i64, i32) -> i64,
        fd,
        offset,
        whence
    )
}
pub unsafe fn nuttx_ioctl(fd: i32, command: u32, argument: usize) -> i32 {
    call!(
        CANOPUS_FW_IOCTL_CALLABLE,
        unsafe extern "C" fn(i32, u32, usize) -> i32,
        fd,
        command,
        argument
    )
}
pub unsafe fn get_errno() -> i32 {
    let p = call!(
        CANOPUS_FW_ERRNO_LOCATION_CALLABLE,
        unsafe extern "C" fn() -> *const i32
    );
    unsafe { p.read() }
}
pub unsafe fn nuttx_unlink(path: *const u8) -> i32 {
    call!(
        CANOPUS_FW_UNLINK_CALLABLE,
        unsafe extern "C" fn(*const u8) -> i32,
        path
    )
}
pub unsafe fn nuttx_rename(old: *const u8, new: *const u8) -> i32 {
    call!(
        CANOPUS_FW_RENAME_CALLABLE,
        unsafe extern "C" fn(*const u8, *const u8) -> i32,
        old,
        new
    )
}

pub unsafe fn canopus_fw_clock_gettime(clock: u32, time: *const stock_timespec_t) -> i32 {
    call!(
        CANOPUS_FW_CLOCK_GETTIME_CALLABLE,
        unsafe extern "C" fn(u32, *const stock_timespec_t) -> i32,
        clock,
        time
    )
}
pub unsafe fn canopus_fw_register_driver(
    path: *const u8,
    fops: *const c_void,
    _mode: u32,
    private: *mut c_void,
) -> i32 {
    call!(
        CANOPUS_FW_REGISTER_DRIVER_CALLABLE,
        unsafe extern "C" fn(*const u8, *const c_void, *mut c_void) -> i32,
        path,
        fops,
        private
    )
}
pub unsafe fn canopus_fw_unregister_driver(_: *const u8) -> i32 {
    ERR_UNSUPPORTED
}

pub unsafe fn app_lookup(id: u16) -> *mut c_void {
    call!(
        CANOPUS_FW_APP_LOOKUP_CALLABLE,
        unsafe extern "C" fn(u16) -> *mut c_void,
        id
    )
}
pub unsafe fn app_install(
    app: *const launcher_app_descriptor,
    pages: *const *mut firmware_page_descriptor,
    count: u32,
) -> i32 {
    call!(
        CANOPUS_FW_APP_INSTALL_CALLABLE,
        unsafe extern "C" fn(
            *const launcher_app_descriptor,
            *const *mut firmware_page_descriptor,
            u32,
        ) -> i32,
        app,
        pages,
        count
    )
}
pub unsafe fn launcher_add(id: u16) -> i32 {
    call!(
        CANOPUS_FW_APP_LAUNCHER_ADD_CALLABLE,
        unsafe extern "C" fn(u16) -> (),
        id
    );
    0
}
pub unsafe fn notification_insert(_: *const firmware_notification_message) -> i32 {
    ERR_UNSUPPORTED
}
pub unsafe fn activity_navigate(a: u32, b: u32, c: u32, d: u32) -> i32 {
    call!(
        CANOPUS_FW_PAGE_GOTO_CALLABLE,
        unsafe extern "C" fn(u32, u32, u32, u32) -> (),
        a,
        b,
        c,
        d
    );
    0
}
pub unsafe fn activity_finish(page: *mut firmware_page_descriptor) -> i32 {
    call!(
        CANOPUS_FW_PAGE_FINISH_CALLABLE,
        unsafe extern "C" fn(*mut firmware_page_descriptor) -> (),
        page
    );
    0
}
pub const CONN_MSG_TYPE_EVENT: u8 = 2;
pub const CONN_MSG_TYPE_DATA: u8 = 0x83;
pub const CONN_STATUS_CONNECTED: i32 = 5;
pub const CONN_STATUS_DISCONNECTED: i32 = 6;
pub const CONN_STATUS_UNINSTALLED: i32 = 7;
pub const CONN_STATUS_FAILED: i32 = 2;
pub const CONN_STATUS_CLOSED: i32 = 3;
pub const CONN_RECV_CB_OFFSET: usize = 4;
pub const INTERCONNECT_APK_PACKAGE: &[u8] = b"com.xiaomi.miwear.interconnect\0";
pub const THIRD_PARTY_PAYLOAD_CAPACITY: usize = 8192;
pub unsafe fn interconnect_loop() -> *mut core::ffi::c_void {
    core::ptr::null_mut()
}
pub unsafe fn interconnect_connect(
    _: *mut core::ffi::c_void,
    _: *mut core::ffi::c_void,
    _: *const u8,
    _: InterconnectRecvCb,
) -> i32 {
    ERR_UNSUPPORTED
}
pub unsafe fn interconnect_send(
    _: *mut core::ffi::c_void,
    _: *const u8,
    _: *const InterconnectConnMessage,
    _: InterconnectSendDone,
    _: *mut core::ffi::c_void,
) -> i32 {
    ERR_UNSUPPORTED
}
pub unsafe fn interconnect_close(_: *mut core::ffi::c_void) -> i32 {
    ERR_UNSUPPORTED
}
pub unsafe fn thirdparty_send_phone_message(_: *const u8, _: *const u8, _: u16) -> i32 {
    ERR_UNSUPPORTED
}
pub unsafe fn quickapp_register_app(_: u16, _: *const QuickAppInfo) -> i32 {
    ERR_UNSUPPORTED
}

// The .139 service inlines several device requests. Use its verified IPC
// envelope, including the full 708 bytes required by sendrecv.
unsafe fn adapter_request(command: u32, payload: &[u8]) -> i32 {
    let adapter = unsafe { bt_adapter_get_instance() };
    if adapter.is_null() || payload.len() > 648 {
        return 7;
    }
    let mut request = [0u32; 177];
    let bytes = unsafe { core::slice::from_raw_parts_mut(request.as_mut_ptr().cast::<u8>(), 708) };
    bytes[60..60 + payload.len()].copy_from_slice(payload);
    let result = call!(
        CANOPUS_FW_BT_SOCKET_CLIENT_SENDRECV_CALLABLE,
        unsafe extern "C" fn(*mut c_void, *mut u32, u32) -> i32,
        adapter,
        request.as_mut_ptr(),
        command
    );
    if result != 0 {
        result
    } else {
        (request[3] & 255) as i32
    }
}
unsafe fn device_request(command: u32, address: *const u8, transport: u32) -> i32 {
    if address.is_null() || transport > 1 {
        return 7;
    }
    let mut payload = [0u8; 8];
    unsafe { core::ptr::copy_nonoverlapping(address, payload.as_mut_ptr(), 6) };
    payload[6] = transport as u8;
    unsafe { adapter_request(command, &payload) }
}

pub const APP_PACKAGE_OFFSET: usize = 12;
#[cfg(target_pointer_width = "32")]
const _: () = {
    assert!(core::mem::size_of::<firmware_page_descriptor>() == 120);
    assert!(core::mem::size_of::<launcher_app_descriptor>() == 80);
    assert!(core::mem::offset_of!(firmware_page_descriptor, on_ui_destroy) == 100);
    assert!(core::mem::offset_of!(launcher_app_descriptor, package_name) == APP_PACKAGE_OFFSET);
};

/// Clear exact .139 flags (16 is SCROLLABLE, as in native settings rows).
pub unsafe fn lvx_object_clear_flag(object: *mut c_void, flags: u32) {
    call!(
        CANOPUS_FW_LV_OBJ_CLEAR_FLAG_CALLABLE,
        unsafe extern "C" fn(*mut c_void, u32),
        object,
        flags
    );
}
