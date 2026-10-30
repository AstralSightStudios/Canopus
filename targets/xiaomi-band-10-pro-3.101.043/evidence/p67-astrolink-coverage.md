# P67 astrolink coverage and porting assessment

Target: `xiaomi-band-10-pro-3.101.043` (P67, `best1503`, NuttX).
Image: inner `vela_ap.bin`, 13,795,728 bytes,
SHA-256 `519307675665e4866d722a8119a98589c397b614ac3294cb87bfc86de45756ec`,
`ro.build.version=3.101.043`, build `CONBINE_LTALM078_T3.101.043_08041658`.
Machine-readable form: `EVID-PORT-1043-FEASIBILITY-001.json`.

## Verdict

All four hosts astrolink needs exist in this image, so the target is not
impossible - but it cannot be adapted by porting from the Band 11 line. The two
lines share the upstream projects, not the build: libuv and the NuttX
configuration differ, most function bodies do not survive translation, and the
`uv_loop_t` the module must reach is laid out differently. What remains is a
discovery pass on this image, entry by entry and offset by offset.

## What the pack already covers

Of the 47 firmware bindings astrolink takes from a target pack, this pack
already carries 6 with independently recovered addresses:

| astrolink binding | .043 record | address |
| --- | --- | --- |
| `F_ALLOC` | `bt_alloc` | `0xc840c54` |
| `F_FREE` | `bt_free` | `0xc840c60` |
| `F_ADAPTER_REGISTER` | `bt_adapter_register` | `0xc3af3e8` |
| `F_ADAPTER_GET_STATE` | `bt_adapter_get_state` | `0xc3af4f4` |
| `F_BT_QUEUE_EXTERNAL` | `bt_queue_external` | `0xc7eb9cc` |
| `D_L2CAP_OWNER` | `bt_l2cap_owner` | `0x2013bdbc` |

Adjacent records that shorten the remaining work: `bt_l2cap_connect`,
`bt_l2cap_disconnect`, `bt_l2cap_submit_cid`, `gap_host_stock_receive`,
`gap_host_receive_slot`, `bt_adapter_get_instance`, `bt_adapter_set_scan_mode`,
`miwear_transport_send`, `miwear_transport_l2_rx`, `miwear_session_lookup`,
`miwear_bt_adapter_instance`.

## What is missing for a complete ABI

41 firmware bindings plus their globals:

- **libuv/NuttX** (13): `uv_mutex_init/lock/unlock`, `uv_async_init`, `uv_close`,
  `files_getfilep`, `fs_getfilep`, `fs_putfilep`, `file_dup3`, `file_write`,
  `file_close`, `nxsched_foreach`, the loop pipe-operations table.
- **GAP** (5): `bt_adapter_enable`, `ble_scan_start/stop`,
  `ble_advertising_start/stop`, plus the MiWear IPC instance global that boot
  creates.
- **Bluelet LE CoC** (10): `l2cap_find_le_psm`, `register_psm_handler`,
  `disconnect_handler`, `connect_handler`, `accept_handler`, `send_handler`,
  `find_channel`, `disconnect_channel`, `queue_push`, `credit_queue_flush`. Note
  the stock `bt_l2cap_*` wrappers in this pack are the public layer, not the
  owner handlers the backend drives.
- **QuickJS Feature** (13 + 1): every `qjs_*` the module calls,
  `feature_register_feature`, the framework instance and both vtables.

## Recovered in this pass

Three records were added to the pack, each anchored in this image:

| record | address | evidence |
| --- | --- | --- |
| `pipe_file_operations` (global) | `0x2CCBE5A4` | `nx_pipe` `0xC1A609C` hands this table to `register_inode` `0xC1AFFD8`; the ten words are the pipe implementations `0xC1A6375/0xC1A6249/0xC1A6525/0xC1A6611/0/0xC1A68BD/0xC1A5F79/0/0xC1A6789/0`, identical under the XIP alias `0x0CCBE5A4` |
| `nx_pipe` | `0xC1A609C` | formats `/var/pipe/%d` from `dword_200EE948`, allocates the 1024-byte FIFO, calls `register_inode` with the pipe table and file type 438 |
| `register_inode` | `0xC1AFFD8` | stores its second argument into `inode+16`, ORs `0xA` into the mode nibble at `inode+14`, keeps the private pointer at `inode+24` |

The third one also settles an offset for this image: **`FILE_INODE = 16`**, verified
from the producer rather than inherited.

## Two measurements that decide the method

1. **Cross-line matching is unsound here.** `.155 -> .043` proposes 313/316
   functions, but of the 206 proposals whose name this pack already carries an
   independently recovered address for, only 46 agree (22%). Nothing transfers
   by body identity either: of the 342 names the `.155` pack carries, 19 have an
   identical whole-body fingerprint at a unique `.043` address; 16 more have
   candidates that are ambiguous.
2. **The `uv_loop_t` layout differs, so the module's loop dispatch cannot inherit
   any offset.** `uv_loop_init` (`0xc2dc9a0`) initialises the ready queue head at
   `loop+68`, the mutex at `loop+76` and the loop async at `loop+104` - the same
   as q66 - but `loop+312/+316` are a self-linked list head here, while q66 keeps
   the loop's pipe descriptors at those offsets. The async pipe pair is created
   lazily by `uv_async_init` (`0xc2e14e8`) into `loop+360/+364` behind a
   `loop+360 == -1` guard, and links the handle's pending queue through
   `loop+312` at `handle+100`. The per-task loop state allocated here is 288
   bytes against 712 on q66. A module built with the q66 constants would fail at
   its first loop wake, exactly as the `.179` build did before its pipe identity
   was corrected.

Version evidence for the same conclusion: 14 libuv source paths here against 12
in `.155` (adds `unix/posix-hrtime.c`, `unix/udp.c`), and 26 NuttX source paths
against 23 (adds `rwbuffer.c`, `serial.c`, `mempool.c`, `socket.c`).

## Work left, in the order that pays off

1. Loop/task layout for this image: `uv_key` global, TCB TLS/group/filelist walk,
   loop state slot, pipe identity. The loop descriptor result above is the first
   entry; the rest follow the anchoring functions listed in astrolink's
   `docs/loop-abi.md`.
2. libuv entry points (`uv_mutex_*`, `uv_async_init`, `uv_close`) - locally
   identifiable through the `%s:%d` assertion wrappers whose file/line literals
   are in this image (the `0xc2dccb0`-region functions).
3. NuttX file layer (`fs_getfilep`, `files_getfilep`, `file_*`,
   `nxsched_foreach`) - same upstream VFS family; the task-group offsets must be
   re-read, not copied.
4. Bluelet owner handlers - the `[NNN][bluelet]:` log strings and
   `sal_adapter_interface.c` anchor the stack; the public `bt_l2cap_*` wrappers
   already in the pack give the callee neighbourhood.
5. QuickJS + Feature registry - `feature_manager_qjs.cpp:NNN` keys and the
   `[FEATURE] [%s:%d]` formats anchor `feature_*`; `quickjs.c` anchors the
   `qjs_*` entry points.
6. Only then port `.043 -> .036` within the line, which is the safe direction
   (the `.043` pack itself was built from `.036` matches).

No address in this document is recorded as a symbol or may be called; these are
measurements the next pass should confirm before use.

## Pass 2 (`EVID-PORT-1043-LOOP-001`): loop, task and libuv layout

The chain on this image is not the q66 one. libuv keeps its state in a pthread
key, and the loop lives inside that state, so `uv_key -> state -> loop` becomes
`key -> task-info slot -> TLS block -> loop`:

```
tcb + 100                       -> task-info            (sched_get_stackinfo 0xC205FAC
                                                         copies tcb+96/100/104, and
                                                         pthread_getspecific 0xC1F5460
                                                         dereferences the +100 word)
  *(task-info + 16 + 4*key)     -> 808-byte uv__tls block (key = *(0x201033AC),
                                                         uv__tls_get 0xC2DEA38)
    uv__tls + 760               -> the default loop      (uv__get_loop 0xC2DE814;
                                                         the loop itself is the embedded
                                                         struct at uv__tls + 136)
```

| item | value | anchor |
| --- | --- | --- |
| `TCB_TLS` | 100 | `sched_get_stackinfo.c` 0xC205FAC stores tcb+96/100/104; 0xC1F5438 dereferences the +100 word |
| `TASKINFO_UVSTATE` / stride | 16 / 4 | `pthread_getspecific` 0xC1F5460: `*(task_info + 4*(key+4))` |
| `UV_KEY_LIMIT` | **7** (8 slots) | same function returns 0 for `key > 7`; q66 allows only 0..3 |
| `uv__tls_key` global | `0x201033AC` | read in `uv__tls_get` 0xC2DEA38, used by getspecific/setspecific 0xC1F5460 / 0xC1F5478 |
| `UVSTATE_LOOP` (loop slot) | **760** in the TLS block, no uv_key indirection | `uv__get_loop` 0xC2DE814 loads +0x2F8 and lazily inits the embedded loop at +136 |
| `LOOP_QUEUE_HEAD` / `LOOP_MUTEX` / `LOOP_ASYNC` | 68 / 76 / 104 | `uv_loop_init` 0xC2DC9A0 |
| `LOOP_READ_FD` / `LOOP_WRITE_FD` | **360 / 364** | `uv_loop_init` stores a -1 qword there; `uv_async_init` 0xC2E14E8 fills them from the created pair behind `loop+360 == -1` |
| `ASYNC_BUSY_WORD` | 6 (+24) | `uv_async_init` clears it |
| `ASYNC_PENDING_WORD` | **27** (+108) | `uv_async_init` clears it; q66 uses 15 |
| `uv_async_t` callback / pending node | +96 / +100 | `uv_async_init` |
| `FILE_INODE` | 16 | producer: `register_inode` 0xC1AFFD8 stores the operations table at inode+16; consumer: `file_close` 0xC1CEC08 loads file+16 then inode+16 |

Recovered entries this pass: `uv__tls_get` `0xC2DEA38`, `uv__tls_key`
`0x201033AC`, `uv__get_loop` `0xC2DE814`, `uv_loop_init` `0xC2DC9A0`,
`uv_async_init` `0xC2E14E8`, `uv_mutex_init` `0xC2DCF1C`, `file_close` `0xC1CEC08`
(7 with `nx_pipe`, `register_inode` and `pipe_file_operations` from pass 1 = 10).

### What this means for the module

`uv__get_loop` is a real function here, so a p67 target does not need the
structure walk at all: it can call the firmware's own getter. That is a code
change in `loop_dispatch`, not an address, and it should be made deliberately
rather than by bending the q66 offsets.

### Still open

`uv_mutex_lock` / `uv_mutex_unlock` (the thread.c wrappers around
`nx_pthread_mutex_take` 0xC204DE4 were not separated with producer evidence),
`uv_close`, `fs_getfilep` / `files_getfilep` / `fs_putfilep` / `file_dup3` /
`file_write` / `nxsched_foreach`, `bt_adapter_enable`, the scan and advertising
entries, the ten Bluelet owner handlers, the thirteen `qjs_*` entries and
`feature_register_feature`, plus the framework/Application vtable globals.
That is 37 of the 47 bindings still missing after two passes; the P67 line
remains a discovery job, not a port.

## Pass 3 (VFS): what closed

Independent evidence on this image, `EVID-PORT-1043-VFS-001`:

| astrolink binding | record | address | anchor |
| --- | --- | --- | --- |
| `F_FS_GETFILEP` | `fs_getfilep` | `0xC1B9700` | takes fd + out-parameter, resolves the task file list via `0xC2055E8`, `8 * rows` bound, `array[fd>>3] + 32*(fd&7)`; 30 VFS callers |
| `F_FILES_GETFILEP` | `files_getfilep` | `0xC1B90DC` | same arithmetic with the list passed in (`+4` row-pointer array), refcount at `file+4`, inode at `file+16` |
| `F_FILE_DUP3` | `file_dup3` | `0xC1CED14` | rejects flags outside `0`/`0x400`, closes the destination, copies flags/offset/inode, dispatches the type-3 open handler at `ops+48` |
| `F_FILE_CLOSE` | `file_close` | `0xC1CEC08` | (recovered earlier) inode at `file+16`, i_ops at `inode+16`, close at `ops+4` |

File and file-list layout measured on this image:

- group file list: `group+720` is the `u8` row count, `group+724` the row-pointer array
- one row is 32 bytes and holds 8 files, so `fd/8` selects the row and `fd%8` the entry
- `file+4` is the reference count, `file+16` the inode, `file+20` the offset

### Pass 3 continued: release, write shape, and two Bluelet entries

| astrolink binding | record | address | anchor |
| --- | --- | --- | --- |
| `F_FS_PUTFILEP` | `fs_putfilep` | `0xC1B979C` | single argument; asserts against `fs_files.c:906`, decrements `file+4` under BASEPRI and calls the list release `0xC1CEC54` when it hits zero; VFS wrappers call it right after `fs_getfilep` |
| `F_ADAPTER_ENABLE` | `bt_adapter_enable` | `0xC3AF4A4` | issues adapter command `0x17`, returns the reply status byte; the btstack init path calls it by name through its error log |
| advertising start | `bt_le_start_advertising` | `0xC70ED94` | builds the parameter block, registers the advertising data record once, calls the GAP start `0xC7D3EB4` with the `flags & 2` / `flags & 8` bits |

The earlier candidate for the release (`0xC1B8D50`) is a group walk, as suspected;
the real single-argument release is `0xC1B979C`, and `IDA` had left it undefined as
a function, so it only showed up as a `loc_` call target from `file_close`'s
neighbours. Its distinct evidence is the refcount decrement at `file+4` followed by
the list release, whereas `file_close` dispatches `i_ops+4`.

**Binding shape change needed for `F_FILE_WRITE`:** this build has no
non-vector `file_write`. `file_writev` (`0xC1D2540`) takes the file, checks
`file+0 & 2`, and dispatches `i_ops+40`, and `nx_write` (`0xC1D25F4`) builds a
one-element iovec for it. Astrolink currently calls
`F_FILE_WRITE(file, buf, n)`, so that adapter has to change on purpose to the
writev shape (`file_writev(file, &iov{base,len})`), not by substituting an address.

Adapter command wrappers observed while anchoring `bt_adapter_enable`, all of the
form `0xC3AC47C(adapter, reply, command)` plus a status read at `reply+12`:

| wrapper | command | note |
| --- | --- | --- |
| `0xC3AF4A4` | `0x17` | `bt_adapter_enable` |
| `0xC3AF4CC` | `0x18` | unrecorded |
| `0xC3AF4F4` | `0x1B` | `bt_adapter_get_state` (recorded earlier) |
| `0xC3AF524` | `0x1E` | takes one argument |

Still open in these groups: `nxsched_foreach` (no `sched_foreach.c` string on this
image, so it has to be found by its list-walk shape rather than by a name),
`file_readv` (`0xC1D119C`, recovered as context for the write shape but not a
binding), the scan start/stop pair, the advertising stop, and the whole LE CoC
owner handler set.

## Pass 4 (LE CoC owner): what closed

`EVID-PORT-1043-L2CAP-001`, owner global `dword_2013BDBC`, all handlers reached
through the public wrappers in the `0xC805A80`–`0xC805CA4` cluster.

| astrolink binding | record | address | anchor |
| --- | --- | --- | --- |
| `F_L2CAP_FIND_LE_PSM` | `l2cap_find_le_psm` | `0xC7FF644` | registry at `*(owner)+16`, psm at entry+4, kind byte at entry+6, `0x80` split; consumed by the connect handler |
| `F_L2CAP_UNREGISTER_PSM` | `l2cap_unregister_psm` | `0xC7FFB04` | matches psm + kind, unlinks via `0xC7EAD28`, builds a 16-byte request from `+16/+20` |
| `F_L2CAP_FIND_CHANNEL` | `l2cap_find_channel` | `0xC8004A4` | list at `*(*(owner))`, key at channel+0, cid at +116, link at channel-8 |
| `F_L2CAP_QUEUE_PUSH` | `l2cap_work_submit` | `0xC7EB988` | 20-byte node (owner/arg/handler/done/flag at 0/4/8/12/16), lock `dword_2013BD7C`, queue `dword_2013BD74`; 80 callers |
| `F_L2CAP_CONNECT_HANDLER` | `l2cap_connect_handler` | `0xC801AC4` | submitted by `bt_l2cap_connect 0xC805B0C`; request psm +2, flags +8, entry +12, cid pair +16/+20, state +23 |
| `F_L2CAP_DISCONNECT_HANDLER` | `l2cap_disconnect_handler` | `0xC80279C` | submitted by `bt_l2cap_disconnect 0xC805B38`; cid at request+0, requires channel+120 == 2 |
| `F_L2CAP_SEND_HANDLER` | `l2cap_send_handler` | `0xC801D10` | submitted by `bt_l2cap_submit_cid 0xC805BE8`, which stores the cid at request+4 |

Supporting helpers recorded because the handlers cannot be used without them:
`l2cap_channel_descriptor_create 0xC8004F4` (44-byte descriptor, cid pair +16/+20,
local cid +38 = 0xFFFF, LE flag +43 bit0) and `l2cap_allocate_cid 0xC7FF93C`
(owner cid counter at `*(owner)+24`, skips `<= 0x3F`, and `> 0x7F` when LE).

### Channel and request layout measured from producers

| object | offset | meaning |
| --- | --- | --- |
| connect request | +0 (u16) | assigned cid, written back by the handler |
| | +2 (u16) | psm |
| | +8 (byte) | flags, bit0 selects LE |
| | +12 | psm registry entry, filled from `l2cap_find_le_psm` when null |
| | +16 / +20 | cid pair handed to the descriptor |
| | +23 (byte) | state written by the handler: 1 = LE, 2 = BR |
| disconnect request | +0 (u16) | cid |
| submit-cid request | +4 | cid stored by the wrapper |
| channel | +0 | list key word |
| | +24..+39 → copied to +68..+83 | 16-byte descriptor block (BR path) |
| | +33 (byte) | state, cleared on disconnect |
| | +36 / +40 / +44 | `-1` (u32) set on connect and reset on disconnect |
| | +48 | `0x0101FFFF` |
| | +52 (u16) | `0x02A0` |
| | +64 (byte) | `1` on connect |
| | +114 (u16) | flags, bit `0x100` set on connect |
| | +116 (u16) | cid |
| | +120 (byte) | state, `2` = connected |
| work node | +0 / +4 / +8 / +12 / +16 | owner, arg, handler, done, flag |
| psm entry | +4 (u16) / +6 (byte) | psm, kind/state |

The channel's total size is **not** claimed: the only allocation seen in this path
is the 44-byte descriptor, while the handlers touch fields up to +120, so the
channel is allocated elsewhere and its extent has to be measured before a module
holds one.

### Still open after this pass

- `l2cap_register_psm_handler` (the insert half of the registry) — no candidate
  with producer evidence; the unregister half is recorded and the find half is
  recorded, so the gap is precisely the insert.
- `credit_queue_flush` — not identified. The fragment/credit queue is the one
  thing the send handler re-fragments in place, but no standalone drain function
  was confirmed.
- `l2cap_accept_handler` — the closest candidate is `0xC803B88` (submitted by
  `0xC805C40`), which parses L2CAP command elements (code/id/length at +0/+1/+2)
  and dispatches per-code with the duplicate-id check at channel+41. It is
  plausibly the accept/reject path but the accept semantics were not proven, so
  it stays unrecorded.
- The rest of the module's QuickJS/Feature group and the scan/advertising pair
  remain open from the earlier passes.

