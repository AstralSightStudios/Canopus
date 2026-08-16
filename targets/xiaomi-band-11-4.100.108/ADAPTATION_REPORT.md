# xiaomi-band-11-4.100.108 — Target Adaptation Report

> Status: **inventory synchronized; static adaptation in progress** (see Claims). Device proof pending.
> Generated per `docs/target-authoring.md` §18.

## Target

- target_id: `xiaomi-band-11-4.100.108`
- model/board: Xiaomi Band 11 (`miwear.watch.q66cn`, board revision `q66cn`)
- firmware version/build: `4.100.108` / `user-4.100.108-cn-202607230300`
- firmware SHA-256: `9315ca353f624cec25dfcfc98a95ba959e2d7b24573bf1d6adf16ea10341bd99`
- IDB/binary source: `/Volumes/EXT0/992e3cbba9cb4b379f7f7c9a816abc3b_upd_miwear.watch.q66cn/vela_ap.bin.i64`

## Target classification

- same-model firmware / cross-model: **cross-model from 3.101.036** (new Band 11)
- UI family: **LVGL v9** (confirmed: `lv_image_*` API, `lv_timer.c`, `lv_obj_*` v9 sources)
- Bluetooth/runtime family: BES1503 / NuttX-derived (same family as Band 10)
- loader: stock modlib (`nuttx-modlib-elf32-rel`), verified family-compatible

## Evidence added

- complete synchronized inventory: **137 symbol records**, matching every 036 function/global/string name
  - 50 exact-IDB address mappings (48 functions + 2 identity strings), all restricted/PENDING
  - 87 inventory-only placeholders with address withheld and `FORBIDDEN` fail-closed policy
  - the two previously ambiguous dispatch mappings were removed rather than guessed
- 8 evidence bundles: EVID-ID-001, EVID-NUTTX-VFS-001, EVID-NUTTX-SEM-001,
  EVID-UI-LVGL-001, EVID-APP-001, EVID-BT-001, EVID-BT-SERVICE-001,
  and systematic mapping bundle EVID-1108-SYNC-001
- mapped records retain their reviewed prototypes/notes; unresolved records explicitly carry no callable address
- unproven assumptions recorded in each evidence bundle

## Generated artifacts

- C veneer: `generated/canopus_veneer.h` (identity guard + no public callables —
  all restricted/PENDING, fail-closed)
- C target config: `generated/canopus_target_config.h`
- Rust bindings: `sdk/rust/canopus-target-generated/src/generated_1108.rs`
  (restricted callable constants for the 48 mapped functions + identity guard)
- target-private facade: `sdk/rust/canopus-target-private/src/targets/xiaomi_band_11_4_100_108.rs`
  (fail-closed, exposes only restricted callables + identity guard)

## Loader

- selected path: stock NuttX modlib (same BES1503 family as Band 10 Pro)
- relocations: R_ARM_ABS32/REL32/TARGET1/PREL31/THM_MOVW_MOVT/THM_CALL/THM_JUMP24
- heap/MPU/cache ownership: not yet recovered on this exact firmware (Band 9's
  tracked MPU ownership is NOT inherited per target-authoring.md)
- bootstrap resources: none yet (stock insmod assumed, must be device-confirmed)
- failure cleanup: next-boot lifecycle (no hot unload, per Canopus policy)

## Validation

- schema: `canopus target validate` PASS
- symbol/evidence records: synchronized inventory validates; 87 unresolved placeholders remain intentionally fail-closed
- generated stability: `cargo test -p canopus-core --test generated_stability` PASS
- Rust features: 11-108 feature compiles+tests in both generated and private crates
- full workspace tests: `cargo test --workspace` PASS (including matcher ground truth)
- full CI: `./scripts/ci.sh` PASS (all 7 gates)
- multi-layer matcher ground truth (036→030): **78.9% best-candidate recall,
  71 confirmed at 100% precision, 0 duplicate claims**
- matcher cross-model (036→11-108): only semantically confirmed mappings are retained; ambiguous candidates remain withheld

## Remaining limits

- fail-closed capabilities: every function symbol is restricted/PENDING; only
  the identity guard is callable today
- device proof pending: no device has run any of these addresses
- identity guard note: 11-108 stores version in a `ro.*` property block with
  `key=value\n` formatting; the guard compares against the bare version and
  currently fails closed — target-private must match the property format
- loader device confirmation pending (stock insmod not yet executed)
- launcher entry, /dev/canopus registration, module lifecycle not yet adapted

## Claims

- STATIC_RECOVERED/PENDING: 48 functions + 2 identity strings with exact-IDB addresses
- inventory synchronized: every one of the 137 036 names is represented; 87 entries have no 11-108 address and are `FORBIDDEN`
- HOST/BUILD VERIFIED: generated stability, full workspace tests, SDK features, and full CI
- DEVICE_PROBED: **no**
- DEVICE_PROVEN: **no**
