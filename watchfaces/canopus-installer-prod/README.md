# Canopus Installer Prod

Production installer watchfaces are packaged per device family. Do not merge the
family directories into one watchface: their LuaLVGL API, display geometry, and
Supervisor loading path differ.

- `xiaomi-band-10-pro/`: 336×480, LVGL v9, exact-target stock `insmod`.
- `xiaomi-band-9/`: 192×490, LVGL v8, exact-target NSH `mw`/`exec` stage-1 and
  stage-2 loader. It currently packages only `xiaomi-band-9-3.1.32`.

Both variants preserve the same production workflow:

1. **Run** loads the exact-target Supervisor once.
2. It restores enabled boot intents.
3. It runs INSTALL stages 0, 1, and 2 in separate miwear event-loop turns.
4. **Clear Env** requires two clicks and removes `/data/canopus` only after the
   explicit confirmation.

Build all packaged targets with:

```sh
./scripts/build_prod_all.sh
```

Each verified artifact is staged only into its matching family directory. Host
builds and Lua smoke tests are not device proof; reboot is still required before
retrying any partial Supervisor load or native registration failure.
