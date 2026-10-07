# Restricted calendar Alpha API approval

Target pack revision: **2**. Exact AP SHA256:
`ff74c6f467529963b6afd40669437d8ec39a02e48c10d0893739847b3ba3dc2b`.

The user explicitly approved the new calendar native APIs in the current coding
session and requested the Canopus pack update. Fifteen previously missing exact
function records under `symbols/*.private_abi.calendar_*.json` now contain
`approval_state: APPROVED` and the human `promotion` record. Evidence is
`evidence/EVID-RESOURCE-CALENDAR-ALPHA-177-001.json`, including full-function
boundaries/AP-byte hashes, prototypes, ownership, thread constraints and limits.

This is **private static ABI approval**, not public SDK or device acceptance:

- All fifteen records remain `policy: restricted`, `status: STATIC_RECOVERED`,
  and `proof.device: not_probed`.
- Public C/Rust wrappers remain unavailable; errno is still the only public
  approved callable for this target. Exact private Rust constants are generated.
- Firmware address ranges, capabilities, loader behavior, verifier logic and
  unrelated symbol approval states are unchanged. Other firmware targets are
  not approved by this update.
- Existing same-address records are reused rather than duplicated. The
  pre-existing pending background-event alias is assigned the unique generated
  name `background_object_event_add`; its address/prototype/proof/approval stay
  unchanged. This prevents duplicate declaration of the canonical
  `CANOPUS_FW_LV_OBJ_ADD_EVENT_CB_CALLABLE` during Rust regeneration.

## Operational constraints

All graphics/font/snapshot work is serialized on the native UI owner outside
IRQ locks. Realtime returns previous BASEPRI, not success status. Default fonts
are borrowed; custom managed outer/fallback wrappers are owned but may share
native cached descriptors/faces. Native manager creation can resize cached FT
faces internally. No direct shared-wrapper/FT mutation is authorized.

A non-NULL snapshot does not prove GPU completion. Require a successful healthy
GPU finish and explicit CPU-cache handling before reading/releasing pixels, tree
or custom fonts. `calendar_draw_buf_invalidate` dispatches an optional handler;
it is not a guaranteed fence or synchronization primitive. Failed GPU fences
retain a bounded pending generation. GPU fault recovery, framework restart or
unload with pending resources, persistent-storage durability and physical pixel
identity remain unsupported/unproven. No signing or installation was requested.

## User-reported device result

On 2026-10-07 the user reported successful device validation of the current .177
transparent/configurable calendar workflow and requested commits. Record this
as **USER_REPORTED_PASS**, separately from exact private API approval. No installed
artifact hash, logs, screenshots, configuration matrix or fault-injection results
were supplied. Individual symbol statuses remain STATIC_RECOVERED/not_probed;
this workflow report does not establish DEVICE_PROVEN, pixel-byte identity,
GPU/storage fault handling, restart safety or other-target acceptance. No signing
or installation was performed by the assistant in this session.

## Validation

```sh
cargo test -p canopus-core --test generated_stability band11_
cargo test -p canopus-core --test codegen_gate --test fixture_validation --test sdk_hygiene
cargo test -p canopus-elf
(cd sdk/rust && cargo check -p canopus-target-generated --no-default-features \
  --features target-xiaomi-band-11-4-100-177)

# From the adjacent Canopus-Module-Resource-Hook repository:
python3 tests/test_calendar_allowlist.py
CC=/usr/bin/clang sh scripts/build.sh xiaomi-band-11-4.100.177
```

The allowlist test validates schema/promotion/AP receipts and private/public
separation. Negative controls remove one exact record in a temporary copied
pack and verify a .177 probe against .155: both are rejected by the unchanged
verifier. `.177` unsigned module build passes strict ELF verification with zero
undefined symbols. Native calendar/adapter probes are host/Unicorn evidence,
not hardware approval.

Full root-workspace tests additionally expose existing `.036` generated C/Rust
staleness around `poll_notify`; those unrelated target files are not changed by
this .177 approval. Scoped .177 generation stability and SDK compilation pass.
