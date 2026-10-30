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

### 7. QuickJS: still open, and why the marshaller lead did not pay off

13 astrolink bindings, 1 resolved (`qjs_free_value 0xC2781E0`).

**The `0xC8670F0` lead was followed and does not work as hoped.** That function is
not the JS interop layer: its 26 indirect calls all dispatch C++ virtual slots on
a target/context object (`[R9] -> +0x30`, an argument type/count reporter), its
callees are `std::string`-shaped helpers from the `0xC20xxx` range, and its only
direct call into the QuickJS region is an error formatter (`0xC28FD50`, `error:
%s`). The Feature framework converts values through its own abstractions
(`feature_context_ref`, `feature_value_t`) and the actual JS binding lives deeper
(AIOTJS/ferry), not in the marshaller. So this path gives no call shapes for the
constructors and accessors.

What the walk did establish, all in the QuickJS region (worth keeping as the
starting map for the next attempt):

| address | what it is | how it was settled |
| --- | --- | --- |
| `0xC2781E0` | `__JS_FreeValue` | its own error string + `quickjs.c:5636`, 1046 callers |
| `0xC27AD94` | `JS_DupValue` | mirror of the free switch, 99 callers |
| `0xC278848` | string release | decrements the string refcount, then `__JS_FreeValue` tag `-7` |
| `0xC278090` | atom ref | guards `atom > 211` and bumps `ctx+56` table entry |
| `0xC2780B0` / `0xC2780D4` | atom unref (two inlined copies) | same guard, decrements and frees at zero |
| `0xC27DB50` | `JS_GetOpaque(obj, class_id)` | `tag == -1 && *(u16*)(obj+6) == class_id`, returns `obj+32` |
| `0xC2748DC` / `0xC294B50` | atom lookup by name pair | called together on a `char *` name from the property path |
| `0xC2D47F8` | empty stub, 111 callers | 12-byte body; a build-time placeholder, not a usable entry |

**What is still missing, per entry** (all twelve): a site that calls the entry
with the shape astrolink needs. The shapes are known (they are in `src/qjs/
feature.rs`), the addresses are not:

- `qjs_new_string_len` `(ctx, ptr, len) -> Value`: needs a 3-argument constructor
  in the string block (`0xC278140`-`0xC278B74`) that allocates and copies. Nine
  functions in that block have zero static callers (`0xC2782B4`, `0xC27843C`,
  `0xC278470`, `0xC2784A8`, `0xC2784F8`, `0xC2788B8`, `0xC278A58`, `0xC278A9C`,
  `0xC278B28`), so at least one of them is reached only through a context hook —
  that hook has to be identified before any of them can be named.
- `qjs_new_object` `(ctx) -> Value`, `qjs_get_exception` `(ctx) -> Value`: the
  tag `-1` / exception-tag constructors were not isolated.
- `qjs_is_function` `(ctx, Value) -> i32`: needs the `JS_CLASS_BYTECODE_FUNCTION` /
  `JS_CLASS_C_FUNCTION` class ids for this build, which are not yet read out.
- `qjs_call` `(ctx, func, this, argc, argv) -> Value`: not isolated; the
  interpreter dispatch is inlined, so this one needs its public wrapper found by
  its caller set rather than its body.
- `qjs_to_cstring_len2` / `qjs_free_cstring`: not isolated; `free_cstring` must
  call the context's free hook, which has not been located.
- `qjs_define_property_value_str` `(ctx, obj, name, value, flags) -> i32`: the
  closest lead is `0xC29A474` (allocates a 408-byte property record through the
  context hook, links it into the object, looks the name up with `0xC2748DC` /
  `0xC294B50`, and initialises the descriptor array with `-1` tags). It takes an
  atom rather than a name, so it is more likely the atom-level define that the
  `_str` variant wraps. Not recorded: the wrapper itself was not found.
- `qjs_new_array_buffer_copy`, `qjs_get_array_buffer`,
  `qjs_get_typed_array_buffer`: not isolated; the typed-array class ids and the
  array-buffer layout (data pointer, length, class id) have to be read out first.

Recorded honestly: twelve of thirteen remain unrecorded, and the reason is not a
lack of effort but the absence of any self-describing text for QuickJS in this
build plus the failure of the marshaller lead. Cross-line matching must still not
be used (22% agreement between .155 and .043).
