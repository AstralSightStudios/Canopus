#!/usr/bin/env python3
"""Generate the target's default MiWear L2 backend config; no symbol promotion.

A target with `evidence/l2-sram-probe.json` gets the encrypted L2 transport and the owner pump it
rides on. A target with only `evidence/owner-pump.json` gets the owner pump: the resident libuv
timer on the UI owner loop that keeps background timers firing while the screen is off."""
import argparse
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("target")
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    evidence = ROOT / "targets" / args.target / "evidence" / "l2-sram-probe.json"
    pump = ROOT / "targets" / args.target / "evidence" / "owner-pump.json"
    lines = ["/* Exact-target L2 configuration; not production approval. */"]
    if evidence.is_file():
        data = json.loads(evidence.read_text())
        lines.append("#define CANOPUS_SUP_HAS_L2 1")
        lines.append("#define CANOPUS_SUP_HAS_OWNER_PUMP 1")
        for macro, value in data["addresses"].items():
            lines.append(f"#define L2_PROBE_{macro} {value}u")
        print(f"      encrypted L2 0x43: dual-owner backend ({args.target})")
    elif pump.is_file():
        data = json.loads(pump.read_text())
        lines.append("#define CANOPUS_SUP_HAS_L2 0")
        lines.append("#define CANOPUS_SUP_HAS_OWNER_PUMP 1")
        for macro, value in data["addresses"].items():
            lines.append(f"#define L2_PROBE_{macro} {value}u")
        print(f"      encrypted L2 0x43: no backend for {args.target}; owner pump for background timers")
    else:
        lines.append("#define CANOPUS_SUP_HAS_L2 0")
        lines.append("#define CANOPUS_SUP_HAS_OWNER_PUMP 0")
        print(f"      encrypted L2 0x43: no backend for {args.target}")
    args.output.write_text("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
