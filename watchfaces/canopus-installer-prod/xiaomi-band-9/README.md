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
   `0x200cb400`; the bootstrap temporarily maps its 64-byte code span as ROX
   through MPU region 7 and keeps the result word at `0x200cb440` outside it.
   The MPU writes and the following `exec` run in one NSH command context so a
   task switch cannot replace the temporary per-task MPU map before instruction
   fetch. Stage 1 uses the same single-context sequence for its executable map.
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
validation; `DEVICE_REJECTED` profiles remain hard-blocked. The current profile
is `STATIC_CANDIDATE` because the excluded device probe has not been run. The
previous SRAM-text cave at `0x2006a9b0` is rejected by device fault evidence and
is no longer used.

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
