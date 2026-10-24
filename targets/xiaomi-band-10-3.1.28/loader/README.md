# Xiaomi Band 10 3.1.28 loader

**Status: STATIC ONLY / NOT_PROBED.** No loader has been run on this image.

- Image: `fwbins/xiaomi-band-10-3.1.28/vela_ap.bin`, SHA-256 `fb726472…8845`
  (o66tc, 7,515,648 bytes).
- UI family: **LVGL v8**. The v9 Manager backend does not apply; a dedicated v8
  backend is required before any UI capability is claimed.
- Selected loading style: **Band-11-style Lua-owned staged loader** — PIC stage1,
  stage2 with the portable ELF32 loader, Supervisor as `ET_REL`. No `insmod`.
- No Lua-level `os.execute` recovery is planned for this firmware.
- The Band 9 loader evidence is a reference only; the o66 memory/MPU/cache
  primitives still need exact recovery.

Stage1/stage2, installer staging and device gates are not implemented yet. Symbol
records are `restricted`/`PENDING`; no approval or device proof exists.
