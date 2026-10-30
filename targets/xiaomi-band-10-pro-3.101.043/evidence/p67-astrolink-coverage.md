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

## Pass 5: LE CoC gaps closed, dispatch table, GAP entries

`EVID-PORT-1043-L2CAP-002`.

| binding | record | address | anchor |
| --- | --- | --- | --- |
| `F_L2CAP_REGISTER_PSM` | `l2cap_register_psm_handler` | `0xC800624` | wrapper `0xC805A80`; inserts into `*(owner)+16` via `0xC7EAC34`, dynamic PSM base `0x1001` for kind 2, `0x0080..0x00FF` for kind 3, collision scan by `(entry+4, entry+6)` |
| `F_L2CAP_UNREGISTER_PSM` | `l2cap_unregister_psm_handler` | `0xC8030F0` | wrapper `0xC805AC4`; matches `+4/+6`, unlinks with `0xC7EAD28`, retires channels bound to the PSM when kind <= 3 |
| `F_L2CAP_CREDIT_QUEUE_FLUSH` | `l2cap_credit_queue_flush` | `0xC801C7C` | drains while credits at `stock+14` remain, dequeue `0xC7EACD0`, send `0xC7FE4D0`, called by the credit handler `0xC803038` |
| `F_L2CAP_ACCEPT_HANDLER` | `l2cap_le_connection_response_handler` | `0xC802F18` | wrapper `0xC805BBC`; channel state `+120 == 2` emits the `0x15` response via `0xC8025C0` with the result byte at `+121` |

The `0x15` response itself is `l2cap_le_connection_response 0xC8025C0`, and the
received-frame entry point is `l2cap_signal_dispatch 0xC803B88` with its
**dispatch table at `0x2CD9F864`** (26 entries of `{handler, u16 minlen, byte,
flags}` indexed by `code - 1`). That table is the authoritative map of
signalling codes on this build: `0x14 0xC802AED` (LE connection request),
`0x15 0xC8031FD` (response), `0x16 0xC803039` (flow control credit),
`0x17 0xC8039A1`, `0x18 0xC803479`.

Correction to the previous pass: the address recorded there as
`l2cap_unregister_psm` (`0xC7FFB04`) is the internal channel-initiated detach, now
recorded as `l2cap_detach_psm_registration`, with a note that the public remove is
`0xC8030F0`.

### GAP entries

| binding | record | address | anchor |
| --- | --- | --- | --- |
| scan start | `bt_le_start_scan_with_filters` | `0xC710BCC` | own log tag; the only scan start on this build is the filtered variant |
| scan stop | `bt_sal_le_stop_scan` | `0xC3BBED0` | bluelet log tag `bt_sal_le_stop_scan`, three callers in the scan state machine |
| advertising stop | `bt_sal_le_stop_advertising` | `0xC3B125C` | adapter command `0x44`; reached from the beacon timer with the stored advertising handle |
| `F_ADAPTER_INSTANCE` | `bt_adapter_get_instance` | `0xCA40F68` | 16-byte getter returning `dword_2013DBE4`, the same global the pack already lists |

`bt_adapter_get_instance` needed no correction: the earlier `0xca40f68` record is
the getter and `0x2013dbe4` is the instance global, so the two records are related
rather than duplicated. The advertising stop is the weakest of the three GAP
entries — its name comes from the calling context, not from a name string.

### Channel allocation, measured

`l2cap_allocate_cid 0xC7FF93C` also allocates the channel: `0xC7EAC14(124)`,
inserted through `0xC7EAC34`, writing descriptor `+0`, PSM entry `+20`, PSM u16
`+112`, CID `+116` and state byte `+120`. **The channel is 124 bytes**, which is
consistent with every offset the handlers touch (max `+121`).

### Still open (pass 6 = QuickJS/Feature)

- `nxsched_foreach`: no trustworthy shape found — the image has no
  `sched_foreach.c` string and the candidate walkers are group walks, so it is
  still unrecorded.
- The QuickJS/Feature group: 13 `qjs_*` entries, `feature_register_feature`,
  `feature_manager_post_task`, and the framework/Application vtable globals.
- `l2cap_accept_handler` naming: the p67 shape records the *response* decision
  path; if the module needs the app-side accept call with credits/MTU, that is
  `0xC805BBC`'s caller side and should be verified on device.

## Pass 6: the task iterator, and the Feature manager

`EVID-PORT-1043-QJS-001`.

### nxsched_foreach — found, and it is the real one

`0xC205E54(void (*worker)(void *tcb, void *arg), void *arg)`

- takes the scheduler critical section through `0xC205EB4` / `0xC205EE8`
  (BASEPRI 0x80),
- iterates the **pid hash**: bucket array at `0x200B24B0`, bucket count at
  `0x200B24AC`,
- calls `worker(bucket_tcb, arg)` for each non-null bucket.

Evidence that this is the task iterator and not something narrower: the
firmware's own global file release calls it with the worker `0xC1B8D50`
(`0xC1B9F1C` is the wrapper: `nxsched_foreach(sub_C1B8D50, 0)`), and four more
scheduler/cleanup callers reach it.

Caveat recorded honestly: iteration is one TCB per hash bucket, so this build
keeps a single TCB per bucket. If a bucket ever chains, the chain is not
followed here — the module should not assume a chain walk.

Related, recorded because the module may prefer resolving its own pid:
`nxsched_get_tcb 0xC205910` (bucket `[(count - 1) & pid]`, verifies the pid word
at `+44`, 44 callers) with the same two globals. Current TCB global is
`0x200B24D4` (used by `sched_get_stackinfo 0xC205FAC`).

### Feature group

| binding | record | address | anchor |
| --- | --- | --- | --- |
| `F_FEATURE_REGISTER_FEATURE` | `feature_register_feature` | `0xC862898` | names itself `FeatureRegisterFeature` in its own failure logs (description null / no registry instance), 32 call sites, then delegates to `0xC86E7C4` |
| manager // createFeature | `FeatureManagerQjs_createFeature` | `0xC8690B8` | signature string at `0xcc8a99c` + vtable slot `+0x24` |
| manager // findFeature | `FeatureManagerQjs_findFeature` | `0xC869270` | signature string at `0xcc8aa48` + vtable slot `+0x20` |
| manager // featureRequire | `FeatureManagerQjs_featureRequire` | `0xC868E94` | signature string at `0xcc8a7bc` + vtable slot `+0x1C` |
| manager // uninit | `FeatureManagerQjs_uninit` | `0xC866AB0` | signature string at `0xcc89528` + vtable slot `+0x2C` |
| manager // createJsInstance | `FeatureManagerQjs_createJsInstance` | `0xC868B18` | signature string at `0xcc8a594`, called by createTargetInterface |
| manager // ensureJsPrototype | `FeatureManagerQjs_ensureJsPrototype` | `0xC8687AC` | signature string at `0xcc8a48c` |
| manager // createTargetInterface | `FeatureManagerQjs_createTargetInterface` | `0xC869034` | signature string at `0xcc8a898` with the `(interf) != (nullptr)` assertion |
| framework // accessor_get / accessor_set | `feature_accessor_get` / `feature_accessor_set` | `0xC8681C8` / `0xC86AFC8` | signature strings `0xcc89d74` / `0xcc8bbd4` |
| framework // event_set / event_get / event_on_off | `feature_event_set` / `feature_event_get` / `feature_event_on_off` | `0xC8697FC` / `0xC86A338` / `0xC86D170` | signature strings `0xcc8ae90` / `0xcc8af7c` / `0xcc8c2d4` |
| framework // method_call | `feature_method_call` | `0xC86C2B8` | signature string `0xcc8c1d4` |

The assertion strings on this image carry **full C++ signatures**
(`feature_framework::FeatureManagerQjs::createTargetInterface(...)` together with
the expression that failed), which is what made this pass possible: every manager
method names itself, so no cross-line matching was involved.

### FeatureManagerQjs dispatch table (global)

`0xCDA2CD0`, Thumb entry points, measured:

| slot | address | method |
| --- | --- | --- |
| `+0x00` | `0xC861968` | unrecorded |
| `+0x04` | `0xC86538C` | unrecorded (also `+0x28`) |
| `+0x08` | `0xC86192C` | unrecorded |
| `+0x14` | `0xC86646C` | unrecorded |
| `+0x18` | `0xC866484` | unrecorded |
| `+0x1C` | `0xC868E94` | `featureRequire` |
| `+0x20` | `0xC869270` | `findFeature` |
| `+0x24` | `0xC8690B8` | `createFeature` |
| `+0x2C` | `0xC866AB0` | `uninit` |

Four slots are pinned by methods that also carry signature strings, so the table's
base and ordering are established even where the remaining slots are unnamed.

### Still open after this pass

- `feature_manager_post_task` (`void(void *manager, const void *task)`): not
  found. The `workflow_post_task 0xC842340` string belongs to the miconnect
  workflow manager (its own global `0x2013C490`), not to the Feature manager, so
  it was not recorded as a substitute. What is needed: the manager's JS-thread
  task queue and its wake, i.e. follow `0xC86E7C4` (register_feature's delegate)
  and the manager `uninit` teardown for the queue object.
- The 13 `qjs_*` entries. The QuickJS core is identifiable in this image
  (`apps/interpreters/quickjs/quickjs/quickjs.c` is present), but each entry needs
  its own producer/consumer proof, and cross-line matching is 22% accurate here,
  so none were recorded from resemblance. Doing them is a full pass on its own.
- Four unnamed FeatureManagerQjs slots above.

## Pass 7: post_task and its wake, and the QuickJS reality check

`EVID-PORT-1043-QJS-002`.

### feature_manager_post_task

| binding | record | address | evidence |
| --- | --- | --- | --- |
| `F_FEATURE_MANAGER_POST_TASK` | `feature_manager_post_task` | `0xC86231C` | names itself `FeaturePost` in its detached/null-handle logs (line 331), requires `handle+19` attached, then forwards to `0xC865D30(*(*(handle+28)+12), …)` |
| — | `feature_runtime_post` | `0xC865D30` | pins the handle with an atomic increment (`0xC8628D4`), builds a 5-word record, calls the enqueue |
| — | `feature_runtime_enqueue` | `0xC865794` | appends a 24-byte element into the runtime message ring at `runtime+0x50..0x64` (buffer/bounds/head/tail/sequence, `0xAA` elements per block) |
| — | `uv_async_send` | `0xC2E15A4` | sets the async pending word at `handle+108` with LDREX/STREX, then writes one byte through `nx_write 0xC1D2640` to fd `*(loop+364)` |

**Wake mechanism, exactly:** the Feature runtime is woken by `uv_async_send` on the
async object stored at `runtime+0x3C` (word 15), i.e. it is a **uv_async on the
runtime's own loop**, not a semaphore and not the module's async. `uv_async_send`
in turn writes to the loop's pipe write end at `loop+364`, which independently
confirms the loop layout measured in pass 2.

### QuickJS: 1 of 13 self-anchored, 12 still open

| binding | record | address | evidence |
| --- | --- | --- | --- |
| `F_QJS_FREE_VALUE` | `qjs_free_value` | `0xC2781E0` | the only self-naming QuickJS string in the image: `__JS_FreeValue: unknown tag=%d`, plus `quickjs.c:5636`; 1046 callers |

The other twelve (`qjs_call`, `qjs_new_object`, `qjs_new_c_function2`,
`qjs_is_function`, `qjs_new_string_len`, `qjs_to_cstring_len2`,
`qjs_free_cstring`, `qjs_define_property_value_str`, `qjs_get_exception`,
`qjs_new_array_buffer_copy`, `qjs_get_array_buffer`,
`qjs_get_typed_array_buffer`) are **not** self-anchorable in this build: unlike the
Feature subsystem, QuickJS here keeps no per-function signature strings (the file
`quickjs.c` appears, but only for assertion file paths), and `qjsmini.c` only names
its arena allocator. They have to be settled one by one by behaviour — e.g. string
tag `-7` with the reference byte at `string+11` for the string constructors, the
`ctx+4` free hook for the cstring helpers, and the exception tag returned through
`JS_GetException` — with the same producer/consumer discipline used elsewhere.
Cross-line matching must not be used: it is 22% accurate between .155 and .043.

### FeatureManagerQjs vtable slots, named

The table at `0xCDA2CD0` has a C++ header in `+0x00..+0x10` and virtual slots from
`+0x14` (the constructor at `0xC86646C` stores vptr `0x2CDA2CE4` = table+0x14).

| slot | address | name | basis |
| --- | --- | --- | --- |
| `+0x00` | `0xC861968` | forwarding stub | calls `0xC8610E8` with `*(arg+8)` |
| `+0x04` | `0xC86538C` | constant-true predicate | body is `return 1` |
| `+0x08` | `0xC86192C` | empty stub | empty body |
| `+0x14` | `0xC86646C` | constructor | stores the vptr |
| `+0x18` | `0xC866484` | deleting destructor | stores the vptr then frees |
| `+0x1C` | `0xC868E94` | `featureRequire` | signature string |
| `+0x20` | `0xC869270` | `findFeature` | signature string |
| `+0x24` | `0xC8690B8` | `createFeature` | signature string |
| `+0x28` | `0xC86538C` | constant-true predicate | same body as `+0x04` |
| `+0x2C` | `0xC866AB0` | `uninit` | signature string |

No functional name was invented for the four unnamed slots; they are recorded
structurally.

## What a p67 abi module still needs

Implementation brief. Everything here is measured on this image and **cannot be
inherited from the Band 11 module**; the addresses live in the pack, these are the
shapes.

### 1. `F_FILE_WRITE` must change shape, not just address

This build has no non-vector `file_write`. Use `file_writev` (`0xC1D2540`,
`i_ops+40`, checks `file+0 & 2`) with a one-element iovec, or `nx_write`
(`0xC1D2640`, the same thing wrapped). The current module binding
`F_FILE_WRITE(file, buf, n)` cannot be filled on this target.

Related: `F_FS_PUTFILEP` is the single-argument `fs_putfilep` (`0xC1B979C`,
`file+4--` then list release `0xC1CEC54`), which is a different function from
`file_close` (`0xC1CEC08`, dispatches `i_ops+4`).

### 2. Task iterator: usable, but with a stated limit

`nxsched_foreach` = `0xC205E54`, callback convention `worker(tcb, arg)`, runs
inside the scheduler critical section.

**It iterates the pid hash, one TCB per bucket, and does not follow a chain.**
Globals: buckets `0x200B24B0`, bucket count `0x200B24AC`; current TCB
`0x200B24D4`; by-pid lookup `nxsched_get_tcb 0xC205910` (verifies the pid word at
`tcb+44`).

Consequence for the loop capture: do not assume the walk visits every TCB on the
system. Either rely on the one-TCB-per-bucket property of this build, or resolve
the module's own pid with `nxsched_get_tcb` first and validate the candidate.

### 3. Loop and task layout (p67 values)

`queue head 68`, `mutex 76`, `loop async 104`, **pipe read/write fd `360`/`364`**
(created lazily by `uv_async_init` behind a `loop+360 == -1` guard; `+312/+316`
is a plain list here), async pending word at `handle+108`, `uv_key` global
`0x201033AC` with **limit 7**, `taskinfo+16+4*key` yields an **808-byte** uv state
with the **loop pointer at `+760`**, `TCB_TLS 100`, `TCB_GROUP 8`, the group's file
list at **`+720` (u8 row count) / `+724` (row-pointer array)**, 32-byte rows of
eight fds, `FILE_INODE 16`, `file+4` refcount, `file+20` offset.

`capture_default_loop` fits this with constants only — do **not** call
`uv__get_loop 0xC2DE814`; the loop pointer already arrives from the adapter
instance.

### 4. L2CAP channel and event layout

- Channel: **124 bytes**. `+0` descriptor, `+20` PSM entry, `+24..+39` (the BR
  path copies this 16-byte block to `+68..+83`), `+33` state byte, `+36/+40/+44`
  `-1`, `+48` `0x0101FFFF`, `+52` MTU (u16), `+60/+62` MPS, `+64` flag, `+112`
  PSM, `+114` flags (bit `0x100`), `+116` cid, `+118` peer cid, `+120` state
  (`2` = connected), `+121` result byte.
- Descriptor (44 bytes): `+16/+20` cid pair, `+38` local cid (starts `0xFFFF`),
  `+43` LE flag bit0.
- Connect request: `+0` out cid, `+2` PSM, `+8` flags (bit0 = LE), `+12` PSM entry,
  `+16/+20` cid pair, `+23` state (`1` LE, `2` BR). Disconnect request: `+0` cid.
  Submit-cid request: `+4` cid.
- Work node (20 bytes): `+0` owner, `+4` arg, `+8` handler, `+12` done, `+16` flag.
  Queue `0x2013BD74`, lock `0x2013BD7C`, owner `0x2013BDBC`; the owner-only
  intrusive insert is `0xC7EAE34`, the public submit is `0xC7EB988`.
- Signalling dispatch table `0x2CD9F864`: 26 entries x 8 bytes
  `{handler, u16 min length, byte, flags}` indexed by `code - 1`; `0x14`/`0x15`/
  `0x16`/`0x17`/`0x18` are the LE CoC codes.
- PSM entry: `+4` PSM (u16), `+6` kind; registry root `*(owner)+16`.

### 5. Adapter command table

`0xC3AC47C(adapter, reply, command)` with the status read at `reply+12`:
`0x17` enable (`0xC3AF4A4`), `0x18` (`0xC3AF4CC`), `0x1B` state (`0xC3AF4F4`),
`0x1E` with one argument (`0xC3AF524`), `0x26` per-channel handshake (from the send
handler), `0x44` advertising stop (`0xC3B125C`).

### 6. Feature runtime chain

`FeaturePost` (`0xC86231C`, requires `handle+19` attached, runtime reached through
`handle+28` then `+12`) → `feature_runtime_post 0xC865D30` → ring append in
`feature_runtime_enqueue 0xC865794` at `runtime+0x50..0x64` (24-byte elements,
`0xAA` per block) → wake `uv_async_send 0xC2E15A4` on the async at `runtime+0x3C`,
which writes one byte to the loop's pipe write end.

Manager dispatch table `0xCDA2CD0`: C++ header in `+0x00..+0x10`, virtual slots
from `+0x14`; `featureRequire +0x1C`, `findFeature +0x20`, `createFeature +0x24`,
`uninit +0x2C`.

### 7. QuickJS: 7 of 13 settled by alignment against .155

Method: the .155 build is ground truth for order, size and body, so each entry was
matched on **all three** and then confirmed on the invariant that makes it that
function. Eight of the thirteen came out; the rest are blocked by a real property
of this build (see below), not by lack of evidence discipline.

Sources are `.155` sizes and bodies from `analysis-q66-177/155/q66-ap.i64`
(`NEW_STRING_LEN` 252, `GET_EXCEPTION` 20, `FREE_VALUE` 26,
`TO_CSTRING_LEN2` 364, `FREE_CSTRING` 20, `NEW_OBJECT` 24,
`NEW_C_FUNCTION2` 30, `IS_FUNCTION` 52, `DEFINE_PROPERTY_VALUE_STR` 64,
`CALL` 42, `NEW_ARRAY_BUFFER_COPY` 38, `GET_ARRAY_BUFFER` 38,
`GET_TYPED_ARRAY_BUFFER` 88).

| binding | p67 address | size p67 / .155 | invariant that confirms it | astrolink call shape |
| --- | --- | --- | --- | --- |
| `qjs_get_exception` | `0xC27DA60` | 26 / 20 | reads `ctx+16` for the runtime, returns `*(u64*)(rt+128)` and stores `0x200000000` (JS_UNDEFINED) back | `(ctx) -> Value` |
| `qjs_free_value` | `0xC278294` | 32 / 26 | tags above `0xFFFFFFF4` decrement the refcount at the pointer and call `__JS_FreeValue` at zero | `(ctx, Value)` |
| `qjs_free_cstring` | `0xC278848` | 38 / 20 | decrements `ptr-16`, frees with tag `-7`; .155 forwarded to FreeValue instead of inlining the decrement | `(ctx, *const u8)` |
| `qjs_is_function` | `0xC27DA88` | 78 / 52 | tag must be `-1`; class `13` → 1; class `41` → byte at `obj+32+16`; else class-table finalizer slot at `*(*(ctx+16)+68)+24*class+16` | `(ctx, Value) -> i32` |
| `qjs_new_object` | `0xC2A41FC` | 24 / 24 | calls the constructor with `*(*(ctx+40)+8)` and `*(*(ctx+40)+12)` and class id 1 | `(ctx) -> Value` |
| `qjs_new_c_function2` | `0xC2A83C4` | 30 / 30 | forwards six arguments plus the context's `ctx+48`/`ctx+52` pair | `(ctx, usize, *const c_char, i32, i32, i32) -> Value` |
| `qjs_call` | `0xC2BFCC0` | 44 / 42 | forwards to the interpreter entry with flag words `3` and `2` | `(ctx, func, this, argc, *const Value) -> Value` |

Also recorded, as internals rather than bindings: `__js_free_value 0xC2781E0`
(the tag switch with the `__JS_FreeValue: unknown tag=%d` string, 1046 callers)
and `__js_throw 0xC27DA24` (writes the exception slot at `rt+128` and returns
`0x600000000`).

**Correction to the previous pass:** the address recorded there as
`qjs_free_value` (`0xC2781E0`) is the internal `__JS_FreeValue`. Astrolink binds
the public wrapper, which is `0xC278294`.

#### Where the alignment breaks

p67 is **not order-preserving** against .155. The violation is exact: .155 keeps
`NEW_OBJECT` and `NEW_C_FUNCTION2` *before* `IS_FUNCTION`, while p67 places
`IS_FUNCTION` (`0xC27DA88`) immediately after `GET_EXCEPTION` (`0xC27DA60`) and
before `NEW_OBJECT` (`0xC2A41FC`) and `NEW_C_FUNCTION2` (`0xC2A83C4`). The public
wrappers are therefore grouped by section/inlining rather than by source order on
this build, so order is only usable inside a group — it settled the three that
cluster together and the two object/function constructors, and it cannot be used
to pick between the remaining six.

#### The six that are still open

| binding | what is missing |
| --- | --- |
| `qjs_new_string_len` | needs the ASCII fast-path allocator: .155 calls `js_alloc_string(ctx, ptr, len)` (its `0xC3B6874`) and only falls into the utf16 builder for non-ASCII. On p67 the builder exists (`0xC2920BC` grows a `StringBuffer`, `string too long` at `0x40000000`, surrogates 55296/56320, replacement 65533) but the allocator it is called from was not isolated. |
| `qjs_to_cstring_len2` | .155 (364 B) calls the free wrapper at the end, so the p67 candidate must be a 340–380 B function calling `0xC278294`; the only size match (`0xC29239C`, 368 B) turned out to be a `StringBuffer` helper, so no candidate passed. |
| `qjs_define_property_value_str` | .155 (64 B) creates an atom from the name, defines, then releases the atom. On p67 the atom creator is `0xC294B50`/`0xC294DCC` and the atom release is `0xC2780B0`/`0xC2780D4`, but **no function calls both**, so this wrapper is inlined here. The module would have to compose atom-create + define + atom-release itself. |
| `qjs_new_array_buffer_copy` | .155 forwards with class id **19** and a free callback; no p67 function passes class 19 from a small wrapper, so the class id for array buffers on this build is not yet established. |
| `qjs_get_array_buffer` | depends on the same class id; .155 (38 B) calls an unwrap plus a length read. |
| `qjs_get_typed_array_buffer` | .155 (88 B) unwraps, then reads the three out-parameters (offset, length, element size) — the typed-array layout has to be read out first. |

Two of the six are therefore structural gaps (inlined wrappers) and four need the
array-buffer/typed-array class ids and layout. None were filled from resemblance.

### 7b. Final state of the QuickJS group

Two of the three remaining handles were worked to a definite answer and one to a
precise invariant gap.

**`qjs_define_property_value_str` is inlined on this build — recorded as a
composition.** The pieces are `|atom create| JS_NewAtomLen `0xC294DCC` (or the
three-argument core `0xC294B50`), the atom-plus-value define (**not yet pinned**;
`0xC2A46F8` is the *integer-key* variant that formats its key with `"%lld"` and
must not be used), and the atom release `0xC2780B0` / `0xC2780D4`, which the module
must call unconditionally, including on the error path. The order is
create → define → release, the same order .155 used in its 64-byte wrapper. This
is written into the pack as `qjs_define_property_value_str_composition`.

**Array buffers: the class-id route is closed by the image itself.** The
`ArrayBuffer` / `Uint8Array` strings at `0xcce9b29`-`0xcce9bdc` sit in a
contiguous QJS name blob (`globalThis`, `not-equal`, …, `Object`, `Array`,
`ArrayBuffer`, `Uint8Array`, …) and there is **no pointer to any address in that
blob anywhere in the database** — checked every segment, the cached-alias form and
the `0x1c…` region. So names are reached by offset into the blob, not by a
`class_name` pointer, and the class table is built at runtime, so the id cannot be
read statically. What is missing for all three array-buffer entries is one of two
things: the class-registration initializer that writes the name into the runtime
table, or an allocation site that passes a literal class id. Note that the id
space is otherwise proven shared — `qjs_is_function` uses class ids `13` and `41`
on both builds — so .155's `19` for array buffers is plausible but **not**
confirmed, and was not recorded.

**`qjs_new_string_len`: the allocation half is now known, the value half is not.**
The context hooks are pinned: runtime hook table at `rt+16`, `+4` free, `+8`
malloc, `+12` usable size (`js_realloc_rt 0xC28FE78`, `js_malloc_rt 0xC2D10F8`).
The string header layout is also pinned from the atom intern path `0xC278140`:
allocation is `len + 17` bytes through the malloc hook, refcount at `+0` (set to
1), length at `+4`, hash at `+8`, and the NUL terminator at `+16 + len`; that is
also why `qjs_free_cstring` subtracts 16. What is still missing is the function
that wraps such an allocation into a **value** (tag `-7`) instead of an atom:
`0xC278140` returns an interned atom, and the ASCII fast path in .155's
`NEW_STRING_LEN` calls a separate `js_alloc_string`. The invariant to look for
next is a function that calls the malloc hook with `len + 17`, sets the header as
above, and returns the pair `(string+16, -7)`.

**`qjs_to_cstring_len2`**: still no candidate. The 340–380 byte window contains no
function that allocates `len + 1`, copies and NUL-terminates before handing the
pointer out, and the only size match (`0xC29239C`) is a `StringBuffer` helper. The
invariant that would settle it: an allocation of `len + 1` through the same malloc
hook followed by a copy of the string payload and a NUL write, returning the raw
pointer (the module frees it with `F_QJS_FREE_CSTRING`, i.e. as `ptr-16` with tag
`-7`, so the allocation must be a normal JSString backing store, not a fresh
`malloc`).

Group status: **8 of 13 settled**, 4 open with a named invariant each, 1 settled as
a composition.

### 7c. Device pass: what Mirage settled and what it did not

Ran the QuickJS question on the Mirage `probe-p67` device (NSH works on p67, unlike
q66). `EVID-PORT-1043-DEVICE-001`.

**Settled and important for anyone repeating this:** the JS heap is in **PSRAM at
`0x3C000000`**, not in the `0x20000000` RAM. A snapshot of `0x20000000..0x20160000`
contains no JS data at all, which is why the first attempt looked empty. With the
PSRAM range the markers are immediate:

| object | marker run | address |
| --- | --- | --- |
| `var ab = new ArrayBuffer(512)` | 512 bytes of `0xA7` | `0x3C39E750` |
| `var t8 = new Uint8Array(8)` | 8 bytes of `0xB3` | `0x3C27A380` |

Practical notes recorded in the pack finding: `qjs` on this build has **no
`print`** (`console.log` works, and `throw <expr>` surfaces a value in the
interpreter error text, which is a convenient probe); `miragectl fs put` must be
done while the device is down; and a background `qjs` busy loop (`do {} while (1)`)
keeps the heap alive and does starve NSH, so `miragectl sh` times out — QMP reads
keep working through it.

**Not settled: the class ids.** The hop the plan relies on — data pointer →
`JSArrayBuffer` → `JSObject.u.array_buffer` → class id at `object+6` — did not land.
The 12 words that point at `0x3C39E750` were enumerated, but none of the candidate
structs around them matches the QuickJS `JSArrayBuffer` layout this build uses
(`ref_count` first, `data`/`byte_length` adjacent); the two candidates that pair
the data pointer with the value 512 are `0x3C313370` (`[512][0][data][…]`) and
`0x3C314810` (`[ptr][data][512][0][2][0x130100]`), and neither is reachable from a
`JSObject`-shaped record with a small refcount at `+0` and a class id at `+6`.

The descriptor-name route is also closed **at runtime**, not just statically: the
PSRAM snapshot contains **no word pointing anywhere into the flash name blob**
(`0xcce9b00..0xcce9d00`, checked in both the XIP and `+0x20000000` alias form), so
the runtime class table does not store `class_name` pointers either.

Next step for this question, in priority order:
1. Resolve the `JSArrayBuffer` layout on this build by allocating a *pair* of
   buffers with different lengths (e.g. 512 and 8) and diffing the two structs that
   point at their data — the common fields are the object, and `byte_length` then
   pins the offsets.
2. Then find `JSObject.u.array_buffer` the same way, and read `object+6`.
3. With the id in hand, the three array-buffer bindings follow from their class-id
   immediates exactly as planned.

Until then the three array-buffer entries and the two string entries stay open in
the pack; nothing was filled from resemblance.

### 7d. Class ids measured on the device, and what they unlocked

Two-length diff on a live runtime (`EVID-PORT-1043-DEVICE-002`): a staged script
allocated `ArrayBuffer(512)`/`0xA7`, `ArrayBuffer(8)`/`0x5C` and
`Uint16Array(16)`/`0x5C5C`, then one PSRAM snapshot was searched for the payloads
and for records with a small refcount, a plausible `+6` class id and the payload
pointer plus its length inside. Result:

| class | id | object found | field layout seen |
| --- | --- | --- | --- |
| `JS_CLASS_ARRAY_BUFFER` | **19** | `0x3C3146A0` (rc 2, payload 512) | payload at `+0x54`, byte length at `+0x58` |
| `JS_CLASS_UINT8_ARRAY` | **23** | `0x3C3146D0` (rc 1, 512 elements) | payload at `+0x24`, element count at `+0x28` |
| `JS_CLASS_UINT16_ARRAY` | **25** | `0x3C314610` (rc 1, 16 elements) | payload at `+0x24`, element count at `+0x28` |

The ids are consecutive in QuickJS's typed-array class order, which is the sanity
check that `+6` really is the class id; and `19` is confirmed a second time on the
static side, where `js_new_array_buffer 0xC2D34C8` passes `19` as the object class
and indexes the prototype table at `ctx+40` with offset `152 = 19*8`, validating
`'invalid array buffer length'` and installing the free callback `0xC27369C` on a
28-byte JSArrayBuffer.

**What this unlocks and what it does not.** `19` is no longer a guess inherited
from .155 — it is measured. The three astrolink wrappers still have to be picked by
shape (creation taking data/length plus a free callback; the two getters taking a
value plus output pointers), and that is the remaining work: the creation function
`0xC2D34C8` itself is *not* the module's `qjs_new_array_buffer_copy`, because the
module's entry copies the caller's data while this one adopts a buffer.

Next pass should therefore look for, in this order:
1. a small function calling `0xC2D34C8` after copying `len` bytes (that is
   `qjs_new_array_buffer_copy`);
2. a function checking class `19` and returning the pair at `+0x54`/`+0x58`
   (`qjs_get_array_buffer`);
3. a function checking the typed-array ids `23`/`25` (or the class range) and
   returning the three out-parameters (`qjs_get_typed_array_buffer`).

The two string entries are unaffected by this and stay open with the invariants
already written down in 7b.

### 7e. The three array-buffer entries are compositions, not addresses

Searched in the order set out, with the class id measured last pass. Result: this
build has **no library-level copy and no class-gated getters**, so all three are
compositions built on measured offsets. `EVID-PORT-1043-QJS-006`.

| binding | verdict | recipe |
| --- | --- | --- |
| `qjs_new_array_buffer_copy` | composition | `p = js_malloc_rt(ctx, length)` = `0xC2D10F8`; copy the caller's bytes; `js_new_array_buffer 0xC2D34C8(ctx, p, length)` adopts `p` and installs the free callback `0xC27369C` |
| `qjs_get_array_buffer` | composition | require tag `-1` and `u16(obj+6) == 19`, then payload = `*(obj+0x54)`, byte length = `*(obj+0x58)` |
| `qjs_get_typed_array_buffer` | composition | require a typed-array class id (`23` Uint8Array, `25` Uint16Array, consecutive run), then payload = `*(obj+0x24)`, element count = `*(obj+0x28)`, element size from the class id. **Byte offset is not measured and must stay unused.** |

Why compositions rather than addresses:

- `js_new_array_buffer 0xC2D34C8` has exactly two callers — `0xC861308` (a 28-byte
  forwarder) and `0xC8712A4` (a firmware JS binding) — and **neither copies**.
  The callee installs `sub_C27369C` as the free callback unconditionally, which is
  the adopt-with-free variant, so a copy must exist *before* the call or the
  callee would free memory the caller still owns.
- Thirteen functions that read `+0x54`/`+0x58` were examined and none of them is
  gated on class `19`; the firmware's own bindings reach the fields only after
  `JS_GetOpaque` (all ten `JS_GetOpaque` call sites pass the class id in a
  register). There is no exported `JS_GetArrayBuffer`/`JS_GetTypedArrayBuffer` to
  bind on this image.

The offsets used in the recipes are the ones measured on the live runtime, not
inherited: payload `+0x54` / byte length `+0x58` for the array buffer, payload
`+0x24` / element count `+0x28` for both typed arrays.

With that, the QuickJS group is: **8 addresses, 4 compositions (this table plus
`qjs_define_property_value_str`), 2 genuinely open** — `qjs_new_string_len` and
`qjs_to_cstring_len2`, whose missing invariants are unchanged from 7b.

### 7f. Route A (search the constant) and the final state of the two string entries

Searched every `ADD(S) #0x11` in the QuickJS region and checked what the sum feeds.
The candidate set is finite and it resolves into three kinds, none of which is the
constructor astrolink binds:

| site | what the `+17` allocation turned out to be |
| --- | --- |
| `0xC278140` | the atom-intern path: allocates through the hook with `len + 17`, writes refcount `+0`, length `+4`, hash `+8`, NUL at `+16 + len`, then looks the atom up and registers it — **returns an atom, not a `-7` value** |
| `0xC29C4B8`, `0xC29A78C` | string quoters (a `0123456789abcdef` table follows): allocate `len + 17` for the quoted form with escape expansion |
| `0xC27BFE0`, `0xC27C1FC` | the qjs file-eval paths, recognisable by a 256 KiB stack buffer |

So on this build the ASCII fast path is **inlined into its callers** rather than
exported: the header layout is confirmed a third time (`refcount +0`, `length +4`,
`hash +8`, `NUL at +16 + len`) but the function that wraps such an allocation into
a tag `-7` value is not a standalone entry, and `qjs_to_cstring_len2` has no
`len + 1` allocation site at all in the examined set (the only `+1` adds in the
region are index increments).

The two entries therefore stay **open**, with these exact gaps:
- `qjs_new_string_len`: needs a caller that invokes the inlined allocator and
  returns `(string + 16, tag -7)`; the allocator itself is inside `0xC278140`.
- `qjs_to_cstring_len2`: needs the `len + 1` allocation site, which the constant
  search did not produce.

Route B (breakpoint the allocator on the device) was not attempted to completion;
it remains the fallback, and the allocator hook is `*(*(rt+16)+8)` with
`js_malloc_rt 0xC2D10F8` as its wrapper.

### 7g. The two string entries are compositions — all three checks passed

The engine does not export these, but it does not have to: the header the module
needs is measured three times on this image and the engine's own behaviour confirms
the conventions. `EVID-PORT-1043-QJS-007`.

**Check 1 — hash convention: zero is "not computed yet", writing zero is correct.**
The atom intern path `0xC275B34` recomputes the hash from the payload
(`h = 263*h + byte`, masked `0x3FFFFFFF`) and only then compares it against the
stored hash of the entries in the atom table (`*(u32*)(entry+8) & 0x3FFFFFFF`).
A fresh string's `+8` is therefore never consumed as a cached value: `0` simply
means "uncomputed", and the engine computes when it needs it.

**Check 2 — the whole header, and the wide flag.** `0xC278140` (interned-atom
allocator) writes `+0 = 1` (refcount), `+4 = len`, `+8 = 0`, `+12 = 0`, and the NUL
at `+16 + len` — i.e. everything an ordinary string needs, with the atom
registration as the only extra step. The word at `+8` carries three fields: hash in
bits 0..29 and `atom_type` in bits 30..31; the atom-string path sets
`atom_type = 3` (`*(u8*)(s+11) |= 0xC0`) and an **ordinary** string must leave it 0.
`is_wide_char` is bit 31 of the word at `+4`, so writing `len` clears it, which is
correct for the 8-bit payload the module copies; a wide string would need the UTF-16
form instead.

**Check 3 — is a non-atom string acceptable where the module uses it?** Yes for the
uses that exist: `src/qjs/feature.rs` builds strings only as *values* — the Feature
name is passed to the define composition as raw `(ptr, len)` bytes, which that path
interns with `JS_NewAtomLen 0xC294DCC`, and every other string is an argument or a
property value. Ordinary (non-atom) strings are the normal case for JS values, and
the engine interns lazily if a value is later used as a key. What must **not** be
done is hand one of these strings to an entry that takes a `JSAtom` argument; the
module has no such call.

| binding | recipe |
| --- | --- |
| `qjs_new_string_len(ctx, ptr, len)` | `p = js_malloc_rt(ctx, len + 17)`; `p[0] = 1`; `p+4 = len` (bit 31 clear); `p+8 = 0`; `p+12 = 0`; copy `len` bytes to `p+16`; `p[16+len] = 0`; return `(p+16, -7)` |
| `qjs_to_cstring_len2(ctx, value, len_out)` | require tag `-7`; `len = *(u32*)(string+4) & 0x7FFFFFFF` (bit 31 = wide, must be converted, not copied); `q = js_malloc_rt(ctx, len + 1)`; copy from `string+16`; `q[len] = 0`; `*len_out = len`; return `q` |

Both allocations go through `js_malloc_rt 0xC2D10F8`, the same hook the array-buffer
composition already needs, and the cstring result is freed by `qjs_free_cstring
0xC278848` exactly as that path expects (`ptr-16`, tag `-7`, refcount decrement).

Consequence: **all 47 firmware bindings are now covered — 41 measured addresses
plus 6 compositions — and the two former gaps are resolved by construction rather
than by lookup.**

## Layout constants: what is measured, and where the holes are

The Band 11 module carries roughly sixty non-address constants. This section
records which of them have a p67 measurement. **Everything not listed in the
"measured" tables below is a hole** and must not be inherited.

### Measured (usable)

| constant | value | how established |
| --- | --- | --- |
| `JSVALUE_CLASS_OFFSET` | `6` | `qjs_is_function 0xC27DA88` reads the class id as `*(u16*)(obj+6)`; device objects confirm it (19 / 23 / 25) |
| `ARRAY_BUFFER_DATA` | `0x54` | device: `ArrayBuffer(512)` object at `0x3C3146A0` holds the `0xA7` payload pointer there |
| `ARRAY_BUFFER_LENGTH` | `0x58` | same object, byte length 512 |
| `TYPED_ARRAY_DATA` | `0x24` | device: `Uint8Array` 0x3C3146D0 and `Uint16Array` 0x3C314610 |
| `TYPED_ARRAY_COUNT` | `0x28` | same objects, element count |
| `JS_CLASS_ARRAY_BUFFER` | `19` | device + `js_new_array_buffer 0xC2D34C8` immediate and `ctx+40` offset `152` |
| `JS_CLASS_UINT8_ARRAY` / `JS_CLASS_UINT16_ARRAY` | `23` / `25` | device |
| uv mutex / loop | see `docs/loop-abi.md` | `uv_loop_init 0xC2DC9A0`, `uv_mutex_init 0xC2DCF1C` |
| `uv_mutex_lock` | `0xC1E9B1C` | 12-byte wrapper, 260 call sites, tail-calls the pthread timedlock body `0xC204E3C` with a null timeout; `uv_mutex_init` initialises a plain NuttX `pthread_mutex_t` |

### Holes that block a correct module today

| group | what is missing |
| --- | --- |
| `ARRAY_BUFFER_DETACHED` | `js_new_array_buffer 0xC2D34C8` writes a zero u16 at `+4` of the 28-byte JSArrayBuffer (`*((_WORD *)v14 + 2) = 0`), which carries the shared/detached flags, but **which bit is detached is not established** |
| `ADAPTER_STATE_WORD`, `ADAPTER_LOOP`, `ADAPTER_READY_STATE` | the adapter instance at `0x2013DBE4` is only used through the recovered getters; the field offsets were not traced from a producer, so no value is recorded |
| scan/advertising parameter and callback layouts | `app_lescan_start 0xC710BCC` builds an 8-byte block (`65538`, `0`) and a 20-byte block before calling `sub_C3B1374(adapter, params, ?, callbacks)`; the split between params, filters and callbacks, and the sizes of each, are **not** proven. `bt_le_start_advertising 0xC70ED94` passes a 28-byte stack block to `0xC7D3EB4`; its field roles are not proven either |
| scan report layout | not traced; the report is delivered through the callback table, which is exactly the structure that is unresolved above |
| CoC event codes (`COC_EVENT_*`) and `COC_QUEUE_EVENT` | the work node layout is measured (`owner +0`, `arg +4`, `handler +8`, `done +12`, flag byte `+16`), but the numeric event codes the handlers deliver were not enumerated |
| `COC_REQUEST_*` (size/status/status-detail/flags/callback/address/address-type) | only `psm +2`, `flags +8`, `psm entry +12`, `cid pair +16/+20`, `state +23`, `cid +0` are measured; the rest of the block is not |
| `COC_CONFIG_MTU/CREDITS/MPS` | channel `+52` (MTU), `+60/+62` (MPS pair) and stock `+14` (credits) are measured as *fields*; the module's constant names map onto them only if the semantics match, which was not proven |
| `COC_CHANNEL_QUEUE`, `COC_CREDIT_QUEUE_CREDITS`, `COC_DISCONNECT_POLICY` | not established |
| `COC_REGISTER_NODE_*`, `COC_EV_*` outputs | not established |
| `SDU_BUFFER_*`, `FRAGMENT_NODE_*` | **this build has no separate SDU buffer/fragment node type that I found**: the send path re-fragments the caller's buffer in place (`l2cap_send_handler 0xC801D10` rewrites the length words) and the credit flush dequeues 20-byte nodes allocated by `l2cap_work_submit 0xC7EB988`. If the module expects an engine-owned SDU buffer, p67 needs a different receive path |
| Feature framework constants (`D_FRAMEWORK`, `D_FRAMEWORK_VTABLE`, `D_APPLICATION_VTABLE`, `VTABLE_PREFIX_WORDS`, factory/init slots, `FRAMEWORK_IMPLEMENTATION`, `FRAMEWORK_MUTEX`, `FRAMEWORK_APPS_*`, `APPLICATION_RUNTIME`, `RUNTIME_MANAGER`, `MANAGER_*`, `HANDLE_LOOP`, `PROTOTYPE_MANAGER`, `INSTANCE_HANDLE`) | only these are measured: the manager dispatch table `0xCDA2CD0` (header `+0x00..+0x10`, slots from `+0x14`, `featureRequire +0x1C`, `findFeature +0x20`, `createFeature +0x24`, `uninit +0x2C`), `FeaturePost` reaching the runtime through `handle+28 → +12`, the detached flag at `handle+19`, the runtime message ring at `runtime+0x50..0x64` and the wake async at `runtime+0x3C`. Every other named constant in that group is a hole |
| `SIGNATURES` (12 bytes x4) | not extracted; the four functions are `bt_le_start_advertising 0xC70ED94`, `bt_le_start_scan_with_filters 0xC710BCC`, `bt_queue_external 0xC7EB9CC` and `l2cap_connect_handler 0xC801AC4`, but their first twelve image bytes were not read out in this session |

`uv_mutex_unlock` and `uv_close` remain **unaddressed**: the unlock primitive was not
isolated (the pthread unlock body has no pointer reference to its own assertion
string — those strings are reached by base+offset on this image — and no one-argument
libuv wrapper with a lock-sized call count exists), and `uv_close` was not located.

The honest summary is that the address half is complete (41 + 6 compositions) while
the layout half is roughly half measured: the QuickJS-side constants are solid
(measured on a live runtime), the L2CAP cookie-cutter fields are solid, and the GAP
parameter blocks plus most of the Feature framework constants need a dedicated pass
each before the module can be written without inheriting them.

## Composition primitives (addresses, so the generated crate can expose them)

The six compositions needed raw addresses that the pack did not carry. These are now
records, so `canopus target generate` exposes them. `EVID-PORT-1043-QJS-008`.

| record | address | what it is |
| --- | --- | --- |
| `js_realloc_rt` | `0xC2D10F8` | realloc slot of the runtime hook table (table at `rt+16`, slot `+8`), called `(hook_ctx, ptr, size)` with `r1`/`r2` passed through |
| `js_free_rt` | `0xC278000` | free slot (table `+4`), called `(hook_ctx, ptr)` |
| `js_new_atom_len` | `0xC294DCC` | atom from `(ctx, str, len)` with the numeric-key fast path |
| `js_new_atom_len_core` | `0xC294B50` | the intern core without that fast path (85 call sites) |
| `js_atom_unref` | `0xC2780B0` | release an atom (twin body at `0xC2780D4`) |

**Correction worth carrying:** `0xC2D10F8` is **not** a `js_malloc_rt` wrapper. Its
disassembly is `R0 = *(rt+0x10); R3 = *(R0+8); R0 += 0x10; BLX R3` with `r1`/`r2`
untouched, i.e. the **realloc** slot. The malloc slot (`+0` of the same table, called
`(hook_ctx, size)`) has no wrapper on this build — every allocation site inlines it
(`js_new_array_buffer 0xC2D34C8` does exactly that for its 28-byte JSArrayBuffer with
`28`, and the string allocator with `len + 17`). So the compositions must allocate
with `js_realloc_rt(rt, NULL, size)`, which is a malloc by contract.

`0xC27369C` (the `JSArrayBuffer` free callback) is **not** recorded on request: it is
a legitimate code address, but the module never passes it — `js_new_array_buffer
0xC2D34C8` installs it itself as part of adopting the buffer, so recording it would
only invite a caller to supply it twice.

libuv coverage check: `uv_mutex_init 0xC2DCF1C`, `uv_async_init 0xC2E14E8`,
`uv_loop_init 0xC2DC9A0` and `uv_mutex_lock 0xC1E9B1C` are all present as records;
`uv_mutex_unlock` and `uv_close` remain the only two addresses still missing.

## Send-path contract on p67 (Band 11's SDU/fragment shape does not exist)

Measured from both ends. `EVID-PORT-1043-COC-002`.

**1. Queue entry.** `bt_l2cap_submit_cid 0xC805BE8(buf, cid)`. It writes the cid
dword at `buf+4` and posts a work node through `l2cap_work_submit 0xC7EB988` with
`{owner = 0x2013BDBC, handler = l2cap_send_handler 0xC801D10, done = 0xC7EBD40,
arg = buf, flag = 1}`. So the object in the node's `arg` field at `+4` is the
**caller's stock buffer**, not a Band 11 SDU.

**2. Shape the handler reads.** `l2cap_stock_alloc 0xC7EAFBC` is the producer:

| offset | meaning |
| --- | --- |
| `+0` (u16) | total length = `payload_len + header_len + 4` |
| `+2` (u16) | payload offset = `header_len + 4` |
| `+4` (u16) | cid, written by `bt_l2cap_submit_cid` |
| `+6` (u16) | the caller's second word (handle) |
| `+8` (u8) | header-type nibble, `3` when the payload offset exceeds 7 |
| `+10` (u8) | flags; the handler writes `0x81` (first|last) on the single-fragment path |
| `+4+offset` | payload |

The handler writes the four-byte L2CAP header (length, cid) immediately before the
payload and re-fragments in place into 8-byte nodes chained through `*(channel+4)`
with the channel fragment size at `+106`.

**3. Ownership.** The **caller allocates** (`l2cap_stock_alloc(payload_len,
header_len)` = `bt_alloc(payload + header + 4 + 6)`), the **handler frees**
(`bt_free` on every exit path, including the early-outs when the PSM or channel is
not found). After `bt_l2cap_submit_cid` the module must not touch the buffer.

**4. Zero length.** Supported: the handler compares `total - payload_offset` and
takes its header-only branch (`LABEL_35` / `LABEL_21`) when it is zero.

A p67 shim can therefore be `queue_send(channel_word, cid, payload, len)` =
allocate with `l2cap_stock_alloc(len, hdr)`, copy, submit, and forget the pointer.

## Feature framework: what p67 actually has

**The manager table is the one the module calls through.** `FeatureManagerQjs`'s
constructor `0xC86646C` stores its vptr as `0x2CDA2CE4`, i.e. `0xCDA2CD0 + 0x14`, so
the table at `0xCDA2CD0` has **`VTABLE_PREFIX_WORDS = 5`** and the slots begin at
`+0x14`: `+0x1C featureRequire`, `+0x20 findFeature`, `+0x24 createFeature`,
`+0x2C uninit`, with `+0x04` a constant-true predicate, `+0x08` an empty stub,
`+0x00` a forwarding stub, and `+0x14`/`+0x18` the constructor and deleting
destructor. There is **no second manager table** in the traced paths.

**Feature objects are JS objects of a runtime-assigned class.** This is the one
finding that changes the module's registration path: `ensureJsClass 0xC867040`
allocates the class id with `0xC2780F4(&dword_2013CE38)` and registers the class
with `0xC278140(ctx, id, &classdef@0x200E7EA8)`. So `dword_2013CE38` is a **JSClassID
global, not the framework instance**, and a p67 module cannot hardcode the class id
for a Feature object — it must read that global after the runtime is up. Every
`JS_GetOpaque` in the framework uses it.

**Routing from a Feature handle (measured):** `handle+19` = detached flag,
`handle+28` → object, `*(that+12)` = the runtime; the runtime's**message ring** is
`runtime+0x50..0x64` (buffer, bounds, head, tail, sequence, 24-byte elements) and the
**wake async** is at `runtime+0x3C`, signalled with `uv_async_send 0xC2E15A4`.

### Named holes in this group

No p67 producer was found for: `D_FRAMEWORK` (a framework/manager *singleton global*
distinct from the JSClassID above), `D_APPLICATION_VTABLE`,
`FRAMEWORK_FACTORY_SLOT`, `FRAMEWORK_COPY_WORDS`, `APPLICATION_INIT_SLOT`,
`APPLICATION_COPY_WORDS`, `FRAMEWORK_IMPLEMENTATION`, `FRAMEWORK_MUTEX`,
`FRAMEWORK_APPS_BEGIN/END`, `APPLICATION_RUNTIME`, `RUNTIME_MANAGER`,
`MANAGER_REGISTRY`, `MANAGER_ASYNC`, `HANDLE_LOOP`, `PROTOTYPE_MANAGER`,
`INSTANCE_HANDLE`. The reason is structural, not a shortcut: on p67 the Feature
runtime is entered through **C++ virtual dispatch and JS objects**, and the host side
that owns the app list and the framework mutex is the `ferry`/`AIOTJS` application
framework (its own strings and vtables), which is a different subsystem from the one
Band 11's constants describe. Naming these on p67 requires tracing that host, and it
would be a mistake to reuse Band 11's offsets for it.

### uv_close and the unlock primitive

- `uv_close`: not located. The libuv teardown paths I traced (`uv_async_init`
  `0xC2E14E8`, `uv_loop_init 0xC2DC9A0`) do not call a single close entry, and the
  Feature teardown (`feature_runtime_enqueue`'s owner, `uninit 0xC866AB0`) does not
  reach one either.
- `uv_mutex_unlock`: the mutex is a NuttX `pthread_mutex_t`; `uv_mutex_init
  0xC2DCF1C` initialises it with the pthread trio, and the lock body `0xC204E3C`
  works on the semaphore at `mutex+8` with the holder pid at `mutex+20`
  (`*(u32 *)((mutex+8)+12)`). The semaphore post entity itself was **not** isolated
  with producer evidence: the best candidate `0xC1E68FC` (post core plus holder
  clear, 50 callers) has **no caller in common with the lock's 188 call sites**, so
  it is not the unlock path and was not recorded.

## p67 host trace: is registration possible without the framework object?

`EVID-PORT-1043-HOST-001`. Answer to the decisive question first.

**3. No — `feature_register_feature` cannot be reached from a native callback
alone.** The chain is short and closed:

```
host object (AIOTJS/ferry, from 0xC911306)
  -> feature_registry_create 0xC8660E4(host)      /* 24-byte registry, list head at +0 */
       -> feature_framework_init 0xC86E764(registry, *(host+12))
            -> feature_register_all 0xC892BC4(registry)
                 -> 28 x 12-byte thunk: feature_register_feature 0xC862898(registry, &descriptor)
  -> bind: 0xC86700C(registry, host)
```

The registry instance is **created per app host object**, never fetched from a
global, and `feature_register_feature` requires it (it logs `Failed to get
FeatureRegistry instance!` when null). So a p67 module cannot register by itself.

**2. The legitimate supply point** is the shape the firmware itself uses: a **12-byte
thunk that passes a static descriptor to `feature_register_feature`** — i.e. the
module would need the host's registry pointer handed to it. Registering an *app* is
the host's job and is not something a native module can do from a callback.

**4. The JSClassID global is safe to read at callback time.**
`dword_2013CE38` is allocated lazily by `ensureJsClass 0xC867040` (class-id allocator
`0xC2780F4`, then `JS_RegisterClass` with the class definition at `0x200E7EA8`) and
is **assigned before any Feature object exists**, because every Feature object is
created with that class. Every unwrap in the framework needs it —
`JS_GetOpaque(obj, dword_2013CE38)` in `event_set 0xC8697FC`, `event_get 0xC86A338`,
`method_call 0xC86C2B8`, `accessor_get/set`, `event_on_off` — and the class id is
already non-zero by the time an event reaches a callback.

**1. Host singleton and vtable: not established.** What the image gives is a naming
lead rather than an address: the ferry framework object appears in its own assertion
strings as `_pImpl->render_.loop`, `_pImpl->render_.stop`, `_pImpl->render_.event`,
`_pImpl->render_.async_del_app` (`uv_async_init` assertions around `0xcc9f348` /
`0xcc9f3dc` / `0xcc9f9b8`), which means the framework implementation member is
`render_` and the app-lifecycle callbacks hang off it. The singleton global, the
vtable address and the create/initialize slot indices were **not** resolved in this
pass, so no `D_FRAMEWORK` equivalent is recorded.

### The unlock primitive, finally

`ensureJsClass` also settled `uv_mutex_unlock`: it wraps its class-id allocation in
the pair `0xC2DCDA8` (tail-calls **pthread_mutex_lock `0xC1E9B1C`**) and `0xC2DCDC4`
(tail-calls **`0xC204F08`**). So the inlined libuv unlock jumps to `0xC204F08`, which
is a label whose real work is `pthread_mutex_unlock_core 0xC205430` (semaphore at
`mutex+8`, holder list, post). Composition for the module: call `0xC205430(mutex)`;
`uv_mutex_lock 0xC1E9B1C` is now confirmed from the caller side as well, not only
from `uv_mutex_init`.

`uv_close` remains unlocated.

## The host contract: four fields, so this is a shim

`EVID-PORT-1043-HOST-002`. `feature_registry_create 0xC8660E4(host)` and the two
calls it makes touch **exactly four host fields**, and none of them is dereferenced
during registration:

| host field | who reads it | instruction / use |
| --- | --- | --- |
| `+0` (u32) | `feature_framework_bind 0xC86700C` | `v6 = *a2`, then `0xC860F08(v6)` → `0xC8617B8` does `*(u32 *)adapter = hostp0` and fills the adapter's callback table at `+8..+68` |
| `+4` (u32) | `feature_registry_create` | `*(_DWORD *)(a1 + 4)` passed to `0xC860F44(registry+8, value)`, whose body is `*(u32 *)(result+4) = a2` |
| `+8` (u8) | `feature_registry_create` | `if (*(_BYTE *)(a1 + 8) != 0) return nullptr;` — an abort flag checked **after** init, so it must be `0` |
| `+12` (u32 → `const char *`) | `feature_registry_create` → `feature_framework_init 0xC86E764(registry, *(host+12))` | init calls `0xC21D974(registry+12, a2)` when `a2 != 0`; that function is a **std::string operator=** (it writes length/tail and a NUL), so the value is a package-name string. `NULL` is accepted. |

Nothing else: init's remaining reads (`registry+12` flags, `registry+16`, `registry+20`
for the `package_name is %s` log) are on the **registry**, not the host, and the
registry was just zeroed by create.

**So: a 16-byte, caller-allocated host struct is structurally sufficient** —
`{ u32 token; u32 sink; u8 abort = 0; u8 pad[3]; const char *package_name; }` — and
the whole integration is `create → init (inside create) → register_all` or, for a
module, `create → feature_register_feature(registry, &descriptor)`.

Two honest caveats:

- `host+0` and `host+4` are **stored for later**, not used during registration. They
  govern event delivery (the 148-byte adapter's callbacks receive `host+0`), so a
  minimal host registers correctly but events reaching JS depends on what those two
  words point at.
- `feature_registry_create` has exactly **one caller** (`0xC86236C`, reached from the
  AIOTJS/ferry side at `0xC911306`), i.e. there is no second, host-free path. The
  shim is therefore the route: build the host struct, create the registry, register.

`_pImpl` tracing is no longer needed for registration. It stays relevant only for
delivery: the adapter built from `host+0` is where the framework will hand events
back, and `_pImpl->render_.loop/.stop/.event/.async_del_app` is what a real host puts
there.

## Final measurement round: array buffers settled, CoC/GAP named as holes

`EVID-PORT-1043-QJS-009`.

### A5. `ARRAY_BUFFER_DETACHED` — settled, and it corrects an earlier record

The engine's own accessor is `JS_GetArrayBuffer 0xC2D362C`:
tag `-1`, class `19` or `20` (ArrayBuffer / SharedArrayBuffer), then

```
abuf    = *(void **)(obj + 32)
detached= *(u8  *)(abuf + 4)          /* non-zero => "ArrayBuffer is detached" */
data    = *(void **)(abuf + 0)
length  = *(u32 *)(abuf + 8)
```

`JS_NewArrayBuffer 0xC2D34C8` is the producer: it allocates the 28-byte `abuf`,
stores the length word, and clears the flag with a 16-bit store at `abuf+4`
(`*((_WORD *)v14 + 2) = 0`), which covers both the detached and shared bytes.

**So detached is a byte, not a bit**, and the earlier device-derived offsets
(`object+0x54` payload, `+0x58` length) were read from the wrong object: the
engine-visible layout is `abuf` reached through `object+32`. That record has been
corrected and the class-id record patched to point here. `JS_GetTypedArrayBuffer`'s
indirection to the same struct (`0xC2D368C` checks it before converting) is **not**
measured, so the typed-array route stays a hole.

### A1–A4, B1–B4: holes, with the reason

The CoC event numbering and request block, and the GAP adapter/scan/advertising
layouts, are **not** established. What was checked this round and why it did not
close:

- `COC_EVENT_*`: the handlers build signalling frames in place (e.g. `0xC802AEC`
  writes code `21` into a frame allocated with `l2cap_stock_alloc(18, 8)` and a
  second variant with `(16, 8)`), and the branch that chooses a `v9` value
  (`2`, `4`, `6`) feeds the **frame**, not an app-facing event. The app-facing
  event codes are delivered through the PSM entry callback (`entry+0`, invoked from
  `sub_C8005A8`) and the numbering was not enumerated.
- `COC_REQUEST_*`: only `cid +0`, `psm +2`, `flags +8`, `psm entry +12`, cid pair
  `+16/+20`, `state +23` are measured. The remaining fields live in the request
  block that the *app* fills; no producer was traced.
- `COC_CONFIG_MTU/CREDITS/MPS`: channel `+52` / `+60` / `+62` and stock `+14` are
  measured as **fields**; whether they carry the module's semantics is unproven.
- `COC_CHANNEL_QUEUE`, `COC_CREDIT_QUEUE_CREDITS`, `COC_DISCONNECT_POLICY`,
  `COC_REGISTER_NODE_*`, `COC_EV_*` outputs: not established.
- GAP: `ADAPTER_STATE_WORD/LOOP/READY_STATE` were not traced from a producer. The
  `app_lescan_start 0xC710BCC` block split (8-byte block `{65538, 0}`, 20-byte block,
  callback table `&unk_200DFAF0`) and the 28-byte advertising block handed to
  `0xC7D3EB4` are seen but their field roles are not proven, and the scan report
  layout was not traced at all.

**Ship decision input.** For a module that only registers a descriptor and answers
method calls, the array-buffer layout is the last structure it needs and it is now
measured. The CoC and GAP groups matter only if the module is to host the BLE scan /
connection services on p67; those need one dedicated round each before they can be
written without guessing, so the honest options are (a) ship p67 with those services
disabled, or (b) fund the two rounds.

