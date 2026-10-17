#!/usr/bin/env python3
"""Generate an explicitly experimental MiWear L2 backend; no symbol promotion."""
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
    if not evidence.is_file():
        parser.error("no audited experimental L2 configuration for this exact target")
    data = json.loads(evidence.read_text())
    lines = ["/* Experimental static recovery only; not production approval. */"]
    for macro, value in data["addresses"].items():
        lines.append(f"#define L2_PROBE_{macro} {value}u")
    args.output.write_text("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
