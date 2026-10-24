# Xiaomi Band 9 3.1.32 loader

**Status: EMULATOR-VERIFIED / DEVICE NOT PROBED.** The staged loader reaches the
Supervisor, registers `/dev/canopus`, and completes the installer's full CPC2
sequence in Mirage (`EVID-EMU-9132-001`). No physical device run exists.

- Image: `fwbins/xiaomi-band-9-3.1.32/vela_ap.bin`, SHA-256 `9c02dab4…3264`
  (n66cn; the complete package's AP hash is identical).
- UI family: **LVGL v8**. Manager backend:
  `manager/target/lvgl_v8/canopus_manager_target_lvgl_v8.c`. Launcher-entry and
  notification service entries still fail closed.
- Loader: Band-11-style Lua-owned stage1 (`mw`/`exec`) → PIC stage2 with the
  portable ELF32 loader → Supervisor `ET_REL`. No `insmod`; no Lua `os.execute`
  recovery shim.

## Verified path (Mirage, 2026-10-25)

1. The **complete** `miwear.watch.n66cn_v3.1.32_full_489f2050` package is
   imported. Only `vela_ap.bin` leaves `/system` and the resource partition empty
   (black panel). The AP SHA matches the target.
2. The installer face is packaged with the patched Facetory exporter
   (`scripts/emu/band11/facetory-lua-layer.patch`) at protocol `0x0800`. The
   engine runs `/data/app/watchface/market/366140016/main.lua`.
3. Stage 0 publishes the cave at `0x200D0000` (RWX first, narrow to RX,
   firmware `DSB/ISB/BX LR` tail) and enters stage 1.
4. Stage 1 allocates stage 2 from Kmem, leases one MPU region, and enters it.
5. Stage 2 reads the Supervisor ELF into Kmem, allocates the 149,866-byte
   resident image from **Umem** via `memalign` (Kmem only leaves ~139 KiB), and
   executes it through the privileged default map (no extra MPU lease).
6. The Supervisor registers `/dev/canopus`. The installer opens it and its CPC2
   `INSTALL 0`, `INSTALL 2`, `RESTORE_AFTER_BOOT`, `INSTALL 1`, `INSTALL 2`
   all return result 5; `/data/canopus/registry.bin` is written with `CDR1`.

The stage-0 cave moved from `0x200CB400` after that address was found to carry
runtime log data (`rc=0x74736967`, ASCII "tsig"). `0x200D0000` is inside the same
unassigned gap between BSS (`0x200CB3D4`) and Kmem (`0x200DB418`).

## Two exact-IDB corrections

- `register_driver` is the **type-1** inode variant at `0xc4f7c40`. The earlier
  record pointed at `0xc4e38ce`, which sets type 7; `/dev/canopus` then opened
  with `ENOSYS`. See `EVID-ABI-9132-REGISTER-001`.
- The v8 Manager's identity guard now accepts NUL/LF/CR after the `3.1.32`
  build.prop token.

## Build

```sh
CANOPUS_TARGET=xiaomi-band-9-3.1.32 scripts/build_canopus_supervisor.sh
```

Supervisor 112,832 bytes file / 149,866 resident, verifier PASS (0 undefined);
stage1 417 bytes, stage2 3,576 bytes. Resources are staged flat under
`watchfaces/canopus-installer-prod/xiaomi-band-9/`.

All symbol records remain `restricted`/`PENDING`; no approval or device proof
exists. The 2026-08/09 failures in `bootstrap.toml` remain historical evidence.
