//! Target-private full-trust bindings for `xiaomi-band-10-pro-3.101.030`.
//!
//! These are **not** part of `canopus-target-generated` (the public, audited
//! per-target bindings). They expose recovered launcher, LVX, Bluetooth, L2CAP,
//! SDP, timer, and miwear/interconnect connection-framework calls that the
//! generated public crate deliberately leaves restricted or FORBIDDEN. They are
//! valid only for firmware SHA-256 `f701a84f…dccd225b`, must never be called
//! before [`canopus_target_generated::canopus_identity_guard`] passes, and are
//! the source of every absolute address a module links. Future targets provide
//! a sibling backend module with the same interface instead of editing this one.
//!
//! Every recovered callable address carries the Thumb bit. All functions are
//! `unsafe`; the module is responsible for state, ownership, and lifecycle
//! discipline.

pub const TARGET_ID: &str = "xiaomi-band-10-pro-3.101.030";

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
    unsafe { core::ptr::read_volatile(CORE_BT_BIND_STATE_ADDRESS as *const u8) }
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

pub unsafe fn bt_buffer_new(payload_length: u16, headroom: u16) -> *mut StockBuffer {
    type F = extern "C" fn(u16, u16) -> *mut StockBuffer;
    let f: F = unsafe { core::mem::transmute(0x0C7D294Dusize) };
    f(payload_length, headroom)
}

/// Queues an L2CAP connect request. Returns the nonzero queue node on accepted
/// submission and 0 when insertion fails; it is not a zero-on-success status.
///
/// Firmware `0x0C7ED49C` delegates directly to the owner-only FSM work list at
/// `0x0C7D3318`. Invoke it from an already-running owner callback; callers on an
/// unrelated thread must first dispatch through [`bt_queue_external`].
pub unsafe fn bt_l2cap_connect(request: *const core::ffi::c_void) -> u32 {
    type F = extern "C" fn(*const core::ffi::c_void) -> u32;
    let f: F = unsafe { core::mem::transmute(0x0C7ED49Dusize) };
    f(request)
}

/// Queues L2CAP teardown through firmware `0x0C7ED54C`. Invoke from an active
/// Bluetooth-owner callback; the wrapper only inserts owner-local work.
pub unsafe fn bt_l2cap_disconnect(request: *const DisconnectRequest) -> i32 {
    type F = extern "C" fn(*const DisconnectRequest) -> i32;
    let f: F = unsafe { core::mem::transmute(0x0C7ED54Dusize) };
    f(request)
}

/// Queues one CID submission through firmware `0x0C7ED578`. Invoke from an
/// active Bluetooth-owner callback; the wrapper only inserts owner-local work.
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
/// Timer insertion at `0x0C7D2C00` updates the timer list but does not signal the
/// sleeping FSM semaphore; use it from an active owner callback, or pair delayed
/// work with a separately guaranteed external wake.
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

/// Queues work through the firmware's external-event ring and wakes the Bluetooth
/// FSM when the ring transitions from empty to non-empty. IDA `0x0C7D335C`
/// inserts the event while holding the FSM lock and signals its semaphore at
/// `0x0C828580`; this is the correct entry point for callers outside the owner.
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
    let f: F = unsafe { core::mem::transmute(0x0C7D335Dusize) };
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

/// GAP host receive callback used for inbound H4 packets.
pub type BtGapTransportReceive = extern "C" fn(*mut core::ffi::c_void, *mut u8, i32) -> i32;

const GAP_HOST_RECEIVE_SLOT: usize = 0x20137EA4;
const GAP_HOST_STOCK_RECEIVE: usize = 0x0C7D3E0D;

/// Replaces the active GAP host receive entry after verifying the exact-target
/// stock pointer. Powering Bluetooth on rebuilds the dispatcher, so callers
/// reassert the hook after adapter ON.
///
/// # Safety
/// The hook must preserve the declared ABI, forward every unmatched packet,
/// and remain resident until reboot.
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
///
/// # Safety
/// The arguments must satisfy [`BtGapTransportReceive`]'s stock H4-buffer
/// contract.
pub unsafe fn bt_gap_stock_receive(
    state: *mut core::ffi::c_void,
    packet: *mut u8,
    packet_length: i32,
) -> i32 {
    let receive: BtGapTransportReceive = unsafe { core::mem::transmute(GAP_HOST_STOCK_RECEIVE) };
    receive(state, packet, packet_length)
}

/// Removes the exact BES mHDT capability option (`7F 01 01`) from an inbound
/// Configuration Request for `local_cid`, leaving all standard options intact.
/// The stock parser in this firmware was built without mHDT support and would
/// otherwise reject this peer capability as an unknown mandatory option.
///
/// The input excludes the H4 type byte. On success, ACL/L2CAP/command lengths
/// are reduced in place and the returned value is the new ACL packet length.
pub fn strip_l2cap_mhdt_option(payload: &mut [u8], local_cid: u16) -> Option<usize> {
    const ACL_HEADER_SIZE: usize = 4;
    const L2CAP_HEADER_SIZE: usize = 4;
    const SIGNALING_CID: u16 = 1;
    const CONFIGURATION_REQUEST: u8 = 0x04;
    const MHDT_TYPE: u8 = 0x7F;
    const MHDT_LENGTH: u8 = 1;
    const MHDT_SUPPORTED: u8 = 1;
    const MHDT_OPTION_SIZE: usize = 3;

    if local_cid <= 0x3F || payload.len() < ACL_HEADER_SIZE + L2CAP_HEADER_SIZE {
        return None;
    }
    // Only an ACL start packet carries the L2CAP header. The observed mHDT
    // request is a complete 19-byte ACL payload (PB=2); continuation fragments
    // are forwarded untouched rather than being misparsed as new L2CAP SDUs.
    let packet_boundary = (u16::from_le_bytes([payload[0], payload[1]]) >> 12) & 0x3;
    if !matches!(packet_boundary, 0 | 2) {
        return None;
    }
    let acl_length = u16::from_le_bytes([payload[2], payload[3]]) as usize;
    let acl_end = ACL_HEADER_SIZE.checked_add(acl_length)?;
    if acl_end > payload.len() || acl_length < L2CAP_HEADER_SIZE {
        return None;
    }
    let l2cap_length = u16::from_le_bytes([payload[4], payload[5]]) as usize;
    let l2cap_end = (ACL_HEADER_SIZE + L2CAP_HEADER_SIZE).checked_add(l2cap_length)?;
    if u16::from_le_bytes([payload[6], payload[7]]) != SIGNALING_CID || l2cap_end > acl_end {
        return None;
    }

    let mut command = ACL_HEADER_SIZE + L2CAP_HEADER_SIZE;
    while command + 4 <= l2cap_end {
        let command_length =
            u16::from_le_bytes([payload[command + 2], payload[command + 3]]) as usize;
        let command_end = command.checked_add(4 + command_length)?;
        if command_end > l2cap_end {
            return None;
        }
        if payload[command] == CONFIGURATION_REQUEST && command_length >= 4 {
            let destination_cid = u16::from_le_bytes([payload[command + 4], payload[command + 5]]);
            let flags = u16::from_le_bytes([payload[command + 6], payload[command + 7]]);
            if destination_cid == local_cid && flags == 0 {
                let mut option = command + 8;
                while option + 2 <= command_end {
                    let option_length = payload[option + 1] as usize;
                    let option_end = option.checked_add(2 + option_length)?;
                    if option_end > command_end {
                        return None;
                    }
                    if payload[option] == MHDT_TYPE
                        && payload[option + 1] == MHDT_LENGTH
                        && payload[option + 2] == MHDT_SUPPORTED
                    {
                        payload.copy_within(option_end..acl_end, option);
                        payload[acl_end - MHDT_OPTION_SIZE..acl_end].fill(0);
                        let new_command_length = command_length - MHDT_OPTION_SIZE;
                        let new_l2cap_length = l2cap_length - MHDT_OPTION_SIZE;
                        let new_acl_length = acl_length - MHDT_OPTION_SIZE;
                        payload[command + 2..command + 4]
                            .copy_from_slice(&(new_command_length as u16).to_le_bytes());
                        payload[4..6].copy_from_slice(&(new_l2cap_length as u16).to_le_bytes());
                        payload[2..4].copy_from_slice(&(new_acl_length as u16).to_le_bytes());
                        return Some(acl_end - MHDT_OPTION_SIZE);
                    }
                    option = option_end;
                }
            }
        }
        command = command_end;
    }
    None
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

pub type LvxTimerCallback = extern "C" fn(*mut core::ffi::c_void);

/// Creates a periodic LVGL timer on the UI owner thread. The callback receives
/// the timer object; `user_data` is retained by LVGL for page-owned context.
pub unsafe fn lvx_timer_create(
    callback: LvxTimerCallback,
    period_ms: u32,
    user_data: *mut core::ffi::c_void,
) -> *mut core::ffi::c_void {
    type F = extern "C" fn(LvxTimerCallback, u32, *mut core::ffi::c_void) -> *mut core::ffi::c_void;
    let f: F = unsafe { core::mem::transmute(0x0C588759usize) };
    f(callback, period_ms, user_data)
}

/// Deletes a page-owned LVGL timer. Must run on the UI owner thread.
pub unsafe fn lvx_timer_delete(timer: *mut core::ffi::c_void) {
    type F = extern "C" fn(*mut core::ffi::c_void);
    let f: F = unsafe { core::mem::transmute(0x0C587EF1usize) };
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

pub const O_RDONLY: i32 = 1;
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
// miwear / interconnect connection framework
// ---------------------------------------------------------------------------
//
// Firmware map (IDA `vela_ap.bin.i64`; exact target only):
//
//   Phone app (`com.xiaomi.miwear.interconnect` / Mi Fitness)
//     ↕ BLE GATT (miwear private protocol)
//   "btserver"        start_btmsg_server        @0x0CAA37E8
//     └ uv_miwear_message_recv_cb @0x0CAA35A0 routes to per-client msq
//   "miwear-server"   quickapp_proxy_server_start @0x0C526628
//   connection framework (named servers over a polled socket/msq transport)
//     ├ server create  sub_C2D1EF0
//     ├ client connect sub_C2D2034  ← [`interconnect_connect`]
//     ├ send           sub_C2D20C4  ← [`interconnect_send`]
//     └ close          sub_C2D2198  ← [`interconnect_close`]
//   quickapp JS: system.interconnect → jse_miwear.cpp → the four calls above.
//
// The framework is shared by the AIOTJS quickapp glue and the
// `interconnect_impl.cpp` feature module; a native module can register a
// connection the same way, without the JS engine. The global loop handle
// (`dword_20121F90`) is the registry all named servers live on.

/// Connection/event message header (`uv_miwear_message_t`), 20 bytes.
///
/// Kept pointer-free (like [`StockBuffer`]) so the fixed-width 32-bit layout
/// holds on the host test toolchain as well as the Cortex-M33 target.
///
/// For data messages (`type_ == [`CONN_MSG_TYPE_DATA`]`) `length` is the payload
/// length and `value` is the 32-bit payload address. For connection events
/// (`type_ == [`CONN_MSG_TYPE_EVENT`]`) `value` is the address of a 32-bit
/// status word whose values are the `CONN_STATUS_*` codes below.
#[repr(C)]
#[derive(Copy, Clone, Debug)]
pub struct InterconnectConnMessage {
    pub type_: u8,
    pub _pad_type: [u8; 3],
    /// Payload length for data messages; `8` for connection events.
    pub length: u32,
    pub _reserved: [u32; 2],
    /// 32-bit payload address (data) or status-word address (events).
    pub value: u32,
}

/// Connection-event message type.
pub const CONN_MSG_TYPE_EVENT: u8 = 2;
/// Data message type (byte `0x83`; a negative signed byte by design).
pub const CONN_MSG_TYPE_DATA: u8 = 0x83;

/// Event status word values delivered to [`InterconnectRecvCb`]. The raw
/// connection framework uses `1` for connected at its socket layer; the miwear
/// proxy re-stamps these `5/6/7` codes through `byte_2CCF98F4`, which is what
/// the AIOTJS glue and a native peer observe (`CONN_STATUS_CONNECTED`,
/// `CONN_STATUS_DISCONNECTED`, `CONN_STATUS_UNINSTALLED`,
/// `CONN_STATUS_FAILED`, `CONN_STATUS_CLOSED`).
pub const CONN_STATUS_CONNECTED: i32 = 5;
pub const CONN_STATUS_DISCONNECTED: i32 = 6;
pub const CONN_STATUS_UNINSTALLED: i32 = 7;
pub const CONN_STATUS_FAILED: i32 = 2;
pub const CONN_STATUS_CLOSED: i32 = 3;

/// Connection-object layout written by [`interconnect_connect`]: `conn[0]` is
/// the firmware node, `conn[4]` the recv callback, `conn[8]` the active flag.
/// A module owns a buffer of at least 12 bytes for the lifetime of the link.
pub const CONN_RECV_CB_OFFSET: usize = 4;

/// Default interconnect phone-side package. The firmware reads it from the
/// `interconnect.appname` config property (`property_get("interconnect.appname")`
/// at `0x0C66B8C0`); every `*.appname` property holds a `com.xiaomi.miwear.*`
/// package name. The phone routes messages to a connection by this package
/// name only — the app display name is not part of the routing. A native module
/// is not limited to this value: it may register and connect its own package
/// name (see [`quickapp_register_app`]).
pub const INTERCONNECT_APK_PACKAGE: &[u8] = b"com.xiaomi.miwear.interconnect\0";

/// App descriptor passed to [`quickapp_register_app`]. Matches the firmware
/// `quickapp_app_info` layout (36 bytes on the 32-bit target).
///
/// `package_name` is the phone-side routing key; `display_name` and `icon_file`
/// are UI metadata; `extra` mirrors a string slot the stock registrar logs;
/// `fingerprint` is a 20-byte blob compared by `quickapp_get_appinfo`.
#[repr(C)]
#[derive(Copy, Clone, Debug)]
pub struct QuickAppInfo {
    /// `com.*` package name — the routing key the phone uses.
    pub package_name: *const u8,
    /// Human-readable app name (not used for routing).
    pub display_name: *const u8,
    /// Icon file name under `/data/app/<package>/` (e.g. `b"icon.png\0"`).
    pub icon_file: *const u8,
    /// Extra string slot (the stock registrar echoes it in its log).
    pub extra: *const u8,
    /// 20-byte app fingerprint verified by `quickapp_get_appinfo`.
    pub fingerprint: [u8; 20],
}

/// Registers a package in the quickapp routing registry. The firmware seeds the
/// registry at bootup from `rpk_info.json` (`quickapp_bootup_register`); the
/// watch→phone send path (`quickapp_send_wearmsg`) looks a package up here, so
/// a native module that wants its own package name must register it first.
///
/// `app_id` is a `u16` identifier; the stock registrar hands out sequential
/// ids. `info` must stay valid for the call. Returns `0` on success, `-1` on
/// allocation failure.
///
/// # Safety
/// `info` must point at a valid [`QuickAppInfo`]; every string field must be a
/// NUL-terminated address readable by the firmware.
pub unsafe fn quickapp_register_app(app_id: u16, info: *const QuickAppInfo) -> i32 {
    type F = extern "C" fn(u16, *const QuickAppInfo) -> i32;
    let f: F = unsafe { core::mem::transmute(0x0C527E39usize) };
    f(app_id, info)
}

/// Receives connection events and data for an interconnect link.
///
/// `status` is nonzero on events; `msg` is either a connection-event message or
/// a data message; `name` is the connection name registered with
/// [`interconnect_connect`]. Runs on the connection-framework owner thread, so
/// the callback must never block and must re-enter the module Core only through
/// a non-blocking lock.
pub type InterconnectRecvCb = extern "C" fn(
    conn: *mut core::ffi::c_void,
    status: i32,
    msg: *const InterconnectConnMessage,
    name: *const u8,
);

/// Completion callback for [`interconnect_send`]: `(conn, status, msg, arg)`.
/// Invoked when the queued message is delivered or fails; `status` is `0` on
/// success.
pub type InterconnectSendDone = extern "C" fn(
    conn: *mut core::ffi::c_void,
    status: i32,
    msg: *const InterconnectConnMessage,
    arg: *mut core::ffi::c_void,
);

/// Reads the global connection-framework loop handle. Every named server
/// ("btserver", "miwear-server") and connection lives on this registry.
pub unsafe fn interconnect_loop() -> *mut core::ffi::c_void {
    let slot = 0x20121F90usize as *const *mut core::ffi::c_void;
    unsafe { *slot }
}

/// Registers a named connection on the connection framework and attaches it to
/// the firmware's "miwear-server". This is the native equivalent of the
/// quickapp `system.interconnect` connect path (`interconnect_impl.cpp`
/// `onRequired` and `jse_miwear.cpp` `__miwear_connect`).
///
/// `conn` is a caller-owned buffer of at least 12 bytes that stays alive for
/// the link; the firmware writes the node pointer, `cb`, and an active flag into
/// it. `name` is the phone-side **package name** — the routing key the phone
/// uses to deliver messages (e.g. [`INTERCONNECT_APK_PACKAGE`]); it is copied
/// into a 64-byte firmware slot. The app display name is not part of the
/// routing. Returns `0` on accepted registration, `-22` on a null argument,
/// `-12` on allocation failure.
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
    type F = extern "C" fn(
        *mut core::ffi::c_void,
        *mut core::ffi::c_void,
        *const u8,
        InterconnectRecvCb,
    ) -> i32;
    let f: F = unsafe { core::mem::transmute(0x0C2D2035usize) };
    f(loop_handle, conn, name, cb)
}

/// Queues one message to the connection framework. `handle` is the `conn` from
/// [`interconnect_connect`] (send to self) or a server handle (broadcast with
/// `name == null`, or targeted at the connection named `name`). `done` is called
/// once the message is accepted or fails; the payload referenced by `msg` must
/// remain valid until then.
///
/// # Safety
/// `msg` must point at a valid [`InterconnectConnMessage`] (type data, length,
/// payload) that outlives the asynchronous send. `done` and `arg` follow
/// [`InterconnectSendDone`].
pub unsafe fn interconnect_send(
    handle: *mut core::ffi::c_void,
    name: *const u8,
    msg: *const InterconnectConnMessage,
    done: InterconnectSendDone,
    arg: *mut core::ffi::c_void,
) -> i32 {
    type F = extern "C" fn(
        *mut core::ffi::c_void,
        *const u8,
        *const InterconnectConnMessage,
        InterconnectSendDone,
        *mut core::ffi::c_void,
    ) -> i32;
    let f: F = unsafe { core::mem::transmute(0x0C2D20C5usize) };
    f(handle, name, msg, done, arg)
}

/// Closes an interconnect connection registered by [`interconnect_connect`].
pub unsafe fn interconnect_close(conn: *mut core::ffi::c_void) -> i32 {
    type F = extern "C" fn(*mut core::ffi::c_void) -> i32;
    let f: F = unsafe { core::mem::transmute(0x0C2D2199usize) };
    f(conn)
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
    fn interconnect_message_layout_and_status_codes_are_stable() {
        assert_eq!(core::mem::size_of::<InterconnectConnMessage>(), 20);
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
        }
    }

    #[test]
    fn mhdt_filter_only_changes_exact_target_configuration_request() {
        let original = [
            0x81, 0x20, 0x13, 0x00, 0x0F, 0x00, 0x01, 0x00, 0x04, 0x25, 0x0B, 0x00, 0x41, 0x00,
            0x00, 0x00, 0x01, 0x02, 0x04, 0x0B, 0x7F, 0x01, 0x01,
        ];
        let mut continuation = original;
        continuation[1] = 0x10;
        assert_eq!(strip_l2cap_mhdt_option(&mut continuation, 0x0041), None);
        assert_eq!(continuation[20..23], [0x7F, 0x01, 0x01]);

        let mut wrong_cid = original;
        assert_eq!(strip_l2cap_mhdt_option(&mut wrong_cid, 0x0042), None);
        assert_eq!(wrong_cid, original);

        let mut request = original;
        assert_eq!(strip_l2cap_mhdt_option(&mut request, 0x0041), Some(20));
        assert_eq!(&request[2..4], &[0x10, 0x00]);
        assert_eq!(&request[4..6], &[0x0C, 0x00]);
        assert_eq!(&request[10..12], &[0x08, 0x00]);
        assert_eq!(&request[16..20], &[0x01, 0x02, 0x04, 0x0B]);
        assert_eq!(&request[20..23], &[0, 0, 0]);
        assert_eq!(strip_l2cap_mhdt_option(&mut request[..20], 0x0041), None);
    }
}
