# Xiaomi Band 9 Pro 3.1.190 loader

**Status: EMULATOR-VERIFIED / DEVICE NOT PROBED.** The staged loader reaches the
Supervisor, registers `/dev/canopus`, and completes the installer's full CPC2
sequence in Mirage (`EVID-EMU-9190-001`). No physical device run exists.

- Image: `fwbins/xiaomi-band-9-pro-3.1.190/vela_ap.bin`, SHA-256 `3e264bc2…f82f`.
- UI family: **LVGL v8**, shared v8 backend
  (`manager/target/lvgl_v8/canopus_manager_target_lvgl_v8.c`).
- Loader: cave entry (`entry_mode = "cave"`): stage 0 executes the trampoline at
  `0x20080000`, stage 1 is written at `0x20080100`, stage 2 loads the Supervisor
  from Kmem input into a Umem image, and the Supervisor constructor registers the
  driver. No `insmod`; no Lua `os.execute` recovery shim.
- Build: Supervisor 112,924 bytes / 149,922 resident (verifier PASS); stage1 413
  bytes, stage2 3,580 bytes.

## Verified path (Mirage, 2026-10-25)

1. Complete package `miwear.watch.n67cn_v3.1.190_full_3e264bc2` boots to the
   stock watchface.
2. The installer face is loaded from the registered market package
   `366240003` (`resource.bin` plus the rebuilt `main.lua` and bootstrap files).
   A newly added market id is not registered by the firmware's watchface DB and
   is never loaded.
3. `Run` writes the cave trampoline and stage 1, executes them through the NSH
   `exec` handler, and the trampoline returns to the handler.
4. Stage 2 allocates the Supervisor image from Umem (`memalign`), invokes the
   constructor, and `/dev/canopus` registers.
5. The installer's CPC2 `INSTALL 0`, `INSTALL 2`, `RESTORE_AFTER_BOOT`,
   `INSTALL 1`, `INSTALL 2` all return result 5 and the page shows
   `Run completed`.
6. `/data/canopus/registry.bin` contains `CDR1`;
   `/data/app/system/install_info_v1.db` contains `com.canopus.manager`.

## Fixes proved by this run

- **Record re-match.** The src-9pro-3175 port had wrong VFS/app/LVGL
  addresses. `open`, `close`, `read`, `write`, `unlink`, `unregister_driver`,
  `page_goto`, `page_finish`, `app_lookup`, the four LVGL entries and
  `watchface_manager_delete_watchface` were re-matched from the
  emulator-verified 3.1.32 records (`EVID-PORT-9190-CORRECT`).
- **Kmem allocator.** `kmem_malloc` must be `0xc0f11f4` (kmm descriptor
  `0x200b0840`), not `0xc0f1170` (Umem descriptor `0x200b0d44`); the Kmem free
  asserts on a foreign address.
- **Cave trampoline.** The old `blx` + `bx lr` tail clobbered LR and looped in
  the cave. The trampoline now saves LR on the stack and returns with
  `pop {r4, pc}`.
- **App descriptor ABI.** 3.1.190's `launcher_load_app_info` reads the metadata
  callback at `+0x1c` and `app_install` copies 0x3c bytes; the 3.1.32 layout
  (+0x14) made the launcher call the null `page_registry` slot. The v8 backend
  selects the 3.1.190 layout with `-DCANOPUS_MGR_APPINFO_190`.
- **Entry mode.** The mailbox entry probe calls `exec_handler+0x45`, which is not
  an instruction boundary in 3.1.190 (UFSR.UNALIGNED). The profile uses the cave
  entry.

All symbol records remain restricted/PENDING; no approval or device proof
exists. The v8 Manager's launcher-entry publish and notification service
entries still fail closed.
