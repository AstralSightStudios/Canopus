# Xiaomi Band 11 4.100.177

Independent exact-target static port from `.155`; neither `.139` nor `.155` is overwritten.

- AP SHA-256: `ff74c6f467529963b6afd40669437d8ec39a02e48c10d0893739847b3ba3dc2b`
- Build: `user-4.100.177-cn-202609241530`
- Version token: `0x0CA07FB6`; build token: `0x0CA0802E`. Both terminate with LF.
- Exact IDB input SHA verified. Extraction recovered 35,143 functions and 16,109 referenced data objects (the database contains 35,145 functions).
- Device status: **NOT_PROBED**. Native loader status: **STATIC_TEST_CANDIDATE**.

## Evidence and scope

The input security audit is `mifw/analysis-q66-177/README.md` in the adjacent development tree. Its selected Lua/NSH entries alone were not treated as a complete loader profile. Both exact databases were independently extracted, raw-image entry bytes checked, and complete Thumb bodies fingerprinted using `thumb-full-v1`. There were 26,762 mutually unique full-body seeds. Matching is candidate retrieval, not automatic ABI approval.

This port retains 157 `STATIC_RECOVERED`, 52 `CANDIDATE`, and one `FORBIDDEN` symbol from the source pack. No uncertain source symbol was promoted. The changed `pthread_create_internal` body remains a candidate. Public static callable approval is limited to `errno_location`, independently checked in the exact `.177` open failure path. Private runtime bindings remain restricted with device validation pending.

Independent exact-target IDB review covered:

- Lua `pmain`, error handler, original `io.open`, library registration and reentry ordering.
- App lookup/install and launcher descriptor ownership; page lifecycle callback slots.
- Driver registration, VFS dispatch and same-model `file_operations` shape.
- Heap initializer and allocation/free consumers: Kmem slot `0x200B0070`, Umem slot `0x200B2330`, Umem descriptor `0x3C356B40`.
- MPU bitmap initializer/consumers at `0x200F4080`; cache-controller leaf instructions and literal pools.
- Notification normalization, cloning, event broker and consumer. The consumer still unconditionally reads `message+92 -> context+13`, so the corrected ordinary-notification context remains required.
- Interior UI/font globals through actual instruction xrefs, including relocated style objects. No global address delta was applied.

See [port manifest](../evidence/fw-match/port-155-to-177.json), [evidence](../evidence/EVID-PORT-4177-001.json), and the critical pseudocode/consumer/interior-global exports in the same evidence directory. The separately preserved ensemble report is unapproved matcher output. Emulation PC/data mappings are test hooks, not callable approval.

## Build

```sh
cargo build -p canopus-cli
target/debug/canopus target generate-veneer xiaomi-band-11-4.100.177 --targets-dir targets
target/debug/canopus target generate-rust-bindings xiaomi-band-11-4.100.177 \
  --targets-dir targets --output sdk/rust/canopus-target-generated/src/generated_1177.rs
CANOPUS_TARGET=xiaomi-band-11-4.100.177 scripts/build_canopus_supervisor.sh
python3 scripts/build_band11_installer.py
```

The installer now selects `.139`, `.155`, or `.177` using version **and** build identity before recovery. Each target carries its own stage1, stage2, loader profile and Supervisor. All three share address-free C/Rust semantics and one Lua entry. The unified resource ZIP is:

`watchfaces/canopus-installer-prod/xiaomi-band-11/build/canopus-installer-prod-xiaomi-band-11.zip`

Use an exact Lua 5.4.0 compiler for the production wrapper, as documented in the installer README. Firmware files and generated binary resources are local build inputs/artifacts and are not included in the target evidence pack.

## Validation

The `.177` Supervisor passes ARM ELF verification: 80 sections, zero undefined symbols, 1,144 relocations, two constructors, one destructor. Its tested build is 81,300 bytes.

Run the exact-image emulation suite with a Python environment containing Unicorn 2.1.4:

```sh
python scripts/tests/band11_177_firmware.py
```

Observed result: seven passed, one skipped. Covered two PIC rebases, wrong-identity rejection before registration, exact bitmap selection, Manager registration, native row/theme instructions, notification insertion/reminder consumption, and reproduction of the old null-context crash. The signed-module/receipt test was skipped because no `.177` signed module was locally staged. `.155` regression suite also passed (six passed, one skipped).

This is **not hardware validation**: allocation/VFS/task/display services are partly modeled; physical cache coherence, live MPU state, RF/audio services, and device reboot behavior remain untested. External Bluetooth Audio/Lyra repositories were not ported or packaged in this change. Do not claim their `.177` modules or device playback are verified.
