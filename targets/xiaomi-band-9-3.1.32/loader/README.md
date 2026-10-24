# Xiaomi Band 9 3.1.32 loader

**Status: STATIC ONLY / NOT_PROBED.** No loader has been run on this image.

- Image: `fwbins/xiaomi-band-9-3.1.32/vela_ap.bin`, SHA-256 `9c02dab4…3264` (n66).
- UI family: **LVGL v8**. The Band 11 / Band 10 Pro v9 Manager backend does not
  apply. A dedicated v8 backend must be restored/written before any UI capability
  is claimed; the old one was removed with tag `band9-frozen`.
- Selected loading style: **Band-11-style Lua-owned staged loader** — a small
  PIC stage1, a stage2 with the portable ELF32 loader, then the Supervisor as an
  `ET_REL` module. This firmware has no `insmod` path; do not use one.
- The Lua bootstrap does **not** need the `.139` `os.execute` recovery trick.
- `bootstrap.toml` is the historical **NSH `mw`/`exec`** profile. It is kept only
  as evidence: the device rejected its SRAM cave write (`CFSR 0x82`,
  `MMFAR 0x2006a9b0`) and later never published `/dev/canopus`
  (`EVID-LOADER-9132-001`, `EVID-STAGE0-9132-002/003`). Do not reactivate it.

The stage1/stage2 memory adapter, installer staging and device gates are not yet
implemented for this target. Symbols in `symbols/` are exact-image records with
`restricted`/`PENDING` policy; no approval or device proof is carried.
