# REDMI Watch 6 3.100.043 loader

**Status: STATIC ONLY / NOT_PROBED.** This image has never been deployed by Canopus.

- Image: `fwbins/xiaomi-p65-3.100.043/vela_ap.bin`, SHA-256 `a19b6014…5858`
  (p65, best1502p, 11,793,540 bytes).
- UI family: **LVGL v9**, same generation as Band 10 Pro; the v9 backend family
  applies (no new v8 backend needed).
- Selected loading style: **Band-11-style Lua-owned staged loader** — PIC stage1,
  stage2 with the portable ELF32 loader, Supervisor as `ET_REL`. No `insmod`.
- No Lua-level `os.execute` recovery is planned for this firmware.
- History: a 2026-09 experimental P65 deployment boot-looped to the Logo and
  required a REC data wipe; the cause was never established (ADR-CAN-007). The
  old target was removed. This is a new static pack; treat any device test as a
  fresh, gated experiment with a known recovery path.

Stage1/stage2, installer staging and device gates are not implemented yet. Symbol
records are `restricted`/`PENDING`; no approval or device proof exists.
