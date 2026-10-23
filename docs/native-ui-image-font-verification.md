# Native UI image fit and font sizing verification

Evidence ID: `EVID-UI-FIT-FONT-HOST-001`.

This change is an additive implementation within UI ABI 2.0. Host tests and static
firmware analysis do not establish device verification or production approval.
No symbol is promoted. `supervisor_max_size` is raised `131072 -> 133120` (+2,048 B)
consistently across Band 10 Pro and Band 11 as a **build threshold, not a proven
hardware loader limit**; `supervisor_max_resident` is unchanged.

## Baseline measured before implementation

All five active targets were rebuilt with
`CANOPUS_TARGET=<id> sh scripts/build_canopus_supervisor.sh`. Each verifier
reported `PASS` and `0 undefined`.

| Target suffix | ELF file bytes / limit | Resident bytes / limit | File headroom | Resident headroom |
|---|---:|---:|---:|---:|
| `.036` | 117,412 / 131,072 | 152,034 / 163,840 | 13,660 | 11,806 |
| `.043` | 123,956 / 131,072 | 162,502 / 163,840 | 7,116 | 1,338 |
| `.139` | 125,608 / 131,072 | 172,826 / 184,320 | 5,464 | 11,494 |
| `.155` | 130,696 / 131,072 | 182,014 / 184,320 | 376 | 2,306 |
| `.179` | 128,992 / 131,072 | 180,370 / 184,320 | 2,080 | 3,950 |

The `.155` baseline ELF SHA-256 is
`a129f8f64aaa79f9a954d14720fe7d999f8562bcaeeaff75ec81b93c84b9512e`.
Logs are under the ignored `target/native-ui-fit-verification/` directory.
On this machine the Rust-provided `ld.lld` requires its toolchain `lib` directory
in `DYLD_FALLBACK_LIBRARY_PATH`; the verification uses a local wrapper under
that ignored directory. No repository build configuration is changed for this.

## Image fit implementation

Evidence: `EVID-UI-1043-IMAGE-FIT-001`, `EVID-UI-4155-IMAGE-FIT-001`, and host
regression `EVID-UI-FIT-FONT-HOST-001` (this report). Both exact firmware images
were inspected in IDA. Scale is `void(image, int32_t x, int32_t y)` with independent
Q8 factors: 256 is identity. The decoder query returns 1/0 and writes an aligned
12-byte header with unsigned 16-bit width/height at offsets 4/6. The corrected
`.043` runtime Thumb callables are scale `0x1c06c971` and query `0x1c036359`;
`.155` uses `0x0c3ae46f` and `0x0c38e4e5`.

The semantic layout solver already knows the final box after bounds and parent
stretch. Fit arithmetic uses that box and decoder dimensions; it does not treat
an object's requested size as a change to its source dimensions.

| Capability | Firmware primitive and private binding | `.043` / `.155` | `.036` / `.139` / `.179` |
|---|---|---|---|
| `IMAGE_FIT_NATIVE` | Existing create/source/size | Existing behavior | Existing behavior |
| `IMAGE_FIT_CONTAIN` | `lvx_image_set_scale` / `FW_UI_IMAGE_SCALE`, `lv_image_decoder_get_info` / `FW_UI_IMAGE_INFO`; native center/pivot | Host implemented; uniform smaller ratio, rounded down | `UNSUPPORTED`; capability bit off |
| `IMAGE_FIT_COVER` | Same scale/query; page-owned fixed viewport with native overflow-visible bit `0x100000` cleared | Host implemented; uniform larger ratio, rounded up, centered and clipped | `UNSUPPORTED`; capability bit off |
| `IMAGE_FIT_STRETCH` | Same scale/query, independent X/Y factors and clipped viewport | Host implemented; each axis rounded up like native stretch | `UNSUPPORTED`; capability bit off |
| `font_px = 0/24/28` | Borrowed MiSans-Medium styles / `FW_LVX_STYLE_APPLY` | Existing fixed styles, counted contract tests | Existing fixed styles, same contract |
| Arbitrary `font_px` (`CANOPUS_UI_CAP_FONT`, bit 17) | Managed factory `FW_UI_FONT_CREATE` (`manager, &key`), property 90 (`0x5a`) via `FW_UI_STYLE_SET`, native `DELETE` event 36 + `FW_UI_FONT_WAIT` + `FW_UI_FONT_RELEASE` | Host implemented for labels, section headings, checkbox primary text and Toast owner; stock rows/titles/msgbox return `UNSUPPORTED` | `UNSUPPORTED`; capability bit off |

```text
CONTAIN        COVER          STRETCH
+----------+   +----------+    +----------+
|          |   |##########|    |##########|
|##########|   |##########|    |##########|
|          |   |##########|    |##########|
+----------+   +----------+    +----------+
letterbox      center crop    independent axes
```

Q8 quantization means not every pixel ratio is exact: for a 300x200 source in a
100x80 box, contain uses `(85,85)`, cover `(103,103)`, and stretch `(86,103)`.
Contain never exceeds the box. Cover/stretch ensure coverage, and the viewport
clips quantization overshoot; the excess per axis is less than source-size/256
pixels. Factors outside 1..65535 return `UNSUPPORTED` rather than wrapping or
passing zero. Failed decoding or zero source dimensions return `ERR_BACKEND`
before native allocation, input cancellation, or published widget mutation.
Identical commits neither requery nor rewrite the image. Other changed snapshots
preflight fitted images before staging; unchanged images still avoid setters.

A fitted image's slot owns its viewport through the native tree; its otherwise
unused `secondary` pointer borrows the image child. No slot/view persistent state
was added. Representation changes are checked against the live slot because a
native dialog commits a dialog snapshot while retaining the prior body widgets. Band 10 Pro's content wrapper explicitly normalizes padding, border,
radius and background; Band 11 removes styles on its generic wrapper. Both clear
overflow-visible and disable scrolling. Native center alignment and 50% pivots
are retained. Switching between fitted and native modes stages a replacement,
so returning to native restores the constructor's identity factors. A failed
allocation keeps the old page and generation. Native parent deletion reclaims
the child exactly once.

No all-mode enum shortcut was recovered: internal mode 11 implements stretch and
12 implements tile, with no contain/cover branch. No raw image field writes,
script calls, new public firmware wrappers, or symbol promotions were added.
`.043` retains its existing restricted/static/APPROVED record states after the
runtime address corrections; `.155` remains restricted/static/PENDING. The new
capabilities have no device verification or production approval. Other targets
need independent exact firmware evidence before binding these primitives.

## Measured final budgets (image fit + arbitrary `font_px` + defensive ablation)

Only `manager/target/canopus_ui_host.c` uses Clang `-Oz`; other translation
units retain their build flags. After image fit (`.155` `129,736 / 180,646 B`),
adding arbitrary `font_px` (`manager/target/canopus_ui_font.inc` + `CAP_FONT` +
`.043`/`.155` private bindings) measured **131,440 file / 182,062 resident bytes**
on `.155` before trimming (+1,704 file / +1,416 resident bytes, 368 bytes above
the previous 131,072 file threshold).

Running the empirical defensive-code ablation study
([`docs/native-ui-defensive-ablation.md`](native-ui-defensive-ablation.md),
`EVID-UI-DEFENSIVE-ABLATION-001`) and trimming redundant checks in both existing
host code (`A2`, `A3`, `B1`, `C1`) and new font integration (`F1`–`F3`) saved
**428 file and 420 resident bytes** on live font-enabled `.155`, bringing it down
to **131,012 file / 181,642 resident bytes** (+1,276 file / +996 resident bytes
net over post-image-fit, and 60 bytes under the old 131,072 threshold).
`supervisor_max_size` in both family TOMLs was raised `131072 -> 133120` (+2,048 B
small build margin; **build threshold, not a proven hardware loader limit; device
loading remains unproven / `not_probed`**). `supervisor_max_resident` (`163,840`
/ `184,320`) is unchanged.

| Target suffix | Post-image-fit file / resident (B) | Final file / threshold (B) | Final resident / threshold (B) | File headroom (B) | Resident headroom (B) |
|---|---:|---:|---:|---:|---:|
| `.036` | 116,160 / 150,518 | 116,292 / 133,120 | 150,610 / 163,840 | 16,828 | 13,230 |
| `.043` | 123,004 / 161,162 | 124,740 / 133,120 | 162,546 / 163,840 | 8,380 | 1,294 |
| `.139` | 124,352 / 171,258 | 124,552 / 133,120 | 171,390 / 184,320 | 8,568 | 12,930 |
| `.155` | 129,736 / 180,646 | 131,012 / 133,120 | 181,642 / 184,320 | 2,108 | 2,678 |
| `.179` | 127,720 / 178,790 | 127,896 / 133,120 | 178,898 / 184,320 | 5,224 | 5,422 |

Each build reports `PASS` and `0 undefined`.

## Arbitrary `font_px` lifecycle and retirement proof

Evidence: `EVID-UI-1043-FONT-LIFECYCLE-001`, `EVID-UI-4155-FONT-LIFECYCLE-001`,
and [`docs/native-ui-font-lifecycle-abi.md`](native-ui-font-lifecycle-abi.md).

1. **Exact firmware ABI**: `.043` `0x0c85f6cd` and `.155` `0x0c494381` are the
   managed wrapper factories `void *native_font_create(void *manager, const struct { const char *family; uint16_t px; uint16_t style; } *key)`,
   where `manager` is `*(void **)((uint8_t *)uikit_root + 28)` loaded from the
   UIKit root slot (`.043` `0x20103174`, `.155` `0x200bd1e8`). The returned
   wrapper copies the 36-byte native font payload at offset 0 and stores its
   backing record at `+36`, so it is directly compatible with LVGL local style
   property **90 (`0x5a`)** via `FW_UI_STYLE_SET(label, 90, (uintptr_t)wrapper, 0)`.
2. **Cache and ownership model**: `manager/target/canopus_ui_font.inc` maintains
   a Supervisor-owned cache (`s_fonts`, bounded by `HOST_FONT_CACHE_LIMIT`) keyed
   by `(manager, pixels)`. Every native label created by `host_label_create`
   allocates an independent heap record (`struct host_font_label`) and registers
   a native `DELETE` callback (event code **36**, userdata at `event + 12`) before
   publication. Reference ownership belongs to the actual native label plus
   per-apply transaction preflight pins (`fonts[]`), never to `view`, `slot`, or
   committed snapshots.
3. **Retirement boundary**: Native `DELETE` is dispatched before object/class
   destruction and does not drain pending GPU glyph references. `host_font_deleted`
   only unlinks its `host_font_label`, decrements `font->references`, and frees
   the binding record. Actual wrapper release happens in `host_font_collect`
   (after `host_apply` or on `canopus_ui_host_poll`) only after `host_font_drain`
   verifies that no display has rendering bit `byte 58 & 2`, no linked layer has
   queued draw tasks (`layer + 100`), every draw unit is a known VG/SW dispatcher
   with no active CPU task (`unit + 32`), and `FW_UI_FONT_WAIT(vg_unit)`
   (`.043` `0x0c15c061`, `.155` `0x0c399941`) has synchronously drained both
   pending glyph arrays. Only then does `FW_UI_FONT_RELEASE(manager, wrapper)`
   (`.043` `0x0c85f8fd`, `.155` `0x0c917385`) run.
4. **R/N/M/P/T lifecycle proof**: Host ASan/UBSan tests (`tests/host/test_ui_backend.c`)
   verify zero leaks and zero UAF across:
   - Distinct `font_px` (`1..65535`) sharing one cached wrapper per size and zero
     factory calls on identical reapply or hide/show (`test_font_size_contract`,
     `test_font_cache_and_visibility`, `test_font_many_sizes_do_not_accumulate`).
   - Native dialog fast-path retaining live body labels while committing a dialog
     snapshot, and replacing labels when returning from custom `font_px` to fixed
     `0/24/28` (`test_font_dialog_retains_live_body`).
   - Shared Toast owner across multiple aliases, visible-driver switches, hidden
     snapshots, expiry, and custom-to-fixed replacement (`test_font_toast_owner`).
   - Page reload, view reset/reuse, and native root deletion **both before and
     after** `canopus_ui_host_detach` + `memset(&view)` (`test_font_page_reload_and_view_reset`).
   - Reentrant `CANCEL` destroying the native root and unpublished candidates
     during apply (`test_font_cancel_destroying_root`).
   - Deferred retirement while display rendering flag, layer task queue, active
     draw unit, or unknown dispatcher is present (`test_font_retirement_boundaries`).
   - Preflight allocation/factory/event-registration failures preserving the old
     page and generation with balanced references (`test_font_preparation_failures`).
   - Section headings, checkbox primary labels, and explicit `UNSUPPORTED` on
     stock rows, titles, empty-heading sections, and msgbox dialogs
     (`test_font_surfaces_and_unsupported_nodes`).
5. **Restricted policy compliance**: All six new symbol records (`native_font_create`,
   `native_font_release`, `native_font_wait` on `.043` and `.155`) are
   `restricted` / `STATIC_RECOVERED` / `PENDING` / `not_probed`, enforced by
   `native_font_bindings_are_private_and_target_gated` in `generated_stability.rs`.
   None is exported via `canopus_veneer.h`.
