# REDMI Watch 6 3.100.043 loader

**Status: STATIC ONLY + CAVE PRIMITIVE PROVEN / NOT_PROBED.** This image has
never been deployed by Canopus.

- Image: `fwbins/xiaomi-p65-3.100.043/vela_ap.bin`, SHA-256 `a19b6014…5858`
  (p65, best1502p, 11,793,540 bytes).
- UI family: **LVGL v9**, same generation as Band 10 Pro; the v9 backend family
  applies (no new v8 backend needed).
- Selected loading style: **Band-11-style Lua-owned staged loader** — PIC stage1,
  stage2 with the portable ELF32 loader, Supervisor as `ET_REL`. No `insmod`, no
  Lua-level `os.execute` recovery.
- History: a 2026-09 experimental P65 deployment boot-looped to the Logo and
  required a REC data wipe (ADR-CAN-007). Treat any device test as a fresh,
  gated experiment with a known recovery path.

## Mirage primitive proof (2026-10-25, `EVID-EMU-65043-001`)

- The injected NSH on a Mirage p65 device lists `mw` and `exec`.
- A four-instruction stub written at `0x2019e800` (gap between RAM8_BSS end
  `0x2019e720` and MSP_STACK start `0x2019f3e0`) returned after
  `exec 0x2019e801` and published `0x5a`. The default map executes unassigned
  SRAM, so the cave loader style is viable.

## Exact strings

- `firmware_version_string` = `0xc3bf9fd` ("3.100.043" inside the property
  blob at `0xc3bf9ec`).
- `firmware_build_string` = `0x0cbff458` ("d516af0:nx_best1502p_ap" in the XIP
  boot info). The old record `0xc3bfa25` pointed into the charger blob and was
  corrected.

## Recovered static ABI (2026-10-25, `EVID-STATIC-P65-002`)

The custom-loader (FW_9/SUP_9) set is being recovered by manual review of the
exact IDB; the XIP copies of the SRAM-resident bodies are thunks, so only the
SRAM/PSRAM entry addresses are recorded.

- NuttX VFS: `open` `0x1c1580e4`, `close` `0x1c154c40`, `read` `0x1c158720`,
  `write` `0x1c158690`, `unlink` `0x1c159824`, `rename` `0x1c1587c8`,
  `errno_location` `0xc4f793c`; helpers `fs_getfilep` `0x1c154700`,
  `file_read` `0x1c158790`, `file_write` `0x1c159a90`.
- Inode/driver: `inode_lock` `0x1c154f50`, `inode_reserve` `0x1c155888`,
  `inode_remove` `0x1c1557b4`, `inode_unlock` `0x1c154f70`,
  `register_driver` `0x1c15a954` (four arguments), `unregister_driver`
  `0x1c15aa18`.
- App/page: `app_lookup` `0x1c151108`, `app_install` `0x1c151ae4`,
  `page_goto` `0x1c132f4c`, `page_finish` `0x1c1325b8`.
- LVGL v9 core: `lv_obj_class_create_obj` `0x3775bc`,
  `lv_obj_class_init_obj` `0x3776b0`, `lv_obj_create` `0x387b78`,
  `lv_label_create` `0xc78bcb4` (`lv_label_class` 0x3c224844 / XIP copy
  0xc3145b8).
- Allocators: default-heap wrappers `0xc50b2a0` (malloc), `0xc50b25c` (free),
  `0xc50b2c8` (memalign) over the descriptor `dword_3C3D4F10[3043]`.

## Remaining blockers

- **Platform ABI (resolved).** `CANOPUS_SUP_PLATFORM_COMPLETE` is 1: the LVGL
  setters, the image/bar create helpers, the launcher trio and the corrected
  `lvx_page_title_create` (0xc21b2fc; the old candidate was the battery
  variant) are recorded. The Supervisor compile now stops on the native
  bindings instead: the v9 UI host needs the `FW_LVX_*`/`FW_UI_*`/style macros
  that Band 11 supplies through `loader/native-bindings.json`.
- **MPU lease (resolved).** The BES MPU driver is present: allocate
  `0xc0c017c` (8-bit bitmap at `algn_3C489EAC`, IRQ-masked, 255 when full),
  configure `0xc9665e4(region, base, size, access, lock)` — the exact
  five-argument ABI the staged Supervisor calls — writing RNR/RBAR/RLAR at
  `0xE000ED98/9C/A0` through `0xc9665a0`, and release `0xc966670` (bitmap
  clear; the Supervisor disables the hardware region itself). `0xc9666b0` is
  the alloc-and-configure `mpu_setup(base, size, access, mode)` used by the
  firmware. The earlier "dead rodata" strings are message ids passed to the
  BES logger, not direct string references.
- **register_driver arity (resolved).** The generator emits the four-argument
  `CANOPUS_SUP_REGISTER_DRIVER` for this target (path, fops, mode, private).
- **Loader profile.** The exec/mw handler addresses are not recorded yet; the
  rest of the staged profile (VFS, inode, allocator) is recovered above.
- **Lua surface.** The watchface Lua engine's `os.execute` path is untested on
  p65; the Facetory Lua-layer packaging applies as on the other v8/v9 faces.

All symbol records remain `restricted`/`PENDING`; no approval or device proof
exists.

