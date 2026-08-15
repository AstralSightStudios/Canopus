#!/usr/bin/env python3
"""Generate 11-108 identity string symbol records.

The 4.100.108 image stores version/build identity inside a property block
at XIP 0x0CA00440 (same pattern as 036's early-XIP identity area, but moved
into the ro.* property table). Each string is `key=value\n`-terminated.

The identity guard's canopus_str_neq compares until NUL; the trailing `\n`
before the NUL means a direct `firmware_version` comparison will mismatch.
The target-private backend must either include the key= prefix in the
expected value or use the property-read path. These records document the
exact addresses; approval stays PENDING until that is decided.
"""
import json
from pathlib import Path

TARGET = "xiaomi-band-11-4.100.108"
FW_SHA = "9315ca353f624cec25dfcfc98a95ba959e2d7b24573bf1d6adf16ea10341bd99"
OUT = Path("targets/xiaomi-band-11-4.100.108/symbols")

IDENTITY = [
    ("firmware_version_string", "0xCA0044D",
     "ro.build.version=4.100.108\\n... (property block)"),
    ("firmware_build_string", "0xCA004D6",
     "user-4.100.108-cn-202607230300\\n (ro.build.id value)"),
]


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    for name, addr, note in IDENTITY:
        record = {
            "schema": 1,
            "symbol_id": f"{TARGET}.identity.{name}",
            "target_id": TARGET,
            "name": name,
            "kind": "string",
            "instruction_set": "n/a",
            "prototype": "const char[]",
            "proof": {"static": "confirmed", "device": "not_probed",
                      "evidence_ids": ["EVID-ID-001"]},
            "policy": "managed",
            "status": "STATIC_RECOVERED",
            "provenance": {
                "firmware_sha256": FW_SHA,
                "evidence_ids": ["EVID-ID-001"],
                "source": "exact 4.100.108 IDB property-block string recovery; cross-checked against raw binary bytes",
            },
            "entry_address": addr,
            "notes": note + " (trailing newline before NUL; guard must match the exact property format)",
            "approval_state": "PENDING",
        }
        path = OUT / f"{TARGET}.identity.{name}.json"
        path.write_text(json.dumps(record, indent=2) + "\n")
        print(f"wrote {path.name}")


if __name__ == "__main__":
    main()
