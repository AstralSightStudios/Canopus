# Band 9 3.1.32 Stage-0 Bootstrap Review

**Target:** `xiaomi-band-9-3.1.32` (`vela_ap.bin`, SHA-256 `9c02dab4020b2cc9666ee7d34cf27d311b76aadcec519a38361bbcbd94c53264`)

## Result

The existing Band 9 implementation already has a complete stage-1/stage-2 loader design. The old mailbox trampoline requires writes to `0x2006a9b0`, a protected SRAM-text address, and device evidence records `CFSR=0x82` / `MMFAR=0x2006a9b0` during the first `mw` store.

The exact IDB does contain a better static stage-0 candidate: `0x200cb400`, inside the unassigned RAM interval `0x200cb3d4..0x200db418` between the end of BSS and the start of Kmem. The candidate has 0x1000 bytes, is 32-byte aligned, and leaves the existing SRAM-text cave untouched. Unlike the earlier startup-zeroed candidate, this gap is not assumed to retain zeroes after boot; the production profile therefore skips the original-word preflight and restoration for this explicitly unassigned stage-0 workspace. It is outside the explicit SRAM-text ROX region and remains `STATIC_CANDIDATE` until a device probe confirms the live MPU policy and runtime ownership.

## Recovered call chain

```text
NSH exec @ 0x0c1c9529
  -> initial BLX target call (r0 is the preceding "Calling %p" print result)
  -> old SRAM-text mailbox trampoline at 0x2006a9b0 [rejected]
  -> new unassigned-RAM stage-0 candidate at 0x200cb400 [static candidate]
  -> Kmem malloc(stage1_size + 31)
  -> Lua aligns the returned block interior to a 32-byte stage-1 image base
  -> mw writes stage-1 into the aligned Kmem block
  -> MPU configuration of the aligned image
  -> exec(stage1 + 1)
  -> stage-1 reads stage-2 and invokes it
  -> stage-2 loads /data/canopus/supervisor.elf and configures ELF regions
```

The code path is in `watchfaces/canopus-installer-prod/xiaomi-band-9/main.lua` and `manager/target/band9/canopus_band9_stage1.c` / `canopus_band9_stage2.c`.

## Exact static observations

| Item | Address | Finding |
|---|---:|---|
| NSH `exec` handler | `0x0c1c9528` | Parses a callable and invokes it with BLX. It does not supply a caller-controlled AAPCS argument frame. |
| Default `memalign` wrapper | `0x0c16ab8d` | Two-argument Umem wrapper; a device retest returned `0` for the stage-1 request, so it is retained for stage-2 only. |
| Kmem `malloc` wrapper | `0x0c16af05` | One-argument wrapper over the initialized Kmem heap at `dword_2007D5D4`; used for the outer stage-1 workspace. |
| Kmem `free` wrapper | `0x0c16a4b5` | One-argument Kmem release wrapper for the original, pre-alignment stage-1 pointer. |
| MPU region allocation | `0x0c5228a5` | Allocates one of eight tracked MPU regions. |
| MPU configuration helper | `0x0c52272d` | Validates region `< 8`, base alignment, length `> 0x1f`, and attributes before publishing RNR/RBAR/RLAR with DSB/ISB. |
| Direct MPU publisher | `0x0c5226e8` | Writes `0xe000ed98`, `0xe000ed9c`, and `0xe000eda0`; temporarily disables IRQs around publication. |
| SRAM-text protection | `0x0c52293c` | Allocates a region and configures `0x200686c0`, length `0x2300`, which contains the prior cave. |

The only static code xrefs to the configuration helper are MPU initialization/protection paths (`0x0c5227d4` and `0x0c52293c`). The initial table passed by `0x0c522a6c` contains regions at `0x00000000/0x20000`, `0x0025c000/0x160000`, and `0x00000000/0xd02000`; none covers the `0x200cb400` candidate. The SRAM-text protector explicitly covers only `0x200686c0..0x2006ade0`. The fast-RAM branch receives a zero length (`0x2015fe80/0`), so it publishes no additional range. This proves that the candidate is outside every recovered explicit MPU range; the live background policy remains a device-side validation item.

## Ordinary-RAM candidate

The exact IDA survey reports these RAM allocations:

| Range | Segment/allocator | Interpretation |
|---|---|---|
| `0x20068000..0x200686c0` | Startup vector table RAM | Used by VTOR; do not overwrite. |
| `0x200686c0..0x2006ade0` | Startup-copied SRAM text | Explicitly protected ROX; old cave is here and is rejected. |
| `0x2006ade0..0x2006be60` | Startup zero-fill interval | Runtime-owned/ambiguous after initialization; earlier candidate rejected after allocation preflight failure. |
| `0x2006be68..0x200cb3d4` | DATA + BSS | Occupied firmware state; do not overwrite. |
| `0x200cb3d4..0x200db418` | No IDA segment/global and outside the recovered Kmem interval | 0x10044-byte unassigned gap. Candidate stage-0 base: `0x200cb400`; candidate span: `0x1000`; original contents are intentionally not assumed. |
| `0x200db418..0x2015f27c` | Kmem heap | Kernel allocator-owned; do not use as a blind fixed address. |
| `0x2015f280..0x2015fe80` | MSP_STACK | Boot/main stack; do not overwrite. |
| `0x3c72bc40..0x3cff0000` | Umem heap | User allocator-owned; writable through normal heap ownership, but not a blind fixed stage-0 location. |

The startup clear loop explains why `0x2006ade0..0x2006be60` looked attractive, but the device result shows that static zero-fill is not a sufficient runtime-ownership proof. The selected gap is tracked separately and the generated profile sets `cave_original_known = false`, so the stage-0 path does not read or restore presumed contents. The device fault register dump reports `CONTROL=0x0000000c`, so the crashing `system -c mw` context is privileged (`nPRIV=0`). The old cave failed because its explicit MPU region is read-only; that does not imply the unassigned gap is read-only. The selected candidate is outside the recovered explicit ROX SRAM-text region. Its RW property is supported by the privileged caller and allocator boundary; the updated stage-0 path explicitly supplies X through a temporary MPU region before executing the mailbox.

The first device retest of the gap candidate reported `stage-1 allocation failed: stage-0 callback returned result sentinel`. The sentinel is the literal result address left in the mailbox when the trampoline's final store is not observed; it does not prove that the callback body was reached. This distinguishes the earlier startup-zeroed candidate failure from a real allocator result: the candidate was writable enough for the mailbox image, but the split `mw`/`exec` task contexts did not preserve the temporary execute mapping.

A subsequent retest after correcting the shifted mailbox literal offsets returned `result=0`. The exact `sub_C16AB8C` body returns zero when its Umem allocation path cannot produce a block. The production path therefore moves only the outer stage-1 workspace to the exact Kmem `malloc` wrapper, requests an extra 31 bytes, aligns an interior image base to 32 bytes, and frees the original Kmem pointer after stage-1 execution. Stage-2 keeps using Umem `memalign` because its ELF image still requires the existing two-argument allocator ABI.

The stage-0 implementation now explicitly configures an otherwise unused MPU slot before every mailbox call:

```text
stage0 region: 7
stage0 executable span: 0x200cb400..0x200cb43f (0x40 bytes)
RBAR: 0x200cb406  (base | access=1 encoding)
RLAR: 0x200cb423  (limit | mem_attr=1 | enable)
result word: 0x200cb440  (outside the ROX span, remains writable)
```

The mailbox starts with DSB/ISB, then loads its two arguments and callback, and stores an entry marker (`0xA5A5A5A5`) into `0x200cb440` before BLX. Before each mailbox execution, Lua initializes that word to its own address as a sentinel. After the callback returns, the stage-0 MPU slot is disabled before `mw` reads the result; an unchanged sentinel means that the callback entry store was not observed, the entry marker means the callback did not return, and `result=0` means the callback actually returned zero. Region 7 is intentionally used as a temporary untracked slot; the firmware allocator can still return its tracked free slot for stage-1's own executable mapping, so the installer must reject that collision before configuring stage-1. Stage-1's mapping, stage-0 barrier, stage-0 release, and stage-1 entry are likewise chained in one NSH command context. This removes the default-map XN assumption while preserving the device check for live region ownership and MPU publication.

## Device evidence binding

The rejected write is recorded in `EVID-LOADER-9132-001.json`:

```text
attempted write: 0x49044803 -> 0x2006a9b0
faulting task: system -c mw
faulting instruction: sub_C1CC340+0x82 (0x0c1cc3c2)
CFSR: 0x00000082
MMFAR: 0x2006a9b0
```

This establishes that the candidate is executable SRAM-text but not writable in the NSH command task. It does not characterize other heap/data regions as executable.

## Completed non-device safeguards

- The production profile is marked `device_status = 'STATIC_CANDIDATE'`; the old cave remains rejected in `EVID-LOADER-9132-001.json`.
- The production Run handler accepts this `STATIC_CANDIDATE` for device validation and hard-blocks `DEVICE_REJECTED` profiles.
- `scripts/lua/test_installer_firmware_selection.lua` clicks Run with a `DEVICE_REJECTED` profile and asserts that no NSH command is emitted.

## SVC result

The reset path copies the first 94 vector entries from `0x0c101960` to RAM at `0x20068000` and sets VTOR to that RAM vector table. The SVC vector (exception 11) resolves to `0x0c100085`, the Thumb entry of `sub_C100084`, an infinite loop. Thus this image does not expose an SVC dispatcher through the architectural SVC exception vector; `SVC #0` cannot provide the proposed public task/MPU/copy bootstrap path.

## External-payload-service sweep

### OTA/mass: confirmed file-only path

The exact IDB has a complete external-data to asynchronous file-write chain:

```text
0x0c561634 (mass packet ingress)
  -> 0x0c560e58
  -> 0x0c214b60(buffer, length, completion callback)
  -> 0x0c534c18 (copy)
  -> 0x0c212b9c (asynchronous completion)
  -> 0x0c1d7084 (file state machine)
  -> 0x0c3933e0 / 0x0c39349e (file write)
```

A separate OTA message path is also proven:

```text
0x0c38deb0 -> 0x0c2d70dc(message + 4) -> 0x0c534c18 -> 0x0c391974
```

It migrates data under `/data/mass` and `/data/mass/silent_ota/`. Both chains terminate in copy/VFS/asynchronous file handling. Neither reaches DMA, MPU configuration, or an executable callback.

### Other external service results

| Surface | Confirmed route | Bootstrap result |
|---|---|---|
| Resource package | Only shared VFS/file-operation infrastructure | No independent ingress or DMA/MPU/exec chain found. |
| NFC | Internal firmware loader `0x0c23aa48` reads `/vendor/nfc/thn31_fw.bin` or `/vendor/nfc/sn100.esfwu` | No application/Lua/NSH-accessible entry or MPU/DMA/exec chain found. |
| RPMsg | `0x0c227070` reads fixed `/dev/logrpmsg` | Log-device path only; no general message ingress or copy/DMA/MPU/exec chain found. |
| DMA | HAL wrappers beginning at `0x0c5308b4` | Callers are internal peripheral/audio/NAND/SPI drivers; no OTA/resource/NFC/RPMsg/NSH path reaches them. |
| Lua | Lua code/string presence | No registration/callback chain to the above service handlers found. |

This turns the earlier string-led pass into a call-chain result: OTA/mass is the only confirmed externally supplied buffer path, and it is file-only. It cannot bootstrap an executable mapping in the recovered call graph.

## Resulting stage-0 status

The first requested replacement bootstrap target is now identified and wired into the generated artifacts:

```text
stage0.kind          = unassigned_privileged_ram_gap
stage0.base          = 0x200cb400
stage0.size          = 0x1000
stage0.result_word   = 0x200cb440
stage0.original_known = false
stage0.status        = STATIC_CANDIDATE
stage1.outer_alloc   = Kmem malloc(0x200 + 31), 32-byte interior alignment
stage1.outer_free    = Kmem free(original_pointer)
```

The second requested target was also fully audited: OTA/mass supplies a trusted copy and asynchronous file state machine, but the exact call graph terminates at VFS/file write and never reaches MPU, DMA, or executable callback. No independent resource-package, NFC, or RPMsg path provides a stronger copy-to-exec route. The candidate therefore removes the old cave dependency; the only remaining gate is live confirmation that the privileged background map permits instruction fetch from the unassigned interval.

The production installer accepts this explicit `STATIC_CANDIDATE` for the requested device validation; `DEVICE_REJECTED` remains hard-blocked. The target profile is intentionally not promoted to `DEVICE_PROVEN` by static analysis.
