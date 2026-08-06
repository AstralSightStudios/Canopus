# Canopus installer watchface

A Lua LVGL watchface that installs and manages Canopus modules on the
xiaomi-band-10-pro-3.101.030, structured exactly like
`firmware_latest/btpatch_phase5_watchface`: a native char-device module
(`canopus_supervisor.bin`) driven by a Lua page over a fixed status/command
ABI.

Opening the page performs no native operation; every action requires an
explicit button press.

## ⚠️ Gate status — read before real-device use

This is an **installable destructive device probe**, not yet a lifecycle-proven
production installer.

| Part | Status |
| --- | --- |
| Watchface UI (`main.lua`) | Structurally complete; mirrors the btpatch pattern (verify ELF, `insmod`, LVGL buttons, fixed ABI). |
| Supervisor core (`manager/service/canopus_supervisor.c`) | Host-tested (12 tests): command/status ABI, lifecycle-aware enable/disable/remove, safe mode. |
| `canopus_supervisor.bin` | **Verifier PASS** (sha256 `1855791d`, 0 undefined, 2 ctors/1 dtor) — a valid zero-import ELF32 ET_REL containing the supervisor, CPC2 snapshot query path, and resident exact-target Manager implementation. |
| Device registration (`/dev/canopus`) | **Implemented.** `canopus_supervisor_platform.c` registers the device via the stock `register_driver` (0x0C1A0D51) exactly like btpatch registers `/dev/btpatch` — the same managed symbol the veneer exposes as `canopus_fw_register_driver`. The read side renders the status ABI, the write side dispatches commands. |
| Native Manager registration path | **DEVICE PASS.** The exact-target Manager code is linked into the resident supervisor. INSTALL writes the legacy bootstrap command to `/dev/canopus`, so `app_install`, `app_launcher_add`, and notification insertion execute synchronously in the calling miwear process rather than the `system -c insmod` process. Manager registration, Launcher opening, stock LVX row rendering, and opening/closing transitions are device-proven with collision-checked system-range app ID `0x00CA`. |
| Manager installed notification | **DEVICE PASS.** After registry lookup succeeds, inserts title `Canopus`, body `Canpous Loaded! Just ENJOY~`, and uses `/data/canopus/manager_loaded.png` for both notification image paths. The watchface packages the original GIF's cleaned first frame as PNG bytes in `manager_loaded.bin`, then restores the `.png` suffix while staging them before sending INSTALL. |
| Loading external Canopus modules via stock modlib | **Pending (G0 for arbitrary target modules).** The native Manager probe itself uses the already-proven zero-import stock `insmod` path. |
| Native Manager UI/backend | **HOST + TARGET BUILD PASS; DEVICE RETEST PENDING.** The backend renders the page title and description text as centered stock labels, reproduces the recovered stock vertical `align_to` chain, keeps informational rows free of the forward-arrow affordance, uses the stock trailing switch for Safe mode, and maintains separate reusable status/action/switch pools. Navigation between Overview / Modules / module detail is real firmware page transitions via the recovered `page_goto`/`page_finish` ABI (`EVID-NAV-001`), with the system back gesture as the natural pop path. |
| Package staging + signature verify | **Pending for arbitrary third-party packages.** Manager bootstrap no longer depends on the old staged INSTALL command. |

**What you can test on device now:** LOAD brings up the stable supervisor;
INSTALL stages the first-frame PNG and sends the Manager bootstrap over
`/dev/canopus`. The device write callback remains in the miwear process and
registers the app/page, adds the Launcher record, and queues the installation
notification. Open it from Launcher to exercise the wearable Overview, Modules,
module detail, and confirmation flows rendered with stock `lvx_list_item` rows.
The Manager obtains safe-mode/module state through CPC2 `QUERY_DEVICE` and
`QUERY_MODULE` responses rather than reading supervisor internals. External
package load/update/remove remains fail-closed until the loader gate is closed.

## Structure

```text
watchfaces/canopus-installer/
├── main.lua                          Lua LVGL bootstrap/recovery page
├── canopus_supervisor.bin            built supervisor + native Manager code
├── manager_loaded.bin                first-frame PNG with packager-safe suffix
└── build/                            build output (gitignored)
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
bash scripts/build_canopus_supervisor.sh
# Optional: verify the exact-target Manager object in isolation.
bash targets/xiaomi-band-10-pro-3.101.030/probe/native-manager/build.sh
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
2. Press **LOAD** once to load the supervisor. Never load it twice or use
   `rmmod`.
3. Press **INSTALL** once. The watchface validates and copies the PNG bytes from
   bundled `manager_loaded.bin` to `/data/canopus/manager_loaded.png`, then writes
   the bootstrap INSTALL command to `/dev/canopus`. This keeps the recovered UI
   registration chain in the miwear process and its valid libuv descriptor table.
4. Expected device effects are: app registry entry `0x00CA`, Launcher item
   **Canopus Manager**, and a native Overview that opens with the stock system
   transition. **Canopus** is rendered as a centered plain-text title with a
   description line below it; informational rows such as **Build** have no
   forward arrow; navigable rows retain the forward affordance; **Safe mode** is
   a stock switch that toggles directly (flipping it requests safe mode on the
   next boot and the switch renders checked+disabled once active). Installation
   also sends a system notification titled **Canopus** with body
   **Canpous Loaded! Just ENJOY~** and the supplied static PNG.
5. Open Manager from Launcher. Verify the Overview → Modules → module detail
   transitions are real firmware page pushes with stock animations, that the
   system back gesture pops each page, and that the `/dev/canopus` fd survives
   page switches. Enter and cancel a destructive-operation confirmation, then
   close/reopen it to exercise create/resume/pause/destroy. Preserve the Band
   log after any crash.
6. Reboot before retrying INSTALL. Do not unload the probe during this first
   lifecycle test; reboot is the reliable cleanup path.
7. REFRESH/QUERY/SAFE MODE continue to exercise `/dev/canopus`; arbitrary
   third-party package install/update/remove remains behind its separate
   package/modlib gate.

## Safety

- Boot-resident supervisor/Manager artifact: never `rmmod`, never insert a
  second copy; reboot between destructive tests.
- Crash probe 1 proved that `app_install` cannot run from the `system -c insmod`
  process: its eventbus path reaches libuv `async.c:213`, where the miwear loop's
  process-local fd is invalid. INSTALL therefore enters through `/dev/canopus`
  from the watchface and executes in miwear context.
- Arbitrary package operations remain fail-closed until their independent
  modlib/signature path is implemented.
