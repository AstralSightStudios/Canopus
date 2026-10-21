#!/usr/bin/env python3
"""Pinned-start smoke on Mirage, Band 10 Pro 3.101.043 (emulator-level evidence only).

A. Original firmware: the Supervisor reads,
   writes and restores the startup script through /dev/bes_flash.
B. The same script patched into a copy of vela_ap.bin offline, because Mirage reloads
   the AP image from the firmware package at every start and a runtime write cannot
   survive a reboot there: a fresh device with only the files a pinned start reads
   boots, and nothing loads the Supervisor by hand. The Supervisor must come up and
   clear its start counter once the Manager is registered.
Exit code 77 when Mirage or the firmware package is missing.
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LIBRARY = Path(os.environ.get("MIRAGE_LIBRARY", "/Volumes/EXT0/Mirage/.scratch/Library"))
TARGET = "xiaomi-band-10-pro-3.101.043"
FW_ID = "miwear.watch.p67tc_v3.101.043_full_a4ce8564"
FACE = "xiaomi-band-10-pro"
WAKE = (168, 240)
MIRAGECTL = Path(os.environ.get("MIRAGECTL", "/Volumes/EXT0/Mirage/.build/out/Products/Debug/miragectl"))
OTA = Path(os.environ.get("CANOPUS_FW_OTA_DIR", str(LIBRARY / "Firmwares" / FW_ID)))
DEVICE = f"canopus-boot-{os.getpid()}"
BOOT_WAIT = 40
START_WAIT = 240
AP_FLASH_BASE = 0xC0000


def ctl(*args, check=True):
    r = subprocess.run([str(MIRAGECTL), *map(str, args)], capture_output=True, text=True)
    if check and r.returncode != 0:
        raise RuntimeError(f"miragectl {' '.join(map(str, args))}: {r.stderr or r.stdout}")
    return r.stdout


def sh(command):
    return ctl("sh", DEVICE, command)


def drv(*args):
    out = sh("lua /data/canopus/driver.lua " + " ".join(args))
    lines = [l for l in out.splitlines() if "@@ " in l]
    if not lines:
        raise RuntimeError("driver produced no result: " + out[-400:])
    return lines[-1].split("@@ ", 1)[1]


def expect(label, got, *needles):
    ok = all(n in got for n in needles)
    print(("PASS " if ok else "FAIL ") + label + ": " + got.strip())
    if not ok:
        sys.exit(1)


def put(files):
    for d in ("/canopus", "/canopus/inbox"):
        ctl("fs", "mkdir", DEVICE, "nand_data", d, check=False)
    for local, remote in files:
        ctl("fs", "rm", DEVICE, "nand_data", remote, check=False)
        ctl("fs", "put", DEVICE, "nand_data", local, remote)


def fresh_device(firmware):
    if DEVICE in ctl("list"):
        ctl("shutdown", DEVICE, check=False)
        time.sleep(3)
        ctl("delete", DEVICE)
    ctl("create", DEVICE, "--firmware", firmware)


def staged_files(work):
    """What the installer's Run leaves in /data/canopus for a pinned start."""
    face = ROOT / "watchfaces/canopus-installer-prod" / FACE
    start = work / "b"
    start.write_text("sleep 30\nlua /data/canopus/boot.lua\n")
    files = [(face / f"canopus_boot-{TARGET}.bin", "/canopus/boot.lua"), (start, "/canopus/b"),
             (ROOT / "watchfaces/canopus-installer/manager_icon.bin", "/canopus/manager_icon.bin"),
             (ROOT / "scripts/emu/driver.lua", "/canopus/driver.lua")]
    files.append((ROOT / f"watchfaces/canopus-installer/build/{TARGET}/canopus_supervisor.elf",
                  "/canopus/supervisor.elf"))
    return files


def flash_write_path(files):
    fresh_device(FW_ID)
    with tempfile.NamedTemporaryFile("w", suffix=".txt") as target:
        target.write(TARGET)
        target.flush()
        put(files + [(target.name, "/canopus/boot.target")])
    ctl("boot", DEVICE)
    time.sleep(BOOT_WAIT)
    sh("insmod /data/canopus/supervisor.elf canopus_supervisor")
    time.sleep(5)
    expect("supported, not pinned", drv("boot", "query"), "flags=1", "err=0")
    expect("pin", drv("boot", "set", "1"), "result=8", "err=0")
    expect("pinned", drv("boot", "query"), "flags=3")
    expect("script on flash", sh("cat /etc/init.d/rc.sysinit"), "sh /data/canopus/b &")
    expect("unpin", drv("boot", "set", "0"), "result=8", "err=0")
    expect("unpinned", drv("boot", "query"), "flags=1")
    expect("script restored", sh("cat /etc/init.d/rc.sysinit"), 'echo "mount /nv"')
    ctl("shutdown", DEVICE)
    ctl("delete", DEVICE)
    print("boot smoke A (flash write path): passed")


def cold_start(files, record):
    patched_id = FW_ID.rsplit("_", 1)[0] + "_b007ed00"
    # Mirage records the size of every image it imports and does not follow symlinks;
    # hard links (same volume) are real files.
    with tempfile.TemporaryDirectory(dir=OTA.parent) as tmp:
        patched = Path(tmp) / patched_id
        patched.mkdir()
        for entry in OTA.iterdir():
            if entry.name == "vela_ap.bin":
                continue
            if entry.is_dir():
                shutil.copytree(entry, patched / entry.name)
            else:
                try:
                    os.link(entry, patched / entry.name)
                except OSError:
                    shutil.copy2(entry, patched / entry.name)
        image = bytearray((OTA / "vela_ap.bin").read_bytes())
        offset = record["flash_offset"] - AP_FLASH_BASE
        original = record["original"].encode()
        assert bytes(image[offset:offset + len(original)]) == original, "startup script not where recorded"
        image[offset:offset + len(original)] = record["patched"].encode()
        (patched / "vela_ap.bin").write_bytes(image)
        ctl("firmware", "import", patched)
        try:
            fresh_device(patched_id)
            put(files)
            ctl("boot", DEVICE)
            deadline = time.time() + START_WAIT
            while time.time() < deadline and "canopus" not in sh("ls /dev"):
                time.sleep(5)
            if "canopus" not in sh("ls /dev"):
                print("FAIL pinned start: the Supervisor never loaded\n" + sh("ls /data/canopus"))
                sys.exit(1)
            print("PASS pinned start: /dev/canopus appeared without a manual load")
            # The owner timer that drives the sequence is bootstrapped by one lit LVGL tick,
            # i.e. the first time the screen is woken; the sequence then runs once it is off.
            ctl("tap", DEVICE, *WAKE)
            ctl("tap", DEVICE, *WAKE)
            for _ in range(12):
                time.sleep(5)
                if "boot.pending" not in sh("ls /data/canopus"):
                    break
            if "boot.pending" in sh("ls /data/canopus"):
                print("FAIL pinned start: boot.pending is still there\n" + sh("ls /data/canopus"))
                sys.exit(1)
            print("PASS start counter cleared once the Manager exists")
            time.sleep(10)
            expect("pinned, sequence finished", drv("boot", "query"), "flags=3", "err=0")
            print("boot smoke B (cold start from patched image): passed")
        finally:
            ctl("shutdown", DEVICE, check=False)
            time.sleep(3)
            ctl("delete", DEVICE, check=False)
            ctl("firmware", "rm", patched_id, check=False)


def main():
    if not MIRAGECTL.exists():
        print(f"SKIP: miragectl not found at {MIRAGECTL}")
        sys.exit(77)
    if not (OTA / "vela_ap.bin").exists():
        print(f"SKIP: firmware package not found at {OTA}")
        sys.exit(77)
    subprocess.run(["cargo", "build", "-q", "-p", "canopus-cli"], cwd=ROOT, check=True)
    subprocess.run(["sh", str(ROOT / "scripts/build_canopus_supervisor.sh")], cwd=ROOT, check=True,
                   env=dict(os.environ, CANOPUS_TARGET=TARGET), capture_output=True)
    record = json.loads((ROOT / f"targets/{TARGET}/evidence/boot-persistence.json").read_text())
    with tempfile.TemporaryDirectory() as work:
        files = staged_files(Path(work))
        flash_write_path(files)
        cold_start(files, record)
    print("boot smoke: all checks passed")


if __name__ == "__main__":
    main()
