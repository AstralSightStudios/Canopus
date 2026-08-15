#!/usr/bin/env python3
"""Generate 11-108 symbol records from verified matcher output.

Each record carries the evidence of the exact-IDB decompilation confirmation
(per target-authoring.md §7): prototype, entry/callable, ownership, evidence
IDs, and the PENDING approval gate. Nothing here is auto-promoted.

Usage:
    generate_1108_symbols.py
"""
import json
from pathlib import Path

TARGET = "xiaomi-band-11-4.100.108"
FW_SHA = "9315ca353f624cec25dfcfc98a95ba959e2d7b24573bf1d6adf16ea10341bd99"
OUT = Path("targets/xiaomi-band-11-4.100.108/symbols")

# Verified matches: (name, domain, entry_addr, prototype, evidence, notes)
# Each entry was DECOMPILED in the exact 4.100.108 IDB and its behavior
# matched the 3.101.036 source semantics (string refs, command dispatch,
# error paths). Candidates that only passed structural scoring but could not
# be confirmed by decompilation are deliberately excluded.
VERIFIED = [
    ("ioctl", "nuttx", "0xC341B98",
     "int32_t(int32_t, uint32_t, uintptr_t)",
     ["EVID-NUTTX-VFS-001"],
     "NuttX fs_ioctl dispatch wrapper. Verified in exact IDB: references ../../nuttx/fs/vfs/fs_ioctl.c:52 and implements the canonical command dispatch (returns -ENOTTY for unsupported)."),
    ("unlink", "nuttx", "0xC33CAF8",
     "int32_t(const char *)",
     ["EVID-NUTTX-VFS-001"],
     "NuttX fs_unlink wrapper. Verified in exact IDB: references ../../nuttx/fs/vfs/fs_unlink.c:84; inode-type dispatch, -ENOSYS/-ENOTDIR paths."),
    ("rename", "nuttx", "0xC33D6C4",
     "int32_t(const char *, const char *)",
     ["EVID-NUTTX-VFS-001"],
     "NuttX fs_rename wrapper. Verified in exact IDB: references ../../nuttx/fs/vfs/fs_rename.c:534; two-path resolution, inode ops dispatch at offsets 100/104."),
    ("sem_wait", "nuttx", "0xC359510",
     "int32_t(void *)",
     ["EVID-NUTTX-SEM-001"],
     "NuttX sem_wait. Verified in exact IDB: references ../../nuttx/sched/semaphore/sem_wait.c; spinlock atomic path, priority-inheritance wait list, scheduler handoff."),
    ("sem_trywait", "nuttx", "0xC359470",
     "int32_t(void *)",
     ["EVID-NUTTX-SEM-001"],
     "NuttX sem_trywait. Verified in exact IDB: non-blocking atomic semaphore check."),
    ("sem_post", "nuttx", "0xC359980",
     "int32_t(void *)",
     ["EVID-NUTTX-SEM-001"],
     "NuttX sem_post. Verified in exact IDB: releases count and wakes highest-priority waiter via list-wake helper."),
    ("lv_image_set_src", "ui", "0xC3B2A5C",
     "int32_t(void *, const void *)",
     ["EVID-UI-LVGL-001"],
     "LVGL v9 lv_image_set_src. Verified in exact IDB: references ../../apps/graphics/lvgl/lvgl/src/widgets/image/lv_image.c (lines 161/172/184); image-source type dispatch, draw-buffer validation, cache invalidation."),
    ("app_install", "app_registry", "0xC6A6BF8",
     "int32_t(void *, const void *, int32_t)",
     ["EVID-APP-001"],
     "Stock app-registry install. Verified in exact IDB: references 'app_install'/'free_app' strings; copies app record, registers into the app list, allocates the ordered-list buffer, logs '[%s] %s: [%s] installation failed'."),
    ("controller_crash_dump", "bt-vendor", "0xC926EE8",
     "int32_t(void)",
     ["EVID-BT-001"],
     "BT controller crash dump. Verified in exact IDB: references 'bt_drv_reg_op_crash_dump' and 'BT controller BusFault_Handler'; dumps the BT controller PC/LR/R0-R7/SL/FP/IP/SP registers after waiting for BT response."),
    ("protobuf_set_ordered_app_list", "launcher", "0xC5490B4",
     "int32_t(const void *)",
     ["EVID-APP-001"],
     "Launcher ordered-app-list setter. Verified in exact IDB: references 'protobuf_set_ordered_app_list' with '[%s] %s: app list is null'/'can't found app %s'/'show'/'hidden' diagnostics; serializes the 16-byte-per-app ordered list to the launcher."),
]


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    for name, domain, entry, proto, evids, notes in VERIFIED:
        entry = entry.lower()
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
            "side_effects": ["mutates NuttX kernel state"],
            "proof": {
                "static": "confirmed",
                "device": "not_probed",
                "host_tested": False,
                "evidence_ids": evids,
            },
            "policy": "restricted",
            "status": "STATIC_RECOVERED",
            "provenance": {
                "firmware_sha256": FW_SHA,
                "evidence_ids": evids,
                "source": "multi-layer matcher (GA+CFG+xref+pattern) from verified 3.101.036 set, confirmed by decompiling the exact 4.100.108 IDB",
            },
            "notes": notes,
            "approval_state": "PENDING",
        }
        path = OUT / f"{TARGET}.{domain}.{name}.json"
        path.write_text(json.dumps(record, indent=2) + "\n")
        print(f"wrote {path.name}")


if __name__ == "__main__":
    main()
