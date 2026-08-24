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
2. Compare all eight SRAM cave words before every temporary write.
3. Stage `stage2.bin` and the exact-target Supervisor under `/data/canopus`.
4. Use NSH `mw` and `exec` to call the firmware allocator and MPU helpers.
5. Execute position-independent stage 1, which loads stage 2.
6. Stage 2 loads the verified Supervisor ELF32 `ET_REL` and invokes its
   constructor.
7. Restore the exact cave words and release temporary allocation/MPU ownership.

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
