# Device gate harness (G0-G13) — CAN-REL-003 / MIG-001..005

Every Canopus device migration must pass gates in order. **Never continue to a
higher-risk gate after a lower one fails** (architecture §20.3). Each gate is
one command against the target device; the result is archived per §20.4.

Host-verifiable parts (build/package/verifier) run automatically in CI
(`scripts/ci.sh`); the device-only steps below need a physical unit.

## Gate list

| Gate | Name | Device step | Pass = |
|---|---|---|---|
| G0 | zero-import load/unload | load the hello/no-heap module ELF, unload it | verifier PASS (0 undefined) + rmmod clean, no crash |
| G1 | wrong-firmware rejection | load a module built for a different firmware sha | activate refuses (identity guard fails closed), no partial init |
| G2 | status/control ABI | send QUERY_MODULE / QUERY_DEVICE, read status | versioned records parse, sequence snapshot even |
| G3 | clock/read-only capability | call `clock_gettime` via the generated veneer | returns sane monotonic time, no fault |
| G4 | character-device lifecycle | register/unregister a char device via the fake-target driver namespace | EBUSY while held, clean unregister |
| G5 | removable drain/unload | disable a removable module, drain, unload | state machine reaches UNLOADED, no retained callbacks |
| G6 | resident barrier | attempt to unload a boot-resident module | no unload path exists; next-boot disable only |
| G7 | callback/timer/worker ownership | register a timer, deactivate, fire | stale generation makes the fire a no-op |
| G8 | native app/launcher register/launch/close/unregister | `app_launcher_add(descriptor)` then `app_launcher_del(appid)` | launcher shows/hides the app, no retained leak |
| G9 | subsystem registration | register a service/block driver | appears in the subsystem, clean teardown |
| G10 | protocol functionality | install/update/rollback over the control channel | result states ACCEPTED→…→COMPLETED only |
| G11 | data plane | bulk package transfer | byte-exact artifact hash on device |
| G12 | reboot disable/update/rollback | stage next-boot change, reboot | correct slot active after boot |
| G13 | safe mode / quarantine | enter safe mode, fail-boot a resident module | minimal recovery boot, quarantine list honored |

## Gates added with the 2026-10-20 production-readiness work

Not scripted in `scripts/device-gates.sh` yet, and none has been run on a device.
Run them after G0–G13 pass.

| Gate | Name | Device step | Pass = |
|---|---|---|---|
| G14 | module application entry | a module registers its UI and calls `ctx->launcher->register_app` with no native pages; watch the launcher | the returned app ID stays bound to the module; one icon appears, opens the page, and back returns; repeated registration adds no icon; 100 open/close cycles leave heap and callback counts flat |
| G15 | launcher entry after the module stops | disable the module (next boot), keep using the launcher until reboot | choosing the entry opens nothing and does not crash; after reboot the entry is gone until the module registers again |
| G16 | crash-loop quarantine | enable a module whose constructor faults | after the second reset the third boot does not load it and the registry shows `UNSTABLE`; re-enabling counts again |
| G17 | update rollback by crash | install a newer generation that faults at start | two resets later the older generation runs (`UPDATE_FALLBACK`) |
| G18 | pinned start (`.043`, ADR-CAN-012) | Run once, pin from the AstroBox Canopus page, reboot with a stock watchface, wake the screen once and let it sleep | `/dev/canopus` appears without opening the installer, the Manager is in the launcher, `boot.pending` is gone; unpin restores the startup script; interrupt the start twice and the third boot starts without Canopus |
| G19 | background timer | create a background timer, turn the screen off, hold a wake lock | the callback keeps firing at its period; without the lock note how far the CPU sleep stretches it |
| G20 | resident size | load the Supervisor on each target | loads without `-403` (NOMEM) on `.043`, `.155` and `.179`; record the free heap before and after |
| G21 | `.179` identity and port | on Band 11 4.100.179 run the installer, then the Manager and one notification | the installer accepts only `4.100.179` + `user-4.100.179-cn-202609302100`; Run registers the Manager; no fault on notification insert/dismiss (the `.179` pack is a static port of `.155`) |
| G22 | new static targets bootstrap | for `xiaomi-band-9-3.1.32`, `xiaomi-band-9-pro-3.1.190`, `xiaomi-band-10-3.1.28`, `redmi-watch-6-3.100.043` build the Band-11-style stage1/stage2 + installer, then Run on the exact image | the identity guard rejects every other version; Run registers the Manager; a module installs, enables and survives a reboot; rollback/remove on reboot works. Records are currently static-only, so no loader, installer or device run exists yet |

## Host-side gate harness

The host gates are automated by `scripts/ci.sh` (G0 build/verifier is covered
by the C + Rust module cross-builds). Device gates use:

```sh
scripts/device-gates.sh <gate> <device-addr>   # run one gate
scripts/device-gates.sh all  <device-addr>     # run 0..13 in order
```

Each device gate appends to `tests/hardware/runs/<date>/gate-<N>.json`:

```json
{ "gate": 5, "name": "removable drain/unload",
  "identity": "…", "pack_revision": 1, "module_sha256": "…",
  "status_records": "…", "result": "PASS|FAIL", "recovery": "…",
  "reviewer": "…", "last_successful_gate": 4 }
```

## Migration order (MIG-001..005)

1. MIG-001 harmless probe → G0-G4
2. MIG-002 removable control module → G5
3. MIG-003 resident lifecycle skeleton → G6-G7
4. MIG-004 AVDTP/SBC reference module → G9-G11
5. MIG-005 real-time pacing → separate acceptance (§24.6)

## Evidence

Every gate run must be saved per architecture §20.4 before the next gate.
