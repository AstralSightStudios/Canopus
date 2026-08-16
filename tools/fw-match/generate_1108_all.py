#!/usr/bin/env python3
"""Generate 11-108 target-pack symbol records from the confirmed 036->1108
mapping table.

Each mapping was verified by decompiling the exact 4.100.108 IDB (string
references, source paths, command numbers, LVGL v9 class/API semantics). See
`confirmed_mappings.json` for the evidence basis.

Symbols are recorded as STATIC_RECOVERED / PENDING (fail-closed): no public
callable is generated until each is independently reviewed and APPROVED.
"""
import json
from pathlib import Path

TARGET = "xiaomi-band-11-4.100.108"
FW_SHA = "9315ca353f624cec25dfcfc98a95ba959e2d7b24573bf1d6adf16ea10341bd99"
ROOT = Path("targets/xiaomi-band-11-4.100.108")
OUT = ROOT / "symbols"

# (name, domain, prototype, notes)
SYMBOLS = [
    # NuttX VFS / kernel
    ("ioctl", "nuttx", "int32_t(int32_t, uint32_t, uintptr_t)",
     "NuttX fs_ioctl dispatch; references ../../nuttx/fs/vfs/fs_ioctl.c:52"),
    ("unlink", "nuttx", "int32_t(const char *)",
     "NuttX fs_unlink; references ../../nuttx/fs/vfs/fs_unlink.c:84"),
    ("rename", "nuttx", "int32_t(const char *, const char *)",
     "NuttX fs_rename; references ../../nuttx/fs/vfs/fs_rename.c:534"),
    ("lseek", "nuttx", "int64_t(int32_t, int64_t, int32_t)",
     "NuttX fs_lseek; references ../../nuttx/fs/vfs/fs_lseek.c:64"),
    ("close", "nuttx", "int32_t(int32_t)",
     "NuttX close; fs_files.c putfilep path"),
    ("sem_wait", "nuttx", "int32_t(void *)",
     "NuttX sem_wait; references ../../nuttx/sched/semaphore/sem_wait.c"),
    ("sem_trywait", "nuttx", "int32_t(void *)",
     "NuttX sem_trywait; references ../../nuttx/sched/semaphore/sem_trywait.c"),
    ("sem_post", "nuttx", "int32_t(void *)",
     "NuttX sem_post; references ../../nuttx/sched/semaphore/sem_post.c"),
    ("heap_free", "nuttx", "void(void *)",
     "NuttX heap free (16-byte wrapper)"),
    ("heap_malloc", "nuttx", "void *(uint32_t)",
     "NuttX heap malloc (30-byte wrapper)"),
    ("heap_zalloc", "nuttx", "void *(uint32_t)",
     "NuttX heap zalloc (30-byte wrapper)"),
    ("unregister_driver", "nuttx", "int32_t(const char *)",
     "NuttX unregister_driver; fs tree remove"),
    ("driver_open_dispatch", "nuttx", "int32_t(file *, const char *, int32_t)",
     "NuttX file-open dispatch; fops+0x04"),
    ("driver_read_dispatch", "nuttx", "int32_t(file *, void *, uint32_t)",
     "NuttX file-read dispatch; fops+0x08"),
    ("driver_ioctl_dispatch", "nuttx", "int32_t(file *, int32_t, uintptr_t)",
     "NuttX file-ioctl dispatch; fops+0x14"),
    # LVGL v9
    ("lv_bar_create", "ui", "void *(void *)",
     "LVGL v9 lv_bar_create (class 'bar')"),
    ("lv_bar_set_range", "ui", "void(void *, int32_t, int32_t)",
     "LVGL v9 lv_bar_set_range (sets min/max)"),
    ("lv_bar_set_value", "ui", "void(void *, int32_t, uint32_t)",
     "LVGL v9 lv_bar_set_value"),
    ("lv_image_create", "ui", "void *(void *)",
     "LVGL v9 lv_image_create (class 'image')"),
    ("lv_image_set_src", "ui", "void(void *, const void *)",
     "LVGL v9 lv_image_set_src; references lv_image.c:161/172/184"),
    ("lv_timer_create", "ui", "void *(canopus_lvx_event_cb, uint32_t, void *)",
     "LVGL v9 lv_timer_create; allocs timer, inserts lv_timer_ll"),
    ("lv_timer_del", "ui", "void(void *)",
     "LVGL v9 lv_timer_del; _lv_ll_remove + free"),
    ("lv_obj_set_style_bg_opa", "ui", "void(void *, uint32_t, uint32_t)",
     "LVGL v9 style property setter wrapper"),
    # app-registry / launcher
    ("app_install", "app_registry", "int32_t(void *, const void *, int32_t)",
     "Stock app-registry install; 'app_install'/'free_app' strings"),
    ("app_launcher_add", "launcher", "int32_t(void)",
     "Launcher add icon; '[%s] %s: add icon'"),
    ("app_launcher_del", "launcher", "int32_t(void *)",
     "Launcher del app; '[%s] %s: del app id'"),
    ("app_launcher_data_init", "launcher", "int32_t(void)",
     "Launcher data init; app_launcher_data_init strings"),
    ("hidden_and_show_app_cb", "launcher", "int32_t(const void *)",
     "Launcher hidden/show callback; appid 26/27"),
    ("protobuf_set_ordered_app_list", "launcher", "int32_t(const void *)",
     "Launcher ordered-app-list setter"),
    ("quickapp_register_app", "miwear", "int32_t(const void *, const void *)",
     "Quickapp register; 'quickapp_register_app' strings"),
    ("page_finish", "activity", "int32_t(void)",
     "Activity page finish"),
    ("page_goto", "activity", "int32_t(void)",
     "Activity page goto"),
    # BT
    ("bt_adapter_register", "bluetooth", "int32_t(void *, const uint32_t *)",
     "BT adapter register; allocs+registers via sub_C466330"),
    ("bt_adapter_get_state", "bluetooth", "int32_t(void *)",
     "BT adapter get_state; command 27"),
    ("bt_adapter_get_scan_mode", "bluetooth", "int32_t(void *)",
     "BT adapter get_scan_mode; command 31"),
    ("bt_adapter_set_scan_mode", "bluetooth", "int32_t(void *, int32_t)",
     "BT adapter set_scan_mode; command 41"),
    ("bt_pair_request_reply", "bluetooth", "int32_t(void *, const uint8_t *, int32_t)",
     "BT pair request reply; command 97"),
    ("bt_socket_server_receive", "bt-server", "int32_t(void *, void *, int32_t)",
     "BT socket server receive"),
    ("btsnoop_avdtp_recognizer", "bt-util", "int32_t(const void *, int32_t)",
     "BTSNOOP AVDTP recognizer"),
    ("controller_crash_dump", "bt-vendor", "int32_t(void)",
     "BT controller crash dump; busfault register dump"),
    ("core_bt_pair_request_callback", "bluetooth", "int32_t(void *, void *)",
     "GAP pair request callback"),
    ("vendor_hci_transport_register", "bt-vendor", "int32_t(const void *)",
     "Vendor HCI transport register"),
    # service manager
    ("service_manager_register", "service", "int32_t(const void *)",
     "BT service-manager register"),
    ("service_manager_get_profile", "service", "int32_t(int32_t)",
     "BT service-manager get-profile"),
    ("service_manager_startup", "service", "int32_t(void)",
     "BT service-manager startup"),
    ("service_manager_shutdown", "service", "int32_t(void)",
     "BT service-manager shutdown"),
    # miwear
    ("watchface_manager_delete_watchface", "watchface", "int32_t(const void *)",
     "Watchface manager delete"),
    ("offload_property_apply", "bt-adapter", "int32_t(void *)",
     "Adapter offload property apply"),
    # lvx (Xiaomi wrappers)
    ("lvx_page_title_create", "ui", "void *(void *, const char *, uint32_t, canopus_lvx_event_cb, void *)",
     "Stock LVX page-title prefab; 'lvx_page_title_create' strings"),
    ("lvx_notification_insert_message", "notification", "int32_t(const void *)",
     "Stock LVX notification insert; 'lvx_notification_insert_message' strings"),
    ("lvx_msgbox_create", "ui", "void *(void *, void *)",
     "Stock page-owned msgbox prefab"),
    ("system_router_get_pages_wrapper", "aiotjs", "int32_t(void)",
     "System router get-pages wrapper"),
]


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    mappings = json.loads((ROOT / "confirmed_mappings.json").read_text())["by_name"]
    written = 0
    for name, domain, proto, notes in SYMBOLS:
        addr = mappings.get(name)
        if not addr:
            print(f"SKIP {name}: no confirmed mapping")
            continue
        entry = addr
        callable_addr = f"0x{int(entry, 16) | 1:x}"
        record = {
            "schema": 1,
            "symbol_id": f"{TARGET}.{domain}.{name}",
            "target_id": TARGET,
            "name": name,
            "kind": "function",
            "instruction_set": "thumb",
            "entry_address": entry,
            "callable_address": callable_addr,
            "prototype": proto,
            "calling_convention": "arm-aapcs",
            "contexts": {"allowed": ["target-private-thread"], "blocking": True},
            "ownership": {"argument": "borrowed", "return_value": "status"},
            "side_effects": ["mutates firmware UI/OS state"],
            "proof": {
                "static": "recovered",
                "device": "not_probed",
                "host_tested": False,
                "evidence_ids": [f"EVID-1108-{name}"],
            },
            "policy": "restricted",
            "status": "STATIC_RECOVERED",
            "provenance": {
                "firmware_sha256": FW_SHA,
                "evidence_ids": [f"EVID-1108-{name}"],
                "source": "multi-layer matcher + decompile-confirmed in exact 4.100.108 IDB (see confirmed_mappings.json)",
            },
            "notes": notes,
            "approval_state": "PENDING",
        }
        path = OUT / f"{TARGET}.{domain}.{name}.json"
        path.write_text(json.dumps(record, indent=2) + "\n")
        written += 1
        print(f"wrote {path.name}")
    print(f"\n{written} symbols generated")


if __name__ == "__main__":
    main()
