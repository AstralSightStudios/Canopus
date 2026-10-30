# p67 .043 native integration: corrected contracts

These contracts were checked against the exact AP image SHA-256
`519307675665e4866d722a8119a98589c397b614ac3294cb87bfc86de45756ec`,
XIP base `0x0C0C0000`. They supersede the incompatible-shape claims in the
older `p67-shim-contract.md`. They establish the native ABI; they do not claim
radio or physical-device validation.

## GAP uses the generic IPC client

`bt_le_start_advertising(flags) 0xC70ED94` is the MiWear application wrapper,
not the only advertising entry. It constructs a 28-byte parameter structure
and calls `ble_advertising_start 0xC3B118C(adapter, params, data, length,
scan_response, response_length, callbacks)`. The structure has mode at +6,
transmit power at +15 and interval units at +16. The generic producer copies
all seven parameter words into command 67 and consumes the caller's advertising
data. Astrolink uses that generic entry with its own payload.

`gap_scan_start_impl 0xC3B1374` and `ble_scan_stop 0xC3B140C` issue commands
237 and 238. The producer reads a parameter word and the byte at +4, exactly
five meaningful bytes; its stack allocation/padding does not make those opaque
commands. Its callback table has size 16. The report begins with the six peer
address bytes; RSSI is +7, address type +8, packet type +9, data length +10,
and AD payload +12. The older report description mistook payload for address.

The process-lifetime stock MiWear client is the pointer at `0x20126738`, loaded
by getter `0xC70B180`. Its business loop is +56: initialization `0xC70B058`
passes the current loop to setter `0xC3AF26C`, which stores it at +56. The
other adapter instance recorded at `0x2013DBE4` does not supply this contract.

## Queue: five arguments and two callback roles

`bt_queue_external 0xC7EB9CC(owner, run, cancel, arg, flag)` has five arguments.
The queue page stores owner/arg/run/cancel/flag at slot +8/+12/+16/+20/+24.
The true dispatcher is `0xC7EB7C4`: a live owner chooses `run`, and a withdrawn
owner chooses `cancel`; both receive `(owner, event, arg)`. The lock-null path
calls `cancel(0, 221, arg)` synchronously. Existing Astrolink cancellation owns
and releases the allocation in this case. `0xC7EB038` is the teardown drain,
not the normal dispatcher. Posting through internal `0xC7EB988` from a foreign
task would bypass the external queue locking/wakeup and is not the binding.

## Ordinary LE CoC retains the common native ABI

The previous event table mixed enhanced-channel and internal protocol callbacks
with the module's registered ordinary CoC callback. The ordinary path emits:

| Callback | Identifier | Producer/layout |
| --- | --- | --- |
| Incoming | 1 | `0xC801710`, request block 68 bytes |
| Assigned | 2 | `0xC801AC4`, request CID at +0 |
| Open | 3 | `0xC801FD4`, CID +108, remote MTU +72 |
| Close | 6 | `0xC801124`, CID +0, reason +2, peer +4 |
| Data | 7 | Stock reassembled buffer, end +0, offset +2, CID +4 |
| Local flow | 8 | `0xC8005A8`, six-byte record |
| PSM registered | 9 | `0xC800624`, copied registry record |
| Credits | 17 | `0xC803038`, CID +0, credits +2 |

The connect handler is `0xC801AC4`; it reads the PSM callback from +12 (a
callback pointer, not a registry-entry pointer), address from +16, LE flag at
+8 and configuration from +52 (MTU), +60 (credits), +62 (MPS). A nonzero
callback is required when connecting to an otherwise unregistered remote PSM.

The ordinary accept handler is **`0xC80279C`**, incorrectly named
`l2cap_disconnect_handler` in an older symbol record. It reads CID +0, status
+4 and detail +6, installs that same local configuration, and calls response
builder `0xC8025C0`. The older statement that this request had no status fields
was incorrect. A separate, correctly named `l2cap_accept_handler` record keeps
this distinction explicit.

`0xC802F18` implements disconnect/reject: state 8/9 calls disconnect primitive
`0xC800A94`; state 2/131 sends a rejection through `0xC8025C0`; other states
tear down through `0xC801124(channel, 1033)`. The target shim executes those
same branches on the owner task without allocating a request the stock handler
would immediately free.

PSM transport 3 allocates 0x80..0xff; LE CID range is 0x40..0x7f. Registry
nodes have an eight-byte intrusive prefix. The channel's stock queue is +4,
and its credits are at queue +14. These fields match the common backend.

## Sending uses the same stock and fragment layouts

`l2cap_stock_alloc 0xC7EAFBC(payload_length, headroom)` writes total +0,
payload offset +2 and metadata +8; payload is at `buffer + 4 + offset`.
`bt_l2cap_submit_cid 0xC805BE8` stores a full CID word at +4 and schedules
send handler `0xC801D10`. The handler creates intrusive fragment nodes with
buffer pointer +0, flags +4, using an eight-byte list prefix. This is the same
fragment representation the common send shim uses. For a nonempty SDU the
module passes the **original SDU buffer** to that handler, which owns and
creates the fragments itself. Passing a prebuilt fragment node would cause it
to read the node fields as an SDU header and free an interior allocation
pointer; this inherited call-site bug has been corrected. The earlier statement
that p67 had no fragment nodes was incorrect.

The stock fragmentation loop omits an empty payload, so the explicit
first+last empty-fragment path is required here too. Its on-wire payload must
contain the two-byte SDU length zero: total=offset+2, and a u16 zero at the
payload. The inherited shim used total=offset and a u32 store, which emitted
an empty L2CAP PDU and wrote two bytes beyond its allocation; both paths now
use the two-byte prefix. Queue insertion
`0xC7EAC34` and credit flush `0xC801C7C` provide that exact path; sending an
empty buffer through `bt_l2cap_submit_cid` alone would silently drop it.

## libuv and file wake ownership

The p67 async handle is 112 bytes, not 64; loop at +4, callback +96,
busy +24 and pending +108. `uv_async_init 0xC2E14E8` and send `0xC2E15A4`
prove these offsets. Loop initialization `0xC2DC9A0` sets queue head +68,
mutex +76, async +104. The loop pipe descriptors are read +360/write +364.

The task lookup follows TCB TLS +104, taskinfo UV slots +16 with stride4,
key `0x201033AC` (maximum7), and UV-state default loop +760. TCB group is +8;
group filelist is +720 (`0xC2055E8`). Files are 32 bytes with inode +16;
filelist rows hold eight entries. This supplies the same independently pinned
file wake used by the common bridge, without borrowing another task's numeric FD.

`file_write 0xC1D25D4(file, bytes, length)` exists as a scalar wrapper. It
constructs both an iovec and `uio { iovec*, count }` for `file_writev 0xC1D2540`.
The earlier draft passed an iovec where that uio was expected and was invalid.

The actual libuv mutex wrappers are init `0xC2DCCCC`, lock `0xC2DCDA8`, unlock
`0xC2DCDC4`; close is `0xC2DB874`. Native Feature layout evidence lives in the
target pack's `EVID-PORT-1043-FERRY-001`.

## QuickJS: direct APIs were mistaken for missing compositions

The engine contains these directly callable entries with the usual AAPCS
JSValue pair. No handwritten allocator or object model is needed:

| Entry | VA | Observed implementation |
| --- | --- | --- |
| NewStringLen | `0xC294B50` | UTF-8 decoding; returns string header pointer with tag -7 |
| NewArrayBufferCopy | `0xC2D34C8` | Allocates backing storage then copies with `0xC588DB0` |
| GetArrayBuffer | `0xC2D362C` | Class 19/20; object opaque +32; detached +4; byte length +0, data +8 |
| GetTypedArrayBuffer | `0xC2D368C` | Duplicates buffer value, returns byte offset, byte length, element size |
| ToCStringLen2 | `0xC29460C` | Native UTF-8/CESU-8 conversion and string retention/allocation |
| DefinePropertyValueStr | `0xC296F14` | Interns string name, defines own property, releases atom and consumes value |

`0xC294B50` was incorrectly named atom-core in an older record. Returning
`string +16` as a JSValue would make reference counting dereference string
bytes. C-string payloads are header +16 and are released by `0xC278848`, which
subtracts16; JS string values point at the header itself. Likewise the older
ArrayBuffer record interchanged payload and length fields. The native getters
supply the reliable contract and replace that composition entirely.

All six public shim operations therefore call direct firmware entries. An
intermediate atom-based property composition was removed once the exact
string-name wrapper was recovered; the final target needs no custom QuickJS
allocator or string/object representation. The last two direct-wrapper proofs
are in `EVID-PORT-1043-FERRY-001`.
