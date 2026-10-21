#!/usr/bin/env python3
import argparse
import hashlib
import json
import re
import struct
import tomllib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HOOK = "sh /data/canopus/b &\n"


def recover(target):
    pack = ROOT / "targets" / target
    image = (ROOT / "fwbins" / target / "vela_ap.bin").read_bytes()
    manifest = tomllib.loads((pack / "target.toml").read_text())
    if hashlib.sha256(image).hexdigest() != manifest["firmware_sha256"]:
        raise ValueError("firmware hash does not match the target")
    base = next(m.start() for m in re.finditer(b"-rom1fs-", image)
                if 32 < int.from_bytes(image[m.start() + 8:m.start() + 12], "big") < 1_000_000)
    size = int.from_bytes(image[base + 8:base + 12], "big")
    rom = image[base:base + size]

    def lookup(first, wanted):
        off = first
        while off:
            next_entry, child, length, _ = struct.unpack_from(">4I", rom, off)
            end = rom.index(0, off + 16)
            name = rom[off + 16:end].decode()
            if name == wanted[0]:
                if len(wanted) > 1:
                    return lookup(child, wanted[1:])
                return off, (end + 16) & ~15, length
            off = next_entry & ~15
        raise ValueError("rc.sysinit missing")

    first = (rom.index(0, 16) + 16) & ~15
    header, data, length = lookup(first, ["init.d", "rc.sysinit"])
    original = rom[data:data + length].decode()
    echoes = [m for m in re.finditer(r'echo "mount [^"]*"\n', original)]
    drop = echoes[-2:]
    if len(drop) != 2 or drop[0].start() < original.index("/dev/nand_data /data"):
        raise ValueError("unexpected startup script")
    tail = drop[0].start()
    patched = original[:tail] + original[tail:].replace(drop[0].group(), "", 1).replace(drop[1].group(), "", 1) + HOOK
    if HOOK in original or len(patched) > length:
        raise ValueError("unexpected startup script")
    patched += "\n" * (length - len(patched))
    original, patched = original[tail:], patched[tail:]
    data += tail
    if sum(struct.unpack(">8I", rom[header:header + 32])) & 0xffffffff:
        raise ValueError("unexpected ROMFS inode checksum")
    boot = image[-364:].decode("ascii", "replace")
    ap = int(re.search(r"OTA_CODE_OFFSET=(0x[0-9A-Fa-f]+)", boot)[1], 16)
    result = {
        "schema": 1,
        "evidence_id": "EVID-BOOT-PERSIST-001",
        "target_id": target,
        "firmware_sha256": hashlib.sha256(image).hexdigest(),
        "verdict": "STATIC_RECOVERED",
        "device": "not_probed",
        "flash_offset": ap + base + data,
        "romfs_image_offset": base,
        "script_header_offset": header,
        "original": original,
        "patched": patched,
        "findings": [
            "Equal-length rc.sysinit data replacement: drops the last two mount echo lines and appends a background Lua start after every mount; only the differing tail is stored.",
            "The ROMFS inode checksum covers the unchanged 32-byte header/name and the filesystem checksum covers its unchanged first 512 bytes.",
            "Physical boot acceptance is NOT verified.",
            "/dev/bes_flash is registered as an MTD driver (erase/bread/bwrite/read/ioctl, 8 KiB sectors); a VFS open goes through mtd_proxy and an FTL read-modify-write, flushed on close. Not exercised on hardware.",
        ],
    }
    path = pack / "evidence" / "boot-persistence.json"
    path.write_text(json.dumps(result, indent=2) + "\n")
    print(f"{target}: Flash offset {result['flash_offset']:#x}, {len(patched)} byte tail")


def boot_payload(target=None, profile_lua=None):
    source = (ROOT / "watchfaces/canopus-installer-prod/boot.lua").read_text()
    if target and target.startswith("xiaomi-band-11-"):
        loader = (ROOT / "watchfaces/canopus-installer-prod/xiaomi-band-11/src/native_loader.lua").read_text()
        if profile_lua is None:
            profile_path = ROOT / "watchfaces/canopus-installer-prod/xiaomi-band-11" / f"canopus_loader_profile-{target}.bin"
            if profile_path.exists():
                profile_lua = profile_path.read_text()
            else:
                profile_lua = "return {}\n"
        load = (
            "    log(\"boot: invoking native loader for Band 11\")\n"
            "    local native_loader = (function()\n"
            f"{loader}\n"
            "    end)()\n"
            "    local profile = (function()\n"
            f"{profile_lua}\n"
            "    end)()\n"
            "    local ok, err = native_loader.load(\n"
            "        profile,\n"
            "        \"/data/canopus/stage1.bin\",\n"
            "        \"/data/canopus/stage2.bin\",\n"
            "        \"/data/canopus/supervisor.elf\",\n"
            "        run, read_all, write_all, log)\n"
            "    log(\"boot: native loader finished: ok=\" .. tostring(ok) .. \", err=\" .. tostring(err))\n"
            "    assert(ok, \"native loader failed: \" .. tostring(err))"
        )
    else:
        load = 'assert(run("insmod /data/canopus/supervisor.elf canopus_supervisor"), "insmod failed")'
    return source.replace("-- @CANOPUS_BOOT_LOAD@", load)


def generate(target, output):
    path = ROOT / "targets" / target / "evidence" / "boot-persistence.json"
    if not path.exists():
        output.write_text("#define CANOPUS_BOOT_ENABLED 0\n")
        return
    record = json.loads(path.read_text())
    output.write_text(
        "#define CANOPUS_BOOT_ENABLED 1\n"
        f"#define CANOPUS_BOOT_FLASH_OFFSET {record['flash_offset']}u\n"
        f"static const char boot_original[] = {json.dumps(record['original'])};\n"
        f"static const char boot_patched[] = {json.dumps(record['patched'])};\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("target")
    parser.add_argument("output", nargs="?", type=Path)
    parser.add_argument("--recover", action="store_true")
    parser.add_argument("--payload", action="store_true")
    args = parser.parse_args()
    if args.recover:
        recover(args.target)
    elif args.payload:
        args.output.write_text(boot_payload(args.target))
    else:
        generate(args.target, args.output)
