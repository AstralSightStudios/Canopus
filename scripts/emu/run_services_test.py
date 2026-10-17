#!/usr/bin/env python3
"""Band 10 Pro .043 services/KV reboot smoke (emulator evidence only).

Builds its own headless Supervisor and services package. Missing Mirage, CLI or
firmware exits 77. Timer and notification services are explicitly unavailable
in this NSH-driven headless configuration, not counted as behavioral passes.
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
DEVICE = "canopus-services-043"
BOOT_WAIT = 70
EXPECTED_SKIPS = {
    "Timers: no approved platform event source",
    "Notify: platform notifications unavailable",
}


def ctl(*args, check=True):
    r = subprocess.run([str(MIRAGECTL), *map(str, args)], capture_output=True,
                       text=True, timeout=180)
    if check and r.returncode != 0:
        raise RuntimeError(f"miragectl {' '.join(map(str, args))}: {r.stderr or r.stdout}")
    return r.stdout


def sh(cmd):
    return ctl("sh", DEVICE, cmd)


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
    out = sh("insmod /data/canopus/sup.elf canopus_supervisor")
    if "failed" in out.lower() or "error" in out.lower():
        raise RuntimeError("Supervisor load failed: " + out)


def drv(*args):
    out = sh("lua /data/canopus/driver.lua " + " ".join(args))
    lines = [line.split("@@ ", 1)[1] for line in out.splitlines() if "@@ " in line]
    if not lines:
        raise RuntimeError("driver produced no result: " + out[-400:])
    result = lines[-1]
    print(" ".join(args) + ": " + result)
    return result


def require_driver(result, pattern):
    if re.search(pattern, result) is None:
        raise RuntimeError("Unexpected driver result: " + result)


def validate_report(report, probe, expected_skips=EXPECTED_SKIPS):
    summary = re.search(r"SUMMARY: (\d+)/(\d+) PASSED, FAILS=(\d+), SKIPS=(\d+)", report)
    result = re.search(r"^TESTS_COMPLETE:(\d+)/(\d+) SKIPS=(\d+)\s*$", probe, re.MULTILINE)
    if summary is None or result is None:
        raise RuntimeError("Missing successful report/probe summary")
    passed, total, failed, skipped = map(int, summary.groups())
    if failed or passed == 0 or passed + skipped != total:
        raise RuntimeError("Service tests failed or counters are inconsistent")
    if tuple(map(int, result.groups())) != (passed, total, skipped):
        raise RuntimeError("Report and probe disagree")
    pass_lines = re.findall(r"^\[PASS\] .+$", report, re.MULTILINE)
    skip_lines = re.findall(r"^\[SKIP\] (.+)$", report, re.MULTILINE)
    if "[FAIL]" in report or len(pass_lines) != passed or len(skip_lines) != skipped:
        raise RuntimeError("Report details and counters disagree")
    if set(skip_lines) != set(expected_skips) or skipped != len(expected_skips):
        raise RuntimeError("Unexpected skipped services: " + repr(skip_lines))
    return passed, total, skipped


def read_results():
    report = sh("cat /data/canopus/m.services_test/test_report.txt").replace("\r", "")
    probe = sh("cat /data/canopus/m.services_test/probe.txt").replace("\r", "")
    print(report)
    print(probe)
    return validate_report(report, probe)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--keep-device", action="store_true", help="keep the stopped test device for inspection")
    args = parser.parse_args()
    sys.stdout.reconfigure(line_buffering=True)
    if not MIRAGECTL.is_file() or not CANOPUS.is_file():
        print("Mirage or built Canopus CLI missing; skipping")
        return 77
    if FIRMWARE not in ctl("firmware", "list"):
        print("Required .043 firmware missing; skipping")
        return 77
    subprocess.run([str(CANOPUS), "build", "--manifest", "modules/examples/all-services-test/Canopus.toml"],
                   cwd=ROOT, check=True)
    subprocess.run([str(CANOPUS), "package", "--manifest", "modules/examples/all-services-test/Canopus.toml"],
                   cwd=ROOT, check=True)
    subprocess.run(["sh", "scripts/build_canopus_supervisor.sh"], cwd=ROOT, check=True,
                   env=dict(os.environ, CANOPUS_TARGET=TARGET,
                            CANOPUS_SUP_EXTRA_DEFINES="-DCANOPUS_SUP_HEADLESS=1"))
    print("Recreating isolated services test device...")
    ctl("shutdown", DEVICE, check=False)
    time.sleep(3)
    ctl("delete", DEVICE, check=False)
    ctl("create", DEVICE, "--firmware", FIRMWARE)
    try:
        with tempfile.TemporaryDirectory(prefix="canopus-services-") as directory:
            work = Path(directory)
            package = ROOT / "modules/examples/all-services-test/dist/services_test-1.0.0.canopus"
            with tarfile.open(package) as archive:
                for name, member in (("next.ko", f"artifacts/{TARGET}/module.elf"),
                                     ("next.cmi", f"receipts/{TARGET}.cmi")):
                    (work / name).write_bytes(archive.extractfile(member).read())
            supervisor = ROOT / f"watchfaces/canopus-installer/build/{TARGET}/canopus_supervisor.elf"
            power_cycle_with([
                (supervisor, "/canopus/sup.elf"),
                (ROOT / "scripts/emu/driver.lua", "/canopus/driver.lua"),
                (work / "next.cmi", "/canopus/inbox/services_test.cmi.next"),
                (work / "next.ko", "/canopus/inbox/services_test.ko.next"),
            ])
            load_supervisor()
            require_driver(drv("install", "services_test"), r"install result=5 diag=0\b")
            require_driver(drv("cmd", "enable", "0"), r"err=0\b.*slot0=state:5,")
            for boot in (1, 2):
                print(f"Reboot/restore {boot}...")
                power_cycle_with([])
                load_supervisor()
                restored = drv("cmd", "restore")
                passed, total, skipped = read_results()
                require_driver(restored, r"err=0\b.*slot0=state:9,")
            marker = sh("cat /data/canopus/m.services_test/kv_reboot.txt").replace("\r", "")
            if re.search(r"^KV_REBOOT_PASS\s*$", marker, re.MULTILINE) is None:
                raise RuntimeError("KV value did not survive a real emulator reboot")
            print(f"PASS .043 emulator: {passed}/{total} suites passed, {skipped} explicit skips; KV survived reboot")
    finally:
        ctl("shutdown", DEVICE, check=False)
        if not args.keep_device:
            ctl("delete", DEVICE, check=False)
    return 0


if __name__ == "__main__":
    sys.exit(main())
