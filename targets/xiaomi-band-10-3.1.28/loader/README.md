# Xiaomi Band 10 (o66) 3.1.28 loader

**Status: EMULATOR-VERIFIED / DEVICE NOT PROBED.** The staged loader reaches the
Supervisor, registers `/dev/canopus`, completes the installer CPC2 sequence, and
installs the Manager app / launcher entry in Mirage (`EVID-EMU-1028-001`). No
physical device run exists.

- Image: `fwbins/xiaomi-band-10-3.1.28/vela_ap.bin`, SHA-256 `fb726472…8845`.
- UI family: **LVGL v8**. Shared v8 backend
  (`manager/target/lvgl_v8/canopus_manager_target_lvgl_v8.c`), app descriptor
  layout `CANOPUS_MGR_APPINFO_1028` (callback +0x1c, hidden +0x3c, 0x40 bytes).
- Loader: cave entry (`entry_mode = "cave"`), cave `0x200E0000`, stage 1 at
  `+0x100`, Umem resident image. No `insmod`; no Lua `os.execute` recovery shim.
- Build: Supervisor 112,956 bytes / 149,954 resident (verifier PASS); stage1 425
  bytes, stage2 3,580 bytes.

## Mirage path (2026-10-25)

1. The registered package `366340003` was replaced with the rebuilt `main.lua`,
   profile, stage1/stage2 and Supervisor. This firmware's Lua surface does not
   dispatch button touches, so the run used the page's `AUTORUN` diagnostic
   (set only in the installed test copy).
2. Stage 0 runs the cave trampoline (LR-preserving), stage 1 from the cave,
   stage 2 from Kmem, the Supervisor from a Umem image, and `/dev/canopus`
   registers. The installer's CPC2 steps all return result 5 and the page shows
   `Run completed`.
3. `/data/canopus/registry.bin` = `CDR1`.
4. The live Supervisor target record has identity/app-install/launcher results
   all 0, so the Manager app and its launcher entry were installed.

## Corrections proved by this run

- `app_install` is `0xc4737e8` (app_lookup, 0x40-byte record, descriptor copy);
  the previous record was a text-layout routine.
- `unregister_driver` is `0xc3938c0` (inode lock/remove/unlock); the previous
  record was a two-argument VFS wrapper.
- The app descriptor uses the 3.1.28 layout: metadata callback at +0x1c, owned
  strings at +0x14/+0x18, hidden flag at +0x3c, 0x40 bytes total.
- The cave trampoline preserves the NSH exec caller's LR.

## Open items

- The Lua watchface surface does not dispatch button touches on this firmware;
  a touch-driven run still needs a fix or a physical device.
- The LVGL object-creator aliases are not disambiguated for this build; the
  Manager page has not been opened yet (`create_count` stays 0).

All symbol records remain restricted/PENDING; no approval or device proof
exists.
