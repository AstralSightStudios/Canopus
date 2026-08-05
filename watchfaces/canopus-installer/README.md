# Canopus installer watchface

A Lua LVGL watchface that installs and manages Canopus modules on the
xiaomi-band-10-pro-3.101.030, structured exactly like
`firmware_latest/btpatch_phase5_watchface`: a native char-device module
(`canopus_supervisor.bin`) driven by a Lua page over a fixed status/command
ABI.

Opening the page performs no native operation; every action requires an
explicit button press.

## ⚠️ Gate status — read before real-device use

This is a **staged test artifact**, not a proven installer.

| Part | Status |
| --- | --- |
| Watchface UI (`main.lua`) | Structurally complete; mirrors the btpatch pattern (verify ELF, `insmod`, LVGL buttons, fixed ABI). |
| Supervisor core (`manager/service/canopus_supervisor.c`) | Host-tested (12 tests): command/status ABI, lifecycle-aware enable/disable/remove, safe mode. |
| `canopus_supervisor.bin` | **Verifier PASS** (sha256 `87c9ebf1`, 0 undefined, 1 ctor/1 dtor) — a valid zero-import ELF32 ET_REL. |
| Device registration (`/dev/canopus`) | **Implemented.** `canopus_supervisor_platform.c` registers the device via the stock `register_driver` (0x0C1A0D51) exactly like btpatch registers `/dev/btpatch` — the same managed symbol the veneer exposes as `canopus_fw_register_driver`. The read side renders the status ABI, the write side dispatches commands. |
| Loading Canopus modules via stock modlib | **Pending (G0 for target modules).** The supervisor's `load_module` hook is fail-closed until the stock modlib path for arbitrary ET_REL modules is proven. |
| Package staging + signature verify | **Pending.** `stage_package` is fail-closed. |

**What you can test on device now:** LOAD the supervisor, then REFRESH — the
page should read a live status ABI from `/dev/canopus` (framework version,
module count, last result). QUERY/SAFE MODE work. INSTALL/ENABLE/DISABLE send
commands that the supervisor dispatches; module load/unload actions report the
fail-closed result until the target-module loader (G0) is wired.

## Structure

```text
watchfaces/canopus-installer/
├── main.lua                Lua LVGL installer page
├── canopus_supervisor.bin  built supervisor module (verifier PASS)
└── build/                  build output (gitignored)
```

Supervisor source:

```text
manager/service/
├── canopus_supervisor.{h,c}          core: status/command ABI + lifecycle dispatch
├── canopus_supervisor_platform.h     device-gated hooks (register/load/unload/stage)
├── canopus_supervisor_platform_stub.c stub platform (fails closed; device RE pending)
└── canopus_supervisor_module.c       module glue: ctor/dtor, /dev/canopus wiring
```

## Build

```sh
bash scripts/build_canopus_supervisor.sh   # cross-compile + verify + stage .bin
```

Requires `clang` (ARM target) and `ld.lld`, and `canopus target
generate-veneer xiaomi-band-10-pro-3.101.030` to have run.

## Status ABI (`/dev/canopus`, read, 384 bytes)

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 4 | magic `0x43505331` ("CPS1") |
| 4 | 4 | abi (1) |
| 8 | 4 | framework revision |
| 12 | 4 | safe mode (0/1) |
| 16 | 4 | module count |
| 20 | 4 | last command |
| 24 | 4 | last result state (`CANOPUS_RESULT_*`) |
| 28 | 4 | flags |
| 32 | 4 | error code |
| 36..127 | 92 | reserved |
| 128..383 | 16×16 | module slots: `state, lifecycle_class, version, flags(bit0=signature_ok)` |

## Command ABI (`/dev/canopus`, write, 16 bytes)

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 4 | magic `0x43504331` ("CPC1") |
| 4 | 4 | command (`QUERY/INSTALL/ENABLE/DISABLE/REMOVE/UPDATE/ROLLBACK/SAFE_MODE`) |
| 8 | 4 | arg0 (module index) |
| 12 | 4 | arg1 (reserved) |

## Fresh-boot procedure (staged)

1. Reboot the Band.
2. Press **LOAD** once. The page verifies the bundled ELF and `insmod`s
   `canopus_supervisor.bin`. Never load twice, never `rmmod`.
3. Press **REFRESH**. Expected while the platform is stubbed:
   `Cannot open /dev/canopus` — this is the current honest boundary.
4. Once the device platform lands, press **INSTALL** to stage a package, then
   enable/disable/remove per module index, then **QUERY** to re-read state.
5. After any fail-stop or after any native action, reboot for complete
   recovery; preserve the Band log.

## Safety

- Boot-resident supervisor: never `rmmod`, never insert a second copy.
- Stubbed operations fail closed (never guess a device API).
- All native addresses restricted to AP SHA-256
  `f701a84f...d225b` (the packed identity guard).
