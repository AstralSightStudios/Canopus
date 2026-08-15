# Firmware function signatures

Canopus keeps address records exact-target, but also records byte signatures so
a tracked function can be located when a later firmware moves it.

The current catalog is:

- `targets/xiaomi-band-10-pro-3.101.030/function-signatures.json`
- 67 function records with recovered entry addresses
- 2 unresolved function records without an entry address
- canonical mapping: raw `vela_ap.bin` at `0x0C0C0000` (`XIP_TEXT_RO`)

## Why matching is scoped

The IDB maps identical application bytes through `XIP_TEXT_RO`, `FLASH_NC`, and
`FLASH_CACHED`. A signature that is unique as code can therefore appear several
times in the IDB. Catalog validation scans only the canonical raw application
image. Cached and non-cached aliases are not independent candidates.

Tiny LVGL veneers are eight-byte Thumb indirect branches. If an eight-byte
veneer is not unique, the extractor includes adjacent veneer-table bytes and
marks the entry with `anchors.tiny_thunk = true`; consumers must still resolve
the returned address to the start of the requested veneer.

## Extraction and validation

```sh
python3 scripts/extract-function-signatures.py \
  --symbols-dir targets/xiaomi-band-10-pro-3.101.030/symbols \
  --firmware /path/to/vela_ap.bin \
  --output targets/xiaomi-band-10-pro-3.101.030/function-signatures.json

python3 scripts/extract-function-signatures.py \
  --catalog targets/xiaomi-band-10-pro-3.101.030/function-signatures.json \
  --firmware /path/to/vela_ap.bin
```

Extraction verifies the firmware SHA-256, requires exactly one match in the
canonical mapping, and requires that match to resolve to the recorded entry
address. The JSON structure is checked by
`schemas/function-signature-catalog.schema.json` in the normal Rust test suite.

## Portability levels

- `exact-target`: exact bytes verified only on the source firmware. It can find
  an unchanged function after an address move, but is not evidence that branch
  operands or literal pools are stable.
- `relocation-masked`: instruction operands known to vary have a reviewed mask.
- `cross-version-confirmed`: the signature was tested against at least two
  distinct firmware hashes.

The generated catalog intentionally starts at `exact-target`. It must not be
promoted merely because a wildcarded pattern happens to be unique in one IDB.
When a second firmware is available, derive relocation masks from instruction
boundaries, validate them against both canonical images, and add string/callee
or xref anchors for short wrappers and veneers.

The two unresolved records (`driver_write_dispatch` and `public_work_queue`)
remain visible in the catalog rather than receiving guessed addresses.

## Multi-layer function matcher (`canopus-fw-match`)

The exact-byte catalog above is a *locator*. For cross-firmware adaptation
(which firmware moves functions, recompiles prologues, and re-bases veneer
tables), the repo ships a multi-layer matcher:

```text
tools/fw-match/
├── extract_corpus.py      standalone idalib dumper -> per-function JSON corpora
├── fw-match CLI           match a source pack's symbols into a target corpus
├── verify_match.py        score matcher output against target-pack ground truth
└── src/
    ├── thumb.rs           Thumb-2 relocation masker (pattern layer)
    ├── score.rs           CFG / string / constant / size / degree layers
    ├── ga.rs              greedy-seeded genetic algorithm over assignments
    └── engine.rs          monotonic anchor rounds (callee/caller xref)
```

Layers:

1. **pattern** — masked entry bytes. The Thumb-2 masker wildcards branch
   offsets, `movw/movt` immediates, literal-pool loads, and the 8-byte
   `5f f8 00 f0` tiny-thunk target, so a rebuild that moves a branch or
   re-bases a veneer does not break the pattern.
2. **cfg** — normalized control-flow shape (relative block layout, successor
   offsets), position-independent under relocation.
3. **xref** — referenced string *content* and small constants; plus caller and
   callee *degree* (a function called by 64 sites is not the one called by 7).
4. **GA** — greedy-seeded population that resolves assignment collisions
   (two source symbols must not claim the same target) while preserving each
   symbol's strongest structural candidate (locked genes).

The engine runs monotonic anchor rounds: decisive matches are frozen and become
anchors, seeding callee/caller overlap that resolves the weaker ties in the next
round. Confirmed matches still require **decompilation in the exact IDB** before
they become target-pack symbols — the matcher ranks, the reviewer confirms.

### Extraction

```sh
# venv from the ida-pro-mcp plugin (has idapro)
.venv/bin/python tools/fw-match/extract_corpus.py \
  --idb <path.i64> --output targets/fw-corpus/<target-id>.json --target-id <target-id>
```

Run only when the MCP worker is not holding the same IDB (idalib opens one
database per process).

### Matching and verification

```sh
cargo run -p canopus-fw-match -- \
  --symbols targets/<source-target>/symbols \
  --source-corpus targets/fw-corpus/<source>.json \
  --target-corpus targets/fw-corpus/<target>.json \
  --output matches.json

python3 tools/fw-match/verify_match.py \
  --matches matches.json \
  --source-symbols targets/<source-target>/symbols \
  --target-symbols targets/<target-target>/symbols
```

`verify_match.py` reports best-candidate recall and confirmed precision against
the target pack's own records for shared semantic names. Ground truth today:
036 → 030 gives ~80% best-candidate recall with 100% precision on confirmed
matches; the confirmed set never contains a wrong address.

### Ground-truth test

`tools/fw-match/tests/ground_truth.rs` runs the 036 → 030 match and asserts
recall ≥ 55% and confirmed precision ≥ 99%. It skips when the corpora are
absent (they are gitignored; regenerate with `extract_corpus.py`).

