# Canopus Installer Prod

A streamlined Xiaomi Band 10 installer watchface with two controls:

- **Run** loads the exact-target Supervisor, applies enabled boot intents after
  `insmod` has returned, then executes native registration stages 0, 1, and 2.
- **Run** is one-shot for the lifetime of the loaded watchface: its first click
  locks the action immediately, including while it is running or after any
  failure. Reboot before retrying so native registration stages cannot be
  replayed in the same session.
- **Clear Env** requires two consecutive clicks. The first displays
  `Click again to clear`; only the second executes `rm -rf /data/canopus` and
  asks for a reboot.

`Run` uses LuaLVGL's `lvgl.Timer`. The restore operation and each INSTALL stage
run in separate timer callbacks, returning to the LVGL/miwear event loop between
native registration transactions. It never loads a third-party module from the
Supervisor constructor's restricted stock-loader stack.

The watchface reads `ro.build.version` with `getprop` while opening and derives
`canopus_supervisor-xiaomi-band-10-pro-<version>.bin`. Multiple exact firmware
artifacts may be bundled together. If the detected version has no matching valid
Supervisor resource, the page creates neither button and displays only
`Firmware version not supported` with the detected value.

The selected versioned Supervisor is exact-firmware-specific. A LOAD failure
instructs the user to verify that the firmware version matches the installer.
Do not retry LOAD or Run without rebooting after a partial failure.

Build and stage every firmware artifact intended for the package; later builds
preserve earlier versioned files:

```sh
CANOPUS_TARGET=xiaomi-band-10-pro-3.101.030 \
  bash scripts/build_canopus_supervisor.sh
CANOPUS_TARGET=xiaomi-band-10-pro-3.101.036 \
  bash scripts/build_canopus_supervisor.sh
```

The build script copies each verified Supervisor under its full target ID into
this directory. It removes obsolete unversioned `canopus_supervisor.bin`
aliases. `manager_icon.bin` is staged to `/data/canopus/manager_icon.bin` before
native registration stage 0.
