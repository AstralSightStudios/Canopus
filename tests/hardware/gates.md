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
