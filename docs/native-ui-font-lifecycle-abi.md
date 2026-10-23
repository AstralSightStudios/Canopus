# Native UI managed-font ABI and retirement

Date: 2026-10-23. Evidence: `EVID-UI-1043-FONT-LIFECYCLE-001` and
`EVID-UI-4155-FONT-LIFECYCLE-001`.

This is the implementation handoff for arbitrary `font_px` after the updated
task decision. It supersedes the earlier feasibility report's budget deferral:
font implementation and measured build-threshold increases are now authorized.
The earlier report remains historical evidence, including its baseline numbers.
No post-font file/resident delta is claimed here; the implementing parent owns
those measurements, cache implementation and verification.

Only new `.043`/`.155` `native_font_create`, `native_font_release`, and
`native_font_wait` records are introduced by this investigation. They remain
**restricted / STATIC_RECOVERED / PENDING / device NOT_PROBED**. Privileged
Manager raw-address use follows updated task B; there is no public veneer,
calendar approval reuse, status promotion, address-range expansion, or verifier
exception. Existing records and their statuses are unchanged.

## Exact callable contract

All calls use ARM AAPCS. The table gives Thumb callable addresses; subtract one
for the even instruction entry.

| Binding | Band 10 Pro `.043` | Band 11 `.155` | Signature |
|---|---|---|---|
| `native_font_create` | `0x0c85f6cd` | `0x0c494381` | `void *(void *manager, const void *key)` |
| `native_font_release` | `0x0c85f8fd` | `0x0c917385` | `void(void *manager, void *font)`; discard incidental return |
| `native_font_wait` | `0x0c15c061` | `0x0c399941` | `void(void *vg_unit)`; discard incidental return |
| Existing UIKit root slot | `0x20103174` | `0x200bd1e8` | Data address containing the current root pointer |
| Existing draw-unit list slot | `0x201032a4` | `0x200bd318` | Data address containing the linked-list head |

Obtain the initialized native manager as `*(void **)((char *)root + 28)` after
loading the current UIKit root from its slot. Offset 20 is not the font manager.
The factory does not accept the UIKit root itself.

The factory's key is exactly eight bytes on 32-bit ARM:

```c
struct native_font_key {
    const char *family;  /* +0, borrowed during the call */
    uint16_t pixel_size; /* +4, nonzero */
    uint16_t style;      /* +6 */
};

font = native_font_create(manager, &key);
```

**Do not call these addresses as `create(family, pixel_size, style)`.** That is
an ABI of different, higher-level functions. Host tests running on a 64-bit
machine naturally have a larger pointer-containing key; the counted substitute
must use the host structure layout while the production ARM structure remains
eight bytes.

The native managed factory searches active backing records, then the bounded
idle cache, by family name and packed pixel-size/style. If necessary it resolves
the path, obtains a FreeType face and creates the backing descriptor. Each call
returns an owned wrapper and increments or creates the corresponding backing
record's reference count. It can return NULL on its checked ordinary failure
paths. Some deeper descriptor/list/wrapper allocations are unchecked before
native writes; **a complete graceful-OOM contract is not established**.

The matching release consumes exactly that managed wrapper using the matching
live manager. It frees the wrapper, updates the backing reference count, and can
move the last ordinary backing font to the firmware's bounded idle cache. It is
correct that releasing a wrapper does not immediately destroy all face/glyph
cache memory; the native cache owns that retained memory. Do not directly free a
wrapper, destroy its FreeType face, or clear the global native cache.

## Returned layout and label compatibility

Both factories copy the first 36 bytes of the native backing font into their
40-byte managed wrapper, then store the backing record at wrapper+36. Native
intrusive list links follow the payload.

| Offset | Native font/wrapper field |
|---|---|
| 0 | Glyph metrics callback |
| 4 | Glyph data/outline acquisition callback |
| 8 | Glyph release callback |
| 24 | FreeType descriptor pointer |
| 28 | Fallback font pointer |
| 32 | Font user data |
| 36 | Managed backing-record pointer; wrapper field |

Thus the returned wrapper is already a native font pointer suitable for a label;
there is no pointer adjustment to wrapper+4 or descriptor+4. Apply it with the
existing local-style setter, property **90 (`0x5a`)**, selector zero. The setter
stores a borrowed font pointer and does not copy the font or acquire a manager
reference. Existing `.155` setter `0x0c387eba` demonstrates this by delegating to
`0x0c3872b4` with property90; `.043` font-property90 and flags5 are independently
documented by `EVID-FONT-1043-001`, with the existing runtime local-style setter
at `0x002bd924`.

For production ARM, the property value is a single 32-bit register. Host64
counted substitutes must use `uintptr_t` for the value and matching setter cast
so that a host heap font pointer is not truncated to `uint32_t`.

The direct managed factory does not append the higher-level family's fallback
wrapper chain. Its owned result must therefore be released with the two-argument
manager release, not the higher-level destroy that walks fallback ownership.
Do not mutate shared wrapper/descriptor fields in place to change font size.
Acquire a different cached wrapper and change the label's borrowed pointer.

## Higher-level ABI and failure identities

For comparison only, these are different functions:

| Operation | `.043` even entry | `.155` even entry |
|---|---|---|
| Higher-level `vg_font_create(family, uint16_t px, uint16_t style)` | `0x0c8603b0` | `0x0c4950b0` |
| Higher-level destroy, including separately created fallback wrappers | `0x0c860478` | `0x0c49258c` |
| Borrowed static failure font used by those functions | `0x2cce1734` | `0x2ca14888` |

The `.155` calendar factory record's inherited ownership text says
`0x2ca12930`, but the exact factory and higher-level destroy both use
`0x2ca14888`. That discrepancy is recorded without editing the existing approved
calendar records. The newly bound **managed** factory does not return that
higher-level static failure font in its inspected checked failure paths.

## Actual label references and native DELETE

Native DELETE event code is **36** on both targets. `.155` deletion core at
`0x0c380404` sends it through `0x0c37fbc8` before child/class destruction and
object free. `.043` installed delete thunk `0x0c588de8` reaches PSRAM
`0x1c02ecdc`, which delegates to `0x1c02eb88`; that function sends event36 before
its destruction path. **Neither inspected deletion path is a graphics drain.**

A per-label heap binding registered for event36 can hold the actual label's
cache reference independently of view/snapshot storage. This lets native parent
teardown release references even after a view is cleared or freed. Hiding or
retaining a label retains that reference. Changing the label's font transfers
that reference; identical snapshot applications must not allocate another font.
Deletion callbacks must handle the implementation's event cancellation,
reentrancy and ownership transitions, rather than accessing a dead view.

The event layout used by the native dispatcher is:

- Current target at +0 and original target at +4.
- Event code: `uint16_t` at +8, mask `0x7fff`.
- Callback userdata: pointer at +12.

`.155` dispatcher `0x0c3a3ea4` checks the event filter, writes each callback's
userdata into event+12 and calls that callback synchronously. Registration
`0x0c37fc1c` delegates to `0x0c3a3f5c`, which allocates an event descriptor and
returns its pointer. It dereferences the allocation without a NULL check, so a
pointer return does **not** prove a recoverable native allocation-failure path.
Host heap binding allocations still need ordinary checked failure handling.

On `.043`, existing userdata getter `0x0c588310` reaches runtime `0x002bfd88`
and loads event+12. Existing code getter `0x0c588210` reaches `0x002bfd64` and
loads `(uint16_t event[+8]) & 0x7fff`. On `.155`, legacy candidate
`lv_event_get_code` at `0x0c0f7728` and recorded userdata thunk `0x0c720f10`
have identical bytes and target `0x00275341`; their names do not establish two
distinct getter contracts. Use the dispatcher-proven event fields for this
feature unless those legacy aliases are separately audited. Registering a
DELETE-only callback also avoids needing a code getter. This investigation does
not change those existing symbols.

## Native pending-reference retirement

The two audited draw units have a common layout: linked next+0, dispatcher+16,
active CPU task+32. Their exact installed dispatcher pointers are:

| Unit | `.043` Thumb identity | `.155` Thumb identity |
|---|---|---|
| VG_LITE | `0x1c045361` | `0x0c3913ed` |
| Software | `0x1c03debd` | `0x0c3948ad` |

The inspected VG and software dispatch paths execute drawing synchronously:
they set unit+32, call the renderer directly, mark the task complete, and clear
unit+32 before returning. No worker dispatch occurs in those inspected paths.
This supports the standard serialized UI-owner contract; it does not prove that
arbitrary third-party drawing or another thread cannot bypass that contract.

The CPU boundary can be checked rather than inferred solely from callback type.
Use the existing `font_reload_display_list` identity (`.043 0x2010317c`,
`.155 0x200bd1f0`). This is an intrusive list header with payload size at+0,
head at+4, and each display's next link at `display + payload_size + 4`.
The independently audited display payload is792 bytes on both targets. For
all displays, rendering flag is byte58 bit1; root layer pointer is display+680.
Each linked layer has draw-task head+100 and next-layer pointer+108. Defer
retirement while the rendering flag is set or any layer task head is nonzero,
as well as while any draw unit has active task+32. Scanning active units alone
misses task generation during render callbacks before a renderer becomes active.

Exact `.155` refresh-area code `0x0c389328` loops until root-layer+100 is zero
before calling the display flush callback; dispatcher `0x0c381484` retires
completed task nodes and nested layers. Exact `.043` runtime refresh-area code
starts at `0x002bb4ec`: instructions `0x002bb5c8..0x002bb5dc` load the root
layer at display+680, check task head+100, and repeat dispatch while nonzero.
Its refresh timer at runtime `0x002bb854` sets rendering bit1 at
`0x002bba10..0x002bba2a` and clears it at `0x002bbc36..0x002bbc42` after
render processing. These runtime addresses were decoded in the existing mapped
IDA database without creating functions or modifying annotations. Together with
the task-list check, this supplies a normal completed-refresh boundary. It does
not authorize calling from concurrent custom draw code or the render callbacks
that the check explicitly excludes.

Deferred GPU glyph releases are different from CPU rendering. VG glyph drawing
copies a description into a pending array, including a font-wrapper pointer.
The immediate CPU glyph reference can be released while that extra pending
reference remains. On `.155`, acquisition `0x0c3a7c44` saves a cache entry;
release `0x0c3a0992` resolves its cache through the supplied font's current
descriptor. Prematurely freeing the wrapper or mutating its descriptor can
therefore cause a dangling pointer or wrong-cache release.

`native_font_wait(vg_unit)` is the existing LVGL native wait/resource-retirement
helper. `.155` VG dispatcher `0x0c3913ec` calls it at `0x0c399940` when no next
task is available. `.043` native dispatcher `0x1c045360` calls the PSRAM helper
`0x1c04ec20`; independently inspected XIP helper `0x0c15c060` has the matching
behavior and is the new raw binding. Calling the XIP helper is not substituting
an XIP clone into a field whose callback identity is compared elsewhere.

The helper:

1. Calls the lower native GPU finish.
2. Logs/dumps an error if the lower call fails.
3. Synchronously drains both gradient pending arrays, both image pending
   arrays, and both glyph pending arrays using their stored native release
   callbacks.
4. Clears submission counter unit+52.

It returns the incidental result of the last drain. **Its ABI is void for the
host; do not interpret the return register as success or failure.** After it
returns, the drained native CPU release callbacks have completed. A wrapper
with no current/future label references and no active or queued CPU drawing
references may then be released through its manager under the serialized
UI-owner contract.

This is a useful memory-reference retirement boundary. It is **not a statement
that a frame rendered successfully or that physical GPU error/reset recovery is
safe**. Native firmware itself drains even after a lower GPU error. The lower
wait can report zero after reset recovery. An empty pending queue or zero lower
result therefore cannot be promoted into a stronger hardware completion claim.

For a design that defers on reported GPU failures, existing lower finish entries
are `.043` `int(void)` callable `0x0c187bb1` and `.155` `int(void)` callable
`0x0c92019d`. A nonzero result can retain zero-label-reference cache entries for
later retry. On zero, the LVGL helper is still needed to retire native CPU
pending references. Its second lower call and native error handling must not be
misrepresented as a fail-safe hardware fence. These existing lower finish
records are not changed here.

## Required integration ordering

The following are implementation preconditions, supported by the inspected
standard native paths rather than a global concurrency proof:

1. Run native label changes, DELETE callbacks, cache mutation and retirement on
   the serialized UI owner outside rendering, interrupt locks and concurrent
   custom drawing.
2. Keep cache entries alive while actual labels, staged transaction references,
   or deferred retirement need their wrappers. Native DELETE of one label only
   drops that label's reference; it does not by itself make graphics idle.
3. Before consuming a zero-reference wrapper, exclude active CPU draw units and
   unexecuted layer draw tasks. If active/queued work or an unknown unit exists,
   defer retirement to a later safe owner callback.
4. Call the exact VG native wait helper while the old wrapper remains intact;
   then release that wrapper through the matching live manager. If the policy
   additionally checks lower finish status, retain/retry on reported failure.
5. Bound retained zero-reference entries and fail allocation rather than grow
   forever during persistent failures. A healthy retirement path must reclaim
   entries; deferred entries need a polling/retry path.
6. Do not retain native identities across framework teardown/restart merely
   because a pointer is equal later. Device restart/error recovery needs its own
   acceptance contract.

A label setter only invalidates styles; a native DELETE only destroys objects.
Neither action alone proves a native pending-reference retirement boundary.
The explicit native wait and the implementation's serialized/no-queued-work
ordering are both material to the claim.

## Hash verification and limits

Exact AP SHA256 was recomputed for `.043`
`519307675665e4866d722a8119a98589c397b614ac3294cb87bfc86de45756ec`
and `.155`
`ea0bdf1920cb30223d616432af00565ca67622e6468328f5eab155f8cdc2fb9f`.
All six complete IDA function extents were independently rehashed from raw AP
bytes and matched the IDA bytes. Extents and SHA256 values are in the two JSON
evidence bundles. No firmware bytes are distributed and extents are evidence,
not newly allowed address ranges.

This handoff is static recovery. It does not claim actual font parsing, complete
native OOM behavior, arbitrary drawing-owner coverage, physical GPU completion,
error recovery, device loading, or device verification. Parent verification
must cover counted creation/release/wait order, real-label DELETE after view
storage disappears, size switching, hidden/retained labels, transactional
failure, bounded deferred cleanup and all supported target gates.
