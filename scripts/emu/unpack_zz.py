#!/usr/bin/env python3
"""Unpack a Xiaomi `ZZ~` full OTA container into a directory Mirage can import.

Newer official full packages (for example MiWearFirmwareArchives'
miwear.watch.n67cn_v3.1.190_full_*.bin, which starts with the bytes `ZZ~` and
the version string) are not ZIP files. They are a bootloader/manifest prelude
followed by one single-entry ZIP per device file, back to back. The prelude holds
a table of 128-byte records whose first field is the device path of the n-th
file, starting at PATH_TABLE. This writes vela_ap.bin and every /system file
into <out>, which `miragectl firmware import <out>` accepts when <out> is named
<product>_v<version>_full_<hash>. Files for other mounts (/data/ota/...) are
skipped.

usage: unpack_zz.py PACKAGE.bin OUT_DIR
"""
import hashlib
import io
import os
import re
import struct
import sys
import zipfile

PATH_TABLE = 576000  # offset of the path table inside the prelude (3.1.190)
RECORD = 128


def main(src, out):
    data = open(src, "rb").read()
    if not data.startswith(b"`ZZ~"):
        sys.exit("not a ZZ~ container")
    segments = []
    for eocd in (m.start() for m in re.finditer(b"PK\x05\x06", data)):
        _, _, _, _, _, cd_size, cd_off, comment = struct.unpack("<IHHHHIIH", data[eocd:eocd + 22])
        start = eocd - cd_size - cd_off
        segments.append((start, eocd + 22 + comment))
    pre = data[:segments[0][0]]
    root = os.path.realpath(out)
    os.makedirs(root, exist_ok=True)
    index = system = 0
    skipped = []
    for start, end in segments:
        entry_zip = zipfile.ZipFile(io.BytesIO(data[start:end]))
        info = entry_zip.infolist()[0]
        raw = pre[PATH_TABLE + RECORD * index:PATH_TABLE + RECORD * (index + 1)]
        path = raw.split(b"\0")[0].decode()
        index += 1
        if os.path.basename(path) != info.filename:
            sys.exit(f"path table does not match entry {index}: {path!r} vs {info.filename!r}")
        content = entry_zip.read(info)
        if info.filename == "vela_ap.bin":
            dest = os.path.join(root, "vela_ap.bin")
            print("vela_ap.bin", len(content), hashlib.sha256(content).hexdigest())
        elif path.startswith("/system/") and ".." not in path.split("/"):
            dest = os.path.join(root, path.lstrip("/"))
            system += 1
        else:
            skipped.append(path)
            continue
        if not os.path.realpath(dest).startswith(root + os.sep):
            sys.exit(f"refusing to write outside {root}: {dest}")
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        open(dest, "wb").write(content)
    print(f"{index} files, {system} under /system, {len(skipped)} skipped")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
