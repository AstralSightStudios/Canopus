# Native module system-back callback investigation

Evidence level: **STATIC_RECOVERED**. This document records read-only IDA/Hex-Rays
inspection of three exact AP images. It does not record execution of the proposed
adapter, a device probe, physical-device verification, or production approval.
No target capability, restricted-field status, symbol approval, or generated
allowlist is changed by this investigation.

## Scope and problem

Corona keeps its resource list, details, mix-match pages, and home route inside
one native firmware page. Its title-back path can call the module's `on_back`,
but the existing framework does not populate the firmware system-back callback.
A system right-swipe therefore takes the firmware default path and closes the
whole native page instead of popping a Corona route.

This investigation follows only the system-return branch around `page_finish`
and the page-descriptor callback at byte offset **104 / 0x68**. It confirms a
way for the framework to consume that system event before page destruction.
It does not establish contracts for the neighboring +108/+112 slots.

## Exact firmware identities

Hashes were calculated from the complete AP files used for analysis and match
the repository's exact target identities.

| Target ID | AP SHA-256 |
|---|---|
| `xiaomi-band-11-4.100.155` | `ea0bdf1920cb30223d616432af00565ca67622e6468328f5eab155f8cdc2fb9f` |
| `xiaomi-band-11-4.100.179` | `307adabe0ff61c77be73d045a8260e279cddd131c7a535a2678eb34199278a2b` |
| `xiaomi-band-10-pro-3.101.043` | `519307675665e4866d722a8119a98589c397b614ac3294cb87bfc86de45756ec` |

Analysis inputs on the investigation workstation:

- `.155`: `/Users/lesetong/develop/mifw/analysis-q66-177/155/vela_ap.bin`,
  database `q66-ap.i64` in the same directory.
- `.179`: `/Users/lesetong/develop/rust/Canopus-Module-Resource-Hook/build/firmware-analysis/vela_ap_4.100.179.bin`,
  database `vela_ap_4.100.179.bin.i64` in the same directory.
- `.043`: `/Users/lesetong/develop/mifw/analysis-p67-043/vela_ap.bin`,
  database `vela_ap.bin.i64` in the same directory.

These are local provenance paths, not required repository files. Firmware images
are not part of this documentation change.

## Handler identity and target-specific completion values

Addresses below are even instruction VAs. Function hashes cover the contiguous
IDA function interval `[start, end)`, excluding the end address. These hashes
identify the inspected interval; they do not assert that every reachable callee
or literal pool is included.

| Target | System handler | End VA | Indirect callback call | Completed-back phase |
|---|---:|---:|---:|---:|
| `.155` | `0x0c69bb50` | `0x0c69beca` | `0x0c69bbc8` | `0x48` / 72 |
| `.179` | `0x0c699fa8` | `0x0c69a322` | `0x0c69a020` | `0x48` / 72 |
| `.043` | `0x0ca6d02c` | `0x0ca6d25a` | `0x0ca6d090` | `0x4a` / 74 |

| Target | Handler interval SHA-256 |
|---|---|
| `.155` | `1966ffe35daf9a85845d78f31df70ba0ec9172e70a6ad285531808b04d0630b7` |
| `.179` | `8d73465766654c5d801bdc6613fefa68c5a3da412e9e0c401568c49b560b0082` |
| `.043` | `4e24c3eaf4e1c6381d8a17761175c579994e978818109f7d9f7540913dbe0ab8` |

The original page-finish anchors used to find these handlers were `.155`
`0x0c69b59c`, `.179` `0x0c6999f4`, and `.043` `0x0ca6b9e8`.
The Band 11 and Band 10 Pro finish functions have different existing argument
contracts; the adapter should retain `module_page_finish()` rather than call a
cross-target finish address itself.

## Recovered callback contract

All three targets use this target-private AAPCS-compatible shape:

```c
typedef int (*page_system_back_fn)(
    struct canopus_target_page *resolved_descriptor,
    const uint8_t *borrowed_payload);
```

The firmware obtains the current stack-top descriptor, checks its root object,
page flags, and in-animation state, and then searches the descriptor and its
`parent_descriptor` chain. It calls the first non-null function pointer at
**descriptor +104**.

- **R0** is the descriptor in that chain whose callback was found. It is not
  guaranteed to be the original stack-top descriptor when the callback is
  inherited. A module should install its callback on its own exact page.
- **R1** is the original system-event pointer **plus 4**.
- Before the call, the firmware writes the stack-top root object into the first
  32-bit word of that payload.
- The completion phase is read as a little-endian `uint16_t` at **payload +8**,
  equivalent to original system-event +12. Stock callbacks sometimes inspect
  only its low byte. No complete payload struct, total payload size, or semantics
  for its other fields are claimed here.
- Returning **nonzero** consumes the system return. The handler logs that the
  page handles back itself, then returns without default finish/exit animation.
- Returning **zero** permits default processing. Default processing only
  finishes a page for the target's completion value above; earlier phases leave
  the page open.

Confirmed in `.155` `sub_C69BB50`: the consumed branch jumps straight to the
`page handles back pressed by itself` log and `return true`, so the stock
`miwear_anim_system_back` page-out never runs. Any adapter that closes its own
page from inside the callback therefore loses the exit transition; the adapter
must answer zero to leave the finish (and its animation) to the firmware. The
Supervisor's module-page adapter does exactly that (ADR-CAN-010): descriptor
+104 returns 1 only when the module handled the back internally, and returns 0
when the module declines so `finish_page` and the transition still happen.

The handler passes the payload synchronously and does not transfer ownership.
The adapter must read the needed fields during the callback and must not retain
or modify the payload. The descriptor/root references are borrowed firmware
state, not objects for the adapter to free. The callback occurs before default
page destruction and exit animation. This establishes that ordering, not a new
threading or arbitrary callback-drain guarantee.

### Exact call-site instructions

```asm
; Band 11 .155, system handler sub_C69BB50
0x0c69bbc2  LDR   R3, [R0,#0x68]
0x0c69bbc4  CBZ   R3, parent_lookup
0x0c69bbc6  ADDS  R1, R4, #4
0x0c69bbc8  BLX   R3
0x0c69bbca  CBZ   R0, default_handling
; nonzero: log consumption and return
0x0c69bbda  LDR   R0, [R0]          ; null callback: walk parent
0x0c69bbde  BNE   0x0c69bbc2
0x0c69bbe0  LDRH  R3, [R4,#0xC]
0x0c69bbe2  CMP   R3, #0x48

; Band 11 .179, system handler sub_C699FA8
0x0c69a01a  LDR   R3, [R0,#0x68]
0x0c69a01c  CBZ   R3, parent_lookup
0x0c69a01e  ADDS  R1, R4, #4
0x0c69a020  BLX   R3
0x0c69a022  CBZ   R0, default_handling
; nonzero: log consumption and return
0x0c69a032  LDR   R0, [R0]
0x0c69a036  BNE   0x0c69a01a
0x0c69a038  LDRH  R3, [R4,#0xC]
0x0c69a03a  CMP   R3, #0x48

; Band 10 Pro .043, system handler sub_CA6D02C
0x0ca6d086  LDR   R2, [R3,#0x68]
0x0ca6d088  CMP   R2, #0
0x0ca6d08a  BEQ   parent_lookup
0x0ca6d08c  MOV   R0, R3
0x0ca6d08e  ADDS  R1, R4, #4
0x0ca6d090  BLX   R2
0x0ca6d092  CMP   R0, #0
0x0ca6d094  BNE   consumed
0x0ca6d096  LDRH  R3, [R4,#0xC]
0x0ca6d098  CMP   R3, #0x4A
```

`parent_lookup` and `consumed` above are explanatory labels, not introduced
firmware symbol names. Hex-Rays initially inferred no-argument calls for the
Band 11 callbacks; the assembly explicitly establishes the R0/R1 arguments.

## Independent stock-page precedents

### Alipay settings consumes back to handle an internal view

| Target | Settings descriptor | +104 callback | Callback interval SHA-256 |
|---|---:|---:|---|
| `.155` | `0x2008593c` | `0x0c5146c4` | `4120e28e4704559750c0fcc48513a8513fd9f70fb6f88d864899c93decf311fd` |
| `.179` | `0x20085918` | `0x0c518ee0` | `43308a93618c4eccfb6ca76acbdf3fd6d57120433bb911516854a0c9611adf0f` |
| `.043` | `0x200cf228` | `0x0c4d8d24` | `7d883304f54395d7477ffeff70d51b9ee60571b28a9e42f51b33dee63b9540fe` |

The hashed callback intervals are `.155` `[0x0c5146c4,0x0c51474c)`, `.179`
`[0x0c518ee0,0x0c518f68)`, and `.043` `[0x0c4d8d24,0x0c4d8d98)`.
Each descriptor names the page `settings`. Each callback checks the phase at
its second argument +8 against its own target's 72/74 value. On the relevant
unbind-tip/success state it dismisses or changes internal UI state and returns
1, retaining the firmware page. This independently corroborates the payload
phase and consumption contract on all three exact targets.

### Synchronous finish inside a consumed callback

On `.155`, blood-oxygen `monitor` descriptor **0x20092fcc** points at +104
callback **0x0c63332c** (`on_backpressed` in its log). When no measuring-tip
subview needs handling, it calls through **off_20084ea4**, whose target is
**page_finish / 0x0c69b59c**, with page key **917505 / 0x000e0001**, then returns
**1**. When its measuring-tip state needs handling, it handles that UI and also
returns 1.

The callback interval `[0x0c63332c,0x0c63367c)` has SHA-256
`353f8ff669a4be9e5e644ec09bc1246d2a8b2c30d6cf1fcfa5282a73efc87930`.
This is a direct stock precedent for synchronous finish followed by consumption;
it supports routing the module root-return case through the existing
`module_page_back()` / `module_page_finish()` path without inventing a new post
queue. It is a `.155` precedent; no separate synchronous-finish stock example
was claimed for `.179` or `.043`.

## Integration recommendation and page identity

Install a typed +104 callback directly on each module launcher-page descriptor.
At callback entry, capture the current module-page object and validate that the
passed descriptor is **exactly that object's page descriptor**, the view is
active, and the module entry/context still belongs to that page. Where the
adapter checks the payload root, compare its first word with that same live
page's current root object. Do not route a callback from an old descriptor to
whichever module page happens to be globally current. Parent-inherited callbacks
must not be treated as proof that the callback belongs to the current module.

For an accepted live callback:

1. Read only the phase at payload +8 and compare with the exact target value.
2. For an earlier phase, return 0 without invoking module navigation.
3. On completion, call the existing return path for the **captured matching
   page/view**, then return 1 even when rendering the replacement route fails.
   Do not propagate a render error as permission for native default finish.
4. That return path must finish only the same captured page when the module
   declines to consume return. If a module callback synchronously closes or
   replaces the page, do not subsequently finish a different globally current
   page. After navigation/finish, do not dereference the captured page, view,
   root, descriptor, or payload without rechecking lifetime.

An identity mismatch must never invoke another page's module callback. Whether
the surrounding host consumes such an event or leaves it to the native default
is a host policy choice; this evidence does not authorize cross-page dispatch.
Root-pointer equality alone is not a lifetime token and cannot replace page
identity or existing serialized lifecycle ownership.

## Bounded verification plan

- Execute each original exact-target handler in the existing ARM/Unicorn test
  environment, with modeled stack-top lookup, descriptor, payload, callback,
  logging, and default-finish dependencies. Assert the callback slot and R0/R1,
  completion value, parent-chain lookup, and nonzero consumption branch.
- Exercise the compiled framework adapter: noncompletion does not pop; one
  completion dispatches once; resource-error return goes home; nested routes
  pop one level; home finishes once; render failure remains consumed.
- Exercise descriptor mismatch, inactive/closed page, missing module entry,
  and synchronous close/replacement without routing or finishing a new page.
- Device acceptance remains separate: test title return and system right-swipe
  on the exact matching Supervisor and module for each target. Host/emulator
  passes must not be labeled physical-device verification.
