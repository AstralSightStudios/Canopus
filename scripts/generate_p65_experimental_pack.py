#!/usr/bin/env python3
"""Build a separate, exact-address experimental verifier overlay for P65.

Never mutates the registered target pack or approves SDK callables. The overlay
retains the original loader rules, image identity and firmware address ranges.
"""
import argparse
import json
from pathlib import Path
import re
import shutil

ROOT = Path(__file__).resolve().parents[1]
TARGET = "xiaomi-p65-3.100.043"
SHA = "a19b601477569765103eaf468b937bd8026b4d68ffa5c896bde222a79e475858"


def generate(output):
    output = Path(output).resolve()
    original = ROOT / "targets" / TARGET
    destination = output / TARGET
    if destination == original or original in destination.parents:
        raise ValueError("experimental overlay must not replace the registered pack")
    destination.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(original / "target.toml", destination / "target.toml")
    symbols = destination / "symbols"
    symbols.mkdir(exist_ok=True)
    for path in symbols.glob("*.json"):
        path.unlink()
    for path in (original / "symbols").glob("*.json"):
        shutil.copyfile(path, symbols / path.name)
    header = (ROOT / "manager/target/p65/canopus_p65_abi.h").read_text()
    # Includes the register_driver cast literal as well as named call macros.
    addresses = sorted(set(int(value, 16) for value in
        re.findall(r"UINT32_C\((0x[0-9A-Fa-f]+)\)", header)))
    if not addresses:
        raise ValueError("no experimental addresses")
    for address in addresses:
        if not (0x0C0C0000 <= address < 0x0CBFF484 or address == 0x3C21B55C):
            raise ValueError(f"address outside the exact P65 ABI: {address:#x}")
        name = f"experimental_{address:08x}"
        record = {
            "schema": 1, "symbol_id": f"{TARGET}.{name}",
            "target_id": TARGET, "name": name,
            "kind": "global" if address == 0x3C21B55C else "function",
            "entry_address": hex(address & ~1),
            "callable_address": None if address == 0x3C21B55C else hex(address),
            "instruction_set": "thumb",
            "prototype": "unknown", "calling_convention": "arm-aapcs",
            "contexts": {"allowed": ["experimental_resident"], "blocking": False},
            "ownership": {"return_value": "implementation_defined"},
            "side_effects": [],
            "proof": {"static": "partial", "device": "not_probed",
                      "evidence_ids": ["EVID-P65-MANAGER-002"]},
            "policy": "restricted", "status": "CANDIDATE",
            "approval_state": "PENDING",
            "provenance": {"firmware_sha256": SHA,
                           "source": "Exact-P65 experimental ABI header; not SDK approval"},
            "notes": "Verifier-only address permission; ABI typedefs are private to the experimental backend.",
        }
        (symbols / f"{TARGET}.{name}.json").write_text(json.dumps(record, indent=2) + "\n")
    (destination / "EXPERIMENTAL.json").write_text(json.dumps({
        "target_id": TARGET, "firmware_sha256": SHA,
        "production_approved": False,
        "allowed_addresses": [hex(address) for address in addresses],
        "source": "manager/target/p65/canopus_p65_abi.h",
    }, indent=2) + "\n")
    return destination


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    generate(args.output)
