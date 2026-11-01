#!/usr/bin/env python3
"""Local Mirage entry point: doctor, ctl, firmware and Canopus module smoke.

This checks the emulator itself. The existing smoke.py / native_ui_v2.py check
Canopus installation and UI; they additionally need the AstroBox bridge.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tarfile
import time
import uuid

from emu_config import BRIDGE, CANOPUS, EMU_HOME, FW_043, LIBRARY, MIRAGECTL, MIRAGE_ROOT, PACKAGER, ROOT, QemuExitCheck, run_capture, shutdown_checked, wake_panel


def ctl(*args, check=True, timeout=240):
    result = run_capture([str(MIRAGECTL), *map(str, args)], timeout=timeout)
    if check and result.returncode:
        raise RuntimeError(result.stderr.strip() or result.stdout.strip())
    return result


def check_windows_screen(path):
    if os.name == "nt":
        from ui_pages import png_lit
        if not png_lit(path):
            raise RuntimeError("Screenshot is entirely black; panel did not wake")


def doctor(full):
    print(f"Mirage checkout: {MIRAGE_ROOT}")
    print(f"Canopus library: {LIBRARY}")
    tools = [("miragectl", MIRAGECTL)]
    if full:
        tools += [("AstroBox smoke bridge", BRIDGE), ("Facetory", PACKAGER)]
        if os.name == "nt":
            print("Canopus compiler: check separately with mirage.ps1 canopus doctor (WSL2)")
        else:
            tools += [("Canopus", CANOPUS)]
    missing = False
    for name, path in tools:
        exists = path.is_file()
        print(f"{'OK' if exists else 'MISSING'} {name}: {path}")
        missing |= not exists
    if not MIRAGECTL.is_file():
        return 77
    qemu = ctl("qemu", check=False)
    print(qemu.stdout, end="")
    if qemu.returncode or "QEMU emulator version" not in qemu.stdout:
        print(qemu.stderr or "QEMU binary is unavailable\n", file=sys.stderr, end="")
        missing = True
    return 77 if missing else 0


def boot_smoke(args):
    if not MIRAGECTL.is_file():
        print(f"SKIP: missing {MIRAGECTL}")
        return 77
    # Keep boot checks away from both the developer library and daily devices.
    library = EMU_HOME / "BootSmokeLibrary"
    library.mkdir(parents=True, exist_ok=True)
    os.environ["MIRAGE_LIBRARY"] = str(library.resolve())
    source = args.firmware_source or LIBRARY / "Firmwares" / args.firmware
    available = ctl("firmware", "list").stdout
    if args.firmware not in available:
        if not source.exists():
            print(f"SKIP: firmware not imported; use ctl firmware download, or --firmware-source ({source})")
            return 77
        print(ctl("firmware", "import", source).stdout, end="")
    work = EMU_HOME / "runs" / time.strftime("%Y%m%d-%H%M%S")
    work.mkdir(parents=True, exist_ok=True)
    device = f"canopus-boot-check-{uuid.uuid4().hex[:12]}"
    record = {"level": "host/emulator", "device": device, "firmware": args.firmware,
              "library": str(library.resolve()), "status": "running"}
    created = False
    process = None
    try:
        ctl("create", device, "--firmware", args.firmware)
        print(f"Created isolated device {device}", flush=True)
        created = True
        print(ctl("boot", device).stdout, end="", flush=True)
        process = QemuExitCheck(device)
        time.sleep(args.wait)
        wake_panel(device)
        shot = work / "screen.png"
        print(ctl("screenshot", device, shot.resolve()).stdout, end="")
        data = shot.read_bytes()
        if data[:8] != b"\x89PNG\r\n\x1a\n" or struct.unpack(">II", data[16:24]) != (336, 480):
            raise RuntimeError("043 screenshot is not a 336x480 PNG")
        check_windows_screen(shot)
        shell = ctl("sh", device, "echo CANOPUS_MIRAGE_BOOT_OK").stdout
        (work / "shell.txt").write_text(shell, encoding="utf-8")
        if "CANOPUS_MIRAGE_BOOT_OK" not in shell:
            raise RuntimeError("NSH did not return the boot marker")
        record.update(status="passed", screenshot_sha256=hashlib.sha256(data).hexdigest())
        if os.name == "nt":
            record["screenshot_nonblack"] = True
    except Exception as exc:
        record.update(status="failed", error=str(exc))
        raise
    finally:
        try:
            if created and not args.keep_device:
                record["qemu_exit"] = shutdown_checked(MIRAGECTL, device, process)
                delete = ctl("delete", device, check=False)
                record["cleanup"] = {"delete": delete.returncode}
                if delete.returncode:
                    raise RuntimeError(delete.stderr or delete.stdout)
            elif process:
                process.close()
                record["qemu_exit"] = {"checked": False, "reason": "--keep-device"}
        except Exception as exc:
            record["cleanup_error"] = str(exc)
            if record["status"] != "failed":
                record.update(status="failed", error=str(exc))
                raise
        finally:
            (work / "run.json").write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
    checks = "boot, touch, screenshot and NSH" + ("" if args.keep_device else ", checked shutdown")
    print(f"PASS: QEMU {checks}; evidence: {work}")
    return 0


def module_smoke(args):
    """Reuse the existing .043 module test with WSL's prebuilt Supervisor."""
    import module_smoke as harness

    library = EMU_HOME / "BootSmokeLibrary"
    os.environ["MIRAGE_LIBRARY"] = str(library.resolve())
    harness.OTA_043 = library / "Firmwares" / FW_043
    if not MIRAGECTL.is_file() or not harness.OTA_043.is_dir():
        print("SKIP: run boot-smoke with the .043 firmware first")
        return 77
    with tarfile.open(args.package) as archive:
        manifest = json.load(archive.extractfile("manifest.json"))
    target = "xiaomi-band-10-pro-3.101.043"
    if not any(item["target_id"] == target for item in manifest["artifacts"]):
        raise RuntimeError("package has no .043 receipt; use the target-kind emu-probe example")
    supervisor = ROOT / f"watchfaces/canopus-installer/build/{target}/canopus_supervisor.elf"
    if not supervisor.is_file():
        print("SKIP: build the .043 headless Supervisor in WSL first")
        return 77
    work = EMU_HOME / "runs" / (time.strftime("%Y%m%d-%H%M%S") + "-module")
    work.mkdir(parents=True, exist_ok=True)
    record = {"level": "host/emulator", "transport": "offline inbox + NSH CPC2 (not Bluetooth)",
              "module": manifest["module_id"], "version_code": manifest["version_code"],
              "package_sha256": hashlib.sha256(args.package.read_bytes()).hexdigest(),
              "supervisor_sha256": hashlib.sha256(supervisor.read_bytes()).hexdigest(),
              "status": "running"}
    record["qemu_exits"] = []
    try:
        ok = harness.test_043(args.package.parent, args.package.resolve(), manifest["module_id"],
                              MIRAGECTL, build_supervisor=False, exit_records=record["qemu_exits"])
        if not ok:
            raise RuntimeError("module smoke did not complete")
        record["status"] = "passed"
        print(f"PASS: Canopus install, enable, reboot and restore; evidence: {work}")
    except Exception as exc:
        record.update(status="failed", error=str(exc))
        raise
    finally:
        (work / "run.json").write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    check = sub.add_parser("doctor")
    check.add_argument("--full", action="store_true", help="also check Canopus delivery prerequisites")
    passthrough = sub.add_parser("ctl")
    passthrough.add_argument("arguments", nargs=argparse.REMAINDER)
    shot = sub.add_parser("screenshot", help="capture a running device; --wake double-taps the panel first")
    shot.add_argument("device")
    shot.add_argument("output", type=Path)
    shot.add_argument("--wake", action="store_true")
    boot = sub.add_parser("boot-smoke", help="Band 10 Pro .043 firmware smoke; no Canopus delivery")
    boot.set_defaults(firmware=FW_043)
    boot.add_argument("--firmware-source", type=Path)
    boot.add_argument("--wait", type=float, default=40)
    boot.add_argument("--keep-device", action="store_true")
    module = sub.add_parser("module-smoke", help="run a prebuilt .043 package with WSL's headless Supervisor")
    module.add_argument("--package", type=Path, required=True)
    args = parser.parse_args()
    if args.command == "doctor":
        return doctor(args.full)
    if args.command == "screenshot":
        if args.wake:
            wake_panel(args.device)
        print(ctl("screenshot", args.device, args.output.resolve()).stdout, end="")
        if args.wake:
            check_windows_screen(args.output)
        return 0
    if args.command == "ctl":
        if not MIRAGECTL.is_file():
            print(f"SKIP: missing {MIRAGECTL}")
            return 77
        if args.arguments and args.arguments[0] == "console":
            return subprocess.run([str(MIRAGECTL), *args.arguments]).returncode
        result = ctl(*args.arguments, check=False)
        print(result.stdout, end="")
        print(result.stderr, end="", file=sys.stderr)
        return result.returncode
    if args.command == "module-smoke":
        return module_smoke(args)
    return boot_smoke(args)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, subprocess.SubprocessError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        sys.exit(1)
