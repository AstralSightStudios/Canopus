#!/usr/bin/env python3
"""Production installer -> encrypted AstroBox CPC2 -> restart on an isolated .043 device."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import sys
import time

from emu_config import EMU_HOME, FW_043, LIBRARY, ROOT, QemuExitCheck, shutdown_checked
import native_ui_v2
import ui_pages as ui


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--work", type=Path)
    parser.add_argument("--keep-device", action="store_true")
    args = parser.parse_args()
    sys.stdout.reconfigure(line_buffering=True)
    work = (args.work or EMU_HOME / "runs" / time.strftime("%Y%m%d-%H%M%S-companion")).resolve()
    work.mkdir(parents=True, exist_ok=True)
    source_firmware = LIBRARY / "Firmwares" / FW_043
    library = work / "Library"
    os.environ["MIRAGE_LIBRARY"] = str(library)
    ui.P = ui.Band10Pro
    ui.P.device = "canopus-companion-043"
    package = ROOT / "modules/examples/emu-probe/dist/emu_probe-1.0.0.canopus"
    installer_source = ROOT / "watchfaces/canopus-installer-prod/xiaomi-band-10-pro"
    record = {"level": "host/emulator", "status": "running", "library": str(library),
              "device": ui.P.device, "shutdowns": []}
    shots = ui.Shots(work / "screenshots")
    created = False
    running = False

    def stop():
        nonlocal running
        process = QemuExitCheck(ui.P.device)
        try:
            record["shutdowns"].append(shutdown_checked(ui.MIRAGECTL, ui.P.device, process))
        finally:
            running = False

    try:
        for tool in (ui.MIRAGECTL, ui.BRIDGE, ui.PACKAGER, package, source_firmware / "vela_ap.bin"):
            if not tool.is_file():
                record.update(status="skipped", reason=f"Missing prerequisite: {tool}")
                print("SKIP:", record["reason"])
                return 77
        installer = native_ui_v2.installer(installer_source, work)
        print("Production installer packaged; preparing isolated .043 device")
        record["installer_sha256"] = hashlib.sha256(installer.read_bytes()).hexdigest()
        record["module_sha256"] = hashlib.sha256(package.read_bytes()).hexdigest()
        ui.ctl("firmware", "import", source_firmware)
        ui.ctl("create", ui.P.device, "--firmware", FW_043)
        created = True
        ui.ctl("boot", ui.P.device, "--no-shell")
        running = True
        print("Waiting for firmware boot, then pairing and installing watchface")
        time.sleep(60)
        ui.phone(work, "install=" + json.dumps({"path": str(installer), "kind": "watchface"}),
                 'setWatchface={"id":"000000000031"}', "watchfaces", "status")
        time.sleep(10)
        print("Running production installer")
        ui.run_installer(shots)
        print("PASS production installer loaded Supervisor")
        output = ui.phone(work, "install=" + json.dumps({"path": str(package), "kind": "canopus"}),
                          "canopusModules", "status")
        installed = ui.modules(output)
        if len(installed) != 1 or installed[0]["moduleId"] != "emu_probe" or installed[0]["state"] != 3:
            raise RuntimeError(f"Unexpected installed modules: {installed}")
        if '"encrypted":true' not in output:
            raise RuntimeError("AstroBox did not report an encrypted transport")
        record["transport_encrypted"] = True
        record["installed"] = installed
        print("PASS encrypted CPC2 installed emu_probe, disabled state=3")
        ui.phone(work, 'canopusSetEnabled={"module":"emu_probe","enabled":true}', "canopusModules")
        print("Module enabled; checking cold restart and installer restore")
        stop()
        ui.ctl("boot", ui.P.device, "--no-shell")
        running = True
        time.sleep(60)
        ui.run_installer(shots)
        restored = ui.modules(ui.phone(work, "canopusModules", "status"))
        record["restored"] = restored
        if len(restored) != 1 or restored[0]["state"] != 9 or restored[0]["activationError"] != 0:
            raise RuntimeError(f"Module did not restore ACTIVE: {restored}")
        stop()
        probe = ui.ctl("fs", "get", ui.P.device, "nand_data", "/canopus/probe.txt").strip()
        (work / "probe.txt").write_text(probe)
        if probe != "1":
            raise RuntimeError(f"Unexpected module probe: {probe!r}")
        record["probe"] = probe
        record["status"] = "passed"
        print("PASS restart restored ACTIVE state=9, activationError=0, probe=1")
        return 0
    except Exception as error:
        record["status"] = "failed"
        record["error"] = str(error)
        raise
    finally:
        try:
            if running:
                stop()
            if created and not args.keep_device:
                ui.ctl("delete", ui.P.device)
        except Exception as error:
            record["cleanup_error"] = str(error)
            if record["status"] != "failed":
                record.update(status="failed", error=str(error))
                raise
        finally:
            record["finished_at"] = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
            (work / "run.json").write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
            print("Evidence:", work)


if __name__ == "__main__":
    sys.exit(main())
