# REDMI Watch 6 3.100.043 loader

**Status: STATIC ONLY + CAVE PRIMITIVE PROVEN / NOT_PROBED.** This image has
never been deployed by Canopus.

- Image: `fwbins/xiaomi-p65-3.100.043/vela_ap.bin`, SHA-256 `a19b6014…5858`
  (p65, best1502p, 11,793,540 bytes).
- UI family: **LVGL v9**, same generation as Band 10 Pro; the v9 backend family
  applies (no new v8 backend needed).
- Selected loading style: **Band-11-style Lua-owned staged loader** — PIC stage1,
  stage2 with the portable ELF32 loader, Supervisor as `ET_REL`. No `insmod`, no
  Lua-level `os.execute` recovery.
- History: a 2026-09 experimental P65 deployment boot-looped to the Logo and
  required a REC data wipe (ADR-CAN-007). Treat any device test as a fresh,
  gated experiment with a known recovery path.

## Mirage primitive proof (2026-10-25, `EVID-EMU-65043-001`)

- The injected NSH on a Mirage p65 device lists `mw` and `exec`.
- A four-instruction stub written at `0x2019e800` (gap between RAM8_BSS end
  `0x2019e720` and MSP_STACK start `0x2019f3e0`) returned after
  `exec 0x2019e801` and published `0x5a`. The default map executes unassigned
  SRAM, so the cave loader style is viable.

## Exact strings

- `firmware_version_string` = `0xc3bf9fd` ("3.100.043" inside the property
  blob at `0xc3bf9ec`).
- `firmware_build_string` = `0x0cbff458` ("d516af0:nx_best1502p_ap" in the XIP
  boot info). The old record `0xc3bfa25` pointed into the charger blob and was
  corrected.

## Remaining blockers

- **Platform ABI.** The target has no family and `CANOPUS_SUP_PLATFORM_COMPLETE`
  is 0; the v9 Supervisor sources need the FW_10/SUP_10 mapping set (41 UI
  symbols) plus the veneer allocator/MPU/inode records. Most p65 records are
  CANDIDATE (21 STATIC_RECOVERED of 314) and several key addresses (open/close
  are plausible, read/write are not) are still wrong.
- **Loader profile.** exec/mw handler addresses, inode primitives,
  Kmem/Umem allocator pairs and the MPU lease API are not recovered yet.
- **Lua surface.** The watchface Lua engine's `os.execute` path is untested on
  p65; the Facetory Lua-layer packaging applies as on the other v8/v9 faces.

All symbol records remain `restricted`/`PENDING`; no approval or device proof
exists.
