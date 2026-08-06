# Firmware function signatures

Canopus keeps address records exact-target, but also records byte signatures so
a tracked function can be located when a later firmware moves it.

The current catalog is:

- `targets/xiaomi-band-10-pro-3.101.030/function-signatures.json`
- 51 function records with recovered entry addresses
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
