# .139 installer revalidation — 2026-09-10

The current bundle remains **STATIC_TEST_CANDIDATE / NOT_PROBED**. This pass
replaced cache-port writes through `mw` with verified fixed firmware entries,
removing unverified reads of cache command ports. It also added executable checks
for firmware paths previously represented by mocks, rebuilt the complete
installer, and passed the host gates. It does not establish a device
success rate or guarantee a crash-free first installation.

Exact raw image SHA-256:
`31ce82257f7c127950dc5070b86316730cf468a41f0d004559e41e7d923b2c74`.
The new tests check this hash before executing firmware bytes. The raw image,
IDB-derived corpus and startup-copy mappings were read without modifying the IDB.

## Newly executed firmware paths

Reproducer: `build/band11-tests/bin/python scripts/tests/band11_firmware_integration.py -v`.
All five tests pass. They are also invoked by the installer regression suite.

| Path | Exact firmware evidence | Result and scope |
| --- | --- | --- |
| `system()` command creation | `0x0c370774`, spawn-attribute initialization `0x0c3496e8` | Actual instructions set a 4096-byte command stack and pass `-c`, command, NULL; `system()` waits for the spawned task. Scheduler queries, spawn and wait are modeled. |
| Task privilege | `task_init 0x0c357e30` → `task_setup 0x0c3557f0`, call at `0x0c355990` → `up_initial_state 0x0c329ee8` | Actual initial-state construction clears `CONTROL.nPRIV` at `0x0c329f68` and stores it at saved-frame +0x28. Caller CONTROL 0, 2 and 3 are tested. Saved EXC_RETURN is `0xffffffed`, and stack limit/start-PC fields are checked. |
| `mw` and prior cache range approach | `cmd_mw 0x0c372834`; relocated clean `0x00275814` and invalidate `0x002757bc` | Actual argument parsing, word access and output show that `mw address 4` reads one word. A write performs read → write → read. The prior Lua line addresses agree with HAL for four TString alignments, but extra command-port reads are not justified by that agreement. |
| New fixed cache entries | D-cache leaf `0x0c93021a`; I-cache leaf `0x0c0c181e`; barrier `0x0c91e542` | Executed through the real NSH dispatcher and exec, with printf returning 17 and clobbering caller-saved registers. They independently establish their inputs. With caches enabled, only D+0x34 and I+0x38 are written, each with 1; cache configuration, MPU CTRL/bitmap, callee-saved registers and stack remain unchanged. Physical peripheral effects are modeled. |
| Manager registration | `app_install 0x0c6ab350`, app lookup `0x0c6ac54c`, page register `0x0c6967fc`, page lookup `0x0c695a44`, Launcher add `0x0c547b54`, record generation `0x0c547a28` | Actual firmware copies the 80-byte app, initializes and links the three writable pages, finds each composite page key, and constructs the 28-byte Launcher record. Native callback addresses survive initialization. Record name is `Canopus 管理器`, package `com.canopus.manager`, icon `/data/canopus/manager_icon.bin`; the firmware reaches the app-list notification call. |

The privilege evidence is no longer just a manually chosen emulator setting.
Static inspection additionally confirms that spawn `0x0c8bbdac` initializes
the ordinary task flags to `0x8000` and calls `task_init`, and that the exception
restore tail loads the saved CONTROL word and executes `msr control, ip` at
`0x0c919558`. Full scheduling and exception-return timing are not emulated.

The Manager test still models allocation/duplication, hash-map creation and
insertion, notifications, Launcher list storage/persistence and logging. It
does not render the actual hardware display or prove that an asynchronous
Launcher notification is processed. App/page copies, field offsets, list
linking, native signal callback, name callback and record construction execute
their real ARM instructions.

## Rebuild and regression results

- `CANOPUS_TARGET=xiaomi-band-11-4.100.139 scripts/build_canopus_supervisor.sh`
  passes: 77,984-byte Supervisor, 80 ELF sections, zero undefined symbols,
  1,092 relocations, two constructors and one destructor.
- `scripts/ci.sh` passes all gates, including both Rust workspaces, schema and
  generated-file checks, C host tests, portable Thumb builds, example module
  verification, Lua tests and installer tests. Log:
  `build/band11-tests/revalidation-ci.log`.
- The existing five ARM bootstrap tests and their sixteen injected failure
  cases pass. MPU boot/configuration/save/restore, two PIC placements, register
  preservation, cleanup and Manager lifecycle remain covered. Measured native
  plus NSH-dispatcher stack is 1,308 bytes and simulated peak Kmem is 101,408
  bytes; allocator/VFS internals and earlier command-parser frames are excluded.
- Both existing raw-firmware `pmain` tests pass. The real C-frame fixture also
  passes for production and freshly rendered Lua under **Lua 5.4.0**, matching
  the firmware's Lua version, as well as the host's Lua 5.5 in CI.
- Generated Lua and all six ZIP members match their resource-root files. The
  resource root contains exactly **one `main.lua` and five `.bin` files**.
  Documentation, tests and intermediates remain in subdirectories or elsewhere
  in the repository. Native binaries are byte-identical to the prior startup
  fix; Lua and the profile were updated for the cache-entry change.

## Identifying the tested bundle

Pack `watchfaces/canopus-installer-prod/xiaomi-band-11/`, or use its archive at
`build/xiaomi-band-11-4.100.139/canopus-installer-prod-xiaomi-band-11-4.100.139.zip`.
The latter path is relative to that resource directory. The adjacent
`manifest.json` records every resource's size and full SHA-256.

| File | Bytes | SHA-256 |
| --- | ---: | --- |
| `main.lua` | 28587 | `a4247eff1d15d74f74dddccfae0f135e3ff3fc4dd7424fcf987071bd6fd766e0` |
| `canopus_loader_profile-xiaomi-band-11-4.100.139.bin` | 910 | `1ad9199450b804a6cad4e71e8dc23c5793c2a314bdd9f564ab23e340ee2c40a4` |
| `canopus_stage1-xiaomi-band-11-4.100.139.bin` | 853 | `b0d3c0bd1eaa91f298fe2a8ac999c1a2d4fc3737ef9da536993ed001f97a5096` |
| `canopus_stage2-xiaomi-band-11-4.100.139.bin` | 4116 | `af6a16ece4557d26ccb5b09d3a14fd6246ca944b2aa88124af24db472f8d1911` |
| `canopus_supervisor-xiaomi-band-11-4.100.139.bin` | 77984 | `4f304b8fbea03288102471a9c3732c96749153126940a0b76f6e432598c477d6` |

## What still requires the device

Physical PSRAM data/instruction alias coherence and cache-maintenance completion
are not established by Unicorn. The new sequence removes the extra command-port
reads: D clean-all → DSB/ISB → enabled-I clean/invalidate-all → DSB/ISB. Only `mw`
to ordinary owned payload memory remains writable from Lua. Firmware instruction
fingerprints cover both cache leaves, their branches and literal addresses.
Cache enable bits are checked before calling them; on this enabled branch the
I-cache helper does not change its enable register. Lua fault tests cover disabled
cache, failure of either helper, and failure of either barrier, all stopping before
entry into stage1. Whole-cache maintenance replaces the old per-line operations;
it does not discard dirty D-cache data or change the MPU mapping.

Actual free/fragmented heap capacity, the complete NSH/VFS stack high-water
mark, event-loop timing and physical Launcher/UI behavior also require device
evidence. The existing once-per-state `pmain` path-allocation leak and the
resident lifetime after constructors remain as documented in the startup fix.

For a clean test, reboot the exact .139 stock firmware, install the complete
tested resource set, open the installer and press Run once. This avoids stale
Lua recovery state or MPU leases left by an earlier attempt. On normal return,
`/data/canopus/bootstrap-result.txt` and `bootstrap-exec.txt` retain native result
and command output. A fault before return may leave no completed result file;
absence of that file is not proof of success.
