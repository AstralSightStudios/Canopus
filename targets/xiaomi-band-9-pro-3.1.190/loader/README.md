# Xiaomi Band 9 Pro 3.1.190 loader

**Status: STATIC ONLY / NOT_PROBED.** No loader has been run on this image.

- Image: `fwbins/xiaomi-band-9-pro-3.1.190/vela_ap.bin`, SHA-256 `3e264bc2…f82f`
  (n67cn, 7,775,288 bytes, extracted from the full package).
- UI family: **LVGL v8**. The v9 Manager backend does not apply; a dedicated v8
  backend is required before any UI capability is claimed.
- Selected loading style: **Band-11-style Lua-owned staged loader** — PIC stage1,
  stage2 with the portable ELF32 loader, Supervisor as `ET_REL`. No `insmod`.
- No Lua-level `os.execute` recovery is planned for this firmware.
- The 3.1.175 loader notes under tag `band9-frozen` are a starting point only;
  every address must be re-recovered from the 3.1.190 IDB.

Stage1/stage2, installer staging and device gates are not implemented yet. Symbol
records are `restricted`/`PENDING` and never carry 3.1.175's approvals.
