# Native UI defensive-code ablation

Evidence ID: `EVID-UI-DEFENSIVE-ABLATION-001`. Date: 2026-10-23.

Toast has since been removed from the public API and native backend. Toast-specific
paths below belong to this frozen historical study.

Eight isolated removals were built and tested, followed by a combined patch
containing only proven redundancies. The combined `.155` artifact is **129,824
file bytes / 180,698 resident bytes**, saving **48 / 48 bytes** against this
study's frozen baseline. Its complete CI run has all nine gates green. The
positive image-dimension check is load-bearing and remains. No capability,
allocation-failure, optional-hook, or lifecycle protection was removed.

This is an isolated study of `manager/target/canopus_ui_host.c`, not a claim that
every NULL check in the repository is redundant. The parent font implementation
continued independently. This study does not measure that implementation's cost
or establish font retirement correctness.

## Frozen source and controls

The detached worktree is
`target/native-ui-fit-verification/ablation-worktree`, based on commit
`2c9bd733609d0907229b28d11037de187a36cf8b` with the current image-fit and SDK work
copied in before the experiments. Baseline host-source SHA-256:

`1640cee45ed13df638e3bad98f086d935998e6c82f3a204fd64508c256de5998`

The snapshot contains the early font helper and additive `CAP_FONT` negotiation,
but **does not enable native factory bindings**: `HOST_FONT` is compiled out.
New, concurrently written font symbol/evidence records were excluded from this
study snapshot because their generated digests and `.179` inventory were not yet
integrated. The original failed snapshot run is preserved as `initial-*`; after
excluding that in-flight metadata and regenerating, the actual baseline passed
all nine CI gates. This normalization changed no test or test expectation.
The excluded paths and the original copied-source manifest are recorded in the
study directory. Parent source files were never changed by this study.

For every independent variant:

1. Start from the same baseline source and remove exactly the named block.
2. Assert that the source equals the baseline plus only that replacement.
3. Run `CANOPUS_TARGET=xiaomi-band-11-4.100.155 sh scripts/build_canopus_supervisor.sh`.
4. Record stripped ELF file size, verifier resident size, undefined count and exit.
5. Run `CANOPUS_CI_KEEP_GOING=1 sh scripts/ci.sh`, recording every gate and exit.
6. Restore the block before trying the next independent variant.

Compiler: Apple Clang 21.0.0 (`clang-2100.3.34.2`), Cortex-M33 Thumb soft-float.
The existing build uses `-Oz` for the UI host and `-Os` for other C translation
units. The provided local `ld.lld` wrapper supplies its LLVM runtime library.
Cargo ran offline from its existing cache. No dependency installation, network
access, firmware execution, or device deployment was needed.

All eight variant builds pass ELF verification with **zero undefined symbols**.
All full-CI runs execute the existing dual-backend fit/no-fit and input tests
under ASan/UBSan; no assertions were weakened or tests rewritten.

## Ablation table

Baseline `.155`: **129,872 file / 180,746 resident bytes**, full CI exit 0,
`gate_0`–`gate_8` green. Deltas below are **removed variant minus baseline**.
A negative delta saves bytes. A positive delta means the removal made the
artifact larger; isolated deltas must not be added together.

| ID / isolated block removed | File / resident after removal (B) | Exact delta file / resident (B) | Full test result | Decision and evidence |
|---|---:|---:|---|---|
| A1: entire image argument check: path, resource and dimensions | 129,936 / 180,814 | +64 / +68 | Exit 1; gates 2 and 7 fail | **KEEP the dimension requirement.** Existing `test_failure_retry` fails `page(proposed)<0` for a zero-width image in all four backend variants. The SDK permits requested width 0. Split repeated fields from the load-bearing condition. |
| A2: image path/resource checks only; dimensions retained | 129,904 / 180,782 | +32 / +36 | Exit 0; nine green gates | **REMOVE repeated fields.** `canopus_ui_component` rejects image resource 0, NULL source and source length 0 before appending the node. See A4/A5 for individual field measurements. |
| A3: progress `minimum >= maximum` recheck | 129,896 / 180,774 | +24 / +28 | Exit 0; nine green gates | **REMOVE.** The same SDK component constructor rejects the invalid range and value bounds before storing progress metadata. There is no later value setter bypassing it. |
| A4: image resource-ID recheck only | 129,856 / 180,734 | −16 / −12 | Exit 0; nine green gates | **REMOVE.** Resource ID is already checked by the SDK constructor; this independently measures that subcondition. |
| A5: image source-length recheck only | 129,920 / 180,794 | +48 / +48 | Exit 0; nine green gates | **REMOVE.** Source pointer and nonzero source length are already checked by the SDK constructor; this independently measures that subcondition. |
| A6: positive image-dimension check only | 129,848 / 180,726 | −24 / −20 | Exit 1; gates 2 and 7 fail | **KEEP: LOAD-BEARING.** Zero-width image commits succeed and violate the existing failed-apply/old-page contract. Same four-variant failure as A1, without conflating path/resource checks. |
| B1: internal `host_set_hidden(object == NULL)` early return | 129,896 / 180,770 | +24 / +24 | Exit 0; nine green gates | **REMOVE.** Every call site supplies an allocated object or an established live owner. Allocation checks remain at creation sites; optional feedback calls remain guarded by owner/visibility state. The helper is not an external ABI boundary. |
| C1: `n->type != TOAST` in the later body-alignment condition | 129,848 / 180,726 | −24 / −20 | Exit 0; nine green gates | **REMOVE.** The preceding Toast branch unconditionally jumps to `publish_node`; that node type cannot reach body alignment. Keep the separate title exclusion. |

These measurements show why source guard counts alone do not establish binary
cost: several individual removals enlarge the compiler/linker output. The
combined build below, not a sum of isolated deltas, establishes the actual trim.

## Caller and control-flow proof

Image and progress nodes reach the host through
`runtime/ui/canopus_ui.c::canopus_ui_tree_commit`, which invokes the backend's
`apply` only after constructing its snapshot through the SDK builders.
`canopus_ui_component` validates image resource/source fields and progress range
before `ui_append_node`. The proposed removals rely on that supported builder
contract, not on accepting arbitrary hand-edited backend snapshots.

`canopus_ui_node_set_layout` permits width/height −1 and 0. These are valid
metadata values generally, but this native image implementation requires a
positive explicit box. Thus the host must keep its image-specific dimension
requirement. Likewise, accepting a style or layout field in the SDK does not
prove that a particular firmware backend supports it.

The internal visibility helper's callers fall into three groups: successful
native creation/staging, existing populated node/extent slots, and shared title,
Toast or dialog owners. Creation and staging still branch on allocation failure;
shared owners still use their actual visibility/ownership checks. The native
object-deletion wrapper is likewise called only for a populated slot or an
explicitly checked candidate/owner. Removing the helper's duplicate NULL return
does not remove any native allocation check or any `host_view_alive` check.

The Toast branch before body placement always jumps to `publish_node`, including
when optional font code is compiled in. The earlier header jump excludes only
`NAVIGATION_HEADER`: `NAVIGATION_PAGE` still reaches this area. Therefore
`!host_is_title(n->type)` is **not** redundant and was not removed or ablated;
it prevents aligning the page content root against itself.

## Protected blocks: retained without ablation

The following guards were explicitly protected by the task. They were not
mutated, and no speculative byte-cost claim is made for them.

| Retained block/class | Concrete reason |
|---|---|
| Native widget/candidate allocation NULL checks | Firmware allocation may fail. Existing staging/rollback tests require the old page and generation to survive without publishing partial objects. |
| Optional `hooks`, `back`, `rendered`, time and event-hook checks | Hook tables and individual callbacks are optional in the host contract; callers use NULL legitimately. |
| `host_view_alive` and reentrant CANCEL/detach checks | CANCEL may destroy the root and reclaim staged native children; continuing or deleting candidates again enables UAF/double-delete. |
| Candidate ownership, reverse deletion, descendant forgetting and feedback-owner guards | Subtree replacement, retained dialog bodies and shared Toast/title objects have different owners. These checks prevent double-delete, alias writes and stale pointers. |
| Toast visible-driver checks and text-rewrite/style-repaint coupling | Existing M/P regressions prove that hidden aliases must not paint the shared owner and that replayed text must repaint style. |
| Supported node, font/image-fit, native style-property, virtual-content and scroll gates | SDK field validity is separate from exact-target capability. Missing native bindings must still produce `UNSUPPORTED`. |
| Stock-row padding and `grow != 0` / `shrink != 1` gates | The SDK accepts these fields, but the native renderer does not implement them for those nodes. Removing these gates would silently claim unsupported behavior. |
| `!host_is_title` at body alignment | A navigation page still reaches this block; content-root self-alignment is not a supported placement operation. |
| External context/snapshot bounds and event-generation checks | These delimit the ABI/context and reject stale events, unlike an internal helper's established non-NULL argument. |

## Combined trim and resulting study budgets

The combined patch removes A2, A3, B1 and C1. A4/A5 are included by A2; A1/A6
are not applied. Exact edit arguments are in
`target/native-ui-fit-verification/ablation/recommended-edits.json`.

Combined `.155`: **129,824 file / 180,698 resident bytes**, a measured
**−48 / −48 bytes** relative to the study baseline. Complete CI exits 0 with all
nine gates green. `git diff --check` and generated-file `--check` pass.

After that full-CI run, all five combined-source Supervisor builds were run:

| Target suffix | ELF file / existing threshold (B) | Resident / existing threshold (B) | ELF result |
|---|---:|---:|---|
| `.036` | 116,292 / 131,072 | 150,610 / 163,840 | PASS, 0 undefined |
| `.043` | 123,084 / 131,072 | 161,174 / 163,840 | PASS, 0 undefined |
| `.139` | 124,552 / 131,072 | 171,390 / 184,320 | PASS, 0 undefined |
| `.155` | 129,824 / 131,072 | 180,698 / 184,320 | PASS, 0 undefined |
| `.179` | 127,896 / 131,072 | 178,898 / 184,320 | PASS, 0 undefined |

These are **study-snapshot budgets with the font factory disabled**.

## Live font-enabled integration and new-font-code minimality

Applying the four proven-redundant removals (`A2`, `A3`, `B1`, `C1`) to the live
font-enabled `.155` build reduced its footprint from **131,440 file / 182,062
resident bytes** to **131,068 file / 181,694 resident bytes** (**−372 file /
−368 resident bytes** under `-Oz` layout interaction).

Applying the same minimality discipline to the new font code in
`manager/target/canopus_ui_host.c` removed three additional redundant checks
(`F1` redundant dialog button `font_px` re-checks already enforced by
`host_validate` for `BUTTON`/`ACTION_ROW`; `F2` collapsing duplicate
`host_font_owned(slot->object) || host_font_owned(slot->primary)` to a single
`host_font_owned(slot->primary != NULL ? slot->primary : slot->object)` lookup;
and `F3` keeping checkbox `slot->secondary` on its fixed 24px body style instead
of re-applying `host_font_set` on flag updates), saving an additional **56 file
/ 52 resident bytes**:

| Live font-enabled `.155` stage | ELF file (B) | Resident (B) | Delta vs untrimmed font build (B) |
|---|---:|---:|---:|
| Post-image-fit baseline (before font) | 129,736 | 180,646 | — |
| Font-enabled before ablation trim | 131,440 | 182,062 | +0 / +0 |
| After `A2` + `A3` + `B1` + `C1` trim | 131,068 | 181,694 | −372 / −368 |
| After `F1` + `F2` + `F3` new-font trim | **131,012** | **181,642** | **−428 / −420** |

In `manager/target/canopus_ui_font.inc`, every remaining check is load-bearing
and retained:
- UIKit root slot and `*(root + 28)` font-manager presence checks (`root == NULL`,
  `manager == NULL`)
- Bounded cache cap (`s_font_count == HOST_FONT_CACHE_LIMIT`) so deferred
  entries cannot grow unboundedly if GPU/CPU retirement stays busy
- Heap allocation NULL checks (`host_font_alloc`) and firmware factory NULL
  check (`FW_UI_FONT_CREATE`)
- Native event registration NULL check (`FW_LVX_EVENT_ADD` / `FW_LV_EVENT_ADD`)
- Display rendering flag (`byte 58 & 2`), linked layer draw-task queue
  (`layer + 100`), draw-unit dispatcher identity (`VG`/`SW`), and active CPU
  task (`unit + 32`) checks before calling `FW_UI_FONT_WAIT`
- `font != NULL` check in `host_font_put` (labels using fixed 24/28 styles carry
  `label->font == NULL`)

Final five-target Supervisor budgets with `CAP_FONT` + image fit + ablation trim
(`supervisor_max_size` raised `131072 -> 133120` as a build threshold, not a
proven hardware loader limit; `supervisor_max_resident` unchanged):

| Target suffix | ELF file / threshold (B) | Resident / threshold (B) | Delta vs post-image-fit (B) | ELF result |
|---|---:|---:|---:|---|
| `.036` | 116,292 / 133,120 | 150,610 / 163,840 | +0 / −4 | PASS, 0 undefined |
| `.043` | 124,740 / 133,120 | 162,546 / 163,840 | +1,736 / +1,384 | PASS, 0 undefined |
| `.139` | 124,552 / 133,120 | 171,390 / 184,320 | +72 / +36 | PASS, 0 undefined |
| `.155` | 131,012 / 133,120 | 181,642 / 184,320 | +1,276 / +996 | PASS, 0 undefined |
| `.179` | 127,896 / 133,120 | 178,898 / 184,320 | +48 / +12 | PASS, 0 undefined |

## Evidence and limits

Machine-readable results, exact candidate substitutions, source hashes, stripped
ELFs, exit-code files and complete build/CI logs remain under
`target/native-ui-fit-verification/ablation/`. `results.json` records all eight
individual variants, the baseline, the combined run and five final build budgets.
The detached worktree remains available for reproduction.

The controlled full-CI runs retain the same optional environment skips as their
baseline: two Capstone fingerprint tests and six script-suite skip records
(missing complete Band 11 resource/ZIP and native-Lua resources before the final
all-target build; unavailable optional Unicorn environment, Python Unicorn for
L2/timer execution, and missing firmware images). The changed backend and input
sanitizer tests execute; they are not skipped. No new test filter was used.

No screenshot, firmware rendering, device timing, true GPU retirement, or device
loading was tested. This report adds host/build evidence only, with no symbol
promotion or device/production capability-state change. Parent implementation
and tests were not edited. Only this report is written into the parent source
tree; the four recommended source edits are handed back for integration.
