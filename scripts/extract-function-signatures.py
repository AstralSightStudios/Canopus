#!/usr/bin/env python3
"""Extract and validate target-scoped firmware function signatures.

The raw application image is the canonical XIP_TEXT_RO mapping. IDA also maps
identical bytes through cached/non-cached aliases, so uniqueness is deliberately
measured in this one mapping rather than over every IDB segment.
"""

import argparse
import hashlib
import json
from pathlib import Path
from typing import Any

SCHEMA = 1
DEFAULT_BASE = 0x0C0C0000
NORMAL_LENGTHS = (32, 48, 64, 96, 128)
THUNK_PREFIX = bytes.fromhex("5f f8 00 f0")


def load_symbols(symbols_dir: Path) -> tuple[list[dict[str, Any]], list[dict[str, str]]]:
    functions: list[dict[str, Any]] = []
    unresolved: list[dict[str, str]] = []
    for path in sorted(symbols_dir.glob("*.json")):
        record = json.loads(path.read_text())
        if record.get("kind") != "function":
            continue
        if "entry_address" not in record:
            unresolved.append(
                {
                    "symbol_id": record["symbol_id"],
                    "reason": "function record has no recovered entry_address",
                }
            )
            continue
        functions.append(record)
    return functions, unresolved


def occurrences(haystack: bytes, needle: bytes) -> list[int]:
    matches: list[int] = []
    start = 0
    while True:
        found = haystack.find(needle, start)
        if found < 0:
            return matches
        matches.append(found)
        start = found + 1


def choose_pattern(image: bytes, offset: int) -> tuple[bytes, bool]:
    prefix = image[offset : offset + 4]
    tiny_thunk = prefix == THUNK_PREFIX
    # Eight-byte import veneers can share a destination. Extend through the
    # adjacent veneer table only when the entry veneer alone is ambiguous; the
    # tiny_thunk anchor records that this is table context, not function body.
    lengths = (8, 16, 24, 32) if tiny_thunk else NORMAL_LENGTHS
    for length in lengths:
        pattern = image[offset : offset + length]
        if len(pattern) != length:
            continue
        if len(occurrences(image, pattern)) == 1:
            return pattern, tiny_thunk
    raise ValueError(f"no unique entry pattern at file offset 0x{offset:x}")


def spaced(data: bytes) -> str:
    return data.hex(" ")


def extract(symbols_dir: Path, firmware: Path, target_id: str, base: int) -> dict[str, Any]:
    image = firmware.read_bytes()
    firmware_sha256 = hashlib.sha256(image).hexdigest()
    functions, unresolved = load_symbols(symbols_dir)
    signatures: list[dict[str, Any]] = []

    for symbol in functions:
        address = int(symbol["entry_address"], 16)
        offset = address - base
        if offset < 0 or offset >= len(image):
            unresolved.append(
                {
                    "symbol_id": symbol["symbol_id"],
                    "reason": "entry_address lies outside canonical XIP_TEXT_RO image",
                }
            )
            continue
        pattern, tiny_thunk = choose_pattern(image, offset)
        matches = occurrences(image, pattern)
        signatures.append(
            {
                "symbol_id": symbol["symbol_id"],
                "expected_entry_address": f"0x{address:08X}",
                "pattern": spaced(pattern),
                "mask": spaced(bytes([0xFF]) * len(pattern)),
                "pattern_sha256": hashlib.sha256(pattern).hexdigest(),
                "pattern_length": len(pattern),
                "instruction_set": "thumb",
                "scope": "canonical:XIP_TEXT_RO",
                "current_match_count": len(matches),
                "portability": "exact-target",
                "anchors": {
                    "entry_offset": offset,
                    "tiny_thunk": tiny_thunk,
                },
            }
        )

    return {
        "schema": SCHEMA,
        "target_id": target_id,
        "firmware_sha256": firmware_sha256,
        "canonical_mapping": {
            "name": "XIP_TEXT_RO",
            "base_address": f"0x{base:08X}",
            "file_offset": 0,
            "size": len(image),
            "alias_policy": "match only this canonical image; ignore FLASH_NC/FLASH_CACHED IDB aliases",
        },
        "signature_policy": (
            "Exact entry signatures are unique in the current canonical image. "
            "They locate unchanged functions in another firmware, but remain exact-target "
            "until a relocation mask or a second-version confirmation is recorded."
        ),
        "signatures": signatures,
        "unresolved": unresolved,
    }


def parse_masked(entry: dict[str, Any]) -> tuple[bytes, bytes]:
    pattern = bytes.fromhex(entry["pattern"])
    mask = bytes.fromhex(entry["mask"])
    if len(pattern) != len(mask) or len(pattern) != entry["pattern_length"]:
        raise ValueError(f"{entry['symbol_id']}: pattern/mask length mismatch")
    return pattern, mask


def masked_occurrences(image: bytes, pattern: bytes, mask: bytes) -> list[int]:
    if all(value == 0xFF for value in mask):
        return occurrences(image, pattern)
    matches: list[int] = []
    limit = len(image) - len(pattern) + 1
    for offset in range(max(0, limit)):
        if all((image[offset + i] & mask[i]) == (pattern[i] & mask[i]) for i in range(len(pattern))):
            matches.append(offset)
    return matches


def validate(catalog: dict[str, Any], firmware: Path) -> None:
    image = firmware.read_bytes()
    actual_hash = hashlib.sha256(image).hexdigest()
    if actual_hash != catalog["firmware_sha256"]:
        raise ValueError(
            f"firmware SHA-256 mismatch: expected {catalog['firmware_sha256']}, got {actual_hash}"
        )
    base = int(catalog["canonical_mapping"]["base_address"], 16)
    seen: set[str] = set()
    for entry in catalog["signatures"]:
        symbol_id = entry["symbol_id"]
        if symbol_id in seen:
            raise ValueError(f"duplicate signature: {symbol_id}")
        seen.add(symbol_id)
        pattern, mask = parse_masked(entry)
        if hashlib.sha256(pattern).hexdigest() != entry["pattern_sha256"]:
            raise ValueError(f"{symbol_id}: pattern SHA-256 mismatch")
        matches = masked_occurrences(image, pattern, mask)
        if len(matches) != 1:
            raise ValueError(f"{symbol_id}: expected one canonical match, got {len(matches)}")
        resolved = base + matches[0]
        expected = int(entry["expected_entry_address"], 16)
        if resolved != expected:
            raise ValueError(
                f"{symbol_id}: resolved 0x{resolved:08X}, expected 0x{expected:08X}"
            )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--symbols-dir", type=Path)
    parser.add_argument("--firmware", required=True, type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--catalog", type=Path)
    parser.add_argument("--target-id", default="xiaomi-band-10-pro-3.101.036")
    parser.add_argument("--base-address", default=f"0x{DEFAULT_BASE:08X}")
    args = parser.parse_args()

    if args.catalog:
        catalog = json.loads(args.catalog.read_text())
        validate(catalog, args.firmware)
        print(f"validated {len(catalog['signatures'])} canonical function signatures")
        return
    if not args.symbols_dir or not args.output:
        parser.error("extraction requires --symbols-dir and --output")
    catalog = extract(
        args.symbols_dir,
        args.firmware,
        args.target_id,
        int(args.base_address, 16),
    )
    validate(catalog, args.firmware)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(catalog, indent=2) + "\n")
    print(
        f"wrote {len(catalog['signatures'])} signatures; "
        f"{len(catalog['unresolved'])} unresolved"
    )


if __name__ == "__main__":
    main()
