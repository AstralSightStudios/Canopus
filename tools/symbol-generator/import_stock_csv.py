#!/usr/bin/env python3
"""Import stock_f701a84_symbols.csv evidence into a target pack symbol set.

This is a *conservative* import: CSV confidence and dynamic_status map to the
lowest defensible evidence status. Nothing is auto-promoted. FORBIDDEN rows
stay FORBIDDEN and can never generate a veneer.

Usage:
    python3 import_stock_csv.py <csv_path> <target_pack_dir> <firmware_sha256>
"""
import csv
import json
import sys
from pathlib import Path

TARGET_ID = "xiaomi-band-10-pro-3.101.030"
FW_SHA = "f701a84ffcafa67f4d4603ad8cd66a11e5442f27140f5af0982e0975dccd225b"

STATIC_MAP = {
    "high": "confirmed",
    "medium-high": "recovered",
    "medium": "recovered",
    "low": "candidate",
    "none": "candidate",
}

STATUS_MAP = {
    "probe-passed": "DEVICE_PROBED",
    "hold": "STATIC_CONFIRMED",
    "probe-ready": "STATIC_RECOVERED",
    "static-verified": "STATIC_CONFIRMED",
    "forbidden": "FORBIDDEN",
}

POLICY_MAP = {
    "forbidden": "forbidden",
    "probe-passed": "managed",
    "hold": "restricted",
    "probe-ready": "restricted",
    "static-verified": "restricted",
}


def kind_for(category: str, name: str) -> str:
    if category == "identity":
        return "string"
    if category == "modlib" and "symtab" in name:
        return "global"
    return "function"


def main() -> int:
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    csv_path = Path(sys.argv[1])
    out_dir = Path(sys.argv[2])
    fw_sha = sys.argv[3] if len(sys.argv) > 3 else FW_SHA

    sym_dir = out_dir / "symbols"
    sym_dir.mkdir(parents=True, exist_ok=True)

    with csv_path.open() as f:
        rows = list(csv.DictReader(f))

    written = 0
    skipped = 0
    for row in rows:
        category = row["category"]
        name = row["name"]
        entry = row["entry"]
        callable_addr = row["callable"]
        proto = row["prototype"]
        confidence = row["confidence"].strip().lower()
        dstatus = row["dynamic_status"].strip().lower()
        notes = row["notes"]

        symbol_id = f"{TARGET_ID}.{category}.{name}"
        kind = kind_for(category, name)
        status = STATUS_MAP.get(dstatus)
        if status is None:
            print(f"skip {symbol_id}: unknown dynamic_status {dstatus!r}")
            skipped += 1
            continue

        def valid_addr(s: str):
            s = (s or "").strip()
            if not s or s.lower() == "unknown":
                return None
            if not s.lower().startswith("0x"):
                s = "0x" + s
            try:
                int(s, 16)
            except ValueError:
                return None
            return s

        entry_a = valid_addr(entry)
        call_a = valid_addr(callable_addr)

        # A function with no usable entry address cannot be called safely:
        # demote to FORBIDDEN regardless of the CSV's optimistic status.
        if kind == "function" and entry_a is None:
            status = "FORBIDDEN"

        policy = POLICY_MAP.get(dstatus, "restricted")
        if status == "FORBIDDEN":
            policy = "forbidden"

        # For Thumb functions the callable address is entry with bit 0 set.
        # Dispatch-only functions (fops table entries) legitimately have no
        # independent callable address and are not directly callable.
        if kind == "function" and entry_a is not None and call_a is None:
            call_a = "0x{:08X}".format((int(entry_a, 16) & ~1) | 1)

        rec = {
            "schema": 1,
            "symbol_id": symbol_id,
            "target_id": TARGET_ID,
            "name": name,
            "kind": kind,
            "instruction_set": "thumb" if kind == "function" else "n/a",
            "prototype": proto if proto not in ("", "unknown") else None,
            "proof": {
                "static": STATIC_MAP.get(confidence, "candidate"),
                "device": "probed" if status == "DEVICE_PROBED" else None,
            },
            "policy": policy,
            "status": status,
            "provenance": {
                "firmware_sha256": fw_sha,
                "source": f"firmware_latest/analysis/stock_f701a84_symbols.csv row category={category} name={name}",
            },
        }
        if entry_a:
            rec["entry_address"] = entry_a
        if call_a:
            rec["callable_address"] = call_a
        if notes:
            rec["notes"] = notes

        # strip None values to keep records clean
        rec = {k: v for k, v in rec.items() if v is not None}
        if rec["kind"] == "function":
            rec["proof"] = {k: v for k, v in rec["proof"].items() if v is not None}

        out = sym_dir / f"{symbol_id}.json"
        out.write_text(json.dumps(rec, indent=2, ensure_ascii=False) + "\n")
        written += 1

    print(f"imported {written} symbols into {sym_dir} ({skipped} skipped)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
