# All Services & Hook Engine Test Suite (`services_test`)

A comprehensive on-device verification suite for Canopus microkernel Service Objects and the universal Cortex-M33 Hook Engine.

## Scope of Verification
The test suite exercises and validates 15 distinct functional areas directly on hardware/emulator:

1. **Universal Hook Engine**:
   - `CANOPUS_HOOK_INLINE`: Thumb-2 function prologue boundary decoding, instruction interception, trampoline execution back to the original function, return value mutation, and clean uninstallation.
   - `CANOPUS_HOOK_POINTER`: Function pointer slot interception, atomic pointer swapping, and uninstallation.
   - `is_hooked`: Accurate query of active hook states across lifecycle.
2. **Key-Value Store (`ctx->kv`)**:
   - `set_int` / `get_int` / fallback default on missing key.
   - `set_string` / `get_string` (exact length semantics).
   - `set_bool` / `get_bool`.
   - `remove` key and `clear` entire store.
3. **File System (`ctx->fs`)**:
   - `open`, `write`, `seek` (`CANOPUS_FS_SEEK_SET`), `tell`, `read`, `close`.
   - `write_all`, `read_all`, `stat`, `exists`, `remove`.
   - Directory enumeration: `opendir`, `readdir`, `closedir`.
4. **Memory Manager (`ctx->mem`)**:
   - `alloc` (4-byte alignment check).
   - `realloc` (preserving data integrity across expansion).
   - `calloc` (zero-filled memory verification).
   - `used` / `available` tracking (confirming the 128 KiB heap budget).
   - `free`.
5. **System Info (`ctx->system`)**:
   - `target_id`, `device_model`, `firmware_version`.
   - `battery_level`, `is_charging`, `is_safe_mode`, `framework_revision`, `uptime_ms`.
6. **Clock & Timers (`ctx->time`)**:
   - `monotonic_ms` monotonicity.
   - `sleep_ms`.
   - `timer_create`, `timer_start`, `timer_stop`, `timer_destroy`.
7. **Haptics Feedback (`ctx->haptics`)**:
   - `vibrate`, `vibrate_pattern`, `vibrate_preset` (`CANOPUS_HAPTIC_CLICK`), `cancel`.
8. **Display & Power (`ctx->display`)**:
   - `get_brightness`, `set_brightness`, `is_screen_on`, `get_resolution` (336x480).
   - `acquire_wake_lock`, `release_wake_lock`, `wake_screen`.
9. **Event Bus (`ctx->bus`)**:
   - `subscribe`, `publish` with synchronous payload delivery, `unsubscribe`.
10. **Bluetooth Layer 2 (`ctx->bt`)**:
    - `is_connected`, `get_rssi`, `adapter_state`, `l2_register_rx`, `l2_send`.
11. **Logging (`ctx->log`)**:
    - `info`, `warn`, `error`, `debug`, `write`.
12. **Notifications (`ctx->notify`)**:
    - `post`, `post_with_icon`.
13. **Declarative UI Builder (`ctx->ui`)**:
    - Full tree composition: `navigation_page`, `section`, `text`, `status_row`, `button`, `action_row`, `switch_row`.
    - Extended catalog: `CANOPUS_UI_NODE_SLIDER`, `CHECKBOX`, `DIVIDER`.
    - Transaction commit: `tree_commit` producing valid snapshot generations.

## Running Tests
```bash
canopus build --manifest modules/examples/all-services-test/Canopus.toml
canopus package --manifest modules/examples/all-services-test/Canopus.toml
```
Deploy the package to device `/canopus/inbox/` and install via `driver.lua install services_test`.
After `driver.lua cmd restore`, inspect `/data/canopus/test_report.txt` for results.
