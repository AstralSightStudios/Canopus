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
| Supervisor core (`manager/service/canopus_supervisor.c`) | Host-tested: command/status ABI, lifecycle-aware enable/disable/remove, persistence/restore, automatic activation, and staged native-app publication. |
| `canopus_supervisor.bin` | **Verifier PASS** (sha256 `91af9061`, 0 undefined, 2 ctors/1 dtor) — a valid zero-import ELF32 ET_REL containing the supervisor, CPC2 snapshot query path, persistence diagnostics, native message-box confirmation, external descriptor registration, automatic enabled-module boot activation with retained callback errors, ABI 1.2 staged miwear native-app publication, and resident exact-target Manager implementation. |
| Device registration (`/dev/canopus`) | **Implemented.** `canopus_supervisor_platform.c` registers the device via the stock `register_driver` (0x0C1A0D51) exactly like btpatch registers `/dev/btpatch` — the same managed symbol the veneer exposes as `canopus_fw_register_driver`. The read side renders the status ABI, the write side dispatches commands. |
| Native Manager registration path | **MANAGER DEVICE PASS; ABI 1.2 STAGED MODULE PUBLICATION DEVICE RETEST PENDING.** Manager registration, Launcher opening, stock LVX row rendering, and opening/closing transitions are device-proven with app ID `0x00CA`. The former two-transaction flow still crashed during BluetoothAudio publication: miwear faulted in an unQLite allocation after `app_install` and immediate `launcher_add`. ABI 1.2 now separates module publication into stage 1 app/page registration and stage 2 Launcher publication on distinct button callbacks; BluetoothAudio uses app ID `0x00CB`. |
| Manager installed notification | **DEVICE PASS (icon format RETEST PENDING).** After registry lookup succeeds, inserts title `Canopus`, body `Canpous Loaded! Just ENJOY~`, and uses `/data/canopus/manager_icon.bin` for both notification image paths and as the app's Launcher icon. The watchface packages the icon as an LVGL v9 ARGB8888 bin (alpha channel preserved) in `manager_icon.bin` and stages it byte-for-byte before sending INSTALL. The previous `manager_loaded.png` first-frame PNG flow is retained only as an unused legacy resource. |
| Loading external Canopus modules via stock modlib | **HOST + TARGET BUILD PASS; DEVICE RETEST PENDING.** Enabled artifacts self-register their ABI descriptor during `insmod`; the supervisor requires an exact slot/id/target match, activates READY descriptors automatically during reboot restore, and persists callback success/error for later Manager queries. Ordinary `/dev/canopus` traffic never invokes third-party activation, and Manager no longer exposes a manual Activate action. |
| Native Manager UI/backend | **HOST + TARGET BUILD PASS; DEVICE RETEST PENDING.** Navigation between Overview / Modules / module detail uses the recovered `page_goto`/`page_finish` ABI (`EVID-NAV-001`). Confirmations use Xiaomi's page-owned `lvx_page_msgbox` two-button prefab (`EVID-MSGBOX-001`) and retain the semantic confirmation page as a constructor-failure fallback. Registry failures expose the exact transaction stage, NuttX errno, and verified-save count in Manager. |
| Package staging + signature verify | **Pending for arbitrary third-party packages.** Manager bootstrap no longer depends on the old staged INSTALL command. |

**What you can test on device now:** LOAD brings up the supervisor. Press INSTALL
once to stage the icon bin and register Manager. After its event has returned to
miwear, press INSTALL a second time to register loaded ABI 1.2 module apps and
pages without touching Launcher persistence. Press INSTALL a third time to add
their Launcher entries in another event-loop transaction. Do not skip or combine
these stages. The second and third module-publication stages remain device gates;
preserve the log and reboot rather than retrying if either fails. Manager obtains
safe-mode/module state through CPC2 `QUERY_DEVICE` and `QUERY_MODULE` responses.
External package load/update/remove remains fail-closed until the loader gate is
closed.

## Structure

```text
watchfaces/canopus-installer/
├── main.lua                          Lua LVGL bootstrap/recovery page
├── canopus_supervisor.bin            built supervisor + native Manager code
├── manager_icon.bin                  LVGL v9 ARGB8888 icon bin (alpha preserved)
├── manager_loaded.bin                unused legacy first-frame PNG resource
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
| 28 | 4 | persistence diagnostics: stage bits 0..7, NuttX errno bits 8..23, verified-save count bits 24..31 |
| 32 | 4 | error code |
| 36..39 | 4 | snapshot sequence begin |
| 40..43 | 4 | snapshot sequence end |
| 44..47 | 4 | first non-zero loaded-module callback error (signed) |
| 48..127 | 80 | reserved |
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
3. Press **INSTALL** once. The watchface validates and copies the LVGL v9 icon bin
   from bundled `manager_icon.bin` to `/data/canopus/manager_icon.bin`, then writes
   stage 0 to `/dev/canopus`. This registers Manager in the miwear process.
4. Press **INSTALL** a second time. Stage 1 invokes loaded ABI 1.2 modules only to
   register their app/page descriptors, then returns so miwear can process the
   app-registry event.
5. Press **INSTALL** a third time. Stage 2 invokes those modules only to publish
   their Launcher entries. For BluetoothAudio this is the first stage that calls
   `launcher_add`; if it crashes, the boundary is Launcher/unQLite rather than
   `app_install` or page registration.
6. Expected Manager effects are app registry entry `0x00CA`, Launcher item
   **Canopus Manager**, and a native Overview that opens with the stock system
   transition. **Canopus** is rendered as a centered plain-text title with a
   description line below it; informational rows such as **Build** have no
   forward arrow; navigable rows retain the forward affordance; **Safe mode** is
   a stock switch that toggles directly (flipping it requests safe mode on the
   next boot and the switch renders checked+disabled once active). Installation
   also sends a system notification titled **Canopus** with body
   **Canpous Loaded! Just ENJOY~** and the supplied static PNG.
7. Open Manager from Launcher. Verify the Overview → Modules → module detail
   transitions are real firmware page pushes with stock animations, that the
   system back gesture pops each page, and that the `/dev/canopus` fd survives
   page switches. Enter and cancel a destructive-operation confirmation, then
   close/reopen it to exercise create/resume/pause/destroy. Preserve the Band
   log after any crash.
8. Reboot before retrying INSTALL. Do not unload the probe during this first
   lifecycle test; reboot is the reliable cleanup path.
9. REFRESH/QUERY/SAFE MODE continue to exercise `/dev/canopus`; arbitrary
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
