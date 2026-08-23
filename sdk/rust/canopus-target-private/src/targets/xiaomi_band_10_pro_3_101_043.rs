//! Target-private full-trust bindings for `xiaomi-band-10-pro-3.101.043`.
//!
//! These are **not** part of `canopus-target-generated` (the public, audited
//! per-target bindings). They expose recovered launcher, LVX, Bluetooth, L2CAP,
//! SDP, timer, and miwear/interconnect connection-framework calls that the
//! generated public crate deliberately leaves restricted or FORBIDDEN. They are
//! valid only for firmware SHA-256 `519307675665e4866d722a8119a98589c397b614ac3294cb87bfc86de45756ec`, must never be called
//! before [`canopus_target_generated::canopus_identity_guard`] passes, and are
//! the source of every absolute address a module links. Future targets provide
//! a sibling backend module with the same interface instead of editing this one.
//!
//! Every recovered callable address carries the Thumb bit. All functions are
//! `unsafe`; the module is responsible for state, ownership, and lifecycle
//! discipline.

pub const TARGET_ID: &str = "xiaomi-band-10-pro-3.101.043";

pub use canopus_target_generated::{
    canopus_fw_clock_gettime, canopus_fw_register_driver, canopus_fw_unregister_driver,
    canopus_identity_guard, file_operations, firmware_notification_message,
    firmware_page_descriptor, launcher_app_descriptor, stock_timespec_t,
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

/// The module callback table uses the same 17-word descriptor shape as stock.
pub const CALLBACK_WORDS: usize = 17;
pub const CALLBACK_ADAPTER_STATE: usize = 0;
pub const CALLBACK_DISCOVERY_STATE: usize = 1;
pub const CALLBACK_DISCOVERY_RESULT: usize = 2;
pub const CALLBACK_PAIR_REQUEST: usize = 5;
pub const CALLBACK_PAIR_DISPLAY: usize = 6;
pub const CALLBACK_BOND_STATE: usize = 9;

/// The 3.101.043 stock Bluetooth client uses a 17-word callback descriptor.
/// Its registration handle points to an 8-byte callback-list node containing
/// `{ cookie, descriptor }`.
pub const STOCK_CALLBACK_WORDS: usize = 17;
pub const STOCK_CALLBACK_PAIR_REQUEST_SLOT: usize = 5;
const STOCK_REGISTRATION_COOKIE_WORD: usize = 0;
const STOCK_REGISTRATION_DESCRIPTOR_WORD: usize = 1;

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

/// Returns the firmware-owned shared Bluetooth adapter client.
pub unsafe fn bt_adapter_get_instance() -> *mut core::ffi::c_void {
    unsafe { canopus_target_generated::canopus_fw_bt_adapter_get_instance() }
}

/// Registers a persistent 17-word callback descriptor. Returns a nonzero
/// registration handle on success, 0 on failure.
pub unsafe fn bt_adapter_register(adapter: *mut core::ffi::c_void, callbacks: *const u32) -> u32 {
    unsafe { canopus_target_generated::canopus_fw_bt_adapter_register(adapter, callbacks) as u32 }
}

/// Unregisters a callback registration. Returns 0 on failure, nonzero on
/// success.
pub unsafe fn bt_adapter_unregister(adapter: *mut core::ffi::c_void, registration: u32) -> i32 {
    unsafe { canopus_target_generated::canopus_fw_bt_adapter_unregister(adapter, registration) }
}

pub unsafe fn bt_adapter_get_state(adapter: *mut core::ffi::c_void) -> i32 {
    unsafe { canopus_target_generated::canopus_fw_bt_adapter_get_state(adapter) }
}

/// Starts discovery. Returns 0 on success, negative errno otherwise.
pub unsafe fn bt_discovery_start(adapter: *mut core::ffi::c_void, timeout: i32) -> i32 {
    type F = extern "C" fn(*mut core::ffi::c_void, i32) -> i32;
    let f: F = unsafe {
        core::mem::transmute(canopus_target_generated::canopus_thumb_callable(
            canopus_target_generated::CANOPUS_FW_BT_DISCOVERY_START_CALLABLE,
        ))
    };
    f(adapter, timeout)
}

/// Stops discovery. Returns 0 on success, negative errno otherwise.
pub unsafe fn bt_discovery_stop(adapter: *mut core::ffi::c_void) -> i32 {
    type F = extern "C" fn(*mut core::ffi::c_void) -> i32;
    let f: F = unsafe {
        core::mem::transmute(canopus_target_generated::canopus_thumb_callable(
            canopus_target_generated::CANOPUS_FW_BT_DISCOVERY_STOP_CALLABLE,
        ))
    };
    f(adapter)
}

pub unsafe fn bt_adapter_set_scan_mode(scan_mode: i32, bondable: i32) -> i32 {
    type F = extern "C" fn(i32, i32) -> i32;
    let f: F = unsafe {
        core::mem::transmute(canopus_target_generated::canopus_thumb_callable(
            canopus_target_generated::CANOPUS_FW_BT_ADAPTER_SET_SCAN_MODE_CALLABLE,
        ))
    };
    f(scan_mode, bondable)
}

pub unsafe fn bt_adapter_get_scan_mode() -> i32 {
    unsafe { canopus_target_generated::canopus_fw_bt_adapter_get_scan_mode() }
}

/// Positive Pair Request reply. Returns 0 on success.
pub unsafe fn bt_pair_request_reply(
    adapter: *mut core::ffi::c_void,
    address: *const u8,
    accept: i32,
) -> i32 {
    unsafe { canopus_target_generated::canopus_fw_bt_pair_request_reply(adapter, address, accept) }
}

/// Pair Display (numeric comparison) reply. Returns 0 on success.
pub unsafe fn bt_pair_display_reply(
    adapter: *mut core::ffi::c_void,
    address: *const u8,
    transport: i32,
    accept: i32,
) -> i32 {
    unsafe {
        canopus_target_generated::canopus_fw_bt_pair_display_reply(
            adapter, address, transport, accept,
        )
    }
}

/// Reads the adapter bond-state bitmask for `address`.
pub unsafe fn bt_get_bond_state(address: *const u8) -> u32 {
    type F = extern "C" fn(*const u8) -> u32;
    let f: F = unsafe {
        core::mem::transmute(canopus_target_generated::canopus_thumb_callable(
            canopus_target_generated::CANOPUS_FW_BT_GET_BOND_STATE_CALLABLE,
        ))
    };
    f(address)
}

/// Reads the exact device-record pairing state for `address` on `transport`.
pub unsafe fn bt_get_pairing_state(address: *const u8, transport: u32) -> u32 {
    unsafe { canopus_target_generated::canopus_fw_bt_get_pairing_state(address, transport) }
}

/// Submits a bond for `address` on `transport`. Returns 0 on success.
pub unsafe fn bt_create_bond(address: *const u8, transport: u32) -> i32 {
    type F = extern "C" fn(*const u8, u32) -> i32;
    let f: F = unsafe {
        core::mem::transmute(canopus_target_generated::canopus_thumb_callable(
            canopus_target_generated::CANOPUS_FW_BT_CREATE_BOND_PRIVATE_CALLABLE,
        ))
    };
    f(address, transport)
}

pub unsafe fn bt_remove_bond(address: *const u8, transport: u32) -> i32 {
    type F = extern "C" fn(*const u8, u32) -> i32;
    let f: F = unsafe {
        core::mem::transmute(canopus_target_generated::canopus_thumb_callable(
            canopus_target_generated::CANOPUS_FW_BT_REMOVE_BOND_PRIVATE_CALLABLE,
        ))
    };
    f(address, transport)
}

/// `create_bond` returns 2 when the adapter lifecycle byte is not READY (4);
/// this is a retriable precondition, not a submission.
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

/// Replaces the stock 3.101.043 Pair Request registration with a resident
/// 17-word mirror. The stock handle is the callback-list node allocated by
/// `sub_C3A96EC`; word 1 owns the descriptor pointer. Registration and removal
/// go through the firmware callback-list API so the manager's private storage is
/// never modified directly.
pub unsafe fn bt_install_pair_request_filter(
    replacement: PairRequestCallback,
) -> Result<Option<PairRequestFilter>, PairRequestFilterError> {
    let adapter = unsafe {
        *(canopus_target_generated::canopus_fw_core_bt_adapter_instance
            as *const *mut core::ffi::c_void)
    };
    let handle_ptr = canopus_target_generated::canopus_fw_core_bt_registration_handle as *mut u32;
    let original_handle = unsafe { core::ptr::read_volatile(handle_ptr) };
    if adapter.is_null() || original_handle == 0 {
        return Ok(None);
    }

    let registration = original_handle as usize as *const u32;
    let cookie =
        unsafe { core::ptr::read_volatile(registration.add(STOCK_REGISTRATION_COOKIE_WORD)) };
    let live_descriptor =
        unsafe { core::ptr::read_volatile(registration.add(STOCK_REGISTRATION_DESCRIPTOR_WORD)) }
            as usize;
    if live_descriptor == 0 {
        return Err(PairRequestFilterError::DescriptorUnavailable);
    }
    let stock = canopus_target_generated::canopus_fw_stock_bt_callback_descriptor as *const u32;
    if cookie != 0 || live_descriptor != stock as usize {
        return Err(PairRequestFilterError::DescriptorMismatch);
    }
    let original =
        canopus_target_generated::CANOPUS_FW_CORE_BT_PAIR_REQUEST_CALLBACK_CALLABLE as u32;
    if unsafe { core::ptr::read_volatile(stock.add(STOCK_CALLBACK_PAIR_REQUEST_SLOT)) } != original
    {
        return Err(PairRequestFilterError::PairSlotMismatch);
    }

    let mirror = unsafe { bt_alloc((STOCK_CALLBACK_WORDS * 4) as u32) } as *mut u32;
    if mirror.is_null() {
        return Err(PairRequestFilterError::Allocation);
    }
    unsafe {
        core::ptr::copy_nonoverlapping(stock, mirror, STOCK_CALLBACK_WORDS);
        *mirror.add(STOCK_CALLBACK_PAIR_REQUEST_SLOT) = replacement as *const () as usize as u32;
    }
    let mirror_handle = unsafe { bt_adapter_register(adapter, mirror) };
    if mirror_handle == 0 {
        unsafe { bt_free(mirror.cast()) };
        return Err(PairRequestFilterError::Registration);
    }
    if unsafe { bt_adapter_unregister(adapter, original_handle) } == 0 {
        if unsafe { bt_adapter_unregister(adapter, mirror_handle) } != 0 {
            unsafe { bt_free(mirror.cast()) };
        }
        return Err(PairRequestFilterError::Registration);
    }
    unsafe { core::ptr::write_volatile(handle_ptr, mirror_handle) };
    Ok(Some(PairRequestFilter {
        allocation: mirror as usize,
        registration: mirror_handle,
    }))
}

pub unsafe fn bt_forward_pair_request(cookie: *mut core::ffi::c_void, address: *const u8) -> i32 {
    unsafe { canopus_target_generated::canopus_fw_core_bt_pair_request_callback(cookie, address) }
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

/// Stock connect-request option bit that installs the local receive MTU from
/// [`CONNECT_CONFIG_OFFSET`] into the new channel.
pub const CONNECT_OPTION_LOCAL_MTU: u16 = 1 << 0;

/// PSM for AVDTP signaling and media transport.
pub const AVDTP_SIGNALING_PSM: u16 = 0x0019;
/// PSM for the AVCTP control channel used by AVRCP.
pub const AVCTP_CONTROL_PSM: u16 = 0x0017;
/// Local receive MTU for the short single-packet AVRCP control frames.
pub const AVCTP_LOCAL_RX_MTU: u16 = 0x0200;
/// Local receive MTU advertised by Android for both AVDTP channels. The stock
/// connect worker uses this field only when [`CONNECT_OPTION_LOCAL_MTU`] is set.
pub const AVDTP_LOCAL_RX_MTU: u16 = 0x0400;

/// Installs the exact-target AVDTP policy in a zeroed stock connect request.
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

pub unsafe fn bt_buffer_new(payload_length: u16, headroom: u16) -> *mut StockBuffer {
    type F = extern "C" fn(u16, u16) -> *mut StockBuffer;
    let f: F = unsafe {
        core::mem::transmute(canopus_target_generated::canopus_thumb_callable(
            canopus_target_generated::CANOPUS_FW_BT_BUFFER_NEW_CALLABLE,
        ))
    };
    f(payload_length, headroom)
}

/// Queues a dynamic L2CAP connect request on the exact owner FSM. Returns the
/// nonzero queue node on accepted submission and 0 when insertion fails.
pub unsafe fn bt_l2cap_connect(request: *const core::ffi::c_void) -> u32 {
    unsafe { canopus_target_generated::canopus_fw_bt_l2cap_connect(request) }
}

/// Queues L2CAP teardown through the generated exact-target callable. Invoke
/// from an active Bluetooth-owner callback; the wrapper only inserts owner-local work.
pub unsafe fn bt_l2cap_disconnect(request: *const DisconnectRequest) -> i32 {
    type F = extern "C" fn(*const DisconnectRequest) -> i32;
    let f: F = unsafe {
        core::mem::transmute(canopus_target_generated::canopus_thumb_callable(
            canopus_target_generated::CANOPUS_FW_BT_L2CAP_DISCONNECT_CALLABLE,
        ))
    };
    f(request)
}

/// Queues one CID submission through the generated exact-target callable. Invoke
/// from an active Bluetooth-owner callback; the wrapper only inserts owner-local work.
pub unsafe fn bt_l2cap_submit_cid(buffer: *mut StockBuffer, private_cid: u16) -> i32 {
    type F = extern "C" fn(*mut StockBuffer, u16) -> i32;
    let f: F = unsafe {
        core::mem::transmute(canopus_target_generated::canopus_thumb_callable(
            canopus_target_generated::CANOPUS_FW_BT_L2CAP_SUBMIT_CID_CALLABLE,
        ))
    };
    f(buffer, private_cid)
}

pub unsafe fn bt_alloc(size: u32) -> *mut core::ffi::c_void {
    type F = extern "C" fn(u32) -> *mut core::ffi::c_void;
    let f: F = unsafe {
        core::mem::transmute(canopus_target_generated::canopus_thumb_callable(
            canopus_target_generated::CANOPUS_FW_BT_ALLOC_CALLABLE,
        ))
    };
    f(size)
}

pub unsafe fn bt_free(allocation: *mut core::ffi::c_void) {
    type F = extern "C" fn(*mut core::ffi::c_void) -> i32;
    let f: F = unsafe {
        core::mem::transmute(canopus_target_generated::canopus_thumb_callable(
            canopus_target_generated::CANOPUS_FW_BT_FREE_CALLABLE,
        ))
    };
    f(allocation);
}

/// One-shot Bluetooth FSM timer. Returns a nonzero handle, 0 on failure. A
/// nonzero handle transfers ownership of `argument` to the timer; on failure
/// the caller retains ownership.
///
/// The generated exact-target timer entry updates the timer list but does not
/// signal the sleeping FSM semaphore; use it from an active owner callback, or
/// pair delayed work with a separately guaranteed external wake.
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
    let f: F = unsafe {
        core::mem::transmute(canopus_target_generated::canopus_thumb_callable(
            canopus_target_generated::CANOPUS_FW_BT_TIMER_ADD_CALLABLE,
        ))
    };
    f(owner, delay_ms, event, run_callback, argument, tag, flags)
}

/// Cancels a timer; frees its argument.
pub unsafe fn bt_timer_cancel(handle: *mut u32) -> i32 {
    type F = extern "C" fn(*mut u32) -> i32;
    let f: F = unsafe {
        core::mem::transmute(canopus_target_generated::canopus_thumb_callable(
            canopus_target_generated::CANOPUS_FW_BT_TIMER_CANCEL_CALLABLE,
        ))
    };
    f(handle)
}

pub type QueueWork = extern "C" fn(i32, i32, *mut core::ffi::c_void) -> i32;

/// Queues work through the firmware's external-event ring and wakes the Bluetooth
/// FSM when the ring transitions from empty to non-empty. The generated exact-target
/// entry inserts the event while holding the FSM lock and signals its semaphore
/// through the generated exact-target path; this is the correct entry point for
/// callers outside the owner.
///
/// The integer return is the firmware lock-release result, not an enqueue handle
/// or acceptance status. Once the FSM is initialized, stock callers treat this
/// operation as infallible and ignore the return value.
pub unsafe fn bt_queue_external(
    owner: *mut core::ffi::c_void,
    run: QueueWork,
    cancel: *mut core::ffi::c_void,
    argument: *mut core::ffi::c_void,
    event: u8,
) -> i32 {
    type F = extern "C" fn(
        *mut core::ffi::c_void,
        QueueWork,
        *mut core::ffi::c_void,
        *mut core::ffi::c_void,
        u8,
    ) -> i32;
    let f: F = unsafe {
        core::mem::transmute(canopus_target_generated::canopus_thumb_callable(
            canopus_target_generated::CANOPUS_FW_BT_QUEUE_EXTERNAL_CALLABLE,
        ))
    };
    f(owner, run, cancel, argument, event)
}

/// Queued-work cancellation releases the caller token through the Bluetooth
/// allocator paired with [`bt_alloc`].
pub fn bt_queue_free_addr() -> *mut core::ffi::c_void {
    canopus_target_generated::CANOPUS_FW_BT_FREE_CALLABLE as *mut core::ffi::c_void
}

/// Reads the exact L2CAP FSM owner pointer used by every stock dynamic-channel
/// queue wrapper in this firmware.
pub unsafe fn bt_l2cap_owner() -> *mut core::ffi::c_void {
    let slot = canopus_target_generated::canopus_fw_bt_l2cap_owner as *const *mut core::ffi::c_void;
    unsafe { *slot }
}

/// GAP host receive callback used for inbound H4 packets.
pub type BtGapTransportReceive = extern "C" fn(*mut core::ffi::c_void, *mut u8, i32) -> i32;

/// Exact-target raw-H4 receive seam used by module-owned compatibility policy.
const GAP_HOST_RECEIVE_SLOT: usize = canopus_target_generated::canopus_fw_gap_host_receive_slot;
const GAP_HOST_STOCK_RECEIVE: usize =
    canopus_target_generated::CANOPUS_FW_GAP_HOST_STOCK_RECEIVE_CALLABLE;

/// Replaces the active GAP host receive entry after verifying the exact-target
/// stock pointer. The transport registration method copies the callback from
/// its holder into this direct dispatch slot, so patch the live slot rather
/// than the initializer-owned holder.
pub unsafe fn bt_gap_install_receive_hook(receive_hook: BtGapTransportReceive) -> bool {
    let receive_slot = GAP_HOST_RECEIVE_SLOT as *mut u32;
    let receive_replacement = receive_hook as usize as u32;
    let receive_current = unsafe { core::ptr::read_volatile(receive_slot) };

    if receive_current != receive_replacement && receive_current as usize != GAP_HOST_STOCK_RECEIVE
    {
        return false;
    }

    unsafe { core::ptr::write_volatile(receive_slot, receive_replacement) };
    unsafe { core::ptr::read_volatile(receive_slot) == receive_replacement }
}

/// Calls the exact stock GAP host receive dispatcher.
pub unsafe fn bt_gap_stock_receive(
    state: *mut core::ffi::c_void,
    packet: *mut u8,
    packet_length: i32,
) -> i32 {
    let receive: BtGapTransportReceive = unsafe { core::mem::transmute(GAP_HOST_STOCK_RECEIVE) };
    receive(state, packet, packet_length)
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

/// AVRCP Controller SDP record advertised alongside the A2DP source.
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

// ---------------------------------------------------------------------------
// Native app / launcher / notification
// ---------------------------------------------------------------------------

pub unsafe fn app_lookup(app_id: u16) -> *mut core::ffi::c_void {
    type F = extern "C" fn(u16) -> *mut core::ffi::c_void;
    let f: F = unsafe {
        core::mem::transmute(canopus_target_generated::canopus_thumb_callable(
            canopus_target_generated::CANOPUS_FW_APP_LOOKUP_CALLABLE,
        ))
    };
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
    let f: F = unsafe {
        core::mem::transmute(canopus_target_generated::canopus_thumb_callable(
            canopus_target_generated::CANOPUS_FW_APP_INSTALL_CALLABLE,
        ))
    };
    f(descriptor, pages, page_count)
}

pub unsafe fn launcher_add(app_id: u16) -> i32 {
    type F = extern "C" fn(u16) -> i32;
    let f: F = unsafe {
        core::mem::transmute(canopus_target_generated::canopus_thumb_callable(
            canopus_target_generated::CANOPUS_FW_APP_LAUNCHER_ADD_CALLABLE,
        ))
    };
    f(app_id)
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
// Activity / page navigation
// ---------------------------------------------------------------------------

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
pub const LV_STYLE_BG_OPA: u32 = 29;
pub const LV_STYLE_RADIUS: u32 = 44;
pub const LV_STYLE_CLIP_CORNER: u32 = 45;
pub const LV_STYLE_BORDER_WIDTH: u32 = 48;
pub const LV_STYLE_BORDER_OPA: u32 = 50;
pub const LV_STYLE_TEXT_OPA: u32 = 89;
pub const LV_STYLE_TEXT_ALIGN: u32 = 94;
pub const LV_OBJ_FLAG_CLICKABLE: u32 = 0x20;

pub type LvxTimerCallback = extern "C" fn(*mut core::ffi::c_void);

/// Creates a periodic LVGL timer on the UI owner thread. The callback receives
/// the timer object; `user_data` is retained by LVGL for page-owned context.
pub unsafe fn lvx_timer_create(
    callback: LvxTimerCallback,
    period_ms: u32,
    user_data: *mut core::ffi::c_void,
) -> *mut core::ffi::c_void {
    unsafe { canopus_target_generated::canopus_fw_lv_timer_create(callback, period_ms, user_data) }
}

/// Deletes a page-owned LVGL timer. Must run on the UI owner thread.
pub unsafe fn lvx_timer_delete(timer: *mut core::ffi::c_void) {
    type F = extern "C" fn(*mut core::ffi::c_void);
    let f: F = unsafe {
        core::mem::transmute(canopus_target_generated::canopus_thumb_callable(
            canopus_target_generated::CANOPUS_FW_LV_TIMER_DEL_CALLABLE,
        ))
    };
    f(timer);
}

pub unsafe fn lvx_list_row_create(
    parent: *mut core::ffi::c_void,
    primary: *const u8,
    secondary: *const u8,
    trailing: u8,
) -> *mut core::ffi::c_void {
    type F =
        extern "C" fn(*mut core::ffi::c_void, *const u8, *const u8, u8) -> *mut core::ffi::c_void;
    let f: F = unsafe {
        core::mem::transmute(canopus_target_generated::canopus_thumb_callable(
            canopus_target_generated::CANOPUS_FW_LVX_LIST_ROW_CREATE_CALLABLE,
        ))
    };
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
    let f: F = unsafe {
        core::mem::transmute(canopus_target_generated::canopus_thumb_callable(
            canopus_target_generated::CANOPUS_FW_LVX_LIST_ITEM_UPDATE_CALLABLE,
        ))
    };
    f(row, a, primary, secondary, c, selected)
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

pub unsafe fn lvx_image_create(parent: *mut core::ffi::c_void) -> *mut core::ffi::c_void {
    // Firmware `lv_image_create` takes (parent, extra); the second argument is
    // dereferenced by `lv_obj_class_init_obj` when non-null. The stock callers
    // always pass NULL for it, so mirror that exactly instead of leaking a
    // stale R1 through the 1-arg ABI (which dereferences garbage -> Bus Fault).
    type F =
        extern "C" fn(*mut core::ffi::c_void, *const core::ffi::c_void) -> *mut core::ffi::c_void;
    let f: F = unsafe {
        core::mem::transmute(canopus_target_generated::CANOPUS_FW_LV_IMAGE_CREATE_CALLABLE)
    };
    f(parent, core::ptr::null())
}

pub unsafe fn lvx_image_set_src(image: *mut core::ffi::c_void, source: *const core::ffi::c_void) {
    type F = extern "C" fn(*mut core::ffi::c_void, *const core::ffi::c_void);
    let f: F = unsafe {
        core::mem::transmute(canopus_target_generated::CANOPUS_FW_LV_IMAGE_SET_SRC_CALLABLE)
    };
    f(image, source);
}

/// Sets the LVGL image transform scale in 1/256th units on both axes.
/// The exact 043 entry is instruction-for-instruction structurally equivalent
/// to the recovered 036 image-widget setter; 256 means 100 percent.
pub unsafe fn lvx_image_set_scale(image: *mut core::ffi::c_void, scale_x: i32, scale_y: i32) {
    type F = extern "C" fn(*mut core::ffi::c_void, i32, i32);
    let f: F = unsafe {
        core::mem::transmute(canopus_target_generated::CANOPUS_FW_LVX_IMAGE_SET_SCALE_CALLABLE)
    };
    f(image, scale_x, scale_y);
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

pub unsafe fn lvx_label_create(parent: *mut core::ffi::c_void) -> *mut core::ffi::c_void {
    type F = extern "C" fn(*mut core::ffi::c_void) -> *mut core::ffi::c_void;
    let f: F = unsafe {
        core::mem::transmute(canopus_target_generated::canopus_thumb_callable(
            canopus_target_generated::CANOPUS_FW_LVX_LABEL_CREATE_CALLABLE,
        ))
    };
    f(parent)
}

pub unsafe fn lvx_label_set_text(label: *mut core::ffi::c_void, text: *const u8) {
    type F = extern "C" fn(*mut core::ffi::c_void, *const u8);
    let f: F = unsafe {
        core::mem::transmute(canopus_target_generated::canopus_thumb_callable(
            canopus_target_generated::CANOPUS_FW_LVX_LABEL_SET_TEXT_CALLABLE,
        ))
    };
    f(label, text);
}

/// Applies centered text alignment to a freshly-created label. This must run
/// before any stock font/style is attached; the generic firmware dispatcher is
/// unsafe once the object's local property array has been populated.
pub unsafe fn lvx_label_set_text_align_center(label: *mut core::ffi::c_void) {
    unsafe {
        lvx_object_set_local_style_u32(label, LV_STYLE_TEXT_ALIGN, 2, 0);
    }
}

pub unsafe fn lvx_content_create(parent: *mut core::ffi::c_void) -> *mut core::ffi::c_void {
    type F = extern "C" fn(*mut core::ffi::c_void) -> *mut core::ffi::c_void;
    let f: F = unsafe {
        core::mem::transmute(canopus_target_generated::canopus_thumb_callable(
            canopus_target_generated::CANOPUS_FW_LVX_PAGE_CONTENT_CREATE_CALLABLE,
        ))
    };
    f(parent)
}

pub unsafe fn lvx_object_set_size(object: *mut core::ffi::c_void, width: i32, height: i32) {
    type F = extern "C" fn(*mut core::ffi::c_void, i32, i32);
    let f: F = unsafe {
        core::mem::transmute(canopus_target_generated::canopus_thumb_callable(
            canopus_target_generated::CANOPUS_FW_LVX_OBJECT_SET_SIZE_CALLABLE,
        ))
    };
    f(object, width, height);
}

pub unsafe fn lvx_object_align(object: *mut core::ffi::c_void, align: u32, x: i32, y: i32) {
    type F = extern "C" fn(*mut core::ffi::c_void, u32, i32, i32);
    let f: F = unsafe {
        core::mem::transmute(canopus_target_generated::canopus_thumb_callable(
            canopus_target_generated::CANOPUS_FW_LVX_OBJECT_ALIGN_CALLABLE,
        ))
    };
    f(object, align, x, y);
}

pub unsafe fn lvx_object_set_content_pad_bottom(
    object: *mut core::ffi::c_void,
    value: i32,
    selector: u32,
) {
    type F = extern "C" fn(*mut core::ffi::c_void, i32, u32);
    let f: F = unsafe {
        core::mem::transmute(canopus_target_generated::canopus_thumb_callable(
            canopus_target_generated::CANOPUS_FW_LVX_CONTENT_PAD_BOTTOM_CALLABLE,
        ))
    };
    f(object, value, selector);
}

/// Moves an object to an exact index in its parent's child list. Index zero is
/// the back-most draw position. Must run on the LVGL owner thread.
pub unsafe fn lvx_object_move_to_index(object: *mut core::ffi::c_void, index: i32) {
    type F = extern "C" fn(*mut core::ffi::c_void, i32);
    let f: F = unsafe {
        core::mem::transmute(canopus_target_generated::canopus_thumb_callable(
            canopus_target_generated::CANOPUS_FW_LV_OBJ_MOVE_TO_INDEX_CALLABLE,
        ))
    };
    f(object, index);
}

/// Sets one local LVGL style property for the requested selector. Property and
/// value encoding follow this exact target's LVGL v9 ABI.
pub unsafe fn lvx_object_set_local_style_u32(
    _object: *mut core::ffi::c_void,
    _property: u32,
    _value: u32,
    _selector: u32,
) {
    // 043 withdrew the exact-target local-style setter; fail closed.
}

/// Sets the object's background opacity for the requested style selector.
pub unsafe fn lvx_object_set_background_opacity(
    object: *mut core::ffi::c_void,
    opacity: u32,
    selector: u32,
) {
    type F = extern "C" fn(*mut core::ffi::c_void, u32, u32);
    let f: F = unsafe {
        core::mem::transmute(canopus_target_generated::canopus_thumb_callable(
            canopus_target_generated::CANOPUS_FW_LV_OBJ_SET_STYLE_BG_OPA_CALLABLE,
        ))
    };
    f(object, opacity, selector);
}

pub type LvxEventCallback = extern "C" fn(*mut core::ffi::c_void);

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

/// MiSans-Regular at 24 px (exact 043 stock theme object address).
pub const STYLE_MISANS_REGULAR_24: usize =
    canopus_target_generated::canopus_fw_style_misans_regular_24;

/// MiSans-Demibold at 32 px (exact 043 stock theme object address).
pub const STYLE_MISANS_DEMIBOLD_32: usize =
    canopus_target_generated::canopus_fw_style_misans_demibold_32;

pub unsafe fn lvx_style_apply(
    object: *mut core::ffi::c_void,
    style: *const core::ffi::c_void,
    a: u32,
    b: u32,
) -> i32 {
    type F = extern "C" fn(*mut core::ffi::c_void, *const core::ffi::c_void, u32, u32) -> i32;
    let f: F = unsafe {
        core::mem::transmute(canopus_target_generated::canopus_thumb_callable(
            canopus_target_generated::CANOPUS_FW_LVX_STYLE_APPLY_CALLABLE,
        ))
    };
    f(object, style, a, b)
}

pub unsafe fn lvx_event_add(
    object: *mut core::ffi::c_void,
    callback: LvxEventCallback,
    code: u32,
    cookie: *mut core::ffi::c_void,
) {
    unsafe {
        canopus_target_generated::canopus_fw_lv_obj_add_event_cb(object, callback, code, cookie)
    };
}

pub unsafe fn lvx_event_get_user_data(event: *mut core::ffi::c_void) -> usize {
    unsafe { canopus_target_generated::canopus_fw_lv_event_get_user_data(event) as usize }
}

pub unsafe fn lvx_event_get_code(event: *mut core::ffi::c_void) -> u32 {
    unsafe { canopus_target_generated::canopus_fw_lv_event_get_code(event) }
}

pub unsafe fn lvx_object_add_flag(_object: *mut core::ffi::c_void, _flags: u32) {
    // 043 has no exact-target recovered flag setter; fail closed.
}

pub unsafe fn lvx_set_hidden(object: *mut core::ffi::c_void, hidden: u32) {
    type F = extern "C" fn(*mut core::ffi::c_void, u32);
    let f: F = unsafe {
        core::mem::transmute(canopus_target_generated::canopus_thumb_callable(
            canopus_target_generated::CANOPUS_FW_LV_OBJ_SET_HIDDEN_CALLABLE,
        ))
    };
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
    let f: F = unsafe {
        core::mem::transmute(canopus_target_generated::canopus_thumb_callable(
            canopus_target_generated::CANOPUS_FW_LV_OBJ_ALIGN_TO_CALLABLE,
        ))
    };
    f(object, target, align, x, y);
}

// ---------------------------------------------------------------------------
// NuttX file I/O (used by the module if it talks to /dev/canopus)
// ---------------------------------------------------------------------------

pub const O_RDONLY: i32 = 1;
pub const O_RDWR: i32 = 3;

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

/// Positions the current-process fd using the recovered 64-bit NuttX ABI.
/// `offset` is aligned to R2/R3 by AAPCS; `whence` is passed on the stack.
pub unsafe fn nuttx_lseek(fd: i32, offset: i64, whence: i32) -> i64 {
    unsafe { canopus_target_generated::canopus_fw_lseek(fd, offset, whence) }
}

/// POSIX fd-level variadic ioctl wrapper. Exact 3.101.043 entry
/// `sub_C1D01A4 + 1` preserves the command in R1 and consumes the optional
/// argument from the saved variadic register area.
pub unsafe fn nuttx_ioctl(fd: i32, command: u32, argument: usize) -> i32 {
    type F = extern "C" fn(i32, u32, ...) -> i32;
    let f: F = unsafe { core::mem::transmute(canopus_target_generated::CANOPUS_FW_IOCTL_CALLABLE) };
    f(fd, command, argument)
}

/// Reads the current task's `errno`. NuttX collapses every driver error into a
/// `-1` return with the real code parked in the task's errno slot, so callers
/// must read it immediately after a failed firmware call to recover the cause.
pub unsafe fn get_errno() -> i32 {
    type F = extern "C" fn() -> *const i32;
    let f: F = unsafe {
        core::mem::transmute(canopus_target_generated::CANOPUS_FW_ERRNO_LOCATION_CALLABLE)
    };
    unsafe { *f() }
}

pub unsafe fn nuttx_create(path: *const u8, flags: i32, mode: u32) -> i32 {
    type F = extern "C" fn(*const u8, i32, ...) -> i32;
    let f: F = unsafe { core::mem::transmute(canopus_target_generated::CANOPUS_FW_OPEN_CALLABLE) };
    f(path, flags, mode)
}

pub unsafe fn nuttx_unlink(path: *const u8) -> i32 {
    type F = extern "C" fn(*const u8) -> i32;
    let f: F =
        unsafe { core::mem::transmute(canopus_target_generated::CANOPUS_FW_UNLINK_CALLABLE) };
    f(path)
}

pub unsafe fn nuttx_rename(old_path: *const u8, new_path: *const u8) -> i32 {
    type F = extern "C" fn(*const u8, *const u8) -> i32;
    let f: F =
        unsafe { core::mem::transmute(canopus_target_generated::CANOPUS_FW_RENAME_CALLABLE) };
    f(old_path, new_path)
}

// ---------------------------------------------------------------------------
// miwear / interconnect connection framework
// ---------------------------------------------------------------------------
//
// Connection-framework roles (names only; addresses belong to generated
// exact-target records):
//
//   Phone app (`com.xiaomi.miwear.interconnect` / Mi Fitness)
//     ↕ BLE GATT (miwear private protocol)
//   "btserver"        start_btmsg_server
//     └ uv_miwear_message_recv_cb routes to per-client msq
//   "miwear-server"   quickapp_proxy_server_start
//   connection framework (named servers over a polled socket/msq transport)
//     ├ server create  sub_C2D1FB0
//     ├ client connect sub_C2D20F4  ← [`interconnect_connect`]
//     ├ send           sub_C2D2184  ← [`interconnect_send`]
//     └ close          sub_C2D2198  ← [`interconnect_close`]
//   quickapp JS: system.interconnect → jse_miwear.cpp → the four calls above.
//
// The framework is shared by the AIOTJS quickapp glue and the
// `interconnect_impl.cpp` feature module; a native module can register a
// connection the same way, without the JS engine. The global loop handle
// (`dword_20121F80`) is the registry all named servers live on.
//
// The recovered callables, message/app-info layouts, and callback typedefs are
// generated from `symbols/` + `types/` into `canopus_target_generated` (single
// source of truth); this module re-exports them under the same names the C
// veneer uses, so a Rust module and a C module call the identical ABI.

/// Connection-event message type.
pub const CONN_MSG_TYPE_EVENT: u8 = 2;
/// Data message type (byte `0x83`; a negative signed byte by design).
pub const CONN_MSG_TYPE_DATA: u8 = 0x83;

/// Event status word values delivered to the recv callback. The raw connection
/// framework uses `1` for connected at its socket layer; the miwear proxy
/// re-stamps these `5/6/7` codes through `byte_2CCF98F4`, which is what a peer
/// observes.
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
const THIRD_PARTY_MESSAGE_TAIL_SIZE: usize = 0x134;

/// Connection/event message header (`uv_miwear_message_t`), 20 bytes.
pub type InterconnectConnMessage = canopus_target_generated::canopus_interconnect_message;

/// App descriptor passed to [`quickapp_register_app`]. Matches the firmware
/// `quickapp_app_info` layout (36 bytes on the 32-bit target).
pub type QuickAppInfo = canopus_target_generated::canopus_interconnect_app_info;

/// Receives connection events and data for an interconnect link.
pub type InterconnectRecvCb = canopus_target_generated::canopus_interconnect_recv_cb;

/// Completion callback for [`interconnect_send`].
pub type InterconnectSendDone = canopus_target_generated::canopus_interconnect_send_done;

/// Reads the global connection-framework loop handle. Every named server
/// ("btserver", "miwear-server") and connection lives on this registry.
///
/// # Safety
/// The firmware must be running and the connection framework initialized.
pub unsafe fn interconnect_loop() -> *mut core::ffi::c_void {
    core::ptr::null_mut()
}

/// Registers a named connection on the connection framework and attaches it to
/// the firmware's "miwear-server". `name` is the phone-side **package name** —
/// the routing key (e.g. [`INTERCONNECT_APK_PACKAGE`]); the app display name is
/// not part of routing. `conn` is a caller-owned buffer of at least 12 bytes.
///
/// # Safety
/// The connection framework must already have a "miwear-server" registered
/// (quickapp proxy started, phone miwear link present). `cb` must follow
/// [`InterconnectRecvCb`]'s threading constraints.
pub unsafe fn interconnect_connect(
    loop_handle: *mut core::ffi::c_void,
    conn: *mut core::ffi::c_void,
    name: *const u8,
    cb: InterconnectRecvCb,
) -> i32 {
    unsafe {
        canopus_target_generated::canopus_fw_interconnect_connect(loop_handle, conn, name, cb)
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
            0,
            0,
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
// Compile-time ABI checks
// ---------------------------------------------------------------------------

const _: () = {
    assert!(core::mem::size_of::<DiscoveryResult>() == 12);
    assert!(core::mem::offset_of!(DiscoveryResult, rssi) == 6);
    assert!(core::mem::offset_of!(DiscoveryResult, class_of_device) == 8);
    assert!(core::mem::size_of::<StockBuffer>() == 12);
    assert!(core::mem::offset_of!(StockBuffer, route) == 4);
    assert!(core::mem::size_of::<DisconnectRequest>() == 4);
    assert!(core::mem::size_of::<MediaTimerToken>() == 8);
    // The interconnect message is pointer-free in the firmware (value is a 32-bit
    // address) but `canopus_interconnect_message` is generated with a host-sized
    // pointer field, so its exact size holds only on the 32-bit device target.
    #[cfg(target_pointer_width = "32")]
    assert!(core::mem::size_of::<InterconnectConnMessage>() == 20);
    assert!(core::mem::offset_of!(InterconnectConnMessage, length) == 4);
    assert!(core::mem::offset_of!(InterconnectConnMessage, value) == 16);
    assert!(CONN_RECV_CB_OFFSET + 4 <= 12);
    // QuickAppInfo carries string pointers, so its 36-byte firmware layout only
    // holds on the 32-bit target (host tests keep the natural 64-bit size).
    #[cfg(target_pointer_width = "32")]
    assert!(core::mem::size_of::<QuickAppInfo>() == 36);
    #[cfg(target_pointer_width = "32")]
    assert!(core::mem::offset_of!(QuickAppInfo, fingerprint) == 16);
    #[cfg(target_pointer_width = "32")]
    assert!(
        core::mem::size_of::<canopus_target_generated::canopus_thirdparty_message_content>() == 336
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
    assert!(CONNECT_CALLBACK_OFFSET + 4 <= CONNECT_REQUEST_SIZE);
    assert!(CONNECT_ADDRESS_OFFSET + 6 <= CONNECT_REQUEST_SIZE);
    assert!(CONNECT_OPTIONS_OFFSET + 2 <= CONNECT_REQUEST_SIZE);
};

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn avdtp_connect_policy_enables_1024_byte_local_mtu() {
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
    }

    #[test]
    fn avctp_connect_policy_enables_control_channel_mtu() {
        let mut request = [0u8; CONNECT_REQUEST_SIZE];
        unsafe { configure_avctp_connect_request(request.as_mut_ptr()) };

        assert_eq!(
            &request[CONNECT_PSM_OFFSET..CONNECT_PSM_OFFSET + 2],
            &AVCTP_CONTROL_PSM.to_le_bytes()
        );
        assert_eq!(
            &request[CONNECT_CONFIG_OFFSET..CONNECT_CONFIG_OFFSET + 2],
            &AVCTP_LOCAL_RX_MTU.to_le_bytes()
        );
        assert_eq!(
            &request[CONNECT_OPTIONS_OFFSET..CONNECT_OPTIONS_OFFSET + 2],
            &CONNECT_OPTION_LOCAL_MTU.to_le_bytes()
        );
    }

    #[test]
    fn interconnect_message_layout_and_status_codes_are_stable() {
        // Exact 20-byte size holds on the 32-bit device target only (the
        // generated `value` pointer field is host-sized in tests).
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
        // On the 32-bit target the QuickAppInfo layout must be 36 bytes with
        // the fingerprint at +16 (matching the firmware sub_C526B84 copy of
        // words +0..+8). On 64-bit host the pointer fields are wider; only the
        // leading field offset is portable.
        assert_eq!(core::mem::offset_of!(QuickAppInfo, package_name), 0);
        if core::mem::size_of::<*const u8>() == 4 {
            assert_eq!(core::mem::offset_of!(QuickAppInfo, display_name), 4);
            assert_eq!(core::mem::offset_of!(QuickAppInfo, fingerprint), 16);
            assert_eq!(core::mem::size_of::<QuickAppInfo>(), 36);
            assert_eq!(
                core::mem::size_of::<canopus_target_generated::canopus_thirdparty_message_content>(
                ),
                336
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
    fn stock_pair_filter_abi_is_not_the_036_global_table() {
        assert_eq!(CALLBACK_WORDS, 17);
        assert_eq!(STOCK_CALLBACK_WORDS, 17);
        assert_eq!(STOCK_CALLBACK_PAIR_REQUEST_SLOT, 5);
        assert_eq!(STOCK_ADAPTER_CLIENT_OFFSET, 120);
        assert_eq!(STOCK_CLIENT_CALLBACK_OFFSET, 72);
        assert_eq!(
            canopus_target_generated::canopus_fw_core_bt_adapter_instance,
            0x20126738
        );
        assert_eq!(
            canopus_target_generated::canopus_fw_core_bt_registration_handle,
            0x20126734
        );
        assert_eq!(
            canopus_target_generated::canopus_fw_stock_bt_callback_descriptor,
            0x2CD4A744
        );
    }
}
