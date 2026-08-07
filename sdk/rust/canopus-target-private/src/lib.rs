//! Target-private full-trust bindings for `xiaomi-band-10-pro-3.101.030`.
//!
//! These are **not** part of `canopus-target-generated` (the public, audited
//! per-target bindings). They expose recovered launcher, LVX, Bluetooth, L2CAP,
//! SDP, and timer calls that the generated public crate deliberately leaves
//! restricted or FORBIDDEN. They are valid only for firmware SHA-256
//! `f701a84f…dccd225b`, must never be called before
//! [`canopus_target_generated::canopus_identity_guard`] passes, and are the
//! source of every absolute address a module links. Future targets provide a
//! sibling crate with the same interface instead of editing this one.
//!
//! Every recovered callable address carries the Thumb bit. All functions are
//! `unsafe`; the module is responsible for state, ownership, and lifecycle
//! discipline.

#![no_std]
#![deny(unsafe_op_in_unsafe_fn)]
#![allow(non_snake_case)]
#![allow(clippy::missing_safety_doc)]

pub use canopus_target_generated::{
    canopus_identity_guard, firmware_notification_message, firmware_page_descriptor,
    launcher_app_descriptor,
};

// ---------------------------------------------------------------------------
// Adapter / discovery / bond
// ---------------------------------------------------------------------------

pub const ADAPTER_STATE_ON: i32 = 4;
pub const DISCOVERY_STOPPED: i32 = 0;
pub const BOND_STATE_NONE: u32 = 0;
pub const BOND_STATE_BONDED: u32 = 2;
pub const CLASSIC_TRANSPORT: u32 = 1;
pub const DISCOVERY_TIMEOUT_SECONDS: i32 = 20;

/// Stock adapter client callback table has 16 word slots.
pub const CALLBACK_WORDS: usize = 16;
pub const CALLBACK_ADAPTER_STATE: usize = 0;
pub const CALLBACK_DISCOVERY_STATE: usize = 1;
pub const CALLBACK_DISCOVERY_RESULT: usize = 2;
pub const CALLBACK_PAIR_REQUEST: usize = 5;
pub const CALLBACK_PAIR_DISPLAY: usize = 6;
pub const CALLBACK_BOND_STATE: usize = 9;

/// Discovery result header; a NUL-terminated name follows immediately after.
#[repr(C)]
#[derive(Copy, Clone, Debug, Default)]
pub struct DiscoveryResult {
    pub address: [u8; 6],
    pub rssi: i8,
    pub reserved: u8,
    pub class_of_device: u32,
}

/// Reads the bounded, NUL-terminated name trailing a discovery-result header.
///
/// # Safety
/// `result` must point at a firmware-owned discovery payload with at least
/// `capacity` readable bytes (header included).
pub unsafe fn discovery_name<'a>(result: *const DiscoveryResult, capacity: usize) -> &'a [u8] {
    let header = core::mem::size_of::<DiscoveryResult>();
    if result.is_null() || capacity < header {
        return &[];
    }
    let name = unsafe { (result.cast::<u8>()).add(header) };
    let mut length = 0usize;
    while length < capacity - header {
        if unsafe { *name.add(length) } == 0 {
            break;
        }
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

/// Recovered `adapter_get_instance` at 0x0CA286C8. Thumb 0x0CA286C9.
pub unsafe fn bt_adapter_get_instance() -> *mut core::ffi::c_void {
    type F = extern "C" fn() -> *mut core::ffi::c_void;
    let f: F = unsafe { core::mem::transmute(0x0CA286C9usize) };
    f()
}

/// Registers a persistent 16-word callback table. Returns a nonzero
/// registration handle on success, 0 on failure.
pub unsafe fn bt_adapter_register(adapter: *mut core::ffi::c_void, callbacks: *const u32) -> u32 {
    type F = extern "C" fn(*mut core::ffi::c_void, *const u32) -> i32;
    let f: F = unsafe { core::mem::transmute(0x0C398C25usize) };
    f(adapter, callbacks) as u32
}

/// Unregisters a callback registration. Returns 0 on failure, nonzero on
/// success.
pub unsafe fn bt_adapter_unregister(adapter: *mut core::ffi::c_void, registration: u32) -> i32 {
    type F = extern "C" fn(*mut core::ffi::c_void, u32) -> i32;
    let f: F = unsafe { core::mem::transmute(0x0C398C8Dusize) };
    f(adapter, registration)
}

/// Recovered `adapter_get_state` at 0x0C398D30. Thumb 0x0C398D31.
pub unsafe fn bt_adapter_get_state(adapter: *mut core::ffi::c_void) -> i32 {
    type F = extern "C" fn(*mut core::ffi::c_void) -> i32;
    let f: F = unsafe { core::mem::transmute(0x0C398D31usize) };
    f(adapter)
}

/// Starts discovery. Returns 0 on success, negative errno otherwise.
pub unsafe fn bt_discovery_start(adapter: *mut core::ffi::c_void, timeout: i32) -> i32 {
    type F = extern "C" fn(*mut core::ffi::c_void, i32) -> i32;
    let f: F = unsafe { core::mem::transmute(0x0C398D61usize) };
    f(adapter, timeout)
}

/// Stops discovery. Returns 0 on success, negative errno otherwise.
pub unsafe fn bt_discovery_stop(adapter: *mut core::ffi::c_void) -> i32 {
    type F = extern "C" fn(*mut core::ffi::c_void) -> i32;
    let f: F = unsafe { core::mem::transmute(0x0C398D8Dusize) };
    f(adapter)
}

pub unsafe fn bt_adapter_set_scan_mode(scan_mode: i32, bondable: i32) -> i32 {
    type F = extern "C" fn(i32, i32) -> i32;
    let f: F = unsafe { core::mem::transmute(0x0C39EFB9usize) };
    f(scan_mode, bondable)
}

pub unsafe fn bt_adapter_get_scan_mode() -> i32 {
    type F = extern "C" fn() -> i32;
    let f: F = unsafe { core::mem::transmute(0x0C39F021usize) };
    f()
}

/// Positive Pair Request reply. Returns 0 on success.
pub unsafe fn bt_pair_request_reply(
    adapter: *mut core::ffi::c_void,
    address: *const u8,
    accept: i32,
) -> i32 {
    type F = extern "C" fn(*mut core::ffi::c_void, *const u8, i32) -> i32;
    let f: F = unsafe { core::mem::transmute(0x0C39988Dusize) };
    f(adapter, address, accept)
}

/// Pair Display (numeric comparison) reply. Returns 0 on success.
pub unsafe fn bt_pair_display_reply(
    adapter: *mut core::ffi::c_void,
    address: *const u8,
    transport: i32,
    accept: i32,
) -> i32 {
    type F = extern "C" fn(*mut core::ffi::c_void, *const u8, i32, i32) -> i32;
    let f: F = unsafe { core::mem::transmute(0x0C3998C9usize) };
    f(adapter, address, transport, accept)
}

/// Reads the adapter bond-state bitmask for `address`.
pub unsafe fn bt_get_bond_state(address: *const u8) -> u32 {
    type F = extern "C" fn(*const u8) -> u32;
    let f: F = unsafe { core::mem::transmute(0x0C39F371usize) };
    f(address)
}

/// Reads the exact device-record pairing state for `address` on `transport`.
pub unsafe fn bt_get_pairing_state(address: *const u8, transport: u32) -> u32 {
    type F = extern "C" fn(*const u8, u32) -> u32;
    let f: F = unsafe { core::mem::transmute(0x0C39F9B1usize) };
    f(address, transport)
}

/// Submits a bond for `address` on `transport`. Returns 0 on success.
pub unsafe fn bt_create_bond(address: *const u8, transport: u32) -> i32 {
    type F = extern "C" fn(*const u8, u32) -> i32;
    let f: F = unsafe { core::mem::transmute(0x0C3A01A9usize) };
    f(address, transport)
}

pub unsafe fn bt_remove_bond(address: *const u8, transport: u32) -> i32 {
    type F = extern "C" fn(*const u8, u32) -> i32;
    let f: F = unsafe { core::mem::transmute(0x0C3A028Dusize) };
    f(address, transport)
}

/// `create_bond` returns 2 when the adapter lifecycle byte is not READY (4);
/// this is a retriable precondition, not a submission.
pub const CREATE_BOND_ADAPTER_NOT_READY: i32 = 2;

pub const CORE_BT_BIND_STATE_ADDRESS: usize = 0x20122D2C;
pub const CORE_BT_COMPANION_ADDRESS: usize = 0x20122D2E;
pub const CORE_BT_ADAPTER_ADDRESS: usize = 0x20122FC0;
pub const CORE_BT_CALLBACK_HANDLE_ADDRESS: usize = 0x20122FBC;
pub const CORE_BT_CALLBACK_TABLE: usize = 0x2CD1F930;
pub const CORE_BT_PAIR_REQUEST_CALLBACK: usize = 0x0C6E1E25;
pub const CORE_BT_BOUND_STATE: u8 = 1;
pub const CORE_BT_PAIR_REQUEST_SLOT: usize = 5;

pub unsafe fn core_bt_bind_state() -> u8 {
    unsafe { *(CORE_BT_BIND_STATE_ADDRESS as *const u8) }
}

pub unsafe fn core_bt_companion() -> *const u8 {
    CORE_BT_COMPANION_ADDRESS as *const u8
}

pub unsafe fn core_bt_adapter() -> *mut core::ffi::c_void {
    unsafe { *(CORE_BT_ADAPTER_ADDRESS as *const *mut core::ffi::c_void) }
}

pub unsafe fn core_bt_callback_handle() -> *mut u32 {
    CORE_BT_CALLBACK_HANDLE_ADDRESS as *mut u32
}

pub unsafe fn core_bt_callback_table() -> *const u32 {
    CORE_BT_CALLBACK_TABLE as *const u32
}

// ---------------------------------------------------------------------------
// L2CAP / buffers / allocator / timer / queue
// ---------------------------------------------------------------------------

/// Stock buffer prefix (12 bytes); media payload starts at `+4+offset`.
#[repr(C)]
#[derive(Copy, Clone, Debug, Default)]
pub struct StockBuffer {
    pub total: u16,
    pub offset: u16,
    pub route: u32,
    pub type_: u8,
    pub tag: u8,
}

/// # Safety
/// `buffer` must be a live stock buffer.
pub unsafe fn stock_buffer_payload_mut(buffer: *mut StockBuffer) -> *mut u8 {
    let base = buffer.cast::<u8>();
    let offset = unsafe { (*buffer).offset } as usize;
    unsafe { base.add(4 + offset) }
}

/// # Safety
/// `buffer` must be a live stock buffer.
pub unsafe fn stock_buffer_payload(buffer: *const StockBuffer) -> *const u8 {
    let base = buffer.cast::<u8>();
    let offset = unsafe { (*buffer).offset } as usize;
    unsafe { base.add(4 + offset) }
}

/// Stock L2CAP connect request is 68 bytes.
pub const CONNECT_REQUEST_SIZE: usize = 68;
pub const CONNECT_PSM_OFFSET: usize = 2;
pub const CONNECT_FLAGS_OFFSET: usize = 8;
pub const CONNECT_CALLBACK_OFFSET: usize = 12;
pub const CONNECT_ADDRESS_OFFSET: usize = 16;
pub const CONNECT_CONFIG_OFFSET: usize = 52;
pub const CONNECT_OPTIONS_OFFSET: usize = 54;

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

/// PSM for AVDTP signaling.
pub const AVDTP_SIGNALING_PSM: u16 = 0x0019;
pub const AVDTP_L2CAP_CONFIG: u16 = 0x0030;

pub unsafe fn bt_buffer_new(payload_length: u16, headroom: u16) -> *mut StockBuffer {
    type F = extern "C" fn(u16, u16) -> *mut StockBuffer;
    let f: F = unsafe { core::mem::transmute(0x0C7D294Dusize) };
    f(payload_length, headroom)
}

pub unsafe fn bt_l2cap_connect(request: *const core::ffi::c_void) -> i32 {
    type F = extern "C" fn(*const core::ffi::c_void) -> i32;
    let f: F = unsafe { core::mem::transmute(0x0C7ED49Dusize) };
    f(request)
}

pub unsafe fn bt_l2cap_disconnect(request: *const DisconnectRequest) -> i32 {
    type F = extern "C" fn(*const DisconnectRequest) -> i32;
    let f: F = unsafe { core::mem::transmute(0x0C7ED54Dusize) };
    f(request)
}

pub unsafe fn bt_l2cap_submit_cid(buffer: *mut StockBuffer, private_cid: u16) -> i32 {
    type F = extern "C" fn(*mut StockBuffer, u16) -> i32;
    let f: F = unsafe { core::mem::transmute(0x0C7ED579usize) };
    f(buffer, private_cid)
}

pub unsafe fn bt_alloc(size: u32) -> *mut core::ffi::c_void {
    type F = extern "C" fn(u32) -> *mut core::ffi::c_void;
    let f: F = unsafe { core::mem::transmute(0x0C828455usize) };
    f(size)
}

pub unsafe fn bt_free(allocation: *mut core::ffi::c_void) {
    type F = extern "C" fn(*mut core::ffi::c_void) -> i32;
    let f: F = unsafe { core::mem::transmute(0x0C828461usize) };
    f(allocation);
}

/// One-shot Bluetooth FSM timer. Returns a nonzero handle, 0 on failure.
pub unsafe fn bt_timer_add(
    owner: *mut core::ffi::c_void,
    delay_ms: u32,
    event: u8,
    run_callback: *mut core::ffi::c_void,
    argument: *mut core::ffi::c_void,
    tag: *const u8,
    flags: u32,
) -> u32 {
    type F = extern "C" fn(
        *mut core::ffi::c_void,
        u32,
        u8,
        *mut core::ffi::c_void,
        *mut core::ffi::c_void,
        *const u8,
        u32,
    ) -> u32;
    let f: F = unsafe { core::mem::transmute(0x0C7D2C01usize) };
    f(owner, delay_ms, event, run_callback, argument, tag, flags)
}

/// Cancels a timer; frees its argument.
pub unsafe fn bt_timer_cancel(handle: *mut u32) -> i32 {
    type F = extern "C" fn(*mut u32) -> i32;
    let f: F = unsafe { core::mem::transmute(0x0C7D2CCDusize) };
    f(handle)
}

pub type QueueWork = extern "C" fn(i32, i32, *mut core::ffi::c_void) -> i32;

/// Queues `run` on the Bluetooth owner; `cancel` owns the argument if the
/// queued work is cancelled. The return value is the inserted queue node, not
/// a status code; stock callers do not use it to determine success.
pub unsafe fn bt_queue_external(
    owner: *mut core::ffi::c_void,
    run: QueueWork,
    cancel: *mut core::ffi::c_void,
    argument: *mut core::ffi::c_void,
    event: u8,
) -> *mut core::ffi::c_void {
    type F = extern "C" fn(
        *mut core::ffi::c_void,
        QueueWork,
        *mut core::ffi::c_void,
        *mut core::ffi::c_void,
        u8,
    ) -> *mut core::ffi::c_void;
    let f: F = unsafe { core::mem::transmute(0x0C7D3319usize) };
    f(owner, run, cancel, argument, event)
}

/// Stock queued-work free callback (used as the `cancel` argument).
pub fn bt_queue_free_addr() -> *mut core::ffi::c_void {
    0x0C7D36D1usize as *mut core::ffi::c_void
}

/// Reads the L2CAP owner pointer (module load context) for timers and queues.
pub unsafe fn bt_l2cap_owner() -> *mut core::ffi::c_void {
    let slot = 0x20137B1Cusize as *const *mut core::ffi::c_void;
    unsafe { *slot }
}

/// Reads the HCI FSM owner pointer (reserved; unused by this module).
pub unsafe fn bt_hci_fsm_owner() -> *mut core::ffi::c_void {
    let slot = 0x20137B14usize as *const *mut core::ffi::c_void;
    unsafe { *slot }
}

// ---------------------------------------------------------------------------
// SDP
// ---------------------------------------------------------------------------

/// Recovers the AVDTP Source SDP record from the legacy source.
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

pub unsafe fn sdp_builder_create(
    old_handle: u32,
    service_uuid: u16,
    profile_version: u16,
    selector: u8,
    service_name: *const u8,
) -> *mut core::ffi::c_void {
    type F = extern "C" fn(u32, u16, u16, u8, *const u8) -> *mut core::ffi::c_void;
    let f: F = unsafe { core::mem::transmute(0x0C7F2015usize) };
    f(
        old_handle,
        service_uuid,
        profile_version,
        selector,
        service_name,
    )
}

pub unsafe fn sdp_set_raw_attribute(
    builder: *mut core::ffi::c_void,
    attribute_id: u16,
    preserved_prefix_length: u16,
    value_length: u16,
    encoded_value: *const core::ffi::c_void,
) -> *mut u8 {
    type F =
        extern "C" fn(*mut core::ffi::c_void, u16, u16, u16, *const core::ffi::c_void) -> *mut u8;
    let f: F = unsafe { core::mem::transmute(0x0C7EFBD5usize) };
    f(
        builder,
        attribute_id,
        preserved_prefix_length,
        value_length,
        encoded_value,
    )
}

pub unsafe fn sdp_commit(builder: *mut core::ffi::c_void) -> u32 {
    type F = extern "C" fn(*mut core::ffi::c_void) -> u32;
    let f: F = unsafe { core::mem::transmute(0x0C7EFFD9usize) };
    f(builder)
}

pub unsafe fn sdp_unregister(handle: u32) -> i32 {
    type F = extern "C" fn(u32) -> i32;
    let f: F = unsafe { core::mem::transmute(0x0C7F20C5usize) };
    f(handle)
}

// ---------------------------------------------------------------------------
// Native app / launcher / notification
// ---------------------------------------------------------------------------

pub unsafe fn app_lookup(app_id: u16) -> *mut core::ffi::c_void {
    type F = extern "C" fn(u16) -> *mut core::ffi::c_void;
    let f: F = unsafe { core::mem::transmute(0x0CA50FD5usize) };
    f(app_id)
}

pub unsafe fn app_install(
    descriptor: *const launcher_app_descriptor,
    pages: *const *mut firmware_page_descriptor,
    page_count: u32,
) -> i32 {
    type F = extern "C" fn(
        *const launcher_app_descriptor,
        *const *mut firmware_page_descriptor,
        u32,
    ) -> i32;
    let f: F = unsafe { core::mem::transmute(0x0CA519ADusize) };
    f(descriptor, pages, page_count)
}

pub unsafe fn launcher_add(app_id: u16) -> i32 {
    type F = extern "C" fn(u16) -> i32;
    let f: F = unsafe { core::mem::transmute(0x0C4F2BDDusize) };
    f(app_id)
}

pub unsafe fn notification_insert(message: *const firmware_notification_message) -> i32 {
    type F = extern "C" fn(*const firmware_notification_message) -> i32;
    let f: F = unsafe { core::mem::transmute(0x0CA81F11usize) };
    f(message)
}

// ---------------------------------------------------------------------------
// Activity / page navigation
// ---------------------------------------------------------------------------

pub unsafe fn activity_navigate(a: u32, b: u32, c: u32, d: u32) -> i32 {
    type F = extern "C" fn(u32, u32, u32, u32) -> i32;
    let f: F = unsafe { core::mem::transmute(0x0CA539F9usize) };
    f(a, b, c, d)
}

pub unsafe fn activity_finish(page: *mut firmware_page_descriptor) -> i32 {
    type F = extern "C" fn(*mut firmware_page_descriptor) -> i32;
    let f: F = unsafe { core::mem::transmute(0x0CA53089usize) };
    f(page)
}

// ---------------------------------------------------------------------------
// Stock LVX backend
// ---------------------------------------------------------------------------

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

pub unsafe fn lvx_list_row_create(
    parent: *mut core::ffi::c_void,
    primary: *const u8,
    secondary: *const u8,
    trailing: u8,
) -> *mut core::ffi::c_void {
    type F =
        extern "C" fn(*mut core::ffi::c_void, *const u8, *const u8, u8) -> *mut core::ffi::c_void;
    let f: F = unsafe { core::mem::transmute(0x0C52B235usize) };
    f(parent, primary, secondary, trailing)
}

pub unsafe fn lvx_list_row_update(
    row: *mut core::ffi::c_void,
    a: *const u8,
    primary: *const u8,
    secondary: *const u8,
    c: i32,
    selected: u8,
) -> i32 {
    type F = extern "C" fn(*mut core::ffi::c_void, *const u8, *const u8, *const u8, i32, u8) -> i32;
    let f: F = unsafe { core::mem::transmute(0x0C4A7BD1usize) };
    f(row, a, primary, secondary, c, selected)
}

pub unsafe fn lvx_list_row_trailing(row: *mut core::ffi::c_void) -> *mut core::ffi::c_void {
    type F = extern "C" fn(*mut core::ffi::c_void) -> *mut core::ffi::c_void;
    let f: F = unsafe { core::mem::transmute(0x0C4A7F2Dusize) };
    f(row)
}

pub unsafe fn lvx_label_create(parent: *mut core::ffi::c_void) -> *mut core::ffi::c_void {
    type F = extern "C" fn(*mut core::ffi::c_void) -> *mut core::ffi::c_void;
    let f: F = unsafe { core::mem::transmute(0x0C588339usize) };
    f(parent)
}

pub unsafe fn lvx_label_set_text(label: *mut core::ffi::c_void, text: *const u8) {
    type F = extern "C" fn(*mut core::ffi::c_void, *const u8);
    let f: F = unsafe { core::mem::transmute(0x0C588849usize) };
    f(label, text);
}

pub unsafe fn lvx_content_create(parent: *mut core::ffi::c_void) -> *mut core::ffi::c_void {
    type F = extern "C" fn(*mut core::ffi::c_void) -> *mut core::ffi::c_void;
    let f: F = unsafe { core::mem::transmute(0x0CA4E8E9usize) };
    f(parent)
}

pub unsafe fn lvx_object_set_size(object: *mut core::ffi::c_void, width: i32, height: i32) {
    type F = extern "C" fn(*mut core::ffi::c_void, i32, i32);
    let f: F = unsafe { core::mem::transmute(0x0C587EF9usize) };
    f(object, width, height);
}

pub unsafe fn lvx_object_align(object: *mut core::ffi::c_void, align: u32, x: i32, y: i32) {
    type F = extern "C" fn(*mut core::ffi::c_void, u32, i32, i32);
    let f: F = unsafe { core::mem::transmute(0x0C5880A9usize) };
    f(object, align, x, y);
}

pub type LvxEventCallback = extern "C" fn(*mut core::ffi::c_void);

pub unsafe fn lvx_page_title_create(
    parent: *mut core::ffi::c_void,
    title: *const u8,
    mode: u32,
    back_callback: LvxEventCallback,
    back_context: *mut core::ffi::c_void,
) -> *mut core::ffi::c_void {
    type F = extern "C" fn(
        *mut core::ffi::c_void,
        *const u8,
        u32,
        LvxEventCallback,
        *mut core::ffi::c_void,
    ) -> *mut core::ffi::c_void;
    let f: F = unsafe { core::mem::transmute(0x0C4A9991usize) };
    f(parent, title, mode, back_callback, back_context)
}

/// MiSans-Demibold at 32 px (stock theme object address).
pub const STYLE_MISANS_DEMIBOLD_32: usize = 0x2010A02C;

pub unsafe fn lvx_style_apply(
    object: *mut core::ffi::c_void,
    style: *const core::ffi::c_void,
    a: u32,
    b: u32,
) -> i32 {
    type F = extern "C" fn(*mut core::ffi::c_void, *const core::ffi::c_void, u32, u32) -> i32;
    let f: F = unsafe { core::mem::transmute(0x0C49EA99usize) };
    f(object, style, a, b)
}

pub unsafe fn lvx_event_add(
    object: *mut core::ffi::c_void,
    callback: LvxEventCallback,
    code: u32,
    cookie: *mut core::ffi::c_void,
) {
    type F = extern "C" fn(*mut core::ffi::c_void, LvxEventCallback, u32, *mut core::ffi::c_void);
    let f: F = unsafe { core::mem::transmute(0x0C5882B9usize) };
    f(object, callback, code, cookie);
}

pub unsafe fn lvx_event_get_user_data(event: *mut core::ffi::c_void) -> usize {
    type F = extern "C" fn(*mut core::ffi::c_void) -> usize;
    let f: F = unsafe { core::mem::transmute(0x0C588601usize) };
    f(event)
}

pub unsafe fn lvx_event_get_code(event: *mut core::ffi::c_void) -> u32 {
    type F = extern "C" fn(*mut core::ffi::c_void) -> u32;
    let f: F = unsafe { core::mem::transmute(0x0C5886D1usize) };
    f(event)
}

pub unsafe fn lvx_set_hidden(object: *mut core::ffi::c_void, hidden: u32) {
    type F = extern "C" fn(*mut core::ffi::c_void, u32);
    let f: F = unsafe { core::mem::transmute(0x0C588459usize) };
    f(object, hidden);
}

pub unsafe fn lvx_align_to(
    object: *mut core::ffi::c_void,
    target: *mut core::ffi::c_void,
    align: u32,
    x: i32,
    y: i32,
) {
    type F = extern "C" fn(*mut core::ffi::c_void, *mut core::ffi::c_void, u32, i32, i32);
    let f: F = unsafe { core::mem::transmute(0x0C588BE9usize) };
    f(object, target, align, x, y);
}

// ---------------------------------------------------------------------------
// NuttX file I/O (used by the module if it talks to /dev/canopus)
// ---------------------------------------------------------------------------

pub const O_RDWR: i32 = 3;

pub unsafe fn nuttx_open(path: *const u8, flags: i32) -> i32 {
    type F = extern "C" fn(*const u8, i32, ...) -> i32;
    let f: F = unsafe { core::mem::transmute(0x0C1C15B1usize) };
    f(path, flags)
}

pub unsafe fn nuttx_close(fd: i32) -> i32 {
    type F = extern "C" fn(i32) -> i32;
    let f: F = unsafe { core::mem::transmute(0x0C1AAB71usize) };
    f(fd)
}

pub unsafe fn nuttx_read(fd: i32, buffer: *mut core::ffi::c_void, count: u32) -> i32 {
    type F = extern "C" fn(i32, *mut core::ffi::c_void, u32) -> i32;
    let f: F = unsafe { core::mem::transmute(0x0C1C1E25usize) };
    f(fd, buffer, count)
}

pub unsafe fn nuttx_write(fd: i32, buffer: *const core::ffi::c_void, count: u32) -> i32 {
    type F = extern "C" fn(i32, *const core::ffi::c_void, u32) -> i32;
    let f: F = unsafe { core::mem::transmute(0x0C1C31C9usize) };
    f(fd, buffer, count)
}

// ---------------------------------------------------------------------------
// Compile-time ABI checks
// ---------------------------------------------------------------------------

const _: () = {
    assert!(core::mem::size_of::<DiscoveryResult>() == 12);
    assert!(core::mem::offset_of!(DiscoveryResult, rssi) == 6);
    assert!(core::mem::offset_of!(DiscoveryResult, class_of_device) == 8);
    assert!(core::mem::size_of::<StockBuffer>() == 12);
    assert!(core::mem::offset_of!(StockBuffer, route) == 4);
    assert!(core::mem::size_of::<DisconnectRequest>() == 4);
    assert!(core::mem::size_of::<MediaTimerToken>() == 4);
    assert!(CONNECT_CALLBACK_OFFSET + 4 <= CONNECT_REQUEST_SIZE);
    assert!(CONNECT_ADDRESS_OFFSET + 6 <= CONNECT_REQUEST_SIZE);
    assert!(CONNECT_OPTIONS_OFFSET + 2 <= CONNECT_REQUEST_SIZE);
};
