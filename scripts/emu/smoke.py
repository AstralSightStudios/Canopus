#!/usr/bin/env python3
"""End-to-end Supervisor update/rollback smoke test on a Mirage emulator device.

Needs miragectl (MIRAGECTL, default: the Mirage checkout's debug build) and a
full OTA package for the target (CANOPUS_FW_OTA_DIR, default
/Volumes/EXT0/miwear.watch.p67tc_v3.101.043_full_a4ce8564). Skips with exit 77
when either is missing. Emulator results are host/emulator-level evidence only;
they never promote a capability to "device verified" (refactor-plan P6.6).
"""
import os
import shutil
import subprocess
import sys
import tarfile
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TARGET = "xiaomi-band-10-pro-3.101.043"
MIRAGECTL = Path(os.environ.get("MIRAGECTL", "/Volumes/EXT0/Mirage/Packages/MirageKit/.build/debug/miragectl"))
OTA = Path(os.environ.get("CANOPUS_FW_OTA_DIR", "/Volumes/EXT0/miwear.watch.p67tc_v3.101.043_full_a4ce8564"))
CANOPUS = ROOT / "target/debug/canopus"
DEVICE = "canopus-smoke-043"
BOOT_WAIT = 40


def skip(reason):
    print("SKIP:", reason)
    sys.exit(77)


def ctl(*args, check=True):
    r = subprocess.run([str(MIRAGECTL), *map(str, args)], capture_output=True, text=True)
    if check and r.returncode != 0:
        raise RuntimeError(f"miragectl {' '.join(map(str, args))}: {r.stderr or r.stdout}")
    return r.stdout


def sh(command):
    return ctl("sh", DEVICE, command)


def power_cycle_with(files):
    """Shut down, write files into /data, boot, load the Supervisor."""
    ctl("shutdown", DEVICE, check=False)
    time.sleep(3)
    for d in ("/canopus", "/canopus/inbox"):
        ctl("fs", "mkdir", DEVICE, "nand_data", d, check=False)
    for local, remote in files:
        ctl("fs", "rm", DEVICE, "nand_data", remote, check=False)
        ctl("fs", "put", DEVICE, "nand_data", local, remote)
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


def build_variant(work, name, version_code, fail_load=False):
    project = work / name
    shutil.copytree(ROOT / "modules/examples/emu-probe", project,
                    ignore=shutil.ignore_patterns("build", "dist"))
    toml = (project / "Canopus.toml").read_text()
    toml = toml.replace("version_code = 1", f"version_code = {version_code}")
    toml = toml.replace('"PROBE_VERSION=1"', f'"PROBE_VERSION={version_code}", "PROBE_FAIL_LOAD={int(fail_load)}"')
    (project / "Canopus.toml").write_text(toml)
    env = dict(os.environ, CANOPUS_ROOT=str(ROOT))
    subprocess.run([str(CANOPUS), "build", "--manifest", str(project / "Canopus.toml")], check=True, env=env, capture_output=True)
    pkg = work / f"{name}.canopus"
    subprocess.run([str(CANOPUS), "package", "--manifest", str(project / "Canopus.toml"), "--output", str(pkg)], check=True, env=env, capture_output=True)
    subprocess.run([str(CANOPUS), "package", "--manifest", str(project / "Canopus.toml"), "--output", str(pkg),
                    "--rpk", "--rpk-key-dir", str(work / "rpk-key")], check=True, env=env, capture_output=True)
    out = work / name
    (out / "phone.rpk").write_bytes(pkg.with_suffix(".rpk").read_bytes())
    with tarfile.open(pkg) as tar:
        (out / "next.ko").write_bytes(tar.extractfile(f"artifacts/{TARGET}/module.elf").read())
        (out / "next.cmi").write_bytes(tar.extractfile(f"receipts/{TARGET}.cmi").read())
    return [(out / "next.cmi", "/canopus/inbox/emu_probe.cmi.next"),
            (out / "next.ko", "/canopus/inbox/emu_probe.ko.next")]


BRIDGE = Path(os.environ.get("MAB_SMOKE", "/Volumes/EXT0/Mirage/AstroBoxBridge/target/smoke"))


def phone_install(rpk):
    """Install a .rpk through the real AstroBox companion core over the emulated band's Bluetooth."""
    info = ctl("info", DEVICE)
    addr = [l.split()[-1] for l in info.splitlines() if l.startswith("bt addr")][0]
    key = [l.split()[-1] for l in info.splitlines() if l.startswith("bound")][0]
    port = [l for l in ctl("list").splitlines() if DEVICE in l][0].split("BT :")[1].rstrip(")")
    state = Path(tempfile.gettempdir()) / ("canopus-abstate-" + info.split("id:")[1].split()[0])
    paired = (state / "link-keys").exists()
    request = '{"path":"%s","kind":"quickapp"}' % rpk
    proc = subprocess.Popen([str(BRIDGE), port, key, str(state), "install=" + request, "quickApps"],
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                            env=dict(os.environ, MAB_BAND=addr))
    if not paired:
        # first contact: wake the screen and accept the pairing dialog on the band
        time.sleep(8)
        ctl("tap", DEVICE, 168, 240, check=False)
        for _ in range(4):
            time.sleep(4)
            ctl("tap", DEVICE, 250, 435, check=False)
    out, _ = proc.communicate(timeout=300)
    result = [l for l in out.splitlines() if '"id":3,' in l and '"type":"result"' in l]
    if not result or '"ok":true' not in result[0]:
        raise RuntimeError("phone install failed:\n" + out[-1500:])
    time.sleep(10)  # the band extracts the package after the transfer completes


def expect(label, got, *needles):
    ok = all(n in got for n in needles)
    print(("PASS " if ok else "FAIL ") + label + ": " + got)
    if not ok:
        sys.exit(1)


def phone_flow(work, v1, v2):
    """The phone delivers modules as quick apps; the Supervisor imports them on SCAN."""
    # a fresh device: only the framework (Supervisor + driver), nothing in the inbox
    ctl("shutdown", DEVICE, check=False)
    time.sleep(3)
    ctl("delete", DEVICE)
    ctl("create", DEVICE, "--firmware", OTA.name)
    supervisor = ROOT / f"watchfaces/canopus-installer/build/{TARGET}/canopus_supervisor.elf"
    power_cycle_with([(supervisor, "/canopus/sup.elf"), (ROOT / "scripts/emu/driver.lua", "/canopus/driver.lua")])
    load_supervisor()
    phone_install(work / "v1" / "phone.rpk")
    expect("scan imports v1", drv("cmd", "scan"), "scan=01000001", "slot0=", "version:1")
    expect("enable", drv("cmd", "enable", "0"), "version:1")
    power_cycle_with([])
    load_supervisor()
    drv("cmd", "restore")
    expect("phone v1 active", drv("cat", "/data/canopus/probe.txt"), "content=1")
    # the phone upgrades the same quick app; the band keeps running v1 until reboot
    phone_install(work / "v2" / "phone.rpk")
    load_supervisor()
    expect("scan stages v2", drv("cmd", "scan"), "scan=01000100", "version:1")
    power_cycle_with([])
    load_supervisor()
    drv("cmd", "restore")
    expect("phone v2 active", drv("cat", "/data/canopus/probe.txt"), "content=2")


def main():
    if not MIRAGECTL.exists():
        skip(f"miragectl not found at {MIRAGECTL}")
    if not (OTA / "vela_ap.bin").exists():
        skip(f"firmware package not found at {OTA}")
    subprocess.run(["cargo", "build", "-q", "-p", "canopus-cli"], cwd=ROOT, check=True)
    subprocess.run(["sh", str(ROOT / "scripts/build_canopus_supervisor.sh")], cwd=ROOT, check=True,
                   env=dict(os.environ, CANOPUS_TARGET=TARGET, CANOPUS_SUP_EXTRA_DEFINES="-DCANOPUS_SUP_HEADLESS=1"), capture_output=True)
    supervisor = ROOT / f"watchfaces/canopus-installer/build/{TARGET}/canopus_supervisor.elf"

    if DEVICE in ctl("list"):
        ctl("shutdown", DEVICE, check=False)
        time.sleep(3)
        ctl("delete", DEVICE)
    if "p67tc_v3.101.043" not in ctl("firmware", "list"):
        ctl("firmware", "import", OTA)
    ctl("create", DEVICE, "--firmware", OTA.name)

    with tempfile.TemporaryDirectory() as tmp:
        work = Path(tmp)
        v1 = build_variant(work, "v1", 1)
        v2 = build_variant(work, "v2", 2)
        v3 = build_variant(work, "v3", 3, fail_load=True)
        v4 = build_variant(work, "v4", 4)
        base = [(supervisor, "/canopus/sup.elf"), (ROOT / "scripts/emu/driver.lua", "/canopus/driver.lua")]

        # 1. fresh install, enable, reboot, load
        power_cycle_with(base + v1)
        load_supervisor()
        expect("install v1", drv("install", "emu_probe"), "result=5")
        expect("enable", drv("cmd", "enable", "0"), "slot0=", "version:1")
        power_cycle_with([])
        load_supervisor()
        expect("restore after reboot", drv("cmd", "restore"), "err=0")
        expect("v1 active", drv("cat", "/data/canopus/probe.txt"), "content=1")

        # 2. update to v2 (reboot required), then it becomes active
        power_cycle_with(v2)
        load_supervisor()
        expect("update v2 staged", drv("install", "emu_probe"), "result=8")
        expect("still v1 until reboot", drv("status"), "version:1", "flags:3")
        power_cycle_with([])
        load_supervisor()
        drv("cmd", "restore")
        expect("v2 active", drv("cat", "/data/canopus/probe.txt"), "content=2")
        expect("v2 slot", drv("status"), "version:2", "flags:1")

        # 3. downgrade is refused
        power_cycle_with(v1)
        load_supervisor()
        expect("downgrade refused", drv("install", "emu_probe"), "result=6")

        # 4. v3 that cannot load falls back to v2
        power_cycle_with(v3)
        load_supervisor()
        expect("update v3 staged", drv("install", "emu_probe"), "result=8")
        power_cycle_with([])
        load_supervisor()
        expect("fell back to v2", drv("cmd", "restore"), "version:2", "err=-22")
        expect("v2 still running", drv("cat", "/data/canopus/probe.txt"), "content=2")

        # 5. a good v4 replaces the broken generation, then rollback returns to v2
        power_cycle_with(v4)
        load_supervisor()
        expect("update v4 staged", drv("install", "emu_probe"), "result=8")
        power_cycle_with([])
        load_supervisor()
        drv("cmd", "restore")
        expect("v4 active", drv("cat", "/data/canopus/probe.txt"), "content=4")
        expect("rollback armed", drv("cmd", "rollback", "0"), "version:4", "flags:3")
        power_cycle_with([])
        load_supervisor()
        drv("cmd", "restore")
        expect("rolled back to v2", drv("cat", "/data/canopus/probe.txt"), "content=2")
        expect("v2 slot after rollback", drv("status"), "version:2")

        print("emulator smoke (inbox flow): all checks passed")
        phone_flow(work, v1, v2)
        print("emulator smoke: all checks passed")


if __name__ == "__main__":
    main()
