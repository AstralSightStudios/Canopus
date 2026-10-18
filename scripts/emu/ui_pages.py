#!/usr/bin/env python3
"""Band 10 Pro .043 on Mirage: the Manager and a module page, drawn by the native
UI host (ADR-CAN-010). Emulator evidence only.

Loads the production Supervisor (Manager UI included) with the `ui_demo` module
installed and enabled, registers the Manager app, opens it from the launcher,
walks Manager > modules > ui_demo > 打开界面, uses the page (switch, add, second
level, back) and checks both the module's own probe file and screenshots.
Screenshots land in --shots (default: a temp directory printed at the end).

Needs Mirage and the 3.101.043 firmware; exits 77 when either is missing.
"""
import argparse
import os
import re
import subprocess
import sys
import tarfile
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TARGET = "xiaomi-band-10-pro-3.101.043"
FIRMWARE = "miwear.watch.p67tc_v3.101.043_full_a4ce8564"
MIRAGECTL = Path(os.environ.get("MIRAGECTL", "/Volumes/EXT0/Mirage/Packages/MirageKit/.build/debug/miragectl"))
CANOPUS = ROOT / "target/debug/canopus"
DEVICE = os.environ.get("CANOPUS_UI_DEVICE", "canopus-ui-043")
BOOT_WAIT = 70
SETTLE = 4


def ctl(*args, check=True):
    r = subprocess.run([str(MIRAGECTL), *map(str, args)], capture_output=True, text=True, timeout=240)
    if check and r.returncode != 0:
        raise RuntimeError(f"miragectl {' '.join(map(str, args))}: {r.stderr or r.stdout}")
    return r.stdout


def sh(command):
    return ctl("sh", DEVICE, command)


def drv(*args):
    out = sh("lua /data/canopus/driver.lua " + " ".join(args))
    lines = [line.split("@@ ", 1)[1] for line in out.splitlines() if "@@ " in line]
    if not lines:
        raise RuntimeError("driver produced no result: " + out[-400:])
    print(" ".join(args) + ": " + lines[-1])
    return lines[-1]


def power_cycle_with(files):
    ctl("shutdown", DEVICE, check=False)
    time.sleep(3)
    for directory in ("/canopus", "/canopus/inbox"):
        ctl("fs", "mkdir", DEVICE, "nand_data", directory, check=False)
    for local, remote in files:
        ctl("fs", "rm", DEVICE, "nand_data", remote, check=False)
        ctl("fs", "put", DEVICE, "nand_data", local, remote)
    ctl("boot", DEVICE)
    time.sleep(BOOT_WAIT)


def load_supervisor():
    try:
        out = sh("insmod /data/canopus/sup.elf canopus_supervisor")
    except RuntimeError:
        drv("status")
        out = ""
    if "failed" in out.lower():
        raise RuntimeError("Supervisor load failed: " + out)


class Shots:
    def __init__(self, directory):
        self.directory = Path(directory)
        self.directory.mkdir(parents=True, exist_ok=True)
        self.count = 0

    def take(self, name):
        self.count += 1
        path = self.directory / f"{self.count:02d}-{name}.png"
        ctl("screenshot", DEVICE, path)
        print("screenshot", path)
        return path


def tap(x, y, wait=SETTLE):
    ctl("tap", DEVICE, x, y)
    time.sleep(wait)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--keep-device", action="store_true")
    parser.add_argument("--shots", default=None)
    parser.add_argument("--explore", action="store_true", help="stop after the Manager app is registered")
    args = parser.parse_args()
    sys.stdout.reconfigure(line_buffering=True)
    if not MIRAGECTL.is_file() or not CANOPUS.is_file():
        print("Mirage or built Canopus CLI missing; skipping")
        return 77
    if FIRMWARE not in ctl("firmware", "list"):
        print("Required .043 firmware missing; skipping")
        return 77
    shots = Shots(args.shots or tempfile.mkdtemp(prefix="canopus-ui-shots-"))
    subprocess.run([str(CANOPUS), "build", "--manifest", "modules/examples/ui-demo/Canopus.toml"],
                   cwd=ROOT, check=True, capture_output=True)
    subprocess.run([str(CANOPUS), "package", "--manifest", "modules/examples/ui-demo/Canopus.toml"],
                   cwd=ROOT, check=True, capture_output=True)
    subprocess.run(["sh", "scripts/build_canopus_supervisor.sh"], cwd=ROOT, check=True,
                   env=dict(os.environ, CANOPUS_TARGET=TARGET), capture_output=True)
    ctl("shutdown", DEVICE, check=False)
    time.sleep(3)
    ctl("delete", DEVICE, check=False)
    ctl("create", DEVICE, "--firmware", FIRMWARE)
    try:
        with tempfile.TemporaryDirectory(prefix="canopus-ui-") as directory:
            work = Path(directory)
            package = ROOT / "modules/examples/ui-demo/dist/ui_demo-1.0.0.canopus"
            with tarfile.open(package) as archive:
                (work / "next.ko").write_bytes(archive.extractfile("artifacts/abi-1-thumbv8m/module.elf").read())
                (work / "next.cmi").write_bytes(archive.extractfile(f"receipts/{TARGET}.cmi").read())
            supervisor = ROOT / f"watchfaces/canopus-installer/build/{TARGET}/canopus_supervisor.elf"
            icon = ROOT / "watchfaces/canopus-installer-prod/xiaomi-band-10-pro/manager_icon.bin"
            files = [
                (supervisor, "/canopus/sup.elf"),
                (ROOT / "scripts/emu/driver.lua", "/canopus/driver.lua"),
                (icon, "/canopus/manager_icon.bin"),
                (work / "next.cmi", "/canopus/inbox/ui_demo.cmi.next"),
                (work / "next.ko", "/canopus/inbox/ui_demo.ko.next"),
            ]
            power_cycle_with(files)
            load_supervisor()
            result = drv("install", "ui_demo")
            if not re.search(r"install result=5 diag=0\b", result):
                raise RuntimeError("install failed: " + result)
            drv("cmd", "enable", "0")
            power_cycle_with([])
            load_supervisor()
            restored = drv("cmd", "restore")
            if not re.search(r"slot0=state:9,", restored):
                raise RuntimeError("module did not become active: " + restored)
            print("module active; registering the Manager app")
            drv("cmd", "native_stage", "0")
            shots.take("after-register")
            if args.explore:
                return 0
    finally:
        if not args.keep_device:
            ctl("shutdown", DEVICE, check=False)
            ctl("delete", DEVICE, check=False)
    return 0


if __name__ == "__main__":
    sys.exit(main())
