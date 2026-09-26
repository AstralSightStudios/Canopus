# P65 G0 probe watchface

This is a one-shot, manual watchface for the P65 **G0 loader smoke test**. It bundles a tiny ARM ET_REL module with no undefined symbols, heap use, Canopus API calls, or Manager/UI registration.

The watchface does not run shell commands on open. Tap **CHECK MODULES**; it reads `ro.build.version` and checks `lsmod`, and refuses loading unless the reported version is `3.100.043`. Then tap **LOAD G0 PROBE** and **UNLOAD PROBE**. Both the watchface `os.execute` bridge and `/tmp` redirection remain untested on P65 hardware. If a command fails or the result is uncertain, it disables further load/unload actions and instructs the operator to reboot before retrying. A probe already present before this watchface loaded is cleanup-only; unloading it does not count as G0 pass.

## Build

```sh
bash watchfaces/p65-g0-probe/build.sh
```

The script cross-builds `src/p65_g0_probe.c`, links `e_entry` to `canopus_module_initialize`, runs the P65 target verifier, and creates:

- `main.lua`
- `p65_g0_probe.bin`
- `build/p65-g0-probe-resources.zip` — a flat resource bundle for the existing watchface packer, **not a flashed watchface firmware image**.

Use the same watchface installation/packing workflow used for other LuaLVGL faces. Keep the two root resources together. The device-side command is the P65 NSH form `insmod <resource-path> canopus_g0_probe`; unload is `rmmod canopus_g0_probe`.

This is a test face, not the Canopus Manager and not proof of G0 until the device actually reports load, `lsmod` presence, clean unload, and `lsmod` absence without a crash. It is P65/3.100.043 only. Do not retry after an uncertain result; reboot first.
