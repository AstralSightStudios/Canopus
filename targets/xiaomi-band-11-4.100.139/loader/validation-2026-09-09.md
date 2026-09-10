# Band 11 .139 second validation record

Firmware SHA-256: `31ce82257f7c127950dc5070b86316730cf468a41f0d004559e41e7d923b2c74`.
The raw image is read only. This record supersedes the earlier assertion that region7
is a proven scheduler stack guard. No device has been connected or probed.

## Firmware instructions used as the reference

| Exact firmware path | Verified result |
| --- | --- |
| `0x0c91e8e4` → `0x0c0c00d8`, `0x0c91e640`, `0x0c91e7b4` | Real MPU boot code produces CTRL5, MAIR0 `0x00447722`, bitmap `0x0f`. Only logging is stubbed. |
| `0x0c91e594` → `0x0c91e550` | Firmware configure with access1/memory1 produces exactly the same RO executable RBAR/RLAR as the loader. |
| Raw `0x0c0ce608` / `0x0c0ce644`, relocated to `0x0027a7dc` / `0x0027a818` | HAL register save/restore preserves all eight banks including the loaded Supervisor's region. Physical sleep is not emulated. |
| `0x0c9194f4`, `0x0c9194fc`, `0x0c919544`, `0x0c919548` | Context switching uses MSPLIM/PSPLIM. Region7 ownership is unproven; the loader conservatively leaves it unused. |
| `0x0c3692c0` → `0x0c36c698` | Real NSH dispatcher and exec parse the odd Thumb address, call it and return normally. Positive printf residue from the barrier-only function normalizes to shell success. |
| `0x0c3507e8`, `0x0c34cd2c` | mm_memalign accepts the explicit Umem heap; umm_free uses the same `*0x200b2590` and mm_free. ELF input and scratch no longer consume Kmem. |
| Raw `0x0c0cb550`, `0x0c0cb4f8` | Firmware HAL uses 32-byte lines and clean/invalidate registers at cache-base+0x80/+0x84. Device MMIO completion and physical PSRAM aliases still require hardware. |

Boot's actual region register pairs are:

| Region | RBAR | RLAR |
| --- | --- | --- |
| 0 | `0x00000006` | `0x0001ffe3` |
| 1 | `0x0026bc06` | `0x003cbbe3` |
| 2 | `0xd0200006` | `0xd020ffe5` |
| 3 | `0x20079426` | `0x2007be23` |

The bootstrap succeeds against these firmware-generated regions. A native lease uses
one free slot in4–6, rejects overlap with every enabled region, modifies hardware and
bitmap under PRIMASK, verifies the register pair, restores RNR and PRIMASK, and removes
the temporary stage2 lease on return. No installer code changes MPU CTRL or stack limits.
Armv8-M overlap behavior is specified in the
[Cortex-M33 user guide](https://documentation-service.arm.com/static/5f16e93d20b7cf4bc524af1d).

## Reproduce

```sh
CANOPUS_TARGET=xiaomi-band-11-4.100.139 scripts/build_canopus_supervisor.sh
build/band11-tests/bin/python scripts/tests/band11_arm_bootstrap.py -v
scripts/ci.sh
```

The five ARM test methods execute the built stages and Supervisor and cover16 injected
failure cases. They check constructor status, device operations, Manager registration,
UI lifecycle, two PIC bases, callee-saved registers, PSP thread entry, PRIMASK restoration,
temporary allocation release, reserved regions, MPU overlap and register-write failure.
The measured Kmem peak is101408 bytes, down from182876 bytes in the previous bundle
(these figures exclude allocator metadata and firmware-call allocations). The resident
Supervisor uses97280 bytes; all ELF input/scratch allocations in Umem are freed.
The measured NSH+native stack depth is1308/4096 bytes, excluding modeled firmware-call
depth. These numbers do not estimate an on-device success probability.

Unicorn2.1.4 has a local, minimal reproduction of a memory-hook defect: execute
`b9f1000f1cbf6069c9f80000002000e003b0bde8f08f` in Thumb M33 mode with r4/r9 pointing
to mapped data. With a no-op `UC_HOOK_MEM_WRITE`, ITSTATE incorrectly remains `0x18`
after the two conditional memory instructions and the following unconditional MOVS.
Without that hook it is0. This can skip a later SP adjustment and produce a false crash.
The test removes broad memory hooks for Manager UI calls, retaining MPU execution-range
checks, immutable-region snapshots, stack canaries and ABI register checks. Bootstrap
and MPU tests still use the memory hooks. No claim of whole-firmware emulation is made.

The pack root remains exactly `main.lua` plus five `.bin` resources; the resource ZIP
contains the same six files. `src/`, `docs/`, `build/` are not watchface resources.
