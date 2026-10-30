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
