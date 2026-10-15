#!/usr/bin/env python3
import os, subprocess, sys, tarfile, tempfile, time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TARGET = "xiaomi-band-10-pro-3.101.043"
MIRAGECTL = Path("/Volumes/EXT0/Mirage/Packages/MirageKit/.build/debug/miragectl")
CANOPUS = ROOT / "target/debug/canopus"
DEVICE = "canopus-smoke-043"
sys.stdout.reconfigure(line_buffering=True)
BOOT_WAIT = 70

def ctl(*args, check=True):
    r = subprocess.run([str(MIRAGECTL), *map(str, args)], capture_output=True, text=True)
    if check and r.returncode != 0:
        raise RuntimeError(f"miragectl {' '.join(map(str, args))}: {r.stderr or r.stdout}")
    return r.stdout

def sh(cmd):
    return ctl("sh", DEVICE, cmd)

def power_cycle_with(files):
    ctl("shutdown", DEVICE, check=False)
    time.sleep(3)
    for d in ("/canopus", "/canopus/inbox"):
        ctl("fs", "mkdir", DEVICE, "nand_data", d, check=False)
    for local, remote in files:
        ctl("fs", "rm", DEVICE, "nand_data", remote, check=False)
        ctl("fs", "put", DEVICE, "nand_data", str(local), remote)
    ctl("boot", DEVICE)
    time.sleep(BOOT_WAIT)

def load_supervisor():
    sh("insmod /data/canopus/sup.elf canopus_supervisor")

def drv(*args):
    out = sh("lua /data/canopus/driver.lua " + " ".join(args))
    lines = [l for l in out.splitlines() if "@@ " in l]
    if not lines:
        raise RuntimeError("driver produced no result: " + out[-400:])
    return lines[-1].split("@@ ", 1)[1]

def main():
    print("Recreating clean emulator device...")
    ctl("shutdown", DEVICE, check=False)
    time.sleep(3)
    ctl("delete", DEVICE, check=False)
    ctl("create", DEVICE, "--firmware", "miwear.watch.p67tc_v3.101.043_full_a4ce8564")

    print("Setting up services_test...")
    tmp = Path(tempfile.mkdtemp(prefix="services-test-"))
    pkg = ROOT / "modules/examples/all-services-test/dist/services_test-1.0.0.canopus"
    with tarfile.open(pkg) as tar:
        (tmp / "next.ko").write_bytes(tar.extractfile(f"artifacts/{TARGET}/module.elf").read())
        (tmp / "next.cmi").write_bytes(tar.extractfile(f"receipts/{TARGET}.cmi").read())

    supervisor = ROOT / f"watchfaces/canopus-installer/build/{TARGET}/canopus_supervisor.elf"
    base = [(supervisor, "/canopus/sup.elf"), (ROOT / "scripts/emu/driver.lua", "/canopus/driver.lua")]
    v = [(tmp / "next.cmi", "/canopus/inbox/services_test.cmi.next"),
         (tmp / "next.ko", "/canopus/inbox/services_test.ko.next")]

    print("Power cycling device with fresh files...")
    power_cycle_with(base + v)

    print("Loading supervisor...")
    load_supervisor()

    print("Installing services_test...")
    inst_res = drv("install", "services_test")
    print("INSTALL RESULT:", inst_res)

    print("Enabling services_test in slot 0...")
    en_res = drv("cmd", "enable", "0")
    print("ENABLE RESULT:", en_res)

    print("Power cycling device (reboot)...")
    power_cycle_with([])

    print("Loading supervisor...")
    load_supervisor()

    print("Restoring modules after reboot...")
    rest_res = drv("cmd", "restore")
    print("RESTORE RESULT:", rest_res)

    print("\n================== TEST REPORT ==================")
    report = sh("cat /data/canopus/test_report.txt")
    print(report)

    print("\n================== PROBE RESULT ==================")
    probe = sh("cat /data/canopus/probe.txt")
    print(probe)

    if "ALL_TESTS_PASS" not in probe:
        print("FAIL: Not all tests passed!")
        sys.exit(1)
    print("PASS: All services tests passed on emulator!")
    sys.exit(0)

if __name__ == "__main__":
    main()
