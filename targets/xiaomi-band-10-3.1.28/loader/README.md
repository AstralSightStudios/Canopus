# Xiaomi Band 10 (o66) 3.1.28 loader

**Status: BUILD-ONLY / NOT RUN.** The staged Supervisor builds and verifies; the
loader has not completed a Mirage run.

- Image: `fwbins/xiaomi-band-10-3.1.28/vela_ap.bin`, SHA-256 `fb726472…8845`.
- UI family: **LVGL v8**. Shared v8 backend
  (`manager/target/lvgl_v8/canopus_manager_target_lvgl_v8.c`).
- Loader profile: `bootstrap.toml` — cave `0x200E0000` (the 64 KiB gap between
  BSS and Kmem), exact exec/mw handlers, Umem default allocator pair
  (`mm_memalign_default 0xc12234c`, `mm_free_default 0xc11eee8`), exact MPU
  helpers and the type-1 character-driver registration `0xc5281d0`.
- Build: Supervisor 112,936 bytes / 149,934 resident, verifier PASS; stage1 425
  bytes, stage2 3,580 bytes.

## ABI state

`static_test_abi` is enabled. 28 v8/platform records are accepted for the
static-test build (`EVID-PORT-1028-V8-ACCEPT`); most carry candidate evidence
only. The LVGL object creators (`lv_obj_create` / `lv_img_create` /
`lv_bar_create`) are aliases and are **not** disambiguated — the Manager page
widgets may be wrong even when the control path works.

## Mirage observations (2026-10-25)

- The complete `o66tc_v3.1.28` package boots to the stock watchface in a
  `o66` device.
- The installer face installs with protocol `0x0800`, but this firmware's Lua
  surface did **not** dispatch button touches (Run and Clear Env both ignored).
  An `AUTORUN` diagnostic hook was added to the shared installer page (default
  off) so the loader can be exercised without touch; the o66 autorun run is
  still pending.
- No device proof exists.
