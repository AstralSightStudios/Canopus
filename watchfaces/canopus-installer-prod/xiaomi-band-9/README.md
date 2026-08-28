# Canopus Installer Prod — Xiaomi Band 9

This production watchface is specific to Xiaomi Band 9 firmware `3.1.32` and its
192×490 LuaLVGL v8 environment. It intentionally does not reuse the Band 10 Pro
page or its stock `insmod` path.

The visible workflow remains identical to the Band 10 Pro production installer:

- **Run** is one-shot until reboot.
- **Clear Env** requires two consecutive clicks.
- Run restores enabled boot intents, registers Manager, registers module apps and
  pages, and publishes Launcher entries in separate miwear event-loop turns.

The loading path is device-specific:

1. Read and verify the exact `STATIC_RECOVERED` target loader profile.
2. Use the static stage-0 candidate in the unassigned RAM gap at
   `0x200cb400`; the bootstrap first publishes its 64-byte code span as RWX
   through MPU region 7, replaces the retained limit, then narrows it to RX. The
   result word at `0x200cb440` remains outside the mapped code span.
   RBAR is deliberately published with the firmware's writable access encoding:
   region 7 can still carry the shell task's old stack RLAR until the following
   write, and publishing ROX first would transiently make that stack read-only.
   After RLAR is written, RBAR is narrowed to RX and `exec 0x0c5226db` runs
   the firmware-bound `DSB SY; ISB SY; BX LR` tail before the first cave
   instruction is fetched.
   All writes, synchronization, and execution remain in one NSH command context.
   Stage 1 is first published RWX for the same transition safety, then narrowed
   to RX before synchronization and entry.
3. Stage `stage2.bin` and the exact-target Supervisor under `/data/canopus`.
4. Use NSH `mw` and `exec` to call the firmware Kmem allocator and MPU helpers.
   Stage 1 requests `payload.size + 31` bytes from Kmem, aligns the code image
   inside that block to 32 bytes for MPU placement, and releases the original
   Kmem pointer after execution. Stage 2 continues to use the exact Umem
   `memalign`/`free` pair for its own image.
5. Execute position-independent stage 1, which loads stage 2.
6. Stage 2 loads the verified Supervisor ELF32 `ET_REL` and invokes its
   constructor.
7. Release the temporary stage-0 MPU slot and stage-1 allocation/ownership;
   the unassigned stage-0 workspace is not restored because its original
   contents are intentionally unknown.

The production action accepts the explicit `STATIC_CANDIDATE` package for device
validation; `DEVICE_REJECTED` profiles remain hard-blocked. A device run of the
previous ROX-first publication failed with `CFSR=0x000000b2` and
`MMFAR=0x3c80874c`: IRQ 3 attempted to stack while the live shell stack was
transiently covered by the new read-only RBAR and the old region-7 RLAR. The
corrected RWX-first/RX-final, pre-entry-synchronized sequence remains `STATIC_CANDIDATE`
until it is retested on-device. The older SRAM-text cave at `0x2006a9b0` is also
rejected by separate device fault evidence and is no longer used.

Packaged resources use the same flat, exact-target naming scheme as the Band 10
Pro production watchface. Watchface packages cannot contain child directories or
more than one `.lua` file, so the generated profile and stage-1 Lua chunks are
packaged with `.bin` resource names and compiled explicitly by `main.lua`:

```text
xiaomi-band-9/
├── main.lua
├── manager_icon.bin
├── canopus_loader_profile-xiaomi-band-9-3.1.32.bin
├── canopus_stage1-xiaomi-band-9-3.1.32.bin
├── canopus_stage2-xiaomi-band-9-3.1.32.bin
└── canopus_supervisor-xiaomi-band-9-3.1.32.bin
```

Build and stage with:

```sh
CANOPUS_TARGET=xiaomi-band-9-3.1.32 \
  ./scripts/build_canopus_supervisor.sh
```

The loader profile and Supervisor are exact-firmware-bound. Host verifier and
smoke passes do not prove device execution; after any partial load failure,
reboot before retrying.
