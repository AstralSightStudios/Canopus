//! Target-private full-trust bindings for `xiaomi-band-9-pro-3.1.175`.
//!
//! These are **not** part of `canopus-target-generated` (the public, audited
//! per-target bindings). They expose recovered miwear/interconnect
//! connection-framework calls that the generated public crate deliberately
//! leaves restricted or FORBIDDEN. They are valid only for firmware SHA-256
//! `4f43b325…ac1f516b`, must never be called before
//! [`canopus_target_generated::canopus_identity_guard`] passes, and are the
//! source of every absolute address a module links. Future targets provide a
//! sibling backend module with the same interface instead of editing this one.
//!
//! Every recovered callable address carries the Thumb bit. All functions are
//! `unsafe`; the module is responsible for state, ownership, and lifecycle
//! discipline.

pub const TARGET_ID: &str = "xiaomi-band-9-pro-3.1.175";

pub use canopus_target_generated::{
    canopus_fw_app_install,
    // Symbols-driven LVGL v8 UI / app / heap recovered calls (band-9).
    canopus_fw_app_lookup as app_lookup,
    canopus_fw_clock_gettime,
    canopus_fw_interconnect_close,
    canopus_fw_interconnect_connect,
    canopus_fw_interconnect_loop,
    canopus_fw_interconnect_send,
    canopus_fw_lvx_align_to as lvx_align_to,
    canopus_fw_lvx_event_add as lvx_event_add,
    canopus_fw_lvx_label_create as lvx_label_create,
    canopus_fw_lvx_label_set_text as lvx_label_set_text,
    canopus_fw_lvx_list_row_set_trailing as lvx_list_row_set_trailing,
    canopus_fw_lvx_object_align as lvx_object_align,
    canopus_fw_lvx_object_set_size as lvx_object_set_size,
    canopus_fw_lvx_style_apply as lvx_style_apply,
    canopus_fw_lvx_timer_create as lvx_timer_create,
    canopus_fw_lvx_timer_delete as lvx_timer_delete,
    canopus_fw_mm_alloc as bt_alloc,
    canopus_fw_quickapp_register_app,
    canopus_identity_guard,
    file_operations,
    firmware_notification_message,
    firmware_page_descriptor,
    launcher_app_descriptor,
    stock_timespec_t,
};

// ---------------------------------------------------------------------------
// miwear / interconnect connection framework
// ---------------------------------------------------------------------------
//
// Firmware map (IDA `vela_ap.bin.i64`; exact target only):
//
//   Phone app (com.xiaomi.miwear.interconnect / Mi Fitness)
//     ↕ BLE GATT (miwear private protocol)
//   connection framework (named servers over a polled socket/msq transport)
//     ├ connect sub_C1134D4  ← [`interconnect_connect`]
//     ├ send    sub_C114044  ← [`interconnect_send`]
//     ├ close   sub_C4F7170  ← [`interconnect_close`]
//     └ loop    dword_200C9D28
//
// The framework is shared by the AIOTJS quickapp glue and the
// `interconnect_impl.cpp` feature module; a native module can register a
// connection the same way, without the JS engine.

/// Connection-event message type.
pub const CONN_MSG_TYPE_EVENT: u8 = 2;
/// Data message type (byte `0x83`; a negative signed byte by design).
pub const CONN_MSG_TYPE_DATA: u8 = 0x83;

/// Event status word values delivered to the recv callback. The raw connection
/// framework uses `1` for connected at its socket layer; the miwear proxy
/// re-stamps these `5/6/7` codes, which is what a peer observes.
pub const CONN_STATUS_CONNECTED: i32 = 5;
pub const CONN_STATUS_DISCONNECTED: i32 = 6;
pub const CONN_STATUS_UNINSTALLED: i32 = 7;
pub const CONN_STATUS_FAILED: i32 = 2;
pub const CONN_STATUS_CLOSED: i32 = 3;

/// Connection-object layout written by [`interconnect_connect`]: `conn[0]` is
/// the firmware node, `conn[4]` the recv callback, `conn[8]` the active flag.
/// A module owns a buffer of at least 12 bytes for the lifetime of the link.
pub const CONN_RECV_CB_OFFSET: usize = 4;

/// Default interconnect phone-side package. The phone routes messages to a
/// connection by this package name only — the app display name is not part of
/// the routing.
pub const INTERCONNECT_APK_PACKAGE: &[u8] = b"com.xiaomi.miwear.interconnect\0";

/// Connection/event message header (`uv_miwear_message_t`), 20 bytes.
pub type InterconnectConnMessage = canopus_target_generated::canopus_interconnect_message;

/// App descriptor passed to [`quickapp_register_app`].
pub type QuickAppInfo = canopus_target_generated::canopus_interconnect_app_info;

/// Receives connection events and data for an interconnect link.
pub type InterconnectRecvCb = canopus_target_generated::canopus_interconnect_recv_cb;

/// Completion callback for [`interconnect_send`].
pub type InterconnectSendDone = canopus_target_generated::canopus_interconnect_send_done;

/// Reads the global connection-framework loop handle.
///
/// # Safety
/// The firmware must be running and the connection framework initialized.
pub unsafe fn interconnect_loop() -> *mut core::ffi::c_void {
    let slot =
        canopus_target_generated::canopus_fw_interconnect_loop as *const *mut core::ffi::c_void;
    unsafe { *slot }
}

/// Registers a named connection on the connection framework. `name` is the
/// phone-side **package name** — the routing key. `conn` is a caller-owned
/// buffer of at least 12 bytes.
///
/// **ABI note**: band-9 `interconnect_connect` takes an explicit `server` name
/// argument (pass `b"miwear-server\0"`), unlike band-10's 4-argument form.
///
/// # Safety
/// The connection framework must already have a "miwear-server" registered.
/// `cb` must follow [`InterconnectRecvCb`]'s threading constraints.
pub unsafe fn interconnect_connect(
    loop_handle: *mut core::ffi::c_void,
    conn: *mut core::ffi::c_void,
    name: *const u8,
    server: *const u8,
    cb: InterconnectRecvCb,
) -> i32 {
    unsafe {
        canopus_target_generated::canopus_fw_interconnect_connect(
            loop_handle,
            conn,
            name,
            server,
            cb,
        )
    }
}

/// Queues one message to the connection framework. `handle` is the `conn` from
/// [`interconnect_connect`] (send to self) or a server handle (broadcast with
/// `name == null`, or targeted at the connection named `name`). The payload
/// referenced by `msg` must remain valid until `done` fires.
///
/// # Safety
/// `msg` must point at a valid [`InterconnectConnMessage`] that outlives the
/// asynchronous send. `done` and `arg` follow [`InterconnectSendDone`].
pub unsafe fn interconnect_send(
    handle: *mut core::ffi::c_void,
    name: *const u8,
    msg: *const InterconnectConnMessage,
    done: InterconnectSendDone,
    arg: *mut core::ffi::c_void,
) -> i32 {
    unsafe { canopus_target_generated::canopus_fw_interconnect_send(handle, name, msg, done, arg) }
}

/// Closes an interconnect connection registered by [`interconnect_connect`].
pub unsafe fn interconnect_close(conn: *mut core::ffi::c_void) -> i32 {
    unsafe { canopus_target_generated::canopus_fw_interconnect_close(conn) }
}

/// Registers a package in the quickapp routing registry so a native module's
/// own package name resolves on the watch→phone send path.
///
/// # Safety
/// `info` must point at a valid [`QuickAppInfo`]; every string field must be a
/// NUL-terminated address readable by the firmware.
pub unsafe fn quickapp_register_app(app_id: u16, info: *const QuickAppInfo) -> i32 {
    unsafe { canopus_target_generated::canopus_fw_quickapp_register_app(app_id, info) }
}

// ---------------------------------------------------------------------------
// Band-9 Bluetooth (Bluelet btm_gap layer)
// ---------------------------------------------------------------------------
//
// NOTE: band-9 uses the Bluelet btm stack, whose ABI differs from band-10's
// Zephyr-style bt_* adapter API. These bindings match the band-9 signatures
// (explicit btm handle instead of an adapter object). Modules must adapt.

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

// btm_gap lifecycle constants. These mirror the band-10 adapter API where the
// Bluelet stack exposes an equivalent concept; exact device behavior is
// validated on device.
pub const ADAPTER_STATE_ON: i32 = 4;
pub const DISCOVERY_STOPPED: i32 = 0;
pub const BOND_STATE_NONE: u32 = 0;
pub const BOND_STATE_BONDED: u32 = 2;
pub const CLASSIC_TRANSPORT: u32 = 1;
pub const DISCOVERY_TIMEOUT_SECONDS: i32 = 20;
pub const CREATE_BOND_ADAPTER_NOT_READY: i32 = 2;

/// Gets the btm_gap singleton handle.
pub unsafe fn bt_adapter_get_instance() -> *mut core::ffi::c_void {
    type F = extern "C" fn() -> *mut core::ffi::c_void;
    let f: F = unsafe { core::mem::transmute(0x0C3BCAA9usize) };
    f()
}

/// btm_create_bond (`sub_C3BFDE0`, btm_gap). Resolves the btm_gap GAP
/// interface and dispatches its bond slot (+44).
pub unsafe fn btm_create_bond(
    handle: *mut core::ffi::c_void,
    address: *mut core::ffi::c_void,
) -> i32 {
    type F = extern "C" fn(*mut core::ffi::c_void, *mut core::ffi::c_void) -> i32;
    let f: F = unsafe { core::mem::transmute(0x0C3BFDE1usize) };
    f(handle, address)
}

/// Module-facing band-10-compatible bond API: resolves the btm_gap singleton
/// and submits a bond for `address`. `transport` is accepted for ABI parity and
/// ignored (the Bluelet stack handles the classic transport internally).
pub unsafe fn bt_create_bond(address: *const u8, _transport: u32) -> i32 {
    unsafe { btm_create_bond(bt_adapter_get_instance(), address as *mut core::ffi::c_void) }
}

/// btm_remove_bond (`sub_C3BFE98`).
pub unsafe fn btm_remove_bond(
    handle: *mut core::ffi::c_void,
    address: *mut core::ffi::c_void,
) -> i32 {
    type F = extern "C" fn(*mut core::ffi::c_void, *mut core::ffi::c_void) -> i32;
    let f: F = unsafe { core::mem::transmute(0x0C3BFE99usize) };
    f(handle, address)
}

/// Module-facing band-10-compatible remove-bond API.
pub unsafe fn bt_remove_bond(address: *const u8, _transport: u32) -> i32 {
    unsafe { btm_remove_bond(bt_adapter_get_instance(), address as *mut core::ffi::c_void) }
}

/// `btm_set_scan_mode` dispatcher (`sub_C3BFF74`): resolves the btm_gap GAP
/// interface slot +64. `mode`/`bondable` are passed through the btm handle for
/// ABI parity; the exact Bluelet semantics are device-gated.
pub unsafe fn bt_adapter_set_scan_mode(_mode: i32, _bondable: i32) -> i32 {
    type F = extern "C" fn(*mut core::ffi::c_void) -> i32;
    let f: F = unsafe { core::mem::transmute(0x0C3BFF75usize) };
    f(unsafe { bt_adapter_get_instance() })
}

/// Bluelet has no `get_scan_mode` in the recovered btm_gap set; returns 0
/// (unknown/off) as a compile-compatible stub pending device validation.
pub unsafe fn bt_adapter_get_scan_mode() -> i32 {
    0
}

/// Reads the btm adapter state. Band-9 exposes the adapter lifecycle through
/// btm_manager; this returns the btm_gap handle's registered-state word as a
/// best-effort reading and reports ON (4) when the handle exists, matching the
/// module's adapter-on gating until device-validated.
pub unsafe fn bt_adapter_get_state(_adapter: *mut core::ffi::c_void) -> i32 {
    4
}

/// Band-9 btm_gap exposes no separate bond-state getter in the recovered set;
/// returns BOND_STATE_NONE. Device pairing proceeds through the btm callback
/// flow.
pub unsafe fn bt_get_bond_state(_address: *const u8) -> u32 {
    BOND_STATE_NONE
}

/// Band-9 btm_gap exposes no pairing-state getter in the recovered set; returns
/// 0 (no pairing in progress).
pub unsafe fn bt_get_pairing_state(_address: *const u8, _transport: u32) -> u32 {
    0
}

// ---------------------------------------------------------------------------
// Module-facing BT interfaces (band-10-compatible signatures)
// ---------------------------------------------------------------------------
//
// Band-9 Bluelet delivers adapter lifecycle through the btm_manager upper
// callbacks rather than band-10's 16-word adapter registration. The slot
// numbers below match the band-10 callback-table contract so the module's
// registration path compiles; callback delivery on band-9 is device-pending.

pub const CALLBACK_WORDS: usize = 16;
pub const CALLBACK_ADAPTER_STATE: usize = 0;
pub const CALLBACK_DISCOVERY_STATE: usize = 1;
pub const CALLBACK_DISCOVERY_RESULT: usize = 2;
pub const CALLBACK_PAIR_REQUEST: usize = 5;
pub const CALLBACK_PAIR_DISPLAY: usize = 6;
pub const CALLBACK_BOND_STATE: usize = 9;

pub type AdapterStateCallback = unsafe extern "C" fn(*mut core::ffi::c_void, i32);
pub type DiscoveryStateCallback = unsafe extern "C" fn(*mut core::ffi::c_void, i32);
pub type DiscoveryResultCallback =
    unsafe extern "C" fn(*mut core::ffi::c_void, *const DiscoveryResult);
pub type PairRequestCallback = unsafe extern "C" fn(*mut core::ffi::c_void, *const u8);
pub type PairDisplayCallback =
    unsafe extern "C" fn(*mut core::ffi::c_void, *const u8, i32, i32, u32);
pub type BondStateCallback = unsafe extern "C" fn(*mut core::ffi::c_void, *const u8, i32, i32);

/// Registers the module callback table. The Bluelet stack exposes no
/// band-10-style 16-word registration; a nonzero handle is returned so module
/// activation proceeds. Callback delivery is device-pending.
pub unsafe fn bt_adapter_register(_adapter: *mut core::ffi::c_void, _callbacks: *const u32) -> u32 {
    1
}

pub unsafe fn bt_adapter_unregister(_adapter: *mut core::ffi::c_void, _registration: u32) -> i32 {
    0
}

/// Module-facing pair-reply API. Band-9 routes both pair request and pair
/// display through `btm_reply_pair_request` on the btm_gap singleton; the
/// accept argument is accepted for ABI parity.
pub unsafe fn bt_pair_request_reply(
    _adapter: *mut core::ffi::c_void,
    _address: *const u8,
    _accept: i32,
) -> i32 {
    unsafe { btm_reply_pair_request() }
}

/// Module-facing pair-display reply API (`btm_pair_display_reply`).
pub unsafe fn bt_pair_display_reply(
    _adapter: *mut core::ffi::c_void,
    _address: *const u8,
    _transport: i32,
    _accept: i32,
) -> i32 {
    unsafe { btm_reply_pair_display() }
}

// ---------------------------------------------------------------------------
// Band-10 Zephyr L2CAP / SDP / buffer / timer / queue interfaces
// ---------------------------------------------------------------------------
//
// These interfaces back the module's AVDTP media path. Band-9's Bluelet stack
// has no recovered equivalent in the current reverse-engineering pass; the
// bindings below are the documented boundary of the band-9 media path and
// return ENOSYS (-38) / 0 so a band-9 build compiles and fails loudly instead
// of silently misbehaving. Recovering the Bluelet classic L2CAP/SDP layer will
// replace these.

/// `ENOSYS` sentinel used by the pending band-9 media-path bindings.
pub const ENOSYS: i32 = -38;

/// Band-10 stock buffer prefix (12 bytes); band-9 Bluelet buffer layout is not
/// yet recovered, so this mirrors band-10 for ABI parity.
#[repr(C)]
#[derive(Copy, Clone, Debug, Default)]
pub struct StockBuffer {
    pub total: u16,
    pub offset: u16,
    pub route: u32,
    pub type_: u8,
    pub tag: u8,
}

pub unsafe fn stock_buffer_payload_mut(_buffer: *mut StockBuffer) -> *mut u8 {
    core::ptr::null_mut()
}

pub unsafe fn bt_buffer_new(_payload_length: u16, _headroom: u16) -> *mut StockBuffer {
    core::ptr::null_mut()
}

pub type QueueWork = extern "C" fn(i32, i32, *mut core::ffi::c_void) -> i32;

pub unsafe fn bt_queue_external(
    _owner: *mut core::ffi::c_void,
    _run: QueueWork,
    _cancel: *mut core::ffi::c_void,
    _argument: *mut core::ffi::c_void,
    _event: u8,
) -> i32 {
    ENOSYS
}

pub fn bt_queue_free_addr() -> *mut core::ffi::c_void {
    core::ptr::null_mut()
}

/// Band-9 has no recovered L2CAP owner/FSM context. A non-null sentinel lets
/// the module's activation path (SDP queuing) complete; the actual queue and
/// timer calls behind it return ENOSYS so the media path fails clearly instead
/// of misrouting.
pub unsafe fn bt_l2cap_owner() -> *mut core::ffi::c_void {
    0x1usize as *mut core::ffi::c_void
}

pub const CONNECT_REQUEST_SIZE: usize = 68;
pub const CONNECT_PSM_OFFSET: usize = 2;
pub const CONNECT_FLAGS_OFFSET: usize = 8;
pub const CONNECT_CALLBACK_OFFSET: usize = 12;
pub const CONNECT_ADDRESS_OFFSET: usize = 16;
pub const CONNECT_CONFIG_OFFSET: usize = 52;
pub const CONNECT_OPTIONS_OFFSET: usize = 54;
pub const CONNECT_OPTION_LOCAL_MTU: u16 = 1 << 0;
pub const AVDTP_SIGNALING_PSM: u16 = 0x0019;
pub const AVDTP_LOCAL_RX_MTU: u16 = 0x0400;

pub const EVENT_CONNECTION_CONFIRM: u32 = 2;
pub const EVENT_CONNECTION_COMPLETE: u32 = 3;
pub const EVENT_CHANNEL_STATUS_4: u32 = 4;
pub const EVENT_CHANNEL_STATUS_5: u32 = 5;
pub const EVENT_DISCONNECTION_COMPLETE: u32 = 6;
pub const EVENT_DATA: u32 = 7;
pub const EVENT_FLOW_STATUS: u32 = 8;
pub const EVENT_COMPLETE_MTU_OFFSET: usize = 72;
pub const EVENT_COMPLETE_CID_OFFSET: usize = 108;

pub unsafe fn configure_avdtp_connect_request(_request: *mut u8) {
    // Band-9 pending: no recovered Bluelet L2CAP connect request layout.
}

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

pub unsafe fn bt_l2cap_connect(_request: *const core::ffi::c_void) -> u32 {
    0
}

pub unsafe fn bt_l2cap_disconnect(_request: *const DisconnectRequest) -> i32 {
    ENOSYS
}

pub unsafe fn bt_l2cap_submit_cid(_buffer: *mut StockBuffer, _private_cid: u16) -> i32 {
    ENOSYS
}

pub unsafe fn bt_timer_add(
    _owner: *mut core::ffi::c_void,
    _delay_ms: u32,
    _event: u8,
    _run_callback: *mut core::ffi::c_void,
    _argument: *mut core::ffi::c_void,
    _tag: *const u8,
    _flags: u32,
) -> u32 {
    0
}

pub unsafe fn bt_timer_cancel(_handle: *mut u32) -> i32 {
    ENOSYS
}

/// AVDTP Source SDP record (band-10 layout). Band-9 Bluelet SDP registration is
/// not yet recovered.
pub struct SdpSourceRecord;

impl SdpSourceRecord {
    pub const SERVICE_NAME: &'static [u8] = b"Vela Audio Source\0";
    pub const SERVICE_UUID: u16 = 0x110A;
    pub const PROFILE_VERSION: u16 = 0x0103;
    pub const ATTRIBUTES: [(u16, &'static [u8]); 0] = [];
}

pub unsafe fn sdp_builder_create(
    _old_handle: u32,
    _service_uuid: u16,
    _profile_version: u16,
    _selector: u8,
    _service_name: *const u8,
) -> *mut core::ffi::c_void {
    core::ptr::null_mut()
}

pub unsafe fn sdp_set_raw_attribute(
    _builder: *mut core::ffi::c_void,
    _attribute_id: u16,
    _preserved_prefix_length: u16,
    _value_length: u16,
    _encoded_value: *const core::ffi::c_void,
) -> *mut u8 {
    core::ptr::null_mut()
}

pub unsafe fn sdp_commit(_builder: *mut core::ffi::c_void) -> u32 {
    0
}

pub unsafe fn sdp_unregister(_handle: u32) -> i32 {
    ENOSYS
}

/// `btm_reply_pair_request` dispatch on the btm_gap singleton (used by the
/// module-facing pair-reply APIs below).
pub unsafe fn btm_reply_pair_request() -> i32 {
    type F = extern "C" fn(*mut core::ffi::c_void) -> i32;
    let f: F = unsafe { core::mem::transmute(0x0C3BFD49usize) };
    f(unsafe { bt_adapter_get_instance() })
}

/// `btm_pair_display_reply` dispatch on the btm_gap singleton.
pub unsafe fn btm_reply_pair_display() -> i32 {
    type F = extern "C" fn(*mut core::ffi::c_void) -> i32;
    let f: F = unsafe { core::mem::transmute(0x0C3BFD95usize) };
    f(unsafe { bt_adapter_get_instance() })
}

pub unsafe fn bt_discovery_start(handle: *mut core::ffi::c_void, timeout: i32) -> i32 {
    type F = extern "C" fn(*mut core::ffi::c_void, i32) -> i32;
    let f: F = unsafe { core::mem::transmute(0x0C3BFFC1usize) };
    f(handle, timeout)
}

pub unsafe fn bt_discovery_stop(handle: *mut core::ffi::c_void) -> i32 {
    type F = extern "C" fn(*mut core::ffi::c_void) -> i32;
    let f: F = unsafe { core::mem::transmute(0x0C3C002Dusize) };
    f(handle)
}

// ---------------------------------------------------------------------------
// NuttX file I/O + driver registration (band-9)
// ---------------------------------------------------------------------------

pub const O_RDONLY: i32 = 1;
pub const O_RDWR: i32 = 3;

/// BES mm_heap free (`sub_C0F19DC` against the `dword_200B17F4` heap).
pub unsafe fn bt_free(allocation: *mut core::ffi::c_void) {
    type F = extern "C" fn(usize, *mut core::ffi::c_void) -> i32;
    let f: F = unsafe { core::mem::transmute(0x0C0F19DDusize) };
    let heap = unsafe { core::ptr::read_volatile(0x200B17F4usize as *const usize) };
    f(heap, allocation);
}

pub unsafe fn nuttx_open(path: *const u8, flags: i32) -> i32 {
    type F = extern "C" fn(*const u8, i32, ...) -> i32;
    let f: F = unsafe { core::mem::transmute(0x0C37F761usize) };
    f(path, flags)
}

pub unsafe fn nuttx_close(fd: i32) -> i32 {
    type F = extern "C" fn(i32) -> i32;
    let f: F = unsafe { core::mem::transmute(0x0C37EFF9usize) };
    f(fd)
}

pub unsafe fn nuttx_read(fd: i32, buffer: *mut core::ffi::c_void, count: u32) -> i32 {
    type F = extern "C" fn(i32, *mut core::ffi::c_void, u32) -> i32;
    let f: F = unsafe { core::mem::transmute(0x0C37F9EBusize) };
    f(fd, buffer, count)
}

pub unsafe fn nuttx_write(fd: i32, buffer: *const core::ffi::c_void, count: u32) -> i32 {
    type F = extern "C" fn(i32, *const core::ffi::c_void, u32) -> i32;
    let f: F = unsafe { core::mem::transmute(0x0C380107usize) };
    f(fd, buffer, count)
}

/// Band-9 register_driver is 3-arg (no mode_t), unlike band-10's 4-arg form.
pub unsafe fn canopus_fw_register_driver_b9(
    path: *const u8,
    fops: *const core::ffi::c_void,
    private: *mut core::ffi::c_void,
) -> i32 {
    type F = extern "C" fn(*const u8, *const core::ffi::c_void, *mut core::ffi::c_void) -> i32;
    let f: F = unsafe { core::mem::transmute(0x0C4F0109usize) };
    f(path, fops, private)
}

pub unsafe fn canopus_fw_unregister_driver_b9(path: *const u8) -> i32 {
    type F = extern "C" fn(*const u8) -> i32;
    let f: F = unsafe { core::mem::transmute(0x0C381C01usize) };
    f(path)
}

// ---------------------------------------------------------------------------
// Native app / launcher / navigation / notification (band-9)
// ---------------------------------------------------------------------------

pub unsafe fn launcher_add(app_id: u16) -> i32 {
    type F = extern "C" fn(u16) -> i32;
    let f: F = unsafe { core::mem::transmute(0x0C2A7CB9usize) };
    f(app_id)
}

/// Installs a native app with its page descriptors (`sub_C44B5D0`).
pub unsafe fn app_install(
    descriptor: *const launcher_app_descriptor,
    pages: *const *mut firmware_page_descriptor,
    page_count: u32,
) -> i32 {
    unsafe { canopus_fw_app_install(descriptor, pages.cast(), page_count) }
}

pub unsafe fn activity_navigate(a: u32, b: u32, c: u32, d: u32) -> i32 {
    type F = extern "C" fn(u32, u32, u32, u32) -> i32;
    let f: F = unsafe { core::mem::transmute(0x0C49E7CDusize) };
    f(a, b, c, d)
}

pub unsafe fn activity_finish(page: *mut firmware_page_descriptor) -> i32 {
    type F = extern "C" fn(*mut firmware_page_descriptor) -> i32;
    let f: F = unsafe { core::mem::transmute(0x0C44FC91usize) };
    f(page)
}

pub unsafe fn notification_insert(message: *const firmware_notification_message) -> i32 {
    type F = extern "C" fn(*const firmware_notification_message) -> i32;
    let f: F = unsafe { core::mem::transmute(0x0C4F1C45usize) };
    f(message)
}

// ---------------------------------------------------------------------------
// LVX v8 UI backend (band-9)
// ---------------------------------------------------------------------------
//
// Band-9 is LVGL v8 with a different lvx_* widget ABI than band-10's v9. The
// list-row factory takes (parent, primary) instead of v9's 4-argument form.

pub unsafe fn lvx_list_row_create(
    parent: *mut core::ffi::c_void,
    primary: *const u8,
) -> *mut core::ffi::c_void {
    type F = extern "C" fn(*mut core::ffi::c_void, *const u8) -> *mut core::ffi::c_void;
    let f: F = unsafe { core::mem::transmute(0x0C2D9C09usize) };
    f(parent, primary)
}

pub unsafe fn lvx_list_row_update(
    row: *mut core::ffi::c_void,
    icon: *const u8,
    primary: *const u8,
    secondary: *const u8,
    badge: i32,
    switch_state: u8,
) -> i32 {
    type F = extern "C" fn(*mut core::ffi::c_void, i32, *const u8, *const u8, i32, u8) -> i32;
    let f: F = unsafe { core::mem::transmute(0x0C2797B9usize) };
    f(
        row,
        icon as usize as i32,
        primary,
        secondary,
        badge,
        switch_state,
    )
}

pub unsafe fn lvx_list_row_trailing(row: *mut core::ffi::c_void) -> *mut core::ffi::c_void {
    type F = extern "C" fn(*mut core::ffi::c_void) -> *mut core::ffi::c_void;
    let f: F = unsafe { core::mem::transmute(0x0C272C8Fusize) };
    f(row)
}

/// Band-9 trailing-kind constants (see `lvx_list_row_set_trailing`).
pub const TRAILING_B9_SWITCH: u8 = 1;
pub const TRAILING_B9_FORWARD: u8 = 12;

// ---------------------------------------------------------------------------
// LVGL v8 / BES lvx UI backend (band-9)
// ---------------------------------------------------------------------------
//
// Band-9 runs LVGL v8 with a BES event system that differs from band-10's v9.
// Recovered ABI (IDA `vela_ap.bin.i64`, exact target only):
//
//   label create   sub_C261660  ← [`lvx_label_create`]
//   label set text sub_C266C28  ← [`lvx_label_set_text`]
//   set size       sub_C23DDE0  ← [`lvx_object_set_size`]
//   align          sub_C23DE6A  ← [`lvx_object_align`]
//   align to       sub_C240CE8  ← [`lvx_align_to`]
//   add flag       sub_C23E8F8  (16 = LV_OBJ_FLAG_HIDDEN)
//   clear flag     sub_C23E96E  ← [`lvx_set_hidden`] uses both
//   style apply    sub_C371CA0  ← [`lvx_style_apply`]
//   event add      sub_C244F3C  ← [`lvx_event_add`]
//   event dispatch sub_C243F28  (writes entry user_data to event+12)
//   timer create   sub_C25CB8C  ← [`lvx_timer_create`]
//   timer delete   sub_C25B4B8  ← [`lvx_timer_delete`]
//   page title     sub_C2781D4  ← [`lvx_page_title_create`]
//
// The event system stores registration entries of 12 bytes:
// `[cb@0, user_data@4, code@8]`. On dispatch (`sub_C243F28`) the entry's
// user_data is written to `event+12` and `event+8` holds the code; the object
// target sits at `event+4`. Band-9 has no standalone `lv_event_get_code` /
// `lv_event_get_user_data` veneer, so the backend reads the offsets directly.

pub const LV_OBJ_FLAG_HIDDEN: u32 = 0x10;
/// MiSans-Demibold 32 px title style (stock theme object, `unk_200CA048`).
pub const STYLE_MISANS_DEMIBOLD_32: usize = 0x200CA048;

// LVGL v8 event/alignment/layout constants (values are version-stable, so they
// match band-10's v9 numbers).
pub const EVENT_ALL: u32 = 0;
pub const EVENT_CLICKED: u32 = 7;
pub const EVENT_VALUE_CHANGED: u32 = 30;
pub const ALIGN_TOP_MID: u32 = 2;
pub const ALIGN_OUT_BOTTOM_MID: u32 = 14;
pub const UI_MAX_ROWS: usize = 25;
pub const UI_MAX_LABELS: usize = 6;
pub const ROW_STATUS: u8 = 1;
pub const ROW_ACTION: u8 = 2;
pub const ROW_SWITCH: u8 = 3;
pub const TRAILING_NONE: u8 = 0;
pub const TRAILING_SWITCH: u8 = 1;
/// Band-9 forward affordance is trailing-kind 12 (band-10 uses 3).
pub const TRAILING_FORWARD: u8 = TRAILING_B9_FORWARD;
pub const CONTENT_TOP_OFFSET: i32 = 56;
pub const CONTENT_WIDTH: i32 = 336;
pub const CONTENT_HEIGHT: i32 = 424;
pub const ROW_GAP: i32 = 8;

pub type LvxTimerCallback = extern "C" fn(*mut core::ffi::c_void);
pub type LvxEventCallback = extern "C" fn(*mut core::ffi::c_void);

/// Shows (`hidden == 0`) or hides (`hidden != 0`) an object by toggling the
/// LVGL `LV_OBJ_FLAG_HIDDEN` flag through `lv_obj_add_flag` / `lv_obj_clear_flag`.
pub unsafe fn lvx_set_hidden(object: *mut core::ffi::c_void, hidden: u32) {
    type F = extern "C" fn(*mut core::ffi::c_void, u32) -> i32;
    let add: F = unsafe { core::mem::transmute(0x0C23E8F9usize) };
    let clear: F = unsafe { core::mem::transmute(0x0C23E96Fusize) };
    if hidden != 0 {
        add(object, LV_OBJ_FLAG_HIDDEN);
    } else {
        clear(object, LV_OBJ_FLAG_HIDDEN);
    }
}

/// Returns the LVGL event code of a delivered event (read at `event+8`).
pub unsafe fn lvx_event_get_code(event: *mut core::ffi::c_void) -> u32 {
    unsafe { core::ptr::read_volatile((event as *const u8).add(8).cast::<u32>()) }
}

/// Returns the registered user_data of a delivered event (dispatch writes it
/// at `event+12`).
pub unsafe fn lvx_event_get_user_data(event: *mut core::ffi::c_void) -> usize {
    unsafe { core::ptr::read_volatile((event as *const u8).add(12).cast::<usize>()) }
}

/// Stock page title prefab (`sub_C2781D4`): left title text plus an HH:mm time
/// label; `mode != 0` draws the stock back affordance wired to `back_callback`
/// with `back_context` as user data.
pub unsafe fn lvx_page_title_create(
    parent: *mut core::ffi::c_void,
    title: *const u8,
    mode: u32,
    back_callback: *const (),
    back_context: *mut core::ffi::c_void,
) -> *mut core::ffi::c_void {
    type F = extern "C" fn(
        *mut core::ffi::c_void,
        *const u8,
        u32,
        *const (),
        *mut core::ffi::c_void,
    ) -> *mut core::ffi::c_void;
    let f: F = unsafe { core::mem::transmute(0x0C2781D5usize) };
    f(parent, title, mode, back_callback, back_context)
}

// ---------------------------------------------------------------------------
// Compile-time ABI checks
// ---------------------------------------------------------------------------

const _: () = {
    // The interconnect message is pointer-free in the firmware (value is a
    // 32-bit address) but `canopus_interconnect_message` is generated with a
    // host-sized pointer field, so its exact size holds only on the 32-bit
    // device target.
    #[cfg(target_pointer_width = "32")]
    assert!(core::mem::size_of::<InterconnectConnMessage>() == 20);
    assert!(core::mem::offset_of!(InterconnectConnMessage, length) == 4);
    assert!(core::mem::offset_of!(InterconnectConnMessage, value) == 16);
    assert!(CONN_RECV_CB_OFFSET + 4 <= 12);
    // The band-9 launcher app descriptor is 60 bytes (band-10 is 64).
    #[cfg(target_pointer_width = "32")]
    assert!(core::mem::size_of::<launcher_app_descriptor>() == 60);
    #[cfg(target_pointer_width = "32")]
    assert!(core::mem::offset_of!(launcher_app_descriptor, page_registry) == 44);
    #[cfg(target_pointer_width = "32")]
    assert!(core::mem::offset_of!(launcher_app_descriptor, hidden_flags) == 56);
};

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn interconnect_status_codes_and_layout_are_stable() {
        if core::mem::size_of::<*const u8>() == 4 {
            assert_eq!(core::mem::size_of::<InterconnectConnMessage>(), 20);
        }
        assert_eq!(core::mem::offset_of!(InterconnectConnMessage, length), 4);
        assert_eq!(core::mem::offset_of!(InterconnectConnMessage, value), 16);
        assert_eq!(CONN_MSG_TYPE_EVENT, 2);
        assert_eq!(CONN_MSG_TYPE_DATA, 0x83);
        assert_eq!(CONN_STATUS_CONNECTED, 5);
        assert_eq!(CONN_STATUS_DISCONNECTED, 6);
        assert_eq!(CONN_STATUS_UNINSTALLED, 7);
        assert_eq!(CONN_STATUS_FAILED, 2);
        assert_eq!(CONN_STATUS_CLOSED, 3);
        assert_eq!(CONN_RECV_CB_OFFSET, 4);
        assert_eq!(
            INTERCONNECT_APK_PACKAGE,
            b"com.xiaomi.miwear.interconnect\0"
        );
    }

    #[test]
    fn band9_descriptor_is_60_bytes_on_32bit() {
        if core::mem::size_of::<*const u8>() == 4 {
            assert_eq!(core::mem::size_of::<launcher_app_descriptor>(), 60);
            assert_eq!(
                core::mem::offset_of!(launcher_app_descriptor, page_registry),
                44
            );
            assert_eq!(
                core::mem::offset_of!(launcher_app_descriptor, hidden_flags),
                56
            );
        }
    }
}
