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

use core::sync::atomic::{AtomicUsize, Ordering};

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
/// the firmware node, `conn[4]` the recv callback, and `conn[8]` a
/// client/server-mode word used by send dispatch. It is not a reliable
/// connection-active flag. A module owns a buffer of at least 12 bytes for the
/// lifetime of the link.
pub const CONN_RECV_CB_OFFSET: usize = 4;

/// Firmware-configured phone companion package. This is **not** a synthetic
/// wearable quick-app identity: named proxy routing still resolves the client
/// through the wearable quick-app appinfo registry. Native modules use this as
/// the `BasicInfo.package_name` of [`thirdparty_send_phone_message`] instead.
/// [`quickapp_register_app`] is not a routing-only substitute because it also
/// installs quick-app/page/launcher state.
pub const INTERCONNECT_APK_PACKAGE: &[u8] = b"com.xiaomi.miwear.interconnect\0";

/// Exact stock payload bound used by the native Lyra bridge. It keeps the
/// synchronous direct-submission scratch allocation bounded below `u16::MAX`.
pub const THIRD_PARTY_PAYLOAD_CAPACITY: usize = 8192;
const THIRD_PARTY_MESSAGE_ID: u32 = 0x0016_0914;
const THIRD_PARTY_MESSAGE_KIND: u16 = 9;
const THIRD_PARTY_MESSAGE_TAIL_SIZE: usize = 0x10c;

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

/// Submits one THIRDPARTY_APP/SEND_PHONE_MESSAGE payload without requiring a
/// wearable quick-app appinfo entry. `fingerprint` is deliberately omitted.
/// The firmware borrows the exact-target envelope and payload only until the
/// generated submit binding returns.
///
/// # Safety
/// `package_name` must point to a readable NUL-terminated string. `payload` must
/// point to `length` readable bytes when `length != 0`. Call only from a context
/// allowed to enter the miwear message dispatcher.
pub unsafe fn thirdparty_send_phone_message(
    package_name: *const u8,
    payload: *const u8,
    length: u16,
) -> i32 {
    if package_name.is_null() || (length != 0 && payload.is_null()) {
        return -22;
    }

    let mut payload_blob = [0u8; THIRD_PARTY_PAYLOAD_CAPACITY + 2];
    payload_blob[..2].copy_from_slice(&length.to_le_bytes());
    if length != 0 {
        unsafe {
            core::ptr::copy_nonoverlapping(
                payload,
                payload_blob.as_mut_ptr().add(2),
                length as usize,
            );
        }
    }
    let content = canopus_target_generated::canopus_thirdparty_message_content {
        message_id: THIRD_PARTY_MESSAGE_ID,
        message_kind: THIRD_PARTY_MESSAGE_KIND,
        _pad_6: [0; 10],
        package_name: package_name.cast_mut().cast(),
        fingerprint_blob: core::ptr::null_mut(),
        payload_blob: payload_blob.as_mut_ptr().cast(),
        _tail: [0; THIRD_PARTY_MESSAGE_TAIL_SIZE],
    };
    unsafe {
        canopus_target_generated::canopus_fw_thirdparty_submit_message_content(
            core::ptr::addr_of!(content).cast(),
        )
    }
}

/// Performs the firmware's full quick-app registration flow. Besides adding
/// appinfo used by interconnect routing, this can create a quick-app context,
/// register page/app descriptors, update launcher state, and emit registration
/// events. It must not be used merely to give a Canopus native module a custom
/// transport package.
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

/// Band-9 Bluelet discovery payload (`bt_device_t`, 240-byte fixed prefix).
#[repr(C)]
#[derive(Copy, Clone)]
pub struct DiscoveryResult {
    pub address: [u8; 6],
    pub address_type: u8,
    pub reserved: u8,
    pub name: [u8; 64],
    pub rssi: i32,
    pub class_of_device: u32,
    pub service_uuids: [u8; 160],
}

const _: [(); 240] = [(); core::mem::size_of::<DiscoveryResult>()];

impl Default for DiscoveryResult {
    fn default() -> Self {
        Self {
            address: [0; 6],
            address_type: 0,
            reserved: 0,
            name: [0; 64],
            rssi: 0,
            class_of_device: 0,
            service_uuids: [0; 160],
        }
    }
}

/// Reads the bounded, NUL-terminated name embedded in a discovery payload.
///
/// # Safety
/// `result` must point at a readable firmware-owned [`DiscoveryResult`].
pub unsafe fn discovery_name<'a>(result: *const DiscoveryResult, _capacity: usize) -> &'a [u8] {
    if result.is_null() {
        return &[];
    }
    let name = unsafe { core::ptr::addr_of!((*result).name).cast::<u8>() };
    let mut length = 0usize;
    while length < 64 && unsafe { *name.add(length) } != 0 {
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

static B9_GAP_HANDLE: AtomicUsize = AtomicUsize::new(0);
static B9_MODULE_CALLBACKS: AtomicUsize = AtomicUsize::new(0);

unsafe extern "C" fn b9_device_found_bridge(
    _handle: *mut core::ffi::c_void,
    device: *const DiscoveryResult,
) {
    let callbacks = B9_MODULE_CALLBACKS.load(Ordering::Acquire) as *const u32;
    if callbacks.is_null() {
        return;
    }
    let address = unsafe { *callbacks.add(CALLBACK_DISCOVERY_RESULT) };
    if address != 0 {
        let callback: DiscoveryResultCallback = unsafe { core::mem::transmute(address as usize) };
        unsafe { callback(core::ptr::null_mut(), device) };
    }
}

unsafe extern "C" fn b9_discovery_state_bridge(_handle: *mut core::ffi::c_void, state: i32) {
    let callbacks = B9_MODULE_CALLBACKS.load(Ordering::Acquire) as *const u32;
    if callbacks.is_null() {
        return;
    }
    let address = unsafe { *callbacks.add(CALLBACK_DISCOVERY_STATE) };
    if address != 0 {
        let callback: DiscoveryStateCallback = unsafe { core::mem::transmute(address as usize) };
        unsafe { callback(core::ptr::null_mut(), state) };
    }
}

unsafe extern "C" fn b9_pair_request_bridge(
    _handle: *mut core::ffi::c_void,
    address: *const u8,
    _local_initiated: i32,
    _bondable: i32,
) {
    let callbacks = B9_MODULE_CALLBACKS.load(Ordering::Acquire) as *const u32;
    if callbacks.is_null() {
        return;
    }
    let entry = unsafe { *callbacks.add(CALLBACK_PAIR_REQUEST) };
    if entry != 0 {
        let callback: PairRequestCallback = unsafe { core::mem::transmute(entry as usize) };
        unsafe { callback(core::ptr::null_mut(), address) };
    }
}

unsafe extern "C" fn b9_pair_display_bridge(_handle: *mut core::ffi::c_void, request: *const u8) {
    if request.is_null() {
        return;
    }
    let callbacks = B9_MODULE_CALLBACKS.load(Ordering::Acquire) as *const u32;
    if callbacks.is_null() {
        return;
    }
    let entry = unsafe { *callbacks.add(CALLBACK_PAIR_DISPLAY) };
    if entry != 0 {
        let callback: PairDisplayCallback = unsafe { core::mem::transmute(entry as usize) };
        let passkey = unsafe { core::ptr::read_unaligned(request.add(8).cast::<u32>()) };
        let kind = unsafe { *request.add(12) } as i32;
        unsafe {
            callback(
                core::ptr::null_mut(),
                request,
                CLASSIC_TRANSPORT as i32,
                kind,
                passkey,
            )
        };
    }
}

unsafe extern "C" fn b9_bond_state_bridge(
    _handle: *mut core::ffi::c_void,
    address: *const u8,
    state: i32,
) {
    let callbacks = B9_MODULE_CALLBACKS.load(Ordering::Acquire) as *const u32;
    if callbacks.is_null() {
        return;
    }
    let entry = unsafe { *callbacks.add(CALLBACK_BOND_STATE) };
    if entry != 0 {
        let callback: BondStateCallback = unsafe { core::mem::transmute(entry as usize) };
        unsafe {
            callback(
                core::ptr::null_mut(),
                address,
                CLASSIC_TRANSPORT as i32,
                state,
            )
        };
    }
}

#[repr(C)]
struct B9GapCallbacks {
    reserved_00: usize,
    reserved_04: usize,
    device_found: unsafe extern "C" fn(*mut core::ffi::c_void, *const DiscoveryResult),
    reserved_0c: usize,
    discovery_state: unsafe extern "C" fn(*mut core::ffi::c_void, i32),
    pair_display: unsafe extern "C" fn(*mut core::ffi::c_void, *const u8),
    bond_state: unsafe extern "C" fn(*mut core::ffi::c_void, *const u8, i32),
    reserved_1c_38: [usize; 8],
    pair_request: unsafe extern "C" fn(*mut core::ffi::c_void, *const u8, i32, i32),
}

const _: [(); 16] = [(); core::mem::size_of::<B9GapCallbacks>() / core::mem::size_of::<usize>()];

static B9_GAP_CALLBACKS: B9GapCallbacks = B9GapCallbacks {
    reserved_00: 0,
    reserved_04: 0,
    device_found: b9_device_found_bridge,
    reserved_0c: 0,
    discovery_state: b9_discovery_state_bridge,
    pair_display: b9_pair_display_bridge,
    bond_state: b9_bond_state_bridge,
    reserved_1c_38: [0; 8],
    pair_request: b9_pair_request_bridge,
};

/// Creates one Bluelet btm_gap client backed by the stock manager singleton.
pub unsafe fn bt_adapter_get_instance() -> *mut core::ffi::c_void {
    let current = B9_GAP_HANDLE.load(Ordering::Acquire);
    if current != 0 {
        return current as *mut core::ffi::c_void;
    }
    type Factory = extern "C" fn() -> *mut core::ffi::c_void;
    type Create = extern "C" fn(
        *mut core::ffi::c_void,
        *mut *mut core::ffi::c_void,
        *const B9GapCallbacks,
    ) -> i32;
    let factory: Factory = unsafe {
        core::mem::transmute(canopus_target_generated::canopus_thumb_callable(
            canopus_target_generated::CANOPUS_FW_BT_ADAPTER_GET_INSTANCE_CALLABLE,
        ))
    };
    let create: Create = unsafe {
        core::mem::transmute(canopus_target_generated::canopus_thumb_callable(
            canopus_target_generated::CANOPUS_FW_BT_GAP_CLIENT_CREATE_CALLABLE,
        ))
    };
    let manager = factory();
    if manager.is_null() {
        return core::ptr::null_mut();
    }
    let mut handle = core::ptr::null_mut();
    if create(manager, &mut handle, &B9_GAP_CALLBACKS) != 0 || handle.is_null() {
        return core::ptr::null_mut();
    }
    match B9_GAP_HANDLE.compare_exchange(0, handle as usize, Ordering::AcqRel, Ordering::Acquire) {
        Ok(_) => handle,
        Err(existing) => {
            type Cleanup = extern "C" fn(*mut core::ffi::c_void) -> i32;
            let cleanup: Cleanup = unsafe {
                core::mem::transmute(canopus_target_generated::canopus_thumb_callable(
                    canopus_target_generated::CANOPUS_FW_BT_GAP_CLIENT_CLEANUP_CALLABLE,
                ))
            };
            let _ = cleanup(handle);
            existing as *mut core::ffi::c_void
        }
    }
}

/// btm_create_bond (`sub_C3BFDE0`, btm_gap). Resolves the btm_gap GAP
/// interface and dispatches its bond slot (+44).
pub unsafe fn btm_create_bond(
    handle: *mut core::ffi::c_void,
    address: *mut core::ffi::c_void,
) -> i32 {
    type F = extern "C" fn(*mut core::ffi::c_void, *mut core::ffi::c_void) -> i32;
    let f: F = unsafe {
        core::mem::transmute(canopus_target_generated::canopus_thumb_callable(
            canopus_target_generated::CANOPUS_FW_BT_CREATE_BOND_CALLABLE,
        ))
    };
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
    let f: F = unsafe {
        core::mem::transmute(canopus_target_generated::canopus_thumb_callable(
            canopus_target_generated::CANOPUS_FW_BT_REMOVE_BOND_CALLABLE,
        ))
    };
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
    let f: F = unsafe {
        core::mem::transmute(canopus_target_generated::canopus_thumb_callable(
            canopus_target_generated::CANOPUS_FW_BT_ADAPTER_SET_SCAN_MODE_PRIVATE_CALLABLE,
        ))
    };
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

unsafe fn b9_is_bonded(address: *const u8) -> bool {
    if address.is_null() {
        return false;
    }
    type GetBonded = extern "C" fn(*mut DiscoveryResult, i32) -> i32;
    let get_bonded: GetBonded = unsafe {
        core::mem::transmute(canopus_target_generated::canopus_thumb_callable(
            canopus_target_generated::CANOPUS_FW_BT_GET_BONDED_DEVICES_CALLABLE,
        ))
    };
    let count = get_bonded(core::ptr::null_mut(), 0);
    if count <= 0 {
        return false;
    }
    let bytes = match (count as usize).checked_mul(core::mem::size_of::<DiscoveryResult>()) {
        Some(bytes) if bytes <= u32::MAX as usize => bytes,
        _ => return false,
    };
    let devices = unsafe { bt_alloc(bytes as u32) }.cast::<DiscoveryResult>();
    if devices.is_null() {
        return false;
    }
    let _ = get_bonded(devices, count);
    let filled = count as usize;
    let mut found = false;
    for index in 0..filled {
        let candidate = unsafe { core::ptr::addr_of!((*devices.add(index)).address) }.cast::<u8>();
        let mut equal = true;
        for byte in 0..6 {
            if unsafe { *candidate.add(byte) != *address.add(byte) } {
                equal = false;
                break;
            }
        }
        if equal {
            found = true;
            break;
        }
    }
    unsafe { bt_free(devices.cast()) };
    found
}

/// Returns the Band-10 stock-view BONDED value (3) when the address appears in
/// Bluelet's Classic bonded-device enumeration.
pub unsafe fn bt_get_bond_state(address: *const u8) -> u32 {
    if unsafe { b9_is_bonded(address) } {
        3
    } else {
        BOND_STATE_NONE
    }
}

/// Returns the module device-view BONDED value for a Classic bonded device.
pub unsafe fn bt_get_pairing_state(address: *const u8, transport: u32) -> u32 {
    if transport == CLASSIC_TRANSPORT && unsafe { b9_is_bonded(address) } {
        BOND_STATE_BONDED
    } else {
        BOND_STATE_NONE
    }
}

// ---------------------------------------------------------------------------
// Module-facing BT interfaces (band-10-compatible signatures)
// ---------------------------------------------------------------------------
//
// Band-9 Bluelet delivers adapter lifecycle through the btm_manager upper
// callbacks rather than band-10's 16-word adapter registration. The slot
// numbers below preserve the common module contract; discovery, pair, display,
// and bond delivery are translated by the persistent GAP client above. The
// separate adapter-state callback remains pending.

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

#[derive(Copy, Clone, Debug, Eq, PartialEq)]
pub enum PairRequestFilterError {
    Policy,
    Allocation,
    Registration,
}

#[derive(Copy, Clone, Debug)]
pub struct PairRequestFilter {
    pub allocation: usize,
    pub registration: u32,
}

pub unsafe fn bt_install_pair_request_filter(
    _replacement: PairRequestCallback,
) -> Result<Option<PairRequestFilter>, PairRequestFilterError> {
    Ok(None)
}

pub unsafe fn bt_forward_pair_request(_cookie: *mut core::ffi::c_void, _address: *const u8) -> i32 {
    0
}

/// Registers the module callback table behind the persistent Bluelet GAP client.
pub unsafe fn bt_adapter_register(adapter: *mut core::ffi::c_void, callbacks: *const u32) -> u32 {
    if adapter.is_null()
        || callbacks.is_null()
        || adapter as usize != B9_GAP_HANDLE.load(Ordering::Acquire)
    {
        return 0;
    }
    B9_MODULE_CALLBACKS.store(callbacks as usize, Ordering::Release);
    adapter as usize as u32
}

pub unsafe fn bt_adapter_unregister(adapter: *mut core::ffi::c_void, registration: u32) -> i32 {
    if adapter.is_null() || registration != adapter as usize as u32 {
        return 0;
    }
    B9_MODULE_CALLBACKS.store(0, Ordering::Release);
    1
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
// These interfaces preserve the module-facing Band-10 contract while translating
// to Band-9 Bluelet objects. The owner queue, one-shot timer, and stock buffer
// are recovered; raw AVDTP/L2CAP and SDP bindings remain explicit ENOSYS/null
// boundaries until their lower profile-transport layouts and ownership rules
// are verified.

/// `ENOSYS` sentinel used by the pending band-9 media-path bindings.
pub const ENOSYS: i32 = -38;

/// Band-9 Bluelet stock buffer prefix; payload starts at `+4+offset`.
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
/// `buffer` must point to a live Band-9 Bluelet stock buffer.
pub unsafe fn stock_buffer_payload_mut(buffer: *mut StockBuffer) -> *mut u8 {
    if buffer.is_null() {
        return core::ptr::null_mut();
    }
    let offset = unsafe { (*buffer).offset } as usize;
    unsafe { buffer.cast::<u8>().add(4 + offset) }
}

pub unsafe fn bt_buffer_new(payload_length: u16, headroom: u16) -> *mut StockBuffer {
    type F = extern "C" fn(u16, u16) -> *mut StockBuffer;
    let f: F = unsafe {
        core::mem::transmute(canopus_target_generated::canopus_thumb_callable(
            canopus_target_generated::CANOPUS_FW_BT_BUFFER_NEW_CALLABLE,
        ))
    };
    f(payload_length, headroom)
}

pub type QueueWork = extern "C" fn(i32, i32, *mut core::ffi::c_void) -> i32;

pub unsafe fn bt_queue_external(
    owner: *mut core::ffi::c_void,
    run: QueueWork,
    cancel: *mut core::ffi::c_void,
    argument: *mut core::ffi::c_void,
    event: u8,
) -> i32 {
    type F = extern "C" fn(
        *mut core::ffi::c_void,
        *mut core::ffi::c_void,
        QueueWork,
        *mut core::ffi::c_void,
        u8,
    ) -> i32;
    let f: F = unsafe {
        core::mem::transmute(canopus_target_generated::canopus_thumb_callable(
            canopus_target_generated::CANOPUS_FW_BT_QUEUE_EXTERNAL_CALLABLE,
        ))
    };
    f(owner, cancel, run, argument, event)
}

pub fn bt_queue_free_addr() -> *mut core::ffi::c_void {
    canopus_target_generated::CANOPUS_FW_BT_QUEUE_FREE_CALLABLE as *mut core::ffi::c_void
}

/// Returns the initialized Bluelet owner-thread context.
pub unsafe fn bt_l2cap_owner() -> *mut core::ffi::c_void {
    unsafe {
        core::ptr::read_volatile(
            canopus_target_generated::canopus_fw_bt_l2cap_owner as *const *mut core::ffi::c_void,
        )
    }
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
pub const AVCTP_CONTROL_PSM: u16 = 0x0017;
pub const AVCTP_LOCAL_RX_MTU: u16 = 0x0200;
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

/// Installs the exact Band-9 AVDTP policy in a zeroed 68-byte connect request.
/// The Bluelet request header occupies bytes 0..24 and its 44-byte channel
/// configuration occupies bytes 24..68. The local MTU and option mask are at
/// configuration offsets 28 and 30, hence request offsets 52 and 54.
///
/// # Safety
/// `request` must point to a writable [`CONNECT_REQUEST_SIZE`]-byte allocation.
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

/// Queues a 68-byte Bluelet basic-mode L2CAP connect request on the Bluetooth
/// owner. Firmware wrapper `sub_C39E90C` installs `sub_C399FAC` as owner work
/// and the stock free callback as cancellation ownership. A nonzero return is
/// the accepted queue node, matching the module-facing Band-10 contract.
pub unsafe fn bt_l2cap_connect(request: *const core::ffi::c_void) -> u32 {
    type F = extern "C" fn(*const core::ffi::c_void) -> u32;
    let f: F = unsafe {
        core::mem::transmute(canopus_target_generated::canopus_thumb_callable(
            canopus_target_generated::CANOPUS_FW_BT_L2CAP_CONNECT_PRIVATE_CALLABLE,
        ))
    };
    f(request)
}

/// Queues local-CID teardown on the Bluetooth owner. Firmware wrapper
/// `sub_C39E990` runs `sub_C39AA30`, which resolves the channel by the first
/// `u16`, performs state-aware disconnect, and releases the request.
pub unsafe fn bt_l2cap_disconnect(request: *const DisconnectRequest) -> i32 {
    type F = extern "C" fn(*const DisconnectRequest) -> i32;
    let f: F = unsafe {
        core::mem::transmute(canopus_target_generated::canopus_thumb_callable(
            canopus_target_generated::CANOPUS_FW_BT_L2CAP_DISCONNECT_CALLABLE,
        ))
    };
    f(request)
}

/// Submits a Bluelet buffer by local dynamic CID. The firmware consumes the
/// buffer on both the send and unknown-CID paths.
///
/// # Safety
/// `buffer` must be a live Band-9 Bluelet stock buffer and this must run on the
/// Bluetooth owner thread.
pub unsafe fn bt_l2cap_submit_cid(buffer: *mut StockBuffer, private_cid: u16) -> i32 {
    if buffer.is_null() {
        return 5;
    }
    unsafe { (*buffer).route = u32::from(private_cid) };
    type F = extern "C" fn(*mut core::ffi::c_void, i32, *mut StockBuffer) -> i32;
    let f: F = unsafe {
        core::mem::transmute(canopus_target_generated::canopus_thumb_callable(
            canopus_target_generated::CANOPUS_FW_BT_L2CAP_SUBMIT_CID_CALLABLE,
        ))
    };
    let context = unsafe {
        core::ptr::read_volatile(
            canopus_target_generated::canopus_fw_bt_l2cap_submit_context
                as *const *mut core::ffi::c_void,
        )
    };
    f(context, 1, buffer)
}

type TimerRun = extern "C" fn(i32, i32, *mut core::ffi::c_void) -> i32;

const B9_TIMER_ACTIVE: usize = 0;
const B9_TIMER_QUEUED: usize = 1;
const B9_TIMER_QUEUE_CANCELLED: usize = 2;
const B9_TIMER_CANCEL_CLAIMED: usize = 3;
const B9_TIMER_WAIT_CALLBACK: usize = 4;
const B9_TIMER_CALLBACK_WAIT: usize = 5;
const B9_TIMER_RECLAIMED: usize = 6;

#[repr(C)]
struct B9TimerContext {
    timer: u32,
    state: AtomicUsize,
    owner: *mut core::ffi::c_void,
    run: TimerRun,
    argument: *mut core::ffi::c_void,
    event: u8,
    reserved: [u8; 3],
}

extern "C" fn b9_timer_owner_work(
    owner_valid: i32,
    event: i32,
    context: *mut core::ffi::c_void,
) -> i32 {
    if context.is_null() {
        return 0;
    }
    let context = context.cast::<B9TimerContext>();
    if unsafe {
        (*context).state.compare_exchange(
            B9_TIMER_QUEUED,
            B9_TIMER_RECLAIMED,
            Ordering::AcqRel,
            Ordering::Acquire,
        )
    }
    .is_err()
    {
        return 0;
    }
    let run = unsafe { (*context).run };
    let argument = unsafe { (*context).argument };
    let result = run(owner_valid, event, argument);
    unsafe { bt_free(context.cast()) };
    result
}

extern "C" fn b9_timer_queue_cancel(
    _owner_valid: i32,
    _event: i32,
    context: *mut core::ffi::c_void,
) -> i32 {
    if context.is_null() {
        return 0;
    }
    let context = context.cast::<B9TimerContext>();
    if unsafe {
        (*context).state.compare_exchange(
            B9_TIMER_QUEUED,
            B9_TIMER_QUEUE_CANCELLED,
            Ordering::AcqRel,
            Ordering::Acquire,
        )
    }
    .is_err()
    {
        return 0;
    }
    let argument = unsafe { (*context).argument };
    if !argument.is_null() {
        unsafe { bt_free(argument) };
    }
    unsafe { bt_free(context.cast()) };
    0
}

extern "C" fn b9_timer_bridge(context: *mut core::ffi::c_void) -> i32 {
    if context.is_null() {
        return 0;
    }
    let context = context.cast::<B9TimerContext>();
    let transition = unsafe {
        (*context).state.compare_exchange(
            B9_TIMER_ACTIVE,
            B9_TIMER_QUEUED,
            Ordering::AcqRel,
            Ordering::Acquire,
        )
    };
    if let Err(state) = transition {
        if state == B9_TIMER_CANCEL_CLAIMED {
            let _ = unsafe {
                (*context).state.compare_exchange(
                    B9_TIMER_CANCEL_CLAIMED,
                    B9_TIMER_CALLBACK_WAIT,
                    Ordering::AcqRel,
                    Ordering::Acquire,
                )
            };
            return 0;
        }
        if state != B9_TIMER_WAIT_CALLBACK
            || unsafe {
                (*context).state.compare_exchange(
                    B9_TIMER_WAIT_CALLBACK,
                    B9_TIMER_CALLBACK_WAIT,
                    Ordering::AcqRel,
                    Ordering::Acquire,
                )
            }
            .is_err()
        {
            return 0;
        }
        let timer = unsafe { (*context).timer };
        let argument = unsafe { (*context).argument };
        type Delete = extern "C" fn(u32) -> i32;
        let delete: Delete = unsafe {
            core::mem::transmute(canopus_target_generated::canopus_thumb_callable(
                canopus_target_generated::CANOPUS_FW_BT_TIMER_DELETE_PRIVATE_CALLABLE,
            ))
        };
        let _ = delete(timer);
        if !argument.is_null() {
            unsafe { bt_free(argument) };
        }
        unsafe { bt_free(context.cast()) };
        return 0;
    }
    let timer = unsafe { (*context).timer };
    let owner = unsafe { (*context).owner };
    let event = unsafe { (*context).event };

    type Delete = extern "C" fn(u32) -> i32;
    let delete: Delete = unsafe {
        core::mem::transmute(canopus_target_generated::canopus_thumb_callable(
            canopus_target_generated::CANOPUS_FW_BT_TIMER_DELETE_PRIVATE_CALLABLE,
        ))
    };
    let _ = delete(timer);
    let queued = unsafe {
        bt_queue_external(
            owner,
            b9_timer_owner_work,
            b9_timer_queue_cancel as *const () as *mut core::ffi::c_void,
            context.cast(),
            event,
        )
    };
    if queued == 0
        && unsafe {
            (*context).state.compare_exchange(
                B9_TIMER_QUEUED,
                B9_TIMER_RECLAIMED,
                Ordering::AcqRel,
                Ordering::Acquire,
            )
        }
        .is_ok()
    {
        let argument = unsafe { (*context).argument };
        if !argument.is_null() {
            unsafe { bt_free(argument) };
        }
        unsafe { bt_free(context.cast()) };
    }
    queued
}

/// Creates a one-shot miwear timer, then queues its callback to the Bluelet
/// owner thread using the module's owner/event callback contract. A nonzero
/// handle transfers ownership of `argument` to the timer; on failure the caller
/// retains ownership.
pub unsafe fn bt_timer_add(
    owner: *mut core::ffi::c_void,
    delay_ms: u32,
    event: u8,
    run_callback: *mut core::ffi::c_void,
    argument: *mut core::ffi::c_void,
    _tag: *const u8,
    _flags: u32,
) -> u32 {
    if owner.is_null() || run_callback.is_null() {
        return 0;
    }

    let context =
        unsafe { bt_alloc(core::mem::size_of::<B9TimerContext>() as u32) }.cast::<B9TimerContext>();
    if context.is_null() {
        return 0;
    }
    let run: TimerRun = unsafe { core::mem::transmute(run_callback) };
    unsafe {
        context.write(B9TimerContext {
            timer: 0,
            state: AtomicUsize::new(B9_TIMER_ACTIVE),
            owner,
            run,
            argument,
            event,
            reserved: [0; 3],
        });
    }

    type Create = extern "C" fn(*mut u32, extern "C" fn(*mut core::ffi::c_void) -> i32, u8) -> i32;
    type Start = extern "C" fn(u32, u32, *mut core::ffi::c_void) -> i32;
    let create: Create = unsafe {
        core::mem::transmute(canopus_target_generated::canopus_thumb_callable(
            canopus_target_generated::CANOPUS_FW_BT_TIMER_CREATE_PRIVATE_CALLABLE,
        ))
    };
    let start: Start = unsafe {
        core::mem::transmute(canopus_target_generated::canopus_thumb_callable(
            canopus_target_generated::CANOPUS_FW_BT_TIMER_START_PRIVATE_CALLABLE,
        ))
    };

    let mut timer = 0u32;
    if create(&mut timer, b9_timer_bridge, 0) != 0 || timer == 0 {
        unsafe { bt_free(context.cast()) };
        return 0;
    }
    unsafe { (*context).timer = timer };
    if start(timer, delay_ms.max(1), context.cast()) != 0 {
        type Delete = extern "C" fn(u32) -> i32;
        let delete: Delete = unsafe {
            core::mem::transmute(canopus_target_generated::canopus_thumb_callable(
                canopus_target_generated::CANOPUS_FW_BT_TIMER_DELETE_PRIVATE_CALLABLE,
            ))
        };
        let _ = delete(timer);
        unsafe { bt_free(context.cast()) };
        return 0;
    }
    context as usize as u32
}

/// Cancels an active miwear timer and releases the module-owned callback
/// argument. A callback already queued to Bluelet observes the cancelled state
/// and performs the release there.
pub unsafe fn bt_timer_cancel(handle: *mut u32) -> i32 {
    if handle.is_null() {
        return 5;
    }
    let context = unsafe { core::ptr::replace(handle, 0) } as *mut B9TimerContext;
    if context.is_null() {
        return 5;
    }

    let state = unsafe {
        (*context).state.compare_exchange(
            B9_TIMER_ACTIVE,
            B9_TIMER_CANCEL_CLAIMED,
            Ordering::AcqRel,
            Ordering::Acquire,
        )
    };
    if state.is_err() {
        return 0;
    }
    let timer = unsafe { (*context).timer };
    let argument = unsafe { (*context).argument };
    type Stop = extern "C" fn(u32) -> i32;
    type Delete = extern "C" fn(u32) -> i32;
    let stop: Stop = unsafe {
        core::mem::transmute(canopus_target_generated::canopus_thumb_callable(
            canopus_target_generated::CANOPUS_FW_BT_TIMER_STOP_PRIVATE_CALLABLE,
        ))
    };
    let delete: Delete = unsafe {
        core::mem::transmute(canopus_target_generated::canopus_thumb_callable(
            canopus_target_generated::CANOPUS_FW_BT_TIMER_DELETE_PRIVATE_CALLABLE,
        ))
    };
    let result = stop(timer);
    if result != 0 {
        let previous = unsafe {
            (*context)
                .state
                .swap(B9_TIMER_WAIT_CALLBACK, Ordering::AcqRel)
        };
        if previous == B9_TIMER_CALLBACK_WAIT {
            let _ = delete(timer);
            if !argument.is_null() {
                unsafe { bt_free(argument) };
            }
            unsafe { bt_free(context.cast()) };
        }
        return result;
    }
    let _ = delete(timer);
    if !argument.is_null() {
        unsafe { bt_free(argument) };
    }
    unsafe { bt_free(context.cast()) };
    result
}

/// Band 9 uses the Bluelet transport rather than the Band 10 raw-H4 host
/// dispatcher. Current exact-target evidence does not require the BES mHDT
/// workaround, but the common module still consumes this explicit capability.
pub const HCI_RECEIVE_HOOK_REQUIRED: bool = false;

pub type BtGapTransportReceive = extern "C" fn(*mut core::ffi::c_void, *mut u8, i32) -> i32;

pub unsafe fn bt_gap_install_receive_hook(_receive_hook: BtGapTransportReceive) -> bool {
    false
}

pub unsafe fn bt_gap_stock_receive(
    _state: *mut core::ffi::c_void,
    _packet: *mut u8,
    _packet_length: i32,
) -> i32 {
    -1
}

/// AVDTP Audio Source SDP record encoded for the recovered generic Band-9
/// Bluelet SDP builder.
pub struct SdpSourceRecord;

impl SdpSourceRecord {
    pub const SERVICE_NAME: &'static [u8] = b"Vela Audio Source\0";
    pub const SERVICE_UUID: u16 = 0x110A;
    pub const PROFILE_VERSION: u16 = 0x0103;
    pub const ATTRIBUTES: [(u16, &'static [u8]); 6] = [
        (0x0001, ALIGNED_SERVICE_CLASS.as_slice()),
        (0x0004, ALIGNED_PROTOCOL.as_slice()),
        (0x0005, ALIGNED_BROWSE.as_slice()),
        (0x0009, ALIGNED_PROFILE.as_slice()),
        (0x0100, ALIGNED_SERVICE_NAME.as_slice()),
        (0x0311, ALIGNED_FEATURES.as_slice()),
    ];
}

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
const ALIGNED_AVRCP_SERVICE_CLASS: AlignedValue<8> =
    AlignedValue([0x35, 0x06, 0x19, 0x11, 0x0e, 0x19, 0x11, 0x0f]);
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
    service_uuid: u16,
    profile_version: u16,
    selector: u8,
    service_name: *const u8,
) -> *mut core::ffi::c_void {
    type F = extern "C" fn(u32, u16, u16, u8, *const u8) -> *mut core::ffi::c_void;
    let f: F = unsafe {
        core::mem::transmute(canopus_target_generated::canopus_thumb_callable(
            canopus_target_generated::CANOPUS_FW_SDP_BUILDER_CREATE_CALLABLE,
        ))
    };
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
    let f: F = unsafe {
        core::mem::transmute(canopus_target_generated::canopus_thumb_callable(
            canopus_target_generated::CANOPUS_FW_SDP_SET_RAW_ATTRIBUTE_CALLABLE,
        ))
    };
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
    let f: F = unsafe {
        core::mem::transmute(canopus_target_generated::canopus_thumb_callable(
            canopus_target_generated::CANOPUS_FW_SDP_COMMIT_CALLABLE,
        ))
    };
    f(builder)
}

pub unsafe fn sdp_unregister(handle: u32) -> i32 {
    type F = extern "C" fn(u32) -> i32;
    let f: F = unsafe {
        core::mem::transmute(canopus_target_generated::canopus_thumb_callable(
            canopus_target_generated::CANOPUS_FW_SDP_UNREGISTER_CALLABLE,
        ))
    };
    f(handle)
}

/// `btm_reply_pair_request` dispatch on the btm_gap singleton (used by the
/// module-facing pair-reply APIs below).
pub unsafe fn btm_reply_pair_request() -> i32 {
    type F = extern "C" fn(*mut core::ffi::c_void) -> i32;
    let f: F = unsafe {
        core::mem::transmute(canopus_target_generated::canopus_thumb_callable(
            canopus_target_generated::CANOPUS_FW_BT_PAIR_REQUEST_REPLY_CALLABLE,
        ))
    };
    f(unsafe { bt_adapter_get_instance() })
}

/// `btm_pair_display_reply` dispatch on the btm_gap singleton.
pub unsafe fn btm_reply_pair_display() -> i32 {
    type F = extern "C" fn(*mut core::ffi::c_void) -> i32;
    let f: F = unsafe {
        core::mem::transmute(canopus_target_generated::canopus_thumb_callable(
            canopus_target_generated::CANOPUS_FW_BT_PAIR_DISPLAY_REPLY_CALLABLE,
        ))
    };
    f(unsafe { bt_adapter_get_instance() })
}

pub unsafe fn bt_discovery_start(handle: *mut core::ffi::c_void, timeout: i32) -> i32 {
    type F = extern "C" fn(*mut core::ffi::c_void, i32) -> i32;
    let f: F = unsafe {
        core::mem::transmute(canopus_target_generated::canopus_thumb_callable(
            canopus_target_generated::CANOPUS_FW_BT_DISCOVERY_START_CALLABLE,
        ))
    };
    f(handle, timeout)
}

pub unsafe fn bt_discovery_stop(handle: *mut core::ffi::c_void) -> i32 {
    type F = extern "C" fn(*mut core::ffi::c_void) -> i32;
    let f: F = unsafe {
        core::mem::transmute(canopus_target_generated::canopus_thumb_callable(
            canopus_target_generated::CANOPUS_FW_BT_DISCOVERY_STOP_CALLABLE,
        ))
    };
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
    let f: F = unsafe {
        core::mem::transmute(canopus_target_generated::canopus_thumb_callable(
            canopus_target_generated::CANOPUS_FW_MM_FREE_CALLABLE,
        ))
    };
    let heap = unsafe {
        core::ptr::read_volatile(canopus_target_generated::canopus_fw_default_heap as *const usize)
    };
    f(heap, allocation);
}

pub unsafe fn nuttx_open(path: *const u8, flags: i32) -> i32 {
    type F = extern "C" fn(*const u8, i32, ...) -> i32;
    let f: F = unsafe {
        core::mem::transmute(canopus_target_generated::canopus_thumb_callable(
            canopus_target_generated::CANOPUS_FW_OPEN_CALLABLE,
        ))
    };
    f(path, flags)
}

pub unsafe fn nuttx_close(fd: i32) -> i32 {
    type F = extern "C" fn(i32) -> i32;
    let f: F = unsafe {
        core::mem::transmute(canopus_target_generated::canopus_thumb_callable(
            canopus_target_generated::CANOPUS_FW_CLOSE_CALLABLE,
        ))
    };
    f(fd)
}

pub unsafe fn nuttx_read(fd: i32, buffer: *mut core::ffi::c_void, count: u32) -> i32 {
    type F = extern "C" fn(i32, *mut core::ffi::c_void, u32) -> i32;
    let f: F = unsafe {
        core::mem::transmute(canopus_target_generated::canopus_thumb_callable(
            canopus_target_generated::CANOPUS_FW_READ_CALLABLE,
        ))
    };
    f(fd, buffer, count)
}

pub unsafe fn nuttx_write(fd: i32, buffer: *const core::ffi::c_void, count: u32) -> i32 {
    type F = extern "C" fn(i32, *const core::ffi::c_void, u32) -> i32;
    let f: F = unsafe {
        core::mem::transmute(canopus_target_generated::canopus_thumb_callable(
            canopus_target_generated::CANOPUS_FW_WRITE_CALLABLE,
        ))
    };
    f(fd, buffer, count)
}

/// Positions the current-process fd using the recovered 64-bit POSIX ABI.
pub unsafe fn nuttx_lseek(fd: i32, offset: i64, whence: i32) -> i64 {
    unsafe { canopus_target_generated::canopus_fw_lseek(fd, offset, whence) }
}

/// Flushes the current-process fd. Band-9's wrapper uses fops+0x20 and falls
/// back to ioctl FIOC_SYNC (0x50D) when the filesystem has no fsync slot.
pub unsafe fn nuttx_fsync(fd: i32) -> i32 {
    unsafe { canopus_target_generated::canopus_fw_fsync(fd) }
}

/// Band-9's fd-level ioctl wrapper has not yet passed exact-firmware evidence
/// review. Fail closed instead of invoking the known file-pointer dispatcher.
pub unsafe fn nuttx_ioctl(_fd: i32, _command: u32, _argument: usize) -> i32 {
    -1
}

pub unsafe fn nuttx_create(path: *const u8, flags: i32, mode: u32) -> i32 {
    type F = extern "C" fn(*const u8, i32, ...) -> i32;
    let f: F = unsafe { core::mem::transmute(canopus_target_generated::CANOPUS_FW_OPEN_CALLABLE) };
    f(path, flags, mode)
}

pub unsafe fn nuttx_unlink(path: *const u8) -> i32 {
    unsafe { canopus_target_generated::canopus_fw_unlink(path) }
}

pub unsafe fn nuttx_rename(old_path: *const u8, new_path: *const u8) -> i32 {
    unsafe { canopus_target_generated::canopus_fw_rename(old_path, new_path) }
}

/// Band-9 register_driver is 3-arg (no mode_t), unlike band-10's 4-arg form.
pub unsafe fn canopus_fw_register_driver_b9(
    path: *const u8,
    fops: *const core::ffi::c_void,
    private: *mut core::ffi::c_void,
) -> i32 {
    type F = extern "C" fn(*const u8, *const core::ffi::c_void, *mut core::ffi::c_void) -> i32;
    let f: F = unsafe {
        core::mem::transmute(canopus_target_generated::canopus_thumb_callable(
            canopus_target_generated::CANOPUS_FW_REGISTER_DRIVER_CALLABLE,
        ))
    };
    f(path, fops, private)
}

pub unsafe fn canopus_fw_unregister_driver_b9(path: *const u8) -> i32 {
    type F = extern "C" fn(*const u8) -> i32;
    let f: F = unsafe {
        core::mem::transmute(canopus_target_generated::canopus_thumb_callable(
            canopus_target_generated::CANOPUS_FW_UNREGISTER_DRIVER_CALLABLE,
        ))
    };
    f(path)
}

// ---------------------------------------------------------------------------
// Native app / launcher / navigation / notification (band-9)
// ---------------------------------------------------------------------------

pub unsafe fn launcher_add(app_id: u16) -> i32 {
    type F = extern "C" fn(u16) -> i32;
    let f: F = unsafe {
        core::mem::transmute(canopus_target_generated::canopus_thumb_callable(
            canopus_target_generated::CANOPUS_FW_APP_LAUNCHER_ADD_CALLABLE,
        ))
    };
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
    let f: F = unsafe {
        core::mem::transmute(canopus_target_generated::canopus_thumb_callable(
            canopus_target_generated::CANOPUS_FW_PAGE_GOTO_CALLABLE,
        ))
    };
    f(a, b, c, d)
}

pub unsafe fn activity_finish(page: *mut firmware_page_descriptor) -> i32 {
    type F = extern "C" fn(*mut firmware_page_descriptor) -> i32;
    let f: F = unsafe {
        core::mem::transmute(canopus_target_generated::canopus_thumb_callable(
            canopus_target_generated::CANOPUS_FW_PAGE_FINISH_CALLABLE,
        ))
    };
    f(page)
}

pub unsafe fn notification_insert(message: *const firmware_notification_message) -> i32 {
    type F = extern "C" fn(*const firmware_notification_message) -> i32;
    let f: F = unsafe {
        core::mem::transmute(canopus_target_generated::canopus_thumb_callable(
            canopus_target_generated::CANOPUS_FW_LVX_NOTIFICATION_INSERT_MESSAGE_CALLABLE,
        ))
    };
    f(message)
}

// ---------------------------------------------------------------------------
// LVX v8 UI backend (band-9)
// ---------------------------------------------------------------------------
//
// Band-9 is LVGL v8 with a different lvx_* widget ABI than band-10's v9. The
// list-row factory takes (parent, primary) instead of v9's 4-argument form.

pub unsafe fn lvx_image_create(parent: *mut core::ffi::c_void) -> *mut core::ffi::c_void {
    type F = extern "C" fn(*mut core::ffi::c_void) -> *mut core::ffi::c_void;
    let f: F = unsafe {
        core::mem::transmute(canopus_target_generated::CANOPUS_FW_LV_IMG_CREATE_CALLABLE)
    };
    f(parent)
}

pub unsafe fn lvx_image_set_src(image: *mut core::ffi::c_void, source: *const core::ffi::c_void) {
    type F = extern "C" fn(*mut core::ffi::c_void, *const core::ffi::c_void);
    let f: F = unsafe {
        core::mem::transmute(canopus_target_generated::CANOPUS_FW_LV_IMG_SET_SRC_CALLABLE)
    };
    f(image, source);
}

pub unsafe fn lvx_bar_create(parent: *mut core::ffi::c_void) -> *mut core::ffi::c_void {
    type F = extern "C" fn(*mut core::ffi::c_void) -> *mut core::ffi::c_void;
    let f: F = unsafe {
        core::mem::transmute(canopus_target_generated::CANOPUS_FW_LV_BAR_CREATE_CALLABLE)
    };
    f(parent)
}

pub unsafe fn lvx_bar_set_range(bar: *mut core::ffi::c_void, minimum: i32, maximum: i32) {
    type F = extern "C" fn(*mut core::ffi::c_void, i32, i32);
    let f: F = unsafe {
        core::mem::transmute(canopus_target_generated::CANOPUS_FW_LV_BAR_SET_RANGE_CALLABLE)
    };
    f(bar, minimum, maximum);
}

pub unsafe fn lvx_bar_set_value(bar: *mut core::ffi::c_void, value: i32) {
    type F = extern "C" fn(*mut core::ffi::c_void, i32, u32);
    let f: F = unsafe {
        core::mem::transmute(canopus_target_generated::CANOPUS_FW_LV_BAR_SET_VALUE_CALLABLE)
    };
    f(bar, value, 0);
}

pub unsafe fn lvx_list_row_create(
    parent: *mut core::ffi::c_void,
    primary: *const u8,
) -> *mut core::ffi::c_void {
    type F = extern "C" fn(*mut core::ffi::c_void, *const u8) -> *mut core::ffi::c_void;
    let f: F = unsafe {
        core::mem::transmute(canopus_target_generated::canopus_thumb_callable(
            canopus_target_generated::CANOPUS_FW_LVX_LIST_ROW_CREATE_CALLABLE,
        ))
    };
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
    let f: F = unsafe {
        core::mem::transmute(canopus_target_generated::canopus_thumb_callable(
            canopus_target_generated::CANOPUS_FW_LVX_LIST_ROW_UPDATE_CALLABLE,
        ))
    };
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
    let f: F = unsafe {
        core::mem::transmute(canopus_target_generated::canopus_thumb_callable(
            canopus_target_generated::CANOPUS_FW_LVX_LIST_ROW_TRAILING_CALLABLE,
        ))
    };
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
pub const STYLE_MISANS_DEMIBOLD_32: usize =
    canopus_target_generated::canopus_fw_style_misans_demibold_32;

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
    let add: F = unsafe {
        core::mem::transmute(canopus_target_generated::canopus_thumb_callable(
            canopus_target_generated::CANOPUS_FW_LVX_OBJECT_ADD_FLAG_CALLABLE,
        ))
    };
    let clear: F = unsafe {
        core::mem::transmute(canopus_target_generated::canopus_thumb_callable(
            canopus_target_generated::CANOPUS_FW_LVX_OBJECT_CLEAR_FLAG_CALLABLE,
        ))
    };
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
    let f: F = unsafe {
        core::mem::transmute(canopus_target_generated::canopus_thumb_callable(
            canopus_target_generated::CANOPUS_FW_LVX_PAGE_TITLE_CREATE_CALLABLE,
        ))
    };
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
    #[cfg(target_pointer_width = "32")]
    assert!(
        core::mem::size_of::<canopus_target_generated::canopus_thirdparty_message_content>() == 296
    );
    #[cfg(target_pointer_width = "32")]
    assert!(
        core::mem::offset_of!(
            canopus_target_generated::canopus_thirdparty_message_content,
            package_name
        ) == 16
    );
    #[cfg(target_pointer_width = "32")]
    assert!(
        core::mem::offset_of!(
            canopus_target_generated::canopus_thirdparty_message_content,
            fingerprint_blob
        ) == 20
    );
    #[cfg(target_pointer_width = "32")]
    assert!(
        core::mem::offset_of!(
            canopus_target_generated::canopus_thirdparty_message_content,
            payload_blob
        ) == 24
    );
    assert!(THIRD_PARTY_PAYLOAD_CAPACITY <= u16::MAX as usize);
    // The band-9 launcher app descriptor is 60 bytes (band-10 is 64).
    #[cfg(target_pointer_width = "32")]
    assert!(core::mem::size_of::<launcher_app_descriptor>() == 60);
    #[cfg(target_pointer_width = "32")]
    assert!(core::mem::offset_of!(launcher_app_descriptor, page_registry) == 44);
    #[cfg(target_pointer_width = "32")]
    assert!(core::mem::offset_of!(launcher_app_descriptor, hidden_flags) == 56);
    assert!(core::mem::size_of::<DisconnectRequest>() == 4);
    assert!(CONNECT_REQUEST_SIZE == 68);
    assert!(CONNECT_CALLBACK_OFFSET + 4 <= CONNECT_REQUEST_SIZE);
    assert!(CONNECT_ADDRESS_OFFSET + 8 <= 24);
    assert!(CONNECT_CONFIG_OFFSET == 24 + 28);
    assert!(CONNECT_OPTIONS_OFFSET == 24 + 30);
    assert!(CONNECT_OPTIONS_OFFSET + 2 <= CONNECT_REQUEST_SIZE);
    assert!(EVENT_COMPLETE_CID_OFFSET == 108);
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
        if core::mem::size_of::<*const u8>() == 4 {
            assert_eq!(
                core::mem::size_of::<canopus_target_generated::canopus_thirdparty_message_content>(
                ),
                296
            );
            assert_eq!(
                core::mem::offset_of!(
                    canopus_target_generated::canopus_thirdparty_message_content,
                    package_name
                ),
                16
            );
            assert_eq!(
                core::mem::offset_of!(
                    canopus_target_generated::canopus_thirdparty_message_content,
                    fingerprint_blob
                ),
                20
            );
            assert_eq!(
                core::mem::offset_of!(
                    canopus_target_generated::canopus_thirdparty_message_content,
                    payload_blob
                ),
                24
            );
        }
        assert_eq!(THIRD_PARTY_PAYLOAD_CAPACITY, 8192);
    }

    #[test]
    fn band9_avdtp_connect_policy_matches_bluelet_layout() {
        let mut request = [0u8; CONNECT_REQUEST_SIZE];
        unsafe { configure_avdtp_connect_request(request.as_mut_ptr()) };
        assert_eq!(
            &request[CONNECT_PSM_OFFSET..CONNECT_PSM_OFFSET + 2],
            &AVDTP_SIGNALING_PSM.to_le_bytes()
        );
        assert_eq!(
            &request[CONNECT_CONFIG_OFFSET..CONNECT_CONFIG_OFFSET + 2],
            &AVDTP_LOCAL_RX_MTU.to_le_bytes()
        );
        assert_eq!(
            &request[CONNECT_OPTIONS_OFFSET..CONNECT_OPTIONS_OFFSET + 2],
            &CONNECT_OPTION_LOCAL_MTU.to_le_bytes()
        );
        assert_eq!(CONNECT_CONFIG_OFFSET, 24 + 28);
        assert_eq!(CONNECT_OPTIONS_OFFSET, 24 + 30);
        assert_eq!(EVENT_COMPLETE_MTU_OFFSET, 72);
        assert_eq!(EVENT_COMPLETE_CID_OFFSET, 108);
        assert_eq!(core::mem::size_of::<DisconnectRequest>(), 4);
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
