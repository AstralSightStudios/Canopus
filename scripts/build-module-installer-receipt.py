#!/usr/bin/env python3
"""Build a fixed CMI1 receipt for a one-shot module installer watchface."""
import argparse
import hashlib
import pathlib
import struct
import subprocess
import tempfile

MAGIC = 0x31494D43
RECEIPT_SIZE = 256
SIGNED_SIZE = 192
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
    parser.add_argument("--private-key", required=True, type=pathlib.Path)
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

    prefix = b"".join([
        struct.pack("<8I", MAGIC, 1, RECEIPT_SIZE, 0, args.lifecycle,
                    args.version, len(artifact), 0),
        fixed(module_id, 32),
        fixed(target_id, 48),
        firmware_sha256,
        hashlib.sha256(artifact).digest(),
        fixed(b"canopus-release", 16),
    ])
    assert len(prefix) == SIGNED_SIZE
    with tempfile.TemporaryDirectory() as temp:
        message = pathlib.Path(temp) / "receipt-prefix.bin"
        signature = pathlib.Path(temp) / "receipt.sig"
        message.write_bytes(prefix)
        subprocess.run([
            "openssl", "pkeyutl", "-sign", "-rawin",
            "-inkey", str(args.private_key), "-in", str(message),
            "-out", str(signature),
        ], check=True)
        sig = signature.read_bytes()
    if len(sig) != 64:
        raise ValueError("Ed25519 signature is not 64 bytes")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(prefix + sig)
    print(f"wrote {args.output} ({len(prefix + sig)} bytes)")
    print(f"artifact sha256 {hashlib.sha256(artifact).hexdigest()}")


if __name__ == "__main__":
    main()
