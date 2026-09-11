# .139 native installer audit (2026-09-09)

Exact raw image SHA-256: `31ce82257f7c127950dc5070b86316730cf468a41f0d004559e41e7d923b2c74`.
This audit reads the raw image and the IDB-derived corpus using Capstone M-class Thumb;
it does not claim another IDA decompilation. EVID-NATIVE-4139-003 supersedes the native
BLOCKED state recorded at the time of EVID-BOOTSTRAP-4139-002. Device remains NOT_PROBED.

## Corrected ABIs

- app_install 0x0c6ab350 copies **80 bytes**, mutates input +0x17/+0x18, ID+0x14,
  package+0xc, icon+0x10, metadata callback+0x24, page registry+0x38, hidden+0x48.
  Launcher 0x0c547b54 delegates to load_app_info 0x0c547a28, which independently
  confirms those offsets and builds a **28 byte** record (flags+0x18).
- Page size **120 bytes**, as in the static descriptor table. on_create+0x4c,
  resume+0x50, foreground+0x54, pause+0x5c (0x0c693a0e), stop+0x60,
  UI destroy+0x64 (0x0c696aa4). The old backend’s pause+0x58/destroy+0x5c is wrong.
- Page root creation sets **212×520**, calling lv_obj_set_size 0x0c387ba8.
  Lua binding tables independently confirm Object, Label, image, text, long mode,
  width and alignment entries. Object 0x0c380686 is a valid zero-extra-argument
  wrapper around class creation 0x0c37905c; an intermediate semantic doubt was resolved.
- Bar class 0x2ca17de8 has constructor0x0c3ad988 and creator0x0c3ad7f4.
  Its min/max/current fields prove setters0x0c3ad920/0x0c3ad8b4. Imported veneer
  matches were not accepted as evidence. The 0x0c350cf8 allocator hypothesis was
  rejected: it calls mm_realloc. Actual mm_memalign is0x0c3507e8, three arguments.
- register_driver0x0c914f64 takes path/fops/private, writes inode type1, fops+0x10,
  private+0x18, faithfully returns inode_reserve failure. No Band9 type7/+0x1c code is used.
- page_finish0x0c69b5ac takes a **composite key**, not a page descriptor. Lua finish
  goes through0x0c69b614, which extracts this key at message+4. Forward navigation
  uses goto_page_no_check_stacktop0x0c8ed780 with key/start-data/two transition arguments.

## Bootstrap ownership and entry

Boot0x0c35804a..0x0c358070 creates Umem at3c356b40..3cfefe00 and Kmem at
2010e9e0..2015f2bc. mm_initialize0x0c34ef08 returns the aligned heap start,
with usable bounds at+0x1c/+0x20. A newly allocated, non-interned Lua long string
holds the stage1 PIC bytes and result word. A closed upvalue roots it through synchronous
NSH exec; UpVal.v+8 points to its own TValue+16, tag+8=0x54, TString.contents+16,
long length+12. These fields, heap ranges, payload markers and resource CRCs are checked
before any owned payload mutation. Only the payload result and parameter words change.

Startup copy0x0c0c024c proves the PSRAM data3c... / instruction1c... relationship.
MPU boot0x0c91e8e4 installs three table entries and one SRAM text region, bitmap0x0f.
With CTRL5, the privileged default Code map covers1c... . The test entry uses that
existing mapping; no Lua RNR/RBAR/RLAR writes and no MPU disable are needed.
Runtime preflight checks CTRL, MAIR0, bitmap and selected ROM instruction fingerprints.
The privileged default-map interpretation follows the [Arm Cortex-M33 generic user guide](https://documentation-service.arm.com/static/5f16e93d20b7cf4bc524af1d). The physical alias and execution path are a hardware-test candidate, not a device result.

BES I/D cache controller bases are07ffa000/07ffc000. The original range routines at
raw0x0c0cb550 and0x0c0cb4f8 use +0x80 for clean lines and+0x84 for invalidate lines.
Those raw routines are relocated at startup, so their ROM-copy addresses are NOT called.
The 2026-09-10 revision replaces Lua's per-line `mw` writes with fixed firmware
leaves: D clean-all at0x0c93021a, **barrier-only tail0x0c91e542**, then the enabled
I-cache clean/invalidate-all branch at0x0c0c181e and the same barrier again. Cache
enable bits and the leaf instruction/literal fingerprints are checked first.
This avoids `mw`'s extra reads of cache command ports. It never calls MPU-disable
entry0x0c91e530; the enabled-cache branch preserves cache configuration. Exact
peripheral completion/alias effects must be confirmed on hardware.

Stage1 uses NSH exec’s normal LR return, no fixed Band9 continuation address. Its ADR
uses a local data label so ELF Thumb tagging cannot shift the mailbox pointer by one byte.
Stage2 loads the zero-import ET_REL, applies relocations and invokes constructors with an
optional r0 mailbox argument; the Supervisor wrapper records its result and clears the
borrowed pointer. Images that have run constructors remain resident on error because
callbacks or device nodes may already reference them.

Native region leases reserve only4–6 under PRIMASK, update the firmware bitmap and MPU
hardware atomically, and restore RNR. Region7 is conservatively left to firmware even
if absent from the BES bitmap. **Correction from the second audit:** its ownership as
a scheduler stack guard was not established; the actual context-switch instructions
at0x0c9194f4/0x0c9194fc and0x0c919544/0x0c919548 save/restore MSPLIM/PSPLIM.
The code/RO region is RO executable, MAIR index1;
RW Kmem stays in the verified privileged SRAM background map. Each resident image uses
one lease, so module capacity is also limited by the remaining leases and Kmem.
No full MPU disable, unowned cave writes, or guessed callable is shipped.

## Validation and remaining boundary

The builder checks exact firmware SHA, resource CRC/size, zero imports, ELF relocations,
PIC link invariants (no writable/GOT/BSS in stages and no absolute link-time fixups),
entry offset and mailbox offset. stage2 keeps its input ELF and bookkeeping in Umem,
leaving Kmem for executable images. The second audit measured 1308 bytes of stack in
the real NSH dispatcher plus native loader under emulation. Modeled VFS/allocator
calls omit their internal stack depth, so this is not a complete hardware stack bound.

Real Lua5.4.0 C-frame tests cover restoration, failed identity, cleanup, and production UI.
ARM Cortex-M33 emulation executes actual built stage binaries and Supervisor at two
rebases, with partial file reads, error unwinds and MPU banking modeled. It verifies
constructor result, /dev/canopus fops, INSTALL0/1/2, app/page descriptor offsets, launcher
publication call, and Manager page create/resume/pause/UI-destroy. Firmware allocator,
VFS and LVGL calls are mocked; this is not a whole-firmware or physical-device emulator.

The general SDK still exports zero approved callables for this target. The installer’s
reviewable private static ABI is separate from the low-confidence ensemble records.
No claim is made that all imported symbols or Rust module services now work on-device.
Unverified Supervisor unregister and watchface-delete calls are omitted; reboot is its
lifecycle boundary, and module installer watchfaces remain available for manual cleanup.

The [2026-09-12 heap budget record](heap-budget-2026-09-12.md) adds the first
device evidence for Kmem capacity and for mm_malloc's panic-on-failure, which
closes the "allocate and check for NULL" option on this target: every allocation
is gated on mm_mallinfo 0x0c34f0a0 first. The Supervisor's own module loader now
keeps its input ELF and bookkeeping in Umem, as stage2 already did, and module
images are resident in Umem too — executed through the 1c... instruction alias
with no MPU lease, so "module capacity is also limited by the remaining leases
and Kmem" above now applies only to the Supervisor itself.

The [2026-09-10 revalidation](revalidation-2026-09-10.md) adds actual firmware
execution for task privilege initialization, system stack attributes, NSH word
access, both cache approaches and app/page/Launcher record creation. It records
the cache-entry change and the remaining physical validation boundary.

## Second audit: raw firmware execution and MPU checks

See [the reproducible validation record](validation-2026-09-09.md). The test executes
the exact firmware's MPU initialization0x0c91e8e4, MPU configure0x0c91e594 and relocated
HAL save/restore bodies, not replacements for those functions. Native leases now reject
overlap with **all eight** enabled hardware regions and verify RBAR/RLAR after writing.
The actual NSH exec body and builtin dispatcher are included in the native success test.
Sixteen injected failure cases cover resource length/CRC, each allocation, MPU rejection,
identity and driver registration. The 101408-byte simulated Kmem peak includes the
resident 97280-byte image and temporary stage2; 81900 bytes of loader scratch/input
now use Umem. Firmware VFS and allocator overhead are still modeled.

Broad Unicorn2.1.4 memory hooks have a reproduced Thumb ITSTATE defect. They remain
enabled during bootstrap/MPU tests, but are removed for Manager UI calls, which retain
execution-range checks, RO-image snapshots, stack canaries and register preservation
checks. The native Manager code is not changed to work around that emulator defect.
