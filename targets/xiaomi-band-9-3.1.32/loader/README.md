# Xiaomi Band 9 3.1.32 loader

**Status: BUILT / EMULATOR-ONLY / NOT_PROBED.** The Supervisor and staged loader
build and pass the verifier, but the loader has not run yet (see Mirage below).

- Image: `fwbins/xiaomi-band-9-3.1.32/vela_ap.bin`, SHA-256 `9c02dab4…3264` (n66).
- UI family: **LVGL v8**. The v9 Manager backend does not apply; the v8 backend
  is restored at `manager/target/lvgl_v8/canopus_manager_target_lvgl_v8.c`.
  Launcher-entry and notification service entries still fail closed.
- Loader: Band-11-style Lua-owned stage1 (`mw`/`exec`) → PIC stage2 with the
  portable ELF32 loader → Supervisor `ET_REL`. No `insmod`; no Lua `os.execute`
  recovery shim (this firmware exposes `os.execute` directly).
- `bootstrap.toml` is the exact 3.1.32 Kmem/MPU profile: cave `0x200CB400`,
  result word `0x200CB440`, region 7, exact allocator/MPU/inode entries and the
  `DSB SY; ISB SY; BX LR` tail at `0x0C5226DA`. The earlier device fault at the
  same cave (`CFSR 0x82`, `MMFAR 0x2006a9b0`) came from the protected SRAM-text
  cave, not this one; the 2026-08-31 retest reached the load check. The installer
  publishes RWX first, narrows to RX, and synchronizes before entry.

## Build

```sh
CANOPUS_TARGET=xiaomi-band-9-3.1.32 scripts/build_canopus_supervisor.sh
```

Supervisor 112,876 bytes / 149,866 resident, verifier PASS (0 undefined);
stage1 417 bytes, stage2 3,524 bytes. Resources are staged flat under
`watchfaces/canopus-installer-prod/xiaomi-band-9/` and the installer `main.lua`
uses the current CPC2 control protocol.

## Mirage status (2026-10-25)

- The complete `n66cn_v3.1.32_full_489f2050` package was imported; its
  `vela_ap.bin` SHA-256 equals this target. Stock watchfaces render (the first
  import contained only `vela_ap.bin`, so the resource/system partition was
  empty and the screen stayed black — a full package is required).
- The installer face packaged by Facetory with **protocol 0x0800** installs and
  sets current, but only its background renders: the n66 watchface engine does
  not execute the packaged Lua script record. The loader therefore has not been
  entered yet. A Band-9-compatible Lua watchface package is the remaining
  blocker for the emulator run.

All symbol records remain `restricted`/`PENDING`; no approval, device probe or
capability state exists.
