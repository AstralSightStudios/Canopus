# .139 heap budget and the allocator's panic-on-failure (2026-09-12)

First **device** evidence for the heap capacity that
[revalidation-2026-09-10](revalidation-2026-09-10.md) left open. Source: a
4.100.139 watch that crashed during `RESTORE_AFTER_BOOT` while activating an
enabled module, minidump `crash1.txt` / `crash2.txt`.

## What the device reported

```
mm_malloc: WARNING: Allocation failed, size 169840
mm_malloc: Total:329940, used:273804, free:56136, largest:51056, nused:699, nfree:3
dump_assert_info: Assertion failed panic: at file: ../../nuttx/mm/mm_heap/mm_malloc.c:417
```

R5 `2010e9e0` identifies the heap as Kmem (`2010e9e0..2015f2bc`). The request is
`mm_memalign(Kmem, 4, 169836)` — `sup_read_artifact` pulling the whole inbox ELF
(`/data/canopus/inbox/bluetooth_audio.ko`, visible in the stack dump) into the
executable heap. Two facts follow.

**1. Kmem has ~50 KiB, not ~300 KiB, to give.** Of 329,940 bytes, 273,804 were
already in use with the Supervisor resident; the largest free chunk was 51,056.
That is the whole budget a module's resident image can draw on.

**2. An allocation that does not fit is fatal, not recoverable.** mm_malloc
`0x0c35056c` ends its failure path with `_assert("mm_malloc.c", 417, "panic")`
for exactly the two known heaps:

```c
if (dword_200B2590 == heap || dword_200B01C8 == heap) {   /* Umem, Kmem */
    mwarn("Allocation failed, size %zu"); mm_mallinfo(&info, heap);
    mwarn("Total:%d, used:%d, ...");
    _assert("../../nuttx/mm/mm_heap/mm_malloc.c", 417, "panic");
}
return 0;
```

mm_memalign `0x0c3507e8` handles NULL correctly, but never receives it: the
panic happens first. So "allocate and check for NULL" cannot be the failure
strategy on this target — capacity has to be established *before* the request.

## The gate

`mm_mallinfo 0x0c34f0a0` is the same function mm_malloc uses to render that
diagnostic: `struct mallinfo mm_mallinfo(heap)`, AAPCS sret pointer in r0 and
the heap in r1, walking the heap through mm_foreach `0x0c34f028` with the heap
lock held and per-node callback `0x0c347d0c`. Its `mxordblk` is the largest free
node's `size & ~3` — precisely the quantity mm_malloc matches a rounded request
against — so `request <= mxordblk` is the allocator's own success test rather
than an estimate. `b11_alloc` / `b11_temp_alloc` now consult it first and return
NULL instead of letting the request reach mm_malloc.

The ensemble candidate `heap_mallinfo` at `0x0c3608b8` (confidence 0.6164) is
**not** this function: it stores a vtable and tail-calls `0x0c35e358`. The
correct symbol is recorded as `mm_mallinfo`; the candidate record still carries
its original low-confidence address and should not be promoted.

## Resulting module ceiling

The loader's input ELF and section bookkeeping are never executed and now use
Umem (`3c356b40..3cfefe00`), the same domain stage2 already used for its own
input — only the resident image needs Kmem. With the Supervisor at 79,172 bytes
the remaining Kmem chunk is ~50 KiB, so a module whose laid-out image exceeds
that is refused with `CANOPUS_SUP_ERR_NOMEM` (-21).

For reference, the module in the crash lays out at 116,576 bytes
(EXEC 95,712 + RO 18,176 + RW 2,688), i.e. roughly 2.3x the available chunk. It
cannot be loaded into Kmem at any level of loader polish.

## Lifting the ceiling: Umem residency through the PSRAM alias

Module images no longer go to Kmem at all. They are allocated in Umem and
reached through the instruction alias the firmware uses for its own
PSRAM-resident code, so the ~50 KiB chunk stops being the limit and the
firmware's own heap is left alone. The Supervisor still loads into Kmem behind
an MPU lease; only modules moved.

**Why this alias, and which window holds what.** Startup copy `0x0c0c024c`
writes the PSRAM image at `0x3c000000` and its code runs at `0x1c000000`
(delta `0x20000000`), which the privileged default Code map covers under
`MPU_CTRL = 5`; the firmware's MPU boot installs regions 0-3, none of which
covers `0x1c…`. Scanning that blob shows the windows are per-attribute rather
than interchangeable: 106 odd `0x1c…` words (Thumb callables), 362 words
pointing into `0x3c0xxxxx` (read-only data) and a third window at
`0x38280000` for writable data, which the same startup routine zero-fills up
to `0x38356b40` — where Umem then begins at `0x3c356b40`. Literal pools inside
`.text` are still read through `0x1c` on every PC-relative load, so the
executable and read-only extent can share the code view.

**What changed.** `canopus_elf32_load` now takes two target bases from
`allocate` — `code_base` for the EXEC and RO regions, `data_base` for RW — and
resolves every symbol and relocation site through its own region's base. Both
operands of a PC-relative relocation therefore come from the same view, so the
encoded difference stays correct across a 512 MB delta (`R_ARM_REL32` is 32-bit
and `R_ARM_PREL31` covers ±1 GB). Single-view platforms (Band 9, and the
Supervisor's own Kmem path) set both bases to the allocation and reject a call
where they differ. `finalize` takes no MPU lease on the alias path; it
re-derives the alias from `data_base` rather than trusting the base it was
handed, then runs the publish sequence: D clean-all `0x0c93021a`, DSB/ISB
`0x0c91e542`, the enabled I cache's clean/invalidate-all branch `0x0c0c181e`,
DSB/ISB again — the same leaves the Lua bootstrap already runs before entering
stage1, with both controllers' enable bits checked first.

**Emulation evidence.** `scripts/tests/band11_module_load.py` boots the
Supervisor, stages a real signed module and drives `RESTORE_AFTER_BOOT`. Both
PSRAM windows are backed by one host buffer, so the alias is modeled rather
than assumed. With the 116,576-byte image from the crash above it records: the
image allocated at `0x3c…`, Kmem peak unchanged, the MPU bitmap unchanged
(no lease), 40,597 basic blocks executed through the `0x1c…` alias, the
D and I cache commands issued in order, and the module's constructor
registering its descriptor. It also passes with Kmem's largest free chunk
forced down to 4096 bytes. The module's own `activate()` then fails in
emulation because the Bluetooth services it calls are not modeled; that is a
harness boundary, not a loader result.

**What still needs the device.** Instruction fetch through the alias is
exercised by stage1 on hardware. Read-only data access through it, and the
physical completion of the cache sequence, are not: Unicorn backs both windows
with one buffer and models no cache at all.
