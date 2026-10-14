# Native App Demo (`native_demo`)

An end-to-end example of a Canopus target module carrying a full native application, designed for Xiaomi Band 10 Pro (`xiaomi-band-10-pro-3.101.043` / `036`).

## Overview
This module demonstrates:
1. **Native Application Descriptor Registration**:
   - Registers with the firmware app registry (`FW_APP_INSTALL`) and launcher (`FW_LAUNCHER_ADD`).
   - App ID: `0x00CB`, package: `com.canopus.native.demo`.
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
