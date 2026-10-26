#!/usr/bin/env python3
"""Native UI ABI2 on an isolated Mirage library; emulator evidence only.

Uses an already built production installer directory. It never builds Supervisor
or uses the user's daily Mirage library. Missing tooling/firmware exits 77.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time

import ui_pages as ui

# From sdk/c/canopus_ui.h; used to read the lab's `open` capability mask.
CAP_SCROLL = 1 << 8
CAP_CAPTURE = 1 << 10
CAP_POINTER_EVENTS = 1 << 11


def installer(source, work):
    face = work / "face"
    face.mkdir()
    for path in source.iterdir():
        if path.is_file() and (path.name == "main.lua" or path.suffix == ".bin"):
            shutil.copy2(path, face / path.name)
    if not (face / "main.lua").is_file() or not list(face.glob("canopus_supervisor*.bin")):
        raise RuntimeError("installer directory needs main.lua and built Supervisor resources")
    ui.preview_png(face / "preview.png")
    package = work / "installer.bin"
    subprocess.run([str(ui.PACKAGER), str(face), "--name", "Canopus", "--package-id", "000000000031",
                    "--width", str(ui.P.size[0]), "--height", str(ui.P.size[1]), "-o", str(package)],
                   check=True, capture_output=True)
    return package


def lab(shots):
    """Exercise native pages, input, feedback and the module's page lifetime."""
    b11 = issubclass(ui.P, ui.Band11)
    x = ui.P.size[0] // 2
    first = 120 if b11 else 104
    second, third, fourth = first + 104, first + 208, first + 312
    back = (106, 50) if b11 else (30, 28)
    # Start inside the lab's inner scroll viewport on either panel size.
    gesture = (x, ui.P.size[1] - 80, x, 140)
    ui.tap((x, first), 1)
    shots.take("lab-card")
    ui.tap((x, fourth), 0.6)
    shots.take("lab-card-updated")
    ui.tap((x, fourth), 0.6)
    shots.take("lab-card-reset")
    ui.tap(back, 1)
    shots.take("lab-home-after-card")
    ui.tap((x, second), 1)
    shots.take("lab-list-top", lit=False)
    for _ in range(6):
        ui.swipe(gesture, 0.6)
    shots.take("lab-list-window", lit=False)
    ui.tap((x, 170), 0.8)
    shots.take("lab-list-selected", lit=False)
    ui.tap(back, 1)
    ui.tap((x, second), 1)
    shots.take("lab-list-restored", lit=False)
    ui.tap(back, 1)
    ui.tap((x, third), 1)
    shots.take("lab-drag", lit=False)
    ui.ctl("swipe", ui.P.device, 80, 280, 140, 350, 900)
    time.sleep(0.5)
    shots.take("lab-drag-moved", lit=False)
    ui.tap(back, 1)
    ui.tap((x, fourth), 1)
    shots.take("lab-selections")
    ui.tap((x, first), 0.8)
    shots.take("lab-select-checked")
    ui.tap((x, third), 0.8)
    # Change title is now the fourth row and fits without scrolling.
    ui.tap((x, fourth), 0.6)
    shots.take("lab-title-hidden")
    ui.tap((x, fourth), 0.6)
    shots.take("lab-title-restored")
    ui.tap(back, 1)
    shots.take("lab-home-before-close")
    ui.tap(back, 1)
    shots.take("lab-return")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("target", choices=["043", "139", "155", "179"], default="043", nargs="?")
    parser.add_argument("--library", type=Path, required=True,
                        help="separate test library; never the normal Mirage library")
    parser.add_argument("--installer-dir", type=Path, required=True,
                        help="finished production installer resources; no Supervisor build occurs")
    parser.add_argument("--firmware-source", type=Path,
                        help="local OTA/directory to import into the separate test library")
    parser.add_argument("--module", choices=["native-ui-lab", "ui-demo", "ui-gallery"], default="native-ui-lab")
    parser.add_argument("--skip-module-build", action="store_true",
                        help="use the existing module package; build and package it before parallel runs")
    parser.add_argument("--work", type=Path, default=None)
    parser.add_argument("--stop-at-page", action="store_true")
    parser.add_argument("--keep-device", action="store_true")
    parser.add_argument("--steps", nargs="+", metavar="STEP",
                        help='explore: "tap X Y", "swipe X0 Y0 X1 Y1", "wait S", "shot NAME"')
    args = parser.parse_args()
    sys.stdout.reconfigure(line_buffering=True)
    normal_library = Path.home() / "Library/Application Support/Mirage"
    marker = args.library / ".canopus-native-ui-v2"
    if args.library.resolve() == normal_library.resolve():
        parser.error("--library must be a separate test library")
    if args.library.exists() and any(args.library.iterdir()) and not marker.is_file():
        parser.error("existing nonempty library is not marked as a Native UI test library")
    args.library.mkdir(parents=True, exist_ok=True)
    marker.write_text("Dedicated Canopus native UI emulator tests\n")
    # Passing the path through the environment avoids changing Mirage preferences.
    os.environ["MIRAGE_LIBRARY"] = str(args.library.resolve())
    class Band11_139(ui.Band11):
        name = "139"
        target = "xiaomi-band-11-4.100.139"
        firmware = "miwear.watch.q66tc_v4.100.139_full_a02b7af5"

    ui.P = {"043": ui.Band10Pro, "139": Band11_139, "155": ui.Band11, "179": ui.Band11_179}[args.target]
    ui.P.device = f"canopus-native-ui-v2-{args.target}-{time.time_ns()}"
    ui.M = ui.MODULES.get(args.module, {
        "id": "native_ui_lab", "manifest": "modules/examples/native-ui-lab/Canopus.toml",
        "package": "modules/examples/native-ui-lab/dist/native_ui_lab-1.0.0.canopus"})
    missing = [p for p in (ui.MIRAGECTL, ui.BRIDGE, ui.PACKAGER, ui.CANOPUS) if not p.is_file()]
    if missing:
        print("SKIP missing " + ", ".join(map(str, missing)))
        return 77
    if args.firmware_source:
        ui.ctl("firmware", "import", args.firmware_source)
    firmware = ui.firmware_id()
    if firmware is None:
        print(f"SKIP .{args.target} firmware unavailable")
        return 77
    if not args.installer_dir.is_dir():
        print("SKIP completed installer directory unavailable")
        return 77
    work = args.work or Path(tempfile.mkdtemp(prefix="canopus-native-ui-v2-run-"))
    work.mkdir(parents=True, exist_ok=True)
    shots = ui.Shots(work / "screenshots")
    record = {"target": ui.P.target, "device": ui.P.device, "library": str(args.library.resolve()),
              "module": ui.M["id"], "firmware": firmware,
              "started_at": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
              "level": "host/emulator", "status": "running"}
    (work / "run.json").write_text(json.dumps(record, indent=2))
    print("Evidence:", work)
    created = False
    try:
        if not args.skip_module_build:
            for command in ("build", "package"):
                subprocess.run([str(ui.CANOPUS), command, "--manifest", ui.M["manifest"]],
                               cwd=ui.ROOT, check=True, capture_output=True)
        package = installer(args.installer_dir, work)
        supervisor = work / "face" / f"canopus_supervisor-{ui.P.target}.bin"
        record["supervisor_sha256"] = hashlib.sha256(supervisor.read_bytes()).hexdigest()
        record["module_package_sha256"] = hashlib.sha256((ui.ROOT / ui.M["package"]).read_bytes()).hexdigest()
        ui.ctl("create", ui.P.device, "--firmware", firmware)
        created = True
        ui.prepare(work, package, ui.ROOT / ui.M["package"], shots)
        ui.open_module_page(shots)
        if args.stop_at_page:
            record["status"] = "page-open"
            return 0
        if args.steps:
            for step in args.steps:
                words = step.split()
                if words[0] == "tap":
                    ui.tap(tuple(map(int, words[1:])))
                elif words[0] == "swipe":
                    ui.swipe(tuple(map(int, words[1:])))
                elif words[0] == "wait":
                    time.sleep(float(words[1]))
                elif words[0] == "shot":
                    shots.take(words[1], lit=False)
                else:
                    raise ValueError("unknown step " + step)
            record["status"] = "explored"
        elif args.module == "native-ui-lab":
            lab(shots)
            ui.ctl("shutdown", ui.P.device)
            probe = ui.ctl("fs", "get", ui.P.device, "nand_data", "/canopus/m.native_ui_lab/ui_probe.txt")
            (work / "ui_probe.txt").write_text(probe)
            lines = probe.splitlines()
            if any(line.startswith("renderfail ") for line in lines):
                raise RuntimeError("lab snapshot failed; see ui_probe.txt")
            opened = [l for l in lines if l.startswith("open ")]
            if not opened:
                raise RuntimeError("the lab never opened; see ui_probe.txt")
            caps = int(opened[-1].split("/")[1])
            for route in range(1, 5):
                if f"route {route}/{route}" not in lines:
                    raise RuntimeError(f"lab route {route} was not reached; see ui_probe.txt")
            for expected in ("card 1/1", "card 1/2", "check 4/1", "radio 4/1",
                             "header 4/1", "header 4/0", "close 0/0"):
                if expected not in lines:
                    raise RuntimeError(f"lab did not report {expected!r}; see ui_probe.txt")
            # Extended style property IDs are audited per target. Where they are
            # missing the backend rejects the styled snapshot with ERR_UNSUPPORTED
            # and the lab retries without that style set; record the target limit
            # as gated evidence instead of failing the run.
            stylefallback = [line for line in lines if line.startswith("stylefallback ")]
            if stylefallback:
                if stylefallback != ["stylefallback 1/8"]:
                    raise RuntimeError("unexpected extended-style fallback; see ui_probe.txt")
                gated_style = ["style-card"]
            else:
                gated_style = []
            if any(line.startswith("post-reentered ") for line in lines):
                raise RuntimeError("UI post ran before its enclosing callback returned")
            if sum(line.startswith("post ") for line in lines) != 3:
                raise RuntimeError("UI post did not execute on the subsequent foreground heartbeat")
            if caps & CAP_POINTER_EVENTS:
                for prefix in ("press ", "move ", "release "):
                    if not any(line.startswith(prefix) for line in lines):
                        raise RuntimeError(f"pointer {prefix.strip()} did not reach the module")
            # Virtual-list evidence only means anything where the backend advertises
            # scroll. Where it does not, the lab labels the page and that label is the
            # evidence — record it as gated instead of failing the run.
            gated = [name for bit, name in ((CAP_SCROLL, "virtual-list"),
                                            (CAP_POINTER_EVENTS, "pointer-drag"),
                                            (CAP_CAPTURE, "pointer-capture"))
                     if caps & bit == 0]
            if CAP_SCROLL & caps:
                windows = [int(line.split("/")[1]) for line in lines if line.startswith("window ")]
                picks = [int(line.split("/")[1]) for line in lines if line.startswith("pick ")]
                if not windows or max(windows) < 32:
                    raise RuntimeError("virtual list did not move beyond the snapshot node limit")
                if not picks or max(picks) <= 32:
                    raise RuntimeError("virtual list did not select an item beyond the snapshot node limit")
            record["capabilities"] = caps
            record["gated"] = gated + gated_style
            record["status"] = "passed" if not record["gated"] else "partial"
        else:
            expected = ui.demo_scenario(shots) if args.module == "ui-demo" else ui.gallery_scenario(shots)
            ui.finish(work, expected)
            record["status"] = "passed"
        return 0
    except Exception as error:
        record["status"] = "failed"
        record["error"] = str(error)
        if created and args.module == "native-ui-lab" and not (work / "ui_probe.txt").is_file():
            ui.ctl("shutdown", ui.P.device, check=False)
            probe = ui.ctl("fs", "get", ui.P.device, "nand_data", "/canopus/m.native_ui_lab/ui_probe.txt", check=False)
            (work / "ui_probe.txt").write_text(probe)
        raise
    finally:
        record["finished_at"] = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
        record["screenshots"] = {
            path.name: hashlib.sha256(path.read_bytes()).hexdigest()
            for path in sorted(shots.directory.glob("*.png"))
        }
        (work / "run.json").write_text(json.dumps(record, indent=2))
        if created and not args.keep_device:
            ui.ctl("shutdown", ui.P.device, check=False)
            ui.ctl("delete", ui.P.device, check=False)


if __name__ == "__main__":
    sys.exit(main())
