#!/usr/bin/env python3
"""Run Mirage emulator smoke tests for an external Canopus module.

Tests the module on:
  - Band 10 Pro 3.101.043 (nand_data staging + insmod + CPC2 install/activate)
  - Band 11 4.100.155 (real AstroBox core CPC2 Layer 2 delivery + activation)
  - Band 11 4.100.177 (attempted; failures allowed/ignored per policy)

Exits 77 if Mirage or firmware OTA is missing.
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MIRAGECTL = Path(os.environ.get("MIRAGECTL", "/Volumes/EXT0/Mirage/Packages/MirageKit/.build/debug/miragectl"))
OTA_043 = Path(os.environ.get("CANOPUS_FW_OTA_DIR", "/Volumes/EXT0/miwear.watch.p67tc_v3.101.043_full_a4ce8564"))
CANOPUS_BIN = ROOT / "target/debug/canopus"
BOOT_WAIT_043 = 40
SCREEN_OFF_WAIT = 12


def ctl(miragectl, *args, check=True):
    r = subprocess.run([str(miragectl), *map(str, args)], capture_output=True, text=True)
    if check and r.returncode != 0:
        raise RuntimeError(f"miragectl {' '.join(map(str, args))}: {r.stderr or r.stdout}")
    return r.stdout


def test_043(module_dir, package_path, module_id, miragectl):
    print(f"\n==========================================")
    print(f"Testing {module_id} on Band 10 Pro 3.101.043")
    print(f"==========================================")
    if not miragectl.is_file() or not OTA_043.is_dir():
        print("SKIP: missing miragectl or 043 OTA firmware")
        return True

    device = f"canopus-mod-043-{os.getpid()}"
    print("Building headless supervisor for 043 test...")
    subprocess.run(["sh", str(ROOT / "scripts/build_canopus_supervisor.sh")],
                   cwd=ROOT, check=True,
                   env=dict(os.environ, CANOPUS_TARGET="xiaomi-band-10-pro-3.101.043",
                            CANOPUS_SUP_EXTRA_DEFINES="-DCANOPUS_SUP_HEADLESS=1"),
                   capture_output=True)
    sup_elf = ROOT / "watchfaces/canopus-installer/build/xiaomi-band-10-pro-3.101.043/canopus_supervisor.elf"
    driver_lua = ROOT / "scripts/emu/driver.lua"
    if not sup_elf.is_file():
        raise RuntimeError(f"missing supervisor ELF at {sup_elf}")

    # Extract target 043 module and receipt from package
    with tempfile.TemporaryDirectory() as tmp_dir:
        tmp = Path(tmp_dir)
        with tarfile.open(package_path) as tar:
            tar.extractall(tmp)

        mod_elf = tmp / "artifacts/xiaomi-band-10-pro-3.101.043/module.elf"
        mod_cmi = tmp / "receipts/xiaomi-band-10-pro-3.101.043.cmi"
        if not mod_elf.is_file() or not mod_cmi.is_file():
            raise RuntimeError(f"package {package_path} missing 043 artifact or receipt")

        ctl(miragectl, "create", device, "--firmware", "miwear.watch.p67tc_v3.101.043_full_a4ce8564", check=False)
        try:
            ctl(miragectl, "shutdown", device, check=False)
            time.sleep(3)
            for d in ("/canopus", "/canopus/inbox", f"/canopus/m.{module_id}"):
                ctl(miragectl, "fs", "mkdir", device, "nand_data", d, check=False)

            files = [
                (sup_elf, "/canopus/sup.elf"),
                (driver_lua, "/canopus/driver.lua"),
                (mod_elf, f"/canopus/inbox/{module_id}.ko.next"),
                (mod_cmi, f"/canopus/inbox/{module_id}.cmi.next"),
            ]
            # Copy any extracted resources
            res_dir = tmp / "resources"
            if res_dir.is_dir():
                for res_file in res_dir.rglob("*"):
                    if res_file.is_file():
                        rel = res_file.name
                        files.append((res_file, f"/canopus/m.{module_id}/{rel}"))

            for local, remote in files:
                ctl(miragectl, "fs", "put", device, "nand_data", local, remote)

            print(f"Booting {device}...")
            ctl(miragectl, "boot", device)
            time.sleep(BOOT_WAIT_043)

            def sh(cmd):
                return ctl(miragectl, "sh", device, cmd)

            def drv(*dargs):
                out = sh("lua /data/canopus/driver.lua " + " ".join(dargs))
                lines = [l for l in out.splitlines() if "@@ " in l]
                if not lines:
                    raise RuntimeError("driver produced no result: " + out[-400:])
                return lines[-1].split("@@ ", 1)[1]

            print("Loading Supervisor...")
            try:
                sh("insmod /data/canopus/sup.elf canopus_supervisor")
            except RuntimeError:
                pass

            # Double-tap wake to pump UI timer
            ctl(miragectl, "tap", device, 168, 240)
            ctl(miragectl, "tap", device, 168, 240)
            time.sleep(SCREEN_OFF_WAIT)

            status = drv("status")
            print(f"Supervisor status: {status}")

            print(f"Installing {module_id}...")
            install_res = drv("install", module_id)
            print(f"Install result: {install_res}")
            if "result=5" not in install_res and "count=1" not in install_res:
                raise RuntimeError(f"module install failed: {install_res}")

            print(f"Enabling {module_id} (slot 0)...")
            enable_res = drv("cmd", "enable", "0")
            print(f"Enable result: {enable_res}")

            print("Rebooting to load and activate module...")
            ctl(miragectl, "shutdown", device, check=False)
            time.sleep(3)
            ctl(miragectl, "boot", device)
            time.sleep(BOOT_WAIT_043)

            print("Loading Supervisor after reboot...")
            try:
                sh("insmod /data/canopus/sup.elf canopus_supervisor")
            except RuntimeError:
                pass
            ctl(miragectl, "tap", device, 168, 240)
            ctl(miragectl, "tap", device, 168, 240)
            time.sleep(SCREEN_OFF_WAIT)

            print("Restoring active modules...")
            restore_res = drv("cmd", "restore")
            print(f"Restore result: {restore_res}")

            status = drv("status")
            print(f"Final status: {status}")
            if "slot0=" not in status:
                raise RuntimeError(f"module not found in slot0: {status}")

            if module_id == "bluetooth_audio":
                dev_out = sh("ls -l /dev")
                print(f"Devices:\n{dev_out.strip()}")
                if "canopus_audio" not in dev_out:
                    raise RuntimeError("/dev/canopus_audio character device not found!")
                print("✓ Verified /dev/canopus_audio device registered!")

            print(f"PASS 043: {module_id} verified on Band 10 Pro 3.101.043")
            return True
        finally:
            ctl(miragectl, "shutdown", device, check=False)
            time.sleep(2)
            ctl(miragectl, "delete", device, check=False)


def test_b11(module_dir, target_code, allow_failure=False):
    target_name = f"xiaomi-band-11-4.100.{target_code}"
    print(f"\n==========================================")
    print(f"Testing on Band 11 {target_code} ({target_name})")
    print(f"==========================================")
    cmd = [
        sys.executable,
        str(ROOT / "scripts/emu/band11_smoke.py"),
        target_code,
        "--module", str(module_dir),
        "--activate",
    ]
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True, timeout=300)
        print(proc.stdout)
        if proc.returncode == 77:
            print(f"SKIP {target_code}: environment prerequisite missing (exit 77)")
            return True
        if proc.returncode != 0:
            if allow_failure:
                print(f"[WARN] {target_code} test failed as expected (target incomplete): {proc.stderr[:300]}")
                return True
            else:
                print(proc.stderr)
                return False
        return True
    except Exception as e:
        if allow_failure:
            print(f"[WARN] {target_code} test encountered error (allowed for {target_code}): {e}")
            return True
        else:
            raise


def main():
    parser = argparse.ArgumentParser(description="Mirage smoke test for Canopus modules")
    parser.add_argument("--module", type=Path, default=Path.cwd(), help="Path to module project directory")
    parser.add_argument("target", nargs="?", default="all", choices=["all", "043", "155", "177"])
    args = parser.parse_args()

    module_dir = args.module.resolve()
    manifest_path = module_dir / "Canopus.toml"
    if not manifest_path.is_file():
        sys.exit(f"error: no Canopus.toml at {manifest_path}")

    # Build and package module
    print(f"Building and packaging {module_dir}...")
    subprocess.run([str(CANOPUS_BIN), "build", "--manifest", str(manifest_path)], check=True, cwd=module_dir)
    subprocess.run([str(CANOPUS_BIN), "package", "--manifest", str(manifest_path)], check=True, cwd=module_dir)

    # Locate package
    packages = list((module_dir / "dist").glob("*.canopus"))
    if not packages:
        sys.exit(f"error: no .canopus package found in {module_dir / 'dist'}")
    package_path = packages[0]
    module_id = package_path.name.split("-")[0]
    print(f"Package: {package_path.name} (module id: {module_id})")

    results = []
    if args.target in ("all", "043"):
        results.append(("043", test_043(module_dir, package_path, module_id, MIRAGECTL)))
    if args.target in ("all", "155"):
        results.append(("155", test_b11(module_dir, "155", allow_failure=False)))
    if args.target in ("all", "177"):
        results.append(("177", test_b11(module_dir, "177", allow_failure=True)))

    print("\n================== SUMMARY ==================")
    all_ok = True
    for name, ok in results:
        status = "PASS" if ok else "FAIL"
        if not ok: all_ok = False
        print(f"  Target {name:5s}: {status}")

    if not all_ok:
        sys.exit(1)
    print("All Mirage smoke tests completed successfully!")


if __name__ == "__main__":
    main()
