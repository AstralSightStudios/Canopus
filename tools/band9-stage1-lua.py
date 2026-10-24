#!/usr/bin/env python3
"""Encode Band-9 stage-1 bytes as a Lua payload table."""

from __future__ import annotations

import argparse
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    data = args.input.read_bytes()
    data += bytes((-len(data)) & 3)
    words = [int.from_bytes(data[i : i + 4], "little") for i in range(0, len(data), 4)]
    lines = ["return {", f"    size = {len(data)},", "    words = {"]
    for i in range(0, len(words), 6):
        lines.append("        " + ", ".join(f"0x{v:08x}" for v in words[i : i + 6]) + ",")
    lines += ["    },", "}", ""]
    args.output.write_text("\n".join(lines))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
