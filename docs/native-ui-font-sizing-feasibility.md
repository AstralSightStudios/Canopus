# Native UI font sizing feasibility

Date: 2026-10-23. Evidence ID: `EVID-UI-FONT-FEASIBILITY-001`. Scope: read-only investigation for
`target/tasks/native-ui-font-image-scale.md`. This report concerns font sizing;
image scaling is outside its scope. No font implementation, symbol promotion,
firmware deployment, or new device validation resulted from this investigation.

## Decision and measured budget

Arbitrary native font sizes are technically achievable through the firmware's
managed font factory. General native UI support remains blocked by the available
budget, incomplete per-target bindings and approval scope, and an unresolved
font retirement design. Keep the native UI contract at `font_px = 0/24/28`,
returning `CANOPUS_UI_ERR_UNSUPPORTED` for other sizes until those blockers are
resolved.

The coordinating parent reproduced this clean Band 11 `.155` baseline before
implementation:

| Budget | Measured usage | Limit | Remaining |
|---|---:|---:|---:|
| Supervisor ELF file | 130,696 B | 131,072 B | 376 B |
| Supervisor resident allocation | 182,014 B | 184,320 B | 2,306 B |

These figures are parent-measured baseline values, not measurements from this
read-only investigation and not post-image-scaling results. The resident limit
must not be raised. No candidate font implementation was compiled, so this
report makes **no byte-exact or speculative implementation delta claim**. Any
future implementation must demonstrate its actual file and resident costs after
budget is reclaimed; the existence of firmware helpers does not establish that
safe integration fits the remaining space.

The subsequent image-fit work reclaimed space using UI-backend-only `-Oz` and
finished at 129,736 file / 180,646 resident bytes on `.155`, leaving 1,336 / 3,674
bytes. See [the final verification report](native-ui-image-font-verification.md).
That result does not close the font ownership, retirement or handoff blockers;
no font implementation delta has been measured against that remaining budget.

## Missing task references and service reuse

The task references `docs/canopus-module-font.md` and `sdk/c/canopus_font.h`.
Neither file exists in the inspected checkout. Searches of `sdk/c/`, `runtime/`,
`manager/`, and the Supervisor service group declarations found no font service,
`ctx->font`, or `CANOPUS_SVC_FONT` to reuse. The current host applies borrowed
MiSans-Medium 24/28 theme style objects through `host_label_style`; it does not
own dynamically generated fonts.

The adjacent `Canopus-Module-Resource-Hook` repository contains a checked font
reload adapter and investigation documents referenced by the target evidence.
That adapter is a separate system for changing managed backing fonts while
preserving existing wrappers. It is not a font service already linked into the
Supervisor, and its full reload transaction is not required merely to select a
new size. Its deferred-render ownership findings remain relevant.

## Verified firmware pipeline

The following virtual addresses are even instruction addresses. A direct Thumb
callable address is the listed address OR 1. The `.155` and `.043` factories and
destructors were independently inspected using IDA/Hex-Rays against their
existing exact-target databases. No database annotations were changed.

| Operation | Band 11 `.155` VA | Band 10 Pro `.043` VA | Current evidence and binding status |
|---|---|---|---|
| High-level managed font creation | `0x0c4950b0` | `0x0c8603b0` | `.155`: `calendar_font_create`, restricted/APPROVED for the calendar private port. `.043`: factory recovered in this investigation; no corresponding factory symbol/binding or general UI handoff found. |
| Font pointer applied to a label | `0x0c387eba` | Existing generic local-style property setter is a candidate; dedicated font use not audited here | `.155`: `calendar_text_font`, restricted/APPROVED for the calendar private port. Its body delegates to `0x0c3872b4` with font property `0x5a` (90). |
| High-level managed font destruction | `0x0c49258c` | `0x0c860478` | `.155`: `calendar_font_destroy`, restricted/APPROVED for the same port. `.043`: destroy recovered here; no corresponding high-level destroy symbol/binding or general UI handoff found. |
| Managed wrapper creation | `0x0c494380` | `0x0c85f6cc` | Searches active/idle fonts by family and packed size/style, then obtains or creates backing FreeType state. |
| Managed wrapper release | `0x0c917384` | `0x0c85f8fc` | Decrements backing-record references; final ordinary backing font may move into the native idle cache rather than being destroyed immediately. |
| Family pathname resolution | `0x0c490edc` | Inlined resolution inspected within `0x0c85f6cc` | `.155`: `font_manager_generate_def_path`, restricted/PENDING, `EVID-RESOURCE-4155-001`. Returns borrowed registry storage or the manager's reusable pathname buffer. |

The high-level creation signature is:

```c
void *native_font_create(const char *family, uint16_t pixel_size, uint16_t style);
```

The native factory rejects a zero pixel size, accepts a nonzero 16-bit size,
resolves the family, obtains or creates the FreeType backing descriptor, and
returns an outer managed font wrapper with any configured fallback chain.
Static code supports size selection; this investigation did not execute actual
font parsing or establish rendering correctness for every size.

The minimum conceptual integration is:

```c
font = native_font_create("MiSans-Medium", font_px, style);
/* Reject NULL and the exact target's borrowed static failure font. */
native_label_set_font(label, font, 0);
/* Keep the wrapper alive through all label and pending-render references. */
native_font_destroy(font);
```

The setter stores the pointer without copying or retaining the font. Selector
zero applies the main-part font property. The destructor releases fallback
wrappers and then the outer wrapper through the manager's ownership path.
Default fonts borrowed from theme styles are never caller-owned and must not be
released by this integration.

The high-level factory avoids a direct low-level face construction pipeline.
For example, `.155` `font_reload_face_create` at `0x0c3967b4` is explicitly an
identity/native-callback-only record under `EVID-FONT-4155-001`; it is not a
general callable factory. `font_reload_metrics_create` at `0x0c3a5ef0` is also a
cache callback. `font_reload_ft_set_pixel_size` at `0x0c8b8a54` changes a native
FreeType face; calling it on shared state does not create a new independently
owned label font and is not a substitute for managed creation.

## Static failure-font discrepancy

The `.155` `calendar_font_create` symbol's ownership text identifies the
borrowed static failure font as `0x2ca12930`. Independent inspection of the exact
`.155` factory at `0x0c4950b0` shows that the failure branches return
`&off_2CA14888`, address **`0x2ca14888`**. The exact destructor at `0x0c49258c`
also compares its input against `0x2ca14888` before attempting managed release.

Thus the inherited ownership literal and the inspected target code disagree.
Do not implement fallback detection with `0x2ca12930` without correcting and
reviewing the exact-target evidence. This read-only investigation did not alter
the record. On `.043`, the corresponding factory and destructor use static
failure font **`0x2cce1734`**. These values must not be extrapolated to another
target by a global address delta.

## Ownership and retirement blocker

Safe sizing needs ownership tracking and a release boundary, not just an
additional native setter. At minimum an implementation must provide:

1. A retained owned-font reference for each affected label, or a bounded shared
   cache keyed by family, pixel size, and style with explicit label references.
2. Preparation before publication, failure cleanup, and preservation of the
   previous font until all old label/render references are retired.
3. Balanced ownership on size changes, subtree replacement/removal, failed
   snapshot application, page close, module unload, and native framework
   teardown.
4. A verified serialized UI-owner boundary for retirement, including native
   pending glyph work. A completed setter or a deleted label alone does not
   prove that queued rendering no longer refers to its font wrapper.
5. Exact per-target factory, setter, destroy, and failure-font identities,
   accompanied by the required policy handoff.

`EVID-FONT-4155-001` establishes a concrete deferred-release hazard. Outline
acquisition at `0x0c3a7c44` saves a held glyph entry in the glyph description.
Release at `0x0c3a0992` finds the cache through the supplied font's **current**
descriptor. The GPU pending glyph description retains the font pointer. Changing
or freeing that wrapper/descriptor prematurely can route an old entry to the
wrong cache or leave a dangling wrapper. Native wrapper reference counts do not
automatically account for every label or queued draw reference.

The native finish wrapper at `0x0c399940` drains pending arrays even after a GPU
error. Empty arrays do not establish successful GPU completion or recovery
safety. This finding is documented in the upstream font investigation and the
font evidence; no unconditional finish/reset operation is proposed here.

The existing `canopus_ui_host_detach` explicitly forgets page-owned native
objects because the native parent may already have destroyed them. Releasing
new owned font wrappers there unconditionally would not establish the required
pre-destruction/render-retirement ordering. A new lifetime contract must connect
font cleanup to native page/object ownership and pending render completion.
Keeping every generated font forever would instead permit unbounded growth
across size changes.

A pointer per 32-node view would itself occupy 128 bytes on Cortex-M33, before
cache keys, retirement metadata, or native heap allocations. This is an
illustration of state requirements, **not an ELF delta or a complete design**.
Native wrappers, descriptors, faces, fallback chains, and glyph caches consume
additional heap memory that must also be accounted for separately from the ELF
resident-slot measurement.

## Target coverage and policy scope

| Active target | Font sizing evidence | Policy and implementation limitation |
|---|---|---|
| Band 10 Pro `.036` | Existing Medium 24/28 styles; approved DemiBold 32 style record | No equivalent audited dynamic-font factory/lifetime mapping found. DemiBold 32 changes weight and does not establish a common dual-backend size capability. |
| Band 10 Pro `.043` | Medium 24/28 styles; high-level create `0x0c8603b0` and destroy `0x0c860478` recovered here; low-level layout/ownership audit `EVID-FONT-1043-001` | New high-level records/bindings and handoff still required. Face-cache payload is 24 bytes versus 28 on `.155`; low-level layouts/callback identities are not interchangeable. |
| Band 11 `.139` | High-level calendar create `0x0c4950b0`, font setter `0x0c387eba`, destroy `0x0c49258c`; `EVID-RESOURCE-BACKGROUND-CALENDAR-PORT-139-001`, `EVID-FONT-4139-001` | Calendar-specific restricted static-port approval exists. General native UI font lifecycle is outside that approval; the `.155` fallback discrepancy must not be assumed resolved for `.139`. |
| Band 11 `.155` | High-level trio and exact code inspection above; `EVID-RESOURCE-BACKGROUND-CALENDAR-PORT-155-001`, `EVID-FONT-4155-001` | Calendar-specific restricted static-port approval exists. Generic lifetime and budget remain unresolved; failure-font ownership text disagrees with exact target code. |
| Band 11 `.179` | Recorded calendar create `0x0c495010`, setter `0x0c3839e4`, destroy `0x0c492504` | Records remain restricted/PENDING. No inherited `.139/.155` approval or general UI font capability may be claimed. |

The formal `.139/.155` background-calendar port approvals authorize the exact
restricted private port and preserve `STATIC_RECOVERED`/`not_probed`. They do
not establish general font-lifecycle correctness, public SDK promotion, or
hardware acceptance. ADR-CAN-013's residual font limitation explicitly says the
calendar-specific approval cannot be reused for the unresolved generic font
allocation/cache/render-reference lifetime.

The stock 17/26/30/32 style clues mentioned by ADR-CAN-013 are not an audited,
common set of usable styles across both backends. Existing approved DemiBold 32
records on `.036/.043` do not supply the equivalent Band 11 mapping or preserve
the current Medium weight. They therefore do not justify advertising arbitrary
sizes or a new common discrete size set.

## Evidence references and limits

- `targets/xiaomi-band-11-4.100.155/evidence/EVID-RESOURCE-BACKGROUND-CALENDAR-PORT-155-001.json`
  and its `.receipts`: exact private calendar ABI mapping and fingerprints.
- `targets/xiaomi-band-11-4.100.155/background-calendar-port-approval.md` and the
  corresponding `.139` approval: restricted static-port authorization scope.
- `targets/xiaomi-band-11-4.100.155/evidence/EVID-RESOURCE-4155-001.json`:
  family pathname resolver and borrowed-storage contract.
- `targets/xiaomi-band-11-4.100.155/evidence/EVID-FONT-4155-001.json`:
  exact callbacks, ownership, cache state, pending glyph lifetime, and limits.
- `targets/xiaomi-band-11-4.100.139/evidence/EVID-FONT-4139-001.json`:
  independently mapped `.139` low-level font identities.
- `targets/xiaomi-band-10-pro-3.101.043/evidence/EVID-FONT-1043-001.json` and
  `targets/xiaomi-band-10-pro-3.101.043/font-reload-static-audit.md`:
  target-specific face layout, callback identities, and ownership constraints.
- `adr/ADR-CAN-013-ui-abi-2-lifecycle.md`, residual font limitation:
  currently supported stock sizes and unresolved general lifetime/approval.
- Adjacent repository evidence source:
  `../Canopus-Module-Resource-Hook/targets/xiaomi-band-11-4.100.155/font-reload-investigation.md`.

This report adds static program-comprehension findings, including the `.043`
high-level factory/destructor and `.155` fallback discrepancy. It does not add
actual TTF parsing tests, dynamic font factory/setter execution, device tests,
GPU completion guarantees, or a measured font implementation budget. All
device-verification and production-approval states remain unchanged. Fixed-style
and rejected-size regression coverage is recorded in the final verification
report; it does not exercise the dynamic font pipeline.
