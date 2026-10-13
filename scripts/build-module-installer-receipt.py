#!/usr/bin/env python3
"""Build a fixed unsigned CMI2 receipt for a one-shot module installer watchface."""
import argparse
import hashlib
import pathlib
import struct

MAGIC = 0x32494D43
RECEIPT_SIZE = 176
ARTIFACT_MAX = 384 * 1024


def fixed(value: bytes, size: int) -> bytes:
    if not value or len(value) >= size:
        raise ValueError(f"value must be 1..{size - 1} bytes")
    return value + bytes(size - len(value))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--module", required=True, type=pathlib.Path)
    parser.add_argument("--module-id", required=True)
    parser.add_argument("--version", required=True, type=int)
    parser.add_argument("--lifecycle", required=True, type=int, choices=range(4))
    parser.add_argument("--target-id", required=True)
    parser.add_argument("--firmware-sha256", required=True)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    args = parser.parse_args()

    module_id = args.module_id.encode("ascii")
    if any(not (chr(c).islower() or chr(c).isdigit() or chr(c) in "_.-")
           for c in module_id):
        raise ValueError("module id must contain only lowercase ASCII, digits, _, -, .")
    target_id = args.target_id.encode("ascii")
    try:
        firmware_sha256 = bytes.fromhex(args.firmware_sha256)
    except ValueError as error:
        raise ValueError("firmware SHA-256 must be 64 hexadecimal characters") from error
    if len(firmware_sha256) != 32:
        raise ValueError("firmware SHA-256 must be 64 hexadecimal characters")
    artifact = args.module.read_bytes()
    if not artifact.startswith(b"\x7fELF") or not (0 < len(artifact) <= ARTIFACT_MAX):
        raise ValueError("artifact must be a non-empty bounded ELF")

    receipt = b"".join([
        struct.pack("<8I", MAGIC, 2, RECEIPT_SIZE, 0, args.lifecycle,
                    args.version, len(artifact), 0),
        fixed(module_id, 32),
        fixed(target_id, 48),
        firmware_sha256,
        hashlib.sha256(artifact).digest(),
    ])
    assert len(receipt) == RECEIPT_SIZE
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(receipt)
    print(f"wrote {args.output} ({len(receipt)} bytes)")
    print(f"artifact sha256 {hashlib.sha256(artifact).hexdigest()}")


if __name__ == "__main__":
    main()
