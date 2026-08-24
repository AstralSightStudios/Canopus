//! Compile-only target-private facade for exact packs whose ABI gates are pending.
//!
//! This backend intentionally contains no firmware addresses and does not call
//! `canopus-target-generated` candidate callables. It exists so module authors can
//! exercise the complete target-specific Rust/C/linker/verifier pipeline before
//! ABI, LVGL layout, and loader evidence are approved. Every firmware operation
//! fails closed; `canopus_identity_guard` also fails closed. Do not treat a
//! successful host or ELF build using this facade as device support.

#![allow(clippy::missing_safety_doc)]
#![allow(non_camel_case_types)]

#[cfg(feature = "target-xiaomi-band-9-pro-3-1-175")]
pub const TARGET_ID: &str = "xiaomi-band-9-pro-3.1.175";
#[cfg(feature = "target-xiaomi-band-11-4-100-108")]
pub const TARGET_ID: &str = "xiaomi-band-11-4.100.108";
#[cfg(feature = "target-xiaomi-band-9-3-1-32")]
pub const TARGET_ID: &str = "xiaomi-band-9-3.1.32";
pub const ERR_UNSUPPORTED: i32 = -38;

#[cfg(feature = "target-xiaomi-band-9-pro-3-1-175")]
pub const SELECTED_TARGET_ID: &str = "xiaomi-band-9-pro-3.1.175";
#[cfg(feature = "target-xiaomi-band-11-4-100-108")]
pub const SELECTED_TARGET_ID: &str = "xiaomi-band-11-4.100.108";
#[cfg(feature = "target-xiaomi-band-9-3-1-32")]
pub const SELECTED_TARGET_ID: &str = "xiaomi-band-9-3.1.32";

pub fn canopus_identity_guard() -> i32 {
    ERR_UNSUPPORTED
}

pub fn capabilities() -> &'static [&'static str] {
    &["identity-guard", "compile-only-static-candidate"]
}

#[repr(C, packed(4))]
#[derive(Copy, Clone, Debug)]
pub struct file_operations {
    pub open: *mut core::ffi::c_void,
    pub close: *mut core::ffi::c_void,
    pub read: *mut core::ffi::c_void,
    pub write: *mut core::ffi::c_void,
    pub lseek: *mut core::ffi::c_void,
    pub ioctl: *mut core::ffi::c_void,
    pub _tail: [u8; 0x18],
}

#[repr(C, packed(4))]
#[derive(Copy, Clone, Debug)]
pub struct stock_timespec_t {
    pub tv_sec: i64,
    pub tv_nsec: i32,
}

#[repr(C, packed(4))]
#[derive(Copy, Clone, Debug)]
pub struct launcher_app_descriptor {
    pub registry_links: u64,
    pub package_name: *mut core::ffi::c_void,
    pub launcher_icon_resource: *mut core::ffi::c_void,
    pub app_id: u16,
    pub flags: u8,
    pub _pad_13: [u8; 1],
    pub owned_string_20: *mut core::ffi::c_void,
    pub owned_string_24: *mut core::ffi::c_void,
    pub launcher_metadata_callback: *mut core::ffi::c_void,
    pub _pad_20: [u8; 0x10],
    pub page_registry: *mut core::ffi::c_void,
    pub _pad_34: [u8; 0x8],
    pub hidden_flags: u8,
    pub _tail: [u8; 3],
}

#[repr(C, packed(4))]
#[derive(Copy, Clone, Debug)]
pub struct firmware_page_descriptor {
    pub parent_descriptor: *mut core::ffi::c_void,
    pub _pad_4: [u8; 0xc],
    pub page_name: *mut core::ffi::c_void,
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
    pub _pad_2b: [u8; 1],
    pub activity_context: *mut core::ffi::c_void,
    pub root_object: *mut core::ffi::c_void,
    pub on_signal: *mut core::ffi::c_void,
    pub runtime_default_56: *mut core::ffi::c_void,
    pub _pad_3c: [u8; 4],
    pub registry_prev: *mut core::ffi::c_void,
    pub registry_next: *mut core::ffi::c_void,
    pub runtime_parent: *mut core::ffi::c_void,
    pub on_create: *mut core::ffi::c_void,
    pub on_resume: *mut core::ffi::c_void,
    pub on_foreground_data: *mut core::ffi::c_void,
    pub on_pause: *mut core::ffi::c_void,
    pub on_destroy: *mut core::ffi::c_void,
    pub on_ui_destroy: *mut core::ffi::c_void,
    pub extension_callback_100: *mut core::ffi::c_void,
    pub extension_callback_104: *mut core::ffi::c_void,
    pub extension_callback_108: *mut core::ffi::c_void,
    pub _tail: [u8; 4],
}

#[repr(C, packed(4))]
#[derive(Copy, Clone, Debug)]
pub struct firmware_notification_message {
    pub message_id: u64,
    pub repeat_count: u32,
    pub title: *mut core::ffi::c_void,
    pub source: *mut core::ffi::c_void,
    pub body: *mut core::ffi::c_void,
    pub auxiliary_text: *mut core::ffi::c_void,
    pub small_icon_path: *mut core::ffi::c_void,
    pub large_icon_path: *mut core::ffi::c_void,
    pub extension_text_36: *mut core::ffi::c_void,
    pub extension_text_40: *mut core::ffi::c_void,
    pub timestamp: u32,
    pub _pad_30: [u8; 8],
    pub action_callback: *mut core::ffi::c_void,
    pub action_context: u32,
    pub extension_64: u32,
    pub extension_68: u32,
    pub open_callback: *mut core::ffi::c_void,
    pub destroy_callback: *mut core::ffi::c_void,
    pub start_reminder: u8,
    pub flags_81: u8,
    pub flags_82: u8,
    pub _pad_53: [u8; 1],
    pub callback_data: *mut core::ffi::c_void,
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

pub unsafe fn bt_adapter_get_instance() -> *mut core::ffi::c_void {
    core::ptr::null_mut()
}
pub unsafe fn bt_adapter_register(_: *mut core::ffi::c_void, _: *const u32) -> u32 {
    0
}
pub unsafe fn bt_adapter_unregister(_: *mut core::ffi::c_void, _: u32) -> i32 {
    ERR_UNSUPPORTED
}
pub unsafe fn bt_adapter_get_state(_: *mut core::ffi::c_void) -> i32 {
    -1
}
pub unsafe fn bt_discovery_start(_: *mut core::ffi::c_void, _: i32) -> i32 {
    ERR_UNSUPPORTED
}
pub unsafe fn bt_discovery_stop(_: *mut core::ffi::c_void) -> i32 {
    ERR_UNSUPPORTED
}
pub unsafe fn bt_adapter_set_scan_mode(_: i32, _: i32) -> i32 {
    ERR_UNSUPPORTED
}
pub unsafe fn bt_adapter_get_scan_mode() -> i32 {
    ERR_UNSUPPORTED
}
pub unsafe fn bt_pair_request_reply(_: *mut core::ffi::c_void, _: *const u8, _: i32) -> i32 {
    ERR_UNSUPPORTED
}
pub unsafe fn bt_pair_display_reply(
    _: *mut core::ffi::c_void,
    _: *const u8,
    _: i32,
    _: i32,
) -> i32 {
    ERR_UNSUPPORTED
}
pub unsafe fn bt_get_bond_state(_: *const u8) -> u32 {
    BOND_STATE_NONE
}
pub unsafe fn bt_get_pairing_state(_: *const u8, _: u32) -> u32 {
    BOND_STATE_NONE
}
pub unsafe fn bt_create_bond(_: *const u8, _: u32) -> i32 {
    ERR_UNSUPPORTED
}
pub unsafe fn bt_remove_bond(_: *const u8, _: u32) -> i32 {
    ERR_UNSUPPORTED
}
pub unsafe fn bt_install_pair_request_filter(
    _: PairRequestCallback,
) -> Result<Option<PairRequestFilter>, PairRequestFilterError> {
    Err(PairRequestFilterError::Policy)
}
pub unsafe fn bt_forward_pair_request(_: *mut core::ffi::c_void, _: *const u8) -> i32 {
    ERR_UNSUPPORTED
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
pub const EVENT_COMPLETE_MTU_OFFSET: usize = 72;
pub const EVENT_COMPLETE_CID_OFFSET: usize = 108;

pub unsafe fn configure_avdtp_connect_request(_: *mut u8) {}
pub unsafe fn configure_avctp_connect_request(_: *mut u8) {}
pub unsafe fn bt_buffer_new(_: u16, _: u16) -> *mut StockBuffer {
    core::ptr::null_mut()
}
pub unsafe fn bt_l2cap_connect(_: *const core::ffi::c_void) -> u32 {
    0
}
pub unsafe fn bt_l2cap_disconnect(_: *const DisconnectRequest) -> i32 {
    ERR_UNSUPPORTED
}
pub unsafe fn bt_l2cap_submit_cid(_: *mut StockBuffer, _: u16) -> i32 {
    ERR_UNSUPPORTED
}
pub unsafe fn bt_alloc(_: u32) -> *mut core::ffi::c_void {
    core::ptr::null_mut()
}
pub unsafe fn bt_free(_: *mut core::ffi::c_void) {}
pub unsafe fn bt_timer_add(
    _: *mut core::ffi::c_void,
    _: u32,
    _: u8,
    _: *mut core::ffi::c_void,
    _: *mut core::ffi::c_void,
    _: *const u8,
    _: u32,
) -> u32 {
    0
}
pub unsafe fn bt_timer_cancel(_: *mut u32) -> i32 {
    ERR_UNSUPPORTED
}
pub type QueueWork = extern "C" fn(i32, i32, *mut core::ffi::c_void) -> i32;
pub unsafe fn bt_queue_external(
    _: *mut core::ffi::c_void,
    _: QueueWork,
    _: *mut core::ffi::c_void,
    _: *mut core::ffi::c_void,
    _: u8,
) -> i32 {
    ERR_UNSUPPORTED
}
pub fn bt_queue_free_addr() -> *mut core::ffi::c_void {
    core::ptr::null_mut()
}
pub unsafe fn bt_l2cap_owner() -> *mut core::ffi::c_void {
    core::ptr::null_mut()
}
pub type BtGapTransportReceive = extern "C" fn(*mut core::ffi::c_void, *mut u8, i32) -> i32;
pub unsafe fn bt_gap_install_receive_hook(_: BtGapTransportReceive) -> bool {
    false
}
pub unsafe fn bt_gap_stock_receive(_: *mut core::ffi::c_void, _: *mut u8, _: i32) -> i32 {
    ERR_UNSUPPORTED
}

pub struct SdpSourceRecord;
impl SdpSourceRecord {
    pub const SERVICE_NAME: &'static [u8] = b"Vela Audio Source\0";
    pub const SERVICE_UUID: u16 = 0x110A;
    pub const PROFILE_VERSION: u16 = 0x0103;
    pub const ATTRIBUTES: [(u16, &'static [u8]); 6] = [
        (0x0001, &[0x35, 0x03, 0x19, 0x11, 0x0A]),
        (
            0x0004,
            &[
                0x35, 0x10, 0x35, 0x06, 0x19, 0x01, 0x00, 0x09, 0x00, 0x19, 0x35, 0x06, 0x19, 0x11,
                0x0D, 0x09, 0x01, 0x03,
            ],
        ),
        (0x0005, &[0x35, 0x03, 0x19, 0x10, 0x02]),
        (
            0x0009,
            &[0x35, 0x08, 0x35, 0x06, 0x19, 0x11, 0x0D, 0x09, 0x01, 0x03],
        ),
        (
            0x0100,
            &[
                0x25, 0x0C, b'A', b'u', b'd', b'i', b'o', b' ', b'S', b'o', b'u', b'r', b'c', b'e',
            ],
        ),
        (0x0311, &[0x09, 0x00, 0x01]),
    ];
}
pub struct SdpAvrcpControllerRecord;
impl SdpAvrcpControllerRecord {
    pub const SERVICE_NAME: &'static [u8] = b"Vela Media Controller\0";
    pub const SERVICE_UUID: u16 = 0x110E;
    pub const PROFILE_VERSION: u16 = 0x0106;
    pub const ATTRIBUTES: [(u16, &'static [u8]); 6] = [
        (
            0x0001,
            &[
                0x35, 0x09, 0x19, 0x11, 0x0E, 0x19, 0x11, 0x0F, 0x19, 0x11, 0x0C,
            ],
        ),
        (
            0x0004,
            &[
                0x35, 0x10, 0x35, 0x06, 0x19, 0x01, 0x00, 0x09, 0x00, 0x17, 0x35, 0x06, 0x19, 0x00,
                0x17, 0x09, 0x01, 0x04,
            ],
        ),
        (0x0005, &[0x35, 0x03, 0x19, 0x10, 0x02]),
        (
            0x0009,
            &[0x35, 0x08, 0x35, 0x06, 0x19, 0x11, 0x0E, 0x09, 0x01, 0x06],
        ),
        (
            0x0100,
            &[
                0x25, 0x15, b'V', b'e', b'l', b'a', b' ', b'M', b'e', b'd', b'i', b'a', b' ', b'C',
                b'o', b'n', b't', b'r', b'o', b'l', b'l', b'e', b'r',
            ],
        ),
        (0x0311, &[0x09, 0x00, 0x01]),
    ];
}
pub unsafe fn sdp_builder_create(
    _: u32,
    _: u16,
    _: u16,
    _: u8,
    _: *const u8,
) -> *mut core::ffi::c_void {
    core::ptr::null_mut()
}
pub unsafe fn sdp_set_raw_attribute(
    _: *mut core::ffi::c_void,
    _: u16,
    _: u16,
    _: u16,
    _: *const core::ffi::c_void,
) -> *mut u8 {
    core::ptr::null_mut()
}
pub unsafe fn sdp_commit(_: *mut core::ffi::c_void) -> u32 {
    0
}
pub unsafe fn sdp_unregister(_: u32) -> i32 {
    ERR_UNSUPPORTED
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
pub const CONTENT_TOP_OFFSET: i32 = 56;
pub const CONTENT_WIDTH: i32 = 336;
pub const CONTENT_HEIGHT: i32 = 424;
pub const ROW_GAP: i32 = 8;
pub const LV_STYLE_BG_OPA: u32 = 29;
pub const LV_STYLE_RADIUS: u32 = 44;
pub const LV_STYLE_CLIP_CORNER: u32 = 45;
pub const LV_STYLE_BORDER_WIDTH: u32 = 48;
pub const LV_STYLE_BORDER_OPA: u32 = 50;
pub const LV_STYLE_TEXT_OPA: u32 = 89;
pub const LV_STYLE_TEXT_ALIGN: u32 = 94;
pub const LV_OBJ_FLAG_CLICKABLE: u32 = 0x20;
pub const STYLE_MISANS_REGULAR_24: usize = 0;
pub const STYLE_MISANS_DEMIBOLD_32: usize = 0;
pub type LvxTimerCallback = extern "C" fn(*mut core::ffi::c_void);
pub type LvxEventCallback = extern "C" fn(*mut core::ffi::c_void);
pub unsafe fn lvx_timer_create(
    _: LvxTimerCallback,
    _: u32,
    _: *mut core::ffi::c_void,
) -> *mut core::ffi::c_void {
    core::ptr::null_mut()
}
pub unsafe fn lvx_timer_delete(_: *mut core::ffi::c_void) {}
pub unsafe fn lvx_list_row_create(
    _: *mut core::ffi::c_void,
    _: *const u8,
    _: *const u8,
    _: u8,
) -> *mut core::ffi::c_void {
    core::ptr::null_mut()
}
pub unsafe fn lvx_list_row_update(
    _: *mut core::ffi::c_void,
    _: *const u8,
    _: *const u8,
    _: *const u8,
    _: i32,
    _: u8,
) -> i32 {
    ERR_UNSUPPORTED
}
pub unsafe fn lvx_list_row_trailing(_: *mut core::ffi::c_void) -> *mut core::ffi::c_void {
    core::ptr::null_mut()
}
pub unsafe fn lvx_image_create(_: *mut core::ffi::c_void) -> *mut core::ffi::c_void {
    core::ptr::null_mut()
}
pub unsafe fn lvx_image_set_src(_: *mut core::ffi::c_void, _: *const core::ffi::c_void) {}
pub unsafe fn lvx_image_set_scale(_: *mut core::ffi::c_void, _: i32, _: i32) {}
pub unsafe fn lvx_bar_create(_: *mut core::ffi::c_void) -> *mut core::ffi::c_void {
    core::ptr::null_mut()
}
pub unsafe fn lvx_bar_set_range(_: *mut core::ffi::c_void, _: i32, _: i32) {}
pub unsafe fn lvx_bar_set_value(_: *mut core::ffi::c_void, _: i32) {}
pub unsafe fn lvx_label_create(_: *mut core::ffi::c_void) -> *mut core::ffi::c_void {
    core::ptr::null_mut()
}
pub unsafe fn lvx_label_set_text(_: *mut core::ffi::c_void, _: *const u8) {}
pub unsafe fn lvx_label_set_text_align_center(_: *mut core::ffi::c_void) {}
pub unsafe fn lvx_content_create(_: *mut core::ffi::c_void) -> *mut core::ffi::c_void {
    core::ptr::null_mut()
}
pub unsafe fn lvx_object_set_size(_: *mut core::ffi::c_void, _: i32, _: i32) {}
pub unsafe fn lvx_object_align(_: *mut core::ffi::c_void, _: u32, _: i32, _: i32) {}
pub unsafe fn lvx_object_set_content_pad_bottom(_: *mut core::ffi::c_void, _: i32, _: u32) {}
pub unsafe fn lvx_object_move_to_index(_: *mut core::ffi::c_void, _: i32) {}
pub unsafe fn lvx_object_set_local_style_u32(_: *mut core::ffi::c_void, _: u32, _: u32, _: u32) {}
pub unsafe fn lvx_object_set_background_opacity(_: *mut core::ffi::c_void, _: u32, _: u32) {}
pub unsafe fn lvx_page_title_create(
    _: *mut core::ffi::c_void,
    _: *const u8,
    _: u32,
    _: *const (),
    _: *mut core::ffi::c_void,
) -> *mut core::ffi::c_void {
    core::ptr::null_mut()
}
pub unsafe fn lvx_style_apply(
    _: *mut core::ffi::c_void,
    _: *const core::ffi::c_void,
    _: u32,
    _: u32,
) -> i32 {
    ERR_UNSUPPORTED
}
pub unsafe fn lvx_event_add(
    _: *mut core::ffi::c_void,
    _: LvxEventCallback,
    _: u32,
    _: *mut core::ffi::c_void,
) {
}
pub unsafe fn lvx_event_get_user_data(_: *mut core::ffi::c_void) -> usize {
    0
}
pub unsafe fn lvx_event_get_code(_: *mut core::ffi::c_void) -> u32 {
    0
}
pub unsafe fn lvx_object_add_flag(_: *mut core::ffi::c_void, _: u32) {}
pub unsafe fn lvx_set_hidden(_: *mut core::ffi::c_void, _: u32) {}
pub unsafe fn lvx_align_to(
    _: *mut core::ffi::c_void,
    _: *mut core::ffi::c_void,
    _: u32,
    _: i32,
    _: i32,
) {
}

pub const O_RDONLY: i32 = 1;
pub const O_RDWR: i32 = 3;
pub unsafe fn nuttx_open(_: *const u8, _: i32) -> i32 {
    ERR_UNSUPPORTED
}
pub unsafe fn nuttx_create(_: *const u8, _: i32, _: u32) -> i32 {
    ERR_UNSUPPORTED
}
pub unsafe fn nuttx_close(_: i32) -> i32 {
    ERR_UNSUPPORTED
}
pub unsafe fn nuttx_read(_: i32, _: *mut core::ffi::c_void, _: u32) -> i32 {
    ERR_UNSUPPORTED
}
pub unsafe fn nuttx_write(_: i32, _: *const core::ffi::c_void, _: u32) -> i32 {
    ERR_UNSUPPORTED
}
pub unsafe fn nuttx_lseek(_: i32, _: i64, _: i32) -> i64 {
    -1
}
pub unsafe fn nuttx_ioctl(_: i32, _: u32, _: usize) -> i32 {
    ERR_UNSUPPORTED
}
pub unsafe fn get_errno() -> i32 {
    -ERR_UNSUPPORTED
}
pub unsafe fn nuttx_unlink(_: *const u8) -> i32 {
    ERR_UNSUPPORTED
}
pub unsafe fn nuttx_rename(_: *const u8, _: *const u8) -> i32 {
    ERR_UNSUPPORTED
}

pub unsafe fn canopus_fw_clock_gettime(_: u32, _: *const stock_timespec_t) -> i32 {
    ERR_UNSUPPORTED
}
pub unsafe fn canopus_fw_register_driver(
    _: *const u8,
    _: *const core::ffi::c_void,
    _: u32,
    _: *mut core::ffi::c_void,
) -> i32 {
    ERR_UNSUPPORTED
}
pub unsafe fn canopus_fw_unregister_driver(_: *const u8) -> i32 {
    ERR_UNSUPPORTED
}

pub unsafe fn app_lookup(_: u16) -> *mut core::ffi::c_void {
    core::ptr::null_mut()
}
pub unsafe fn app_install(
    _: *const launcher_app_descriptor,
    _: *const *mut firmware_page_descriptor,
    _: u32,
) -> i32 {
    ERR_UNSUPPORTED
}
pub unsafe fn launcher_add(_: u16) -> i32 {
    ERR_UNSUPPORTED
}
pub unsafe fn notification_insert(_: *const firmware_notification_message) -> i32 {
    ERR_UNSUPPORTED
}
pub unsafe fn activity_navigate(_: u32, _: u32, _: u32, _: u32) -> i32 {
    ERR_UNSUPPORTED
}
pub unsafe fn activity_finish(_: *mut firmware_page_descriptor) -> i32 {
    ERR_UNSUPPORTED
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

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn candidate_backend_is_fail_closed() {
        assert_ne!(canopus_identity_guard(), 0);
        assert!(capabilities().contains(&"compile-only-static-candidate"));
        assert_eq!(unsafe { bt_adapter_get_instance() }, core::ptr::null_mut());
    }
}
