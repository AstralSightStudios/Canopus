#!/usr/bin/env python3
"""Package the experimental P65 native installer as flat watchface resources.

The result is a resource ZIP for the existing packer, not a flashable image.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]
TARGET = "xiaomi-p65-3.100.043"
SHA = "a19b601477569765103eaf468b937bd8026b4d68ffa5c896bde222a79e475858"


def deployment_enabled():
    policy = (ROOT / "manager/target/p65/canopus_p65_safety.h").read_text()
    enabled = re.search(r'^#define CANOPUS_P65_DEPLOYMENT_ENABLED ([01])$', policy, re.M)
    return enabled is not None and enabled[1] == "1"


def build(supervisor, output, targets):
    if not deployment_enabled():
        raise ValueError("P65 deployment withdrawn after Logo boot loop; do not package or retry")
    supervisor = Path(supervisor).resolve()
    output = Path(output).resolve()
    data = supervisor.read_bytes()
    if len(data) < 52 or data[:7] != b"\x7fELF\x01\x01\x01":
        raise ValueError("expected ELF32 little-endian native Supervisor")
    if struct.unpack_from("<HH", data, 16) != (1, 40):
        raise ValueError("expected ARM ET_REL")
    if not struct.unpack_from("<I", data, 24)[0] & 1:
        raise ValueError("missing Thumb module entry")
    if len(data) > 262144:
        raise ValueError("experimental Supervisor exceeds bundle size limit")
    subprocess.run([str(ROOT / "target/debug/canopus"), "verify", str(supervisor),
                    "--target", TARGET, "--targets-dir", str(targets)], check=True)
    lua = (ROOT / "watchfaces/canopus-installer-prod/xiaomi-p65/main.lua").read_text()
    marker = "local EXPECTED_SIZE = 0 -- Replaced by the resource-bundle builder."
    if lua.count(marker) != 1:
        raise ValueError("missing resource size placeholder")
    lua = lua.replace(marker, f"local EXPECTED_SIZE = {len(data)}")
    icon = (ROOT / "watchfaces/canopus-installer/manager_icon.bin").read_bytes()
    contents = {"main.lua": lua.encode(), "supervisor.bin": data,
                "manager_icon.bin": icon}
    output.mkdir(parents=True, exist_ok=True)
    for name, payload in contents.items():
        (output / name).write_bytes(payload)
    archive = output / "canopus-p65-resources.zip"
    with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED) as packed:
        for name, payload in contents.items():
            info = zipfile.ZipInfo(name, (2020, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            packed.writestr(info, payload)
    manifest = {
        "target_id": TARGET, "firmware_sha256": SHA,
        "experimental": True, "flashable": False, "unload": "reboot",
        "resources": {name: {"size": len(payload),
            "sha256": hashlib.sha256(payload).hexdigest()}
            for name, payload in contents.items()},
        "archive_sha256": hashlib.sha256(archive.read_bytes()).hexdigest(),
    }
    (output / "build-info.json").write_text(json.dumps(manifest, indent=2) + "\n")
    return archive


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--supervisor", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--targets", required=True, type=Path)
    args = parser.parse_args()
    print(build(args.supervisor, args.output, args.targets))
