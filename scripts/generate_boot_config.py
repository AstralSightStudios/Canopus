#!/usr/bin/env python3
import argparse
import hashlib
import json
import os
import re
import struct
import tomllib
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HOOK = "sh /data/canopus/b &\n"
BLOCK = 8192  # erase sector of the dual-chip bes_flash (page 512, sector 8 KiB)
PROBE = b" #"  # over "ec" of the first dropped echo: a comment, bits only cleared
NEUTRAL = b"#"  # over "s" of the hook: a comment, bits only cleared
FLASH_HAL = ("app_flash_erase", "app_flash_program", "app_flash_flush")


def cleared_only(old, new):
    return all(n & ~o == 0 for o, n in zip(old, new))


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
    flash_offset = ap + base + data
    block = flash_offset & ~(BLOCK - 1)
    at = flash_offset - block
    hook_at = at + patched.index(HOOK)
    if at + len(original) > BLOCK or (at + 1) // 512 != at // 512:
        raise ValueError("startup script tail crosses its erase block or the probe a page")
    states = {"original": bytearray(image[block - ap:block - ap + BLOCK])}
    if bytes(states["original"][at:at + len(original)]) != original.encode():
        raise ValueError("startup script not where recovered")
    states["probed"] = bytearray(states["original"])
    states["probed"][at:at + len(PROBE)] = PROBE
    states["patched"] = bytearray(states["original"])
    states["patched"][at:at + len(patched)] = patched.encode()
    states["neutral"] = bytearray(states["patched"])
    states["neutral"][hook_at:hook_at + len(NEUTRAL)] = NEUTRAL
    if not (cleared_only(states["original"][at:], states["probed"][at:])
            and cleared_only(states["patched"][hook_at:], states["neutral"][hook_at:])):
        raise ValueError("probe or neutral bytes would set bits")
    path = pack / "evidence" / "boot-persistence.json"
    previous = json.loads(path.read_text()) if path.exists() else {}
    result = {
        "schema": 1,
        "evidence_id": "EVID-BOOT-PERSIST-001",
        "target_id": target,
        "firmware_sha256": hashlib.sha256(image).hexdigest(),
        "verdict": "STATIC_RECOVERED",
        "device": previous.get("device", "not_probed"),
        "flash_offset": flash_offset,
        "romfs_image_offset": base,
        "script_header_offset": header,
        "erase_block": block,
        "erase_size": BLOCK,
        "probe_at": at,
        "neutral_at": hook_at,
        "block_crc32": {name: zlib.crc32(content) for name, content in states.items()},
        "live_after_romfs": sum(1 for b in image[base + size:block - ap + BLOCK] if b),
        "original": original,
        "patched": patched,
        "findings": previous.get("findings", []),
    }
    path.write_text(json.dumps(result, indent=2) + "\n")
    print(f"{target}: flash offset {flash_offset:#x} in erase block {block:#x}, {len(patched)} byte tail, "
          f"{result['live_after_romfs']} live bytes after the ROMFS in that block")


# The firmware's `lua` opens its screen library, which keeps the interpreter's state in a
# three-slot table for its screen-state callback and only forgets it when the app framework
# closes a Lua instance. A `lua` process that exits leaves freed memory there, and the next screen
# change runs Lua on it: the miwear task faults and the band restarts, at every boot that ran
# boot.lua. A script run by `lua` therefore clears its own slot (one aligned word) first.
LUA_RELEASE = """do
    local states = @STATES@
    local self = tonumber(tostring(coroutine.running()):match("0[xX](%x+)$") or "", 16)
    for slot = states, states + 8, 4 do
        os.execute(string.format("mw %08x 4 > /data/canopus/lua-slot.txt", slot))
        local file = io.open("/data/canopus/lua-slot.txt", "r")
        local text = file and file:read("a") or ""
        if file then file:close() end
        if self and tonumber(text:match("=%s*0[xX](%x+)") or "", 16) == self then
            os.execute(string.format("mw %08x=00000000", slot))
        end
    end
end
"""


def lua_release(target):
    """The Lua prologue that releases the interpreter's screen slot; empty without a record."""
    path = ROOT / "targets" / target / "symbols" / f"{target}.lua.screen_lua_states.json"
    if not path.exists():
        return ""
    record = json.loads(path.read_text())
    manifest = tomllib.loads((ROOT / "targets" / target / "target.toml").read_text())
    if record["provenance"]["firmware_sha256"] != manifest["firmware_sha256"] or record["status"] in ("FORBIDDEN", "WITHDRAWN"):
        raise ValueError(f"{path.name}: not usable for {target}")
    return LUA_RELEASE.replace("@STATES@", record["entry_address"])


def boot_payload(target=None, profile_lua=None):
    source = (ROOT / "watchfaces/canopus-installer-prod/boot.lua").read_text()
    source = source.replace("-- @CANOPUS_LUA_RELEASE@\n", lua_release(target) if target else "")
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


def flash_hal(target, firmware_sha256):
    addresses = []
    for name in FLASH_HAL:
        path = ROOT / "targets" / target / "symbols" / f"{target}.flash.{name}.json"
        if not path.exists():
            return None
        record = json.loads(path.read_text())
        if record["provenance"]["firmware_sha256"] != firmware_sha256 or record["status"] in ("FORBIDDEN", "WITHDRAWN"):
            raise ValueError(f"{path.name}: not usable for {target}")
        addresses.append(int(record["callable_address"], 16))
    return addresses


# Pinned start needs the owner pump to run its start sequence. Until the owner pump stood on its
# own, only targets with the L2 backend had one, and pinned start has only been validated there
# (scripts/emu/boot_smoke.py 043/155/179). .139 now has the pump for background timers, but its
# start sequence does not complete in Mirage (boot_smoke.py 139: the Supervisor loads, boot.pending
# is never cleared), so pinned start stays off on targets without the L2 backend.
# The startup script is rewritten through the firmware's flash HAL (canopus_supervisor_boot.c), so
# a target also needs those three records. A record whose device check failed stays off unless the
# build is a device retest (CANOPUS_BOOT_DEVICE_TEST=1): the first writer, which went through
# /dev/bes_flash, corrupted flash on a real Band 11.
def generate(target, output):
    path = ROOT / "targets" / target / "evidence" / "boot-persistence.json"
    l2 = ROOT / "targets" / target / "evidence" / "l2-sram-probe.json"
    record = json.loads(path.read_text()) if path.exists() else None
    retest = os.environ.get("CANOPUS_BOOT_DEVICE_TEST") == "1"
    hal = flash_hal(target, record["firmware_sha256"]) if record else None
    if record is None or hal is None or not l2.exists() or (record["device"] == "failed" and not retest):
        output.write_text("#define CANOPUS_BOOT_ENABLED 0\n")
        return
    crc = record["block_crc32"]
    output.write_text(
        "#define CANOPUS_BOOT_ENABLED 1\n"
        f"#define CANOPUS_BOOT_FLASH_OFFSET {record['flash_offset']}u\n"
        f"#define CANOPUS_BOOT_BLOCK_OFFSET {record['erase_block']}u\n"
        f"#define CANOPUS_BOOT_BLOCK_SIZE {record['erase_size']}u\n"
        f"#define CANOPUS_BOOT_PROBE_AT {record['probe_at']}u\n"
        f"#define CANOPUS_BOOT_NEUTRAL_AT {record['neutral_at']}u\n"
        f"#define CANOPUS_BOOT_CRC_ORIGINAL 0x{crc['original']:08X}u\n"
        f"#define CANOPUS_BOOT_CRC_PROBED 0x{crc['probed']:08X}u\n"
        f"#define CANOPUS_BOOT_CRC_PATCHED 0x{crc['patched']:08X}u\n"
        f"#define CANOPUS_BOOT_CRC_NEUTRAL 0x{crc['neutral']:08X}u\n"
        f"#define CANOPUS_BOOT_FLASH_ERASE 0x{hal[0]:08X}u\n"
        f"#define CANOPUS_BOOT_FLASH_PROGRAM 0x{hal[1]:08X}u\n"
        f"#define CANOPUS_BOOT_FLASH_FLUSH 0x{hal[2]:08X}u\n"
        f"#define CANOPUS_BOOT_PROBE {json.dumps(PROBE.decode())}\n"
        f"#define CANOPUS_BOOT_NEUTRAL {json.dumps(NEUTRAL.decode())}\n"
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
