#!/usr/bin/env python3
"""Generate the target's default MiWear L2 backend config; no symbol promotion."""
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
    lines = ["/* Exact-target L2 configuration; not production approval. */"]
    if evidence.is_file():
        data = json.loads(evidence.read_text())
        lines.append("#define CANOPUS_SUP_HAS_L2 1")
        for macro, value in data["addresses"].items():
            lines.append(f"#define L2_PROBE_{macro} {value}u")
        print(f"      encrypted L2 0x43: dual-owner backend ({args.target})")
    else:
        lines.append("#define CANOPUS_SUP_HAS_L2 0")
        print(f"      encrypted L2 0x43: no backend for {args.target}")
    args.output.write_text("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
