# Xiaomi Band 9 Pro 3.1.190 loader

**Status: BUILD-ONLY / MIRAGE RUN INCOMPLETE.** The Supervisor builds and
verifies, and the loader enters stage 0 in Mirage, but the run crashes with a
usage fault before the Supervisor is registered.

- Image: `fwbins/xiaomi-band-9-pro-3.1.190/vela_ap.bin`, SHA-256 `3e264bc2…f82f`.
- UI family: **LVGL v8**, shared v8 backend.
- Loader profile: cave `0x20080000` (unassigned low-RAM gap below DATA), exact
  exec/mw handlers, exact Kmem allocators and MPU helpers, and the type-1
  character-driver registration `0xc4f03f0`.
- Build: Supervisor 112,960 bytes / 149,958 resident, verifier PASS; stage1 373
  bytes, stage2 3,584 bytes.

## Mirage (2026-10-25)

The complete package was reconstructed from the nested full `.bin` (the inner
table's 83 `/system/...` ZIP entries plus `vela_ap.bin`) and imported as
`miwear.watch.n67cn_v3.1.190_full_3e264bc2`; the stock watchface renders.

The installer face installs and runs, but tapping Run crashes:

```text
arm_usagefault: CFSR 00100000 (UNDEFINSTR / usage fault)
task: system, stack 0x3cffb400 size 0x3864
stack contains 0x20080001 (stage-0 cave +1)
```

The stack dump proves stage 0 was entered. The fault is reported as a stack
overflow in the `system` exec task before any canopus log line. Next step:
reduce the exec-task stack use (or execute from a task with a larger stack) and
recheck the cave publication order for this firmware.

All records remain restricted/PENDING; no approval or device proof exists.
