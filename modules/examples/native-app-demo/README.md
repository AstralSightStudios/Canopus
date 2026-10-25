# Native App Demo (`native_demo`)

An end-to-end example of a Canopus target module carrying a full native application, designed for Xiaomi Band 10 Pro (`xiaomi-band-10-pro-3.101.043` / `036`).

## Overview
This module demonstrates:
1. **Supervisor-Owned Native Registration**:
   - Uses `CANOPUS_SVC_LAUNCHER` version 1 and submits its writable exact-target page through `ctx->launcher->register_app` during publication stage 1.
   - The Supervisor assigns and persists the app ID for `native_demo`, uses package `com.canopus.module.native_demo`, and performs firmware install and launcher addition on separate UI ticks.
   - Publication stage 2 navigates to the assigned app ID. The demo never chooses an ID or calls firmware application install/launcher-add itself.
   - The page descriptor and callbacks stay resident until reboot; the temporary page pointer array is copied by the service.
2. **Declarative & Firmware-Native UI Widgets**:
   - Page Title Header: `FW_LVX_PAGE_TITLE_CREATE`
   - Scrolling Viewport: `FW_LVX_CONTENT_CREATE`
   - Subtitle Labels: `FW_LVX_LABEL_CREATE` / `FW_LVX_LABEL_SET_TEXT`
   - List & Status Rows: `FW_LVX_LIST_ROW_CREATE` (showing Device, Run Count, Battery)
   - Interactive Action Rows: `FW_LVX_EVENT_ADD` triggering haptic click feedback
3. **Microkernel Service Objects**:
   - `ctx->system`: reads device model and battery percentage
   - `ctx->kv`: persistent persistent key-value store for activation counters and clicks
   - `ctx->fs`: sandboxed file I/O writing activation logs
   - `ctx->log`: structured logging
   - `ctx->haptics`: tactile vibration presets

## Build & Package
```bash
canopus build --manifest modules/examples/native-app-demo/Canopus.toml
canopus package --manifest modules/examples/native-app-demo/Canopus.toml
```

## Mirage Emulator Verification
1. Push `native_demo.cmi.next` and `native_demo.ko.next` into `/data/canopus/inbox/`.
2. Run `lua /data/canopus/driver.lua install native_demo` -> result = 5 (COMPLETED).
3. Enable module: `lua /data/canopus/driver.lua cmd enable 1`.
4. Reboot device or invoke `lua /data/canopus/driver.lua cmd restore`.
5. Verify `/data/canopus/probe.txt` and `/data/canopus/ui_probe.txt` contain `UI_PAGE_ACTIVE_OK`.
