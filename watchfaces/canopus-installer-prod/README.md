# Canopus Installer Prod

A streamlined Xiaomi Band 10 installer watchface with two controls:

- **Run** loads the exact-target Supervisor, applies enabled boot intents after
  `insmod` has returned, then executes native registration stages 0, 1, and 2.
- **Clear Env** executes `rm -rf /data/canopus` and asks for a reboot.

`Run` uses LuaLVGL's `lvgl.Timer`. The restore operation and each INSTALL stage
run in separate timer callbacks, returning to the LVGL/miwear event loop between
native registration transactions. It never loads a third-party module from the
Supervisor constructor's restricted stock-loader stack.

The bundled `canopus_supervisor.bin` is exact-firmware-specific. A LOAD failure
instructs the user to verify that the firmware version matches the installer.
Do not retry LOAD or Run without rebooting after a partial failure.

Build and stage the 3.101.036 artifact with:

```sh
CANOPUS_TARGET=xiaomi-band-10-pro-3.101.036 \
  bash scripts/build_canopus_supervisor.sh
```

The build script copies the verified Supervisor artifact into this directory.
`manager_icon.bin` is staged to `/data/canopus/manager_icon.bin` before native
registration stage 0.
