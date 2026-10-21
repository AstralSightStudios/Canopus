#!/usr/bin/env python3
"""Band 11 end-to-end smoke on Mirage (emulator-level evidence only).

Real AstroBox core (bridge) installs the production Canopus installer watchface;
the installer's native loader loads the Supervisor; native .canopus modules
are delivered over encrypted CPC2 Layer 2. Needs, besides Mirage and the firmware (exit 77 if
missing):
  * a qemu-miwear that includes commit 3e05ca7 (BES cache-controller words,
    8 MPU regions; the one Mirage pins),
  * Facetory's package_lua_watchface example built with
    scripts/emu/band11/facetory-lua-layer.patch (FACETORY_PACKAGER=<binary>),
  * the AstroBox bridge (MAB_SMOKE).
Usage: band11_smoke.py [155|179] [--services | --module PROJECT_DIR [--activate]]
"""
import argparse, os, re, subprocess, sys, tempfile, time, tomllib, zlib, struct, shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MIRAGECTL = Path(os.environ.get("MIRAGECTL", "/Volumes/EXT0/Mirage/.build/out/Products/Debug/miragectl"))
BRIDGE = Path(os.environ.get("MAB_SMOKE", "/Volumes/EXT0/Mirage/AstroBoxBridge/target/smoke"))
PACKAGER = Path(os.environ.get("FACETORY_PACKAGER",
                               "/Volumes/EXT0/GitHub/facetory/rust/target/debug/examples/package_lua_watchface"))
FIRMWARE = {"155": "miwear.watch.q66tc_v4.100.155_full_f1c824fe",
            "179": "miwear.watch.q66tc_v4.100.179_full_a93678a9"}
DEVICE = os.environ.get("CANOPUS_SMOKE_DEVICE", f"canopus-native-b11-{os.getpid()}")
W, H = 212, 520
PAIR_CONFIRM = (106, 478)   # check mark on the pairing dialog
RUN_BUTTON = (106, 383)     # installer page: title, status, then Run / Clear Env

EXPECTED_SERVICES_SKIPS = set()

# What the Mirage run skips: the emulator has no reachable vibrator.
EMULATOR_SERVICES_SKIPS = {
    "Haptics: the vibrator service is not reachable",
}


def validate_report(report, probe, expected_skips=EXPECTED_SERVICES_SKIPS):
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


def ctl(*args, check=True):
    r = subprocess.run([str(MIRAGECTL), *map(str, args)], capture_output=True, text=True)
    if check and r.returncode != 0:
        raise RuntimeError(f"miragectl {' '.join(map(str, args))}: {r.stderr or r.stdout}")
    return r.stdout


def png(path):
    rows = b"".join(b"\0" + bytes([40, 90, 200]) * W for _ in range(H))
    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))


def package_face(src_dir, package_id, out):
    png(src_dir / "preview.png")
    subprocess.run([str(PACKAGER), str(src_dir), "--name", "Canopus", "--package-id", package_id,
                    "--width", str(W), "--height", str(H), "-o", str(out)], check=True, capture_output=True)


def bridge(port, key, addr, state, *methods):
    """Run bridge methods while confirming the pairing dialog on the band."""
    proc = subprocess.Popen([str(BRIDGE), str(port), key, str(state), *methods], stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True, env=dict(os.environ, MAB_BAND=addr))
    if not (Path(state) / "link-keys").exists():
        time.sleep(8)
        ctl("tap", DEVICE, 106, 260, check=False)
        for _ in range(6):
            time.sleep(4)
            ctl("tap", DEVICE, *PAIR_CONFIRM, check=False)
    out, _ = proc.communicate(timeout=300)
    log_path = Path(state).parent / f"bridge-{time.time_ns()}.log"
    log_path.write_text(out)
    if proc.returncode != 0 or '"ok":false' in out:
        raise RuntimeError(f"bridge failed (full log: {log_path}):\n" + out[-3000:])
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("target", nargs="?", default="155", choices=["155", "179"],
                        help="Target firmware version (default: 155)")
    parser.add_argument("--services", action="store_true",
                        help="Run full microkernel services test suite instead of hello-portable SCAN")
    parser.add_argument("--module", type=Path,
                        help="module project to deliver and scan instead of hello-portable "
                             "(it must list this target)")
    parser.add_argument("--activate", action="store_true",
                        help="with --module: also enable and activate the module and require it "
                             "to reach the active state (its descriptor registered and prepared)")
    parser.add_argument("--keep-device", action="store_true",
                        help="Keep the test device for inspection")
    args = parser.parse_args()

    target = args.target
    if not (ROOT / f"targets/xiaomi-band-11-4.100.{target}/evidence/l2-sram-probe.json").exists():
        print(f"No L2 backend configuration for Band 11 .{target}; skipping")
        sys.exit(77)
    fw = FIRMWARE[target]
    if not MIRAGECTL.exists() or not PACKAGER.exists() or not BRIDGE.exists() or fw not in ctl("firmware", "list"):
        print("Mirage, the firmware, the Facetory packager or the bridge is missing; skipping")
        sys.exit(77)
    work = Path(tempfile.mkdtemp(prefix="canopus-b11-"))
    tid = f"xiaomi-band-11-4.100.{target}"
    # 1. production installer for this firmware
    out_dir = work / "installer"
    out_dir.mkdir()
    subprocess.run(["sh", str(ROOT / "scripts/build_canopus_supervisor.sh")], cwd=ROOT, check=True, capture_output=True,
                   env=dict(os.environ, CANOPUS_TARGET=tid, CANOPUS_BAND11_OUTPUT_DIR=str(out_dir)))
    src = work / "face"
    src.mkdir()
    for f in [out_dir / "main.lua", *out_dir.glob("*.bin")]:
        shutil.copy(f, src / f.name)
    package_face(src, "000000000031", work / "installer.bin")
    # 2. the phone-delivered module and the driver face
    canopus = ROOT / "target/debug/canopus"
    e2e = work / "e2e"
    e2e.mkdir()
    if args.services:
        services_manifest = ROOT / "modules/examples/all-services-test/Canopus.toml"
        subprocess.run([str(canopus), "build", "--manifest", str(services_manifest)], cwd=ROOT, check=True, capture_output=True)
        subprocess.run([str(canopus), "package", "--manifest", str(services_manifest)], cwd=ROOT, check=True, capture_output=True)
        package = ROOT / "modules/examples/all-services-test/dist/services_test-1.0.0.canopus"
        shutil.copy(ROOT / "scripts/emu/band11/e2e_services.lua", e2e / "main.lua")
    else:
        project = (args.module or ROOT / "modules/examples/hello-portable").resolve()
        manifest = tomllib.loads((project / "Canopus.toml").read_text())["module"]
        module_id = manifest["id"]
        subprocess.run([str(canopus), "build"], cwd=project, check=True, capture_output=True)
        subprocess.run([str(canopus), "package"], cwd=project, check=True, capture_output=True)
        package = project / f"dist/{module_id}-{manifest['version']}.canopus"
        driver = "e2e_services.lua" if args.activate else "e2e_scan.lua"
        shutil.copy(ROOT / "scripts/emu/band11" / driver, e2e / "main.lua")
    package_face(e2e, "000000000032", work / "e2e.bin")
    # 3. fresh device, pair, install and run the installer
    ctl("shutdown", DEVICE, check=False)
    time.sleep(3)
    ctl("delete", DEVICE, check=False)
    ctl("create", DEVICE, "--firmware", fw)
    ctl("boot", DEVICE, "--no-shell")
    time.sleep(60)
    info = ctl("info", DEVICE)
    addr = [l.split()[-1] for l in info.splitlines() if l.startswith("bt addr")][0]
    key = [l.split()[-1] for l in info.splitlines() if l.startswith("bound")][0]
    port = [l for l in ctl("list").splitlines() if DEVICE in l][0].split("BT :")[1].rstrip(")")
    state = work / "abstate"
    bridge(port, key, addr, state, 'install={"path":"%s","kind":"watchface"}' % (work / "installer.bin"),
           'setWatchface={"id":"000000000031"}', "watchfaces")
    time.sleep(10)
    ctl("tap", DEVICE, 106, 260)
    time.sleep(3)
    ctl("tap", DEVICE, *RUN_BUTTON)
    time.sleep(45)
    # 4. deliver the module like the phone does, then drive SCAN/activation from a second face
    bridge(port, key, addr, state, 'install={"path":"%s","kind":"canopus"}' % package,
           'install={"path":"%s","kind":"watchface"}' % (work / "e2e.bin"),
           'setWatchface={"id":"000000000032"}', "canopusModules")
    time.sleep(40 if args.services or args.activate else 25)
    ctl("shutdown", DEVICE)
    try:
        result = ctl("fs", "get", DEVICE, "nand_data", "/canopus/bootstrap-result.txt").replace("\r", "")
        log = ctl("fs", "get", DEVICE, "nand_data", "/canopus/e2e.log").replace("\r", "")
        print(result.strip())
        print(log.strip())
        if args.services:
            report = ctl("fs", "get", DEVICE, "nand_data", "/canopus/m.services_test/test_report.txt").replace("\r", "")
            probe = ctl("fs", "get", DEVICE, "nand_data", "/canopus/m.services_test/probe.txt").replace("\r", "")
            print(report.strip())
            print(probe.strip())
            passed, total, skipped = validate_report(report, probe, EMULATOR_SERVICES_SKIPS)
            if "activate result=5" not in log and "err=0" not in log:
                raise RuntimeError("Services test module activation failed according to e2e log: " + log)
            print(f"PASS band 11 {target}: {passed}/{total} suites passed, {skipped} explicit skips; services verified")
        else:
            inbox = ctl("fs", "ls", DEVICE, "nand_data", "/canopus/inbox")
            ok = ("result=0" in result and "scan result=5" in log and "after-scan err=0 count=1" in log
                  and f"{module_id}.cmi" in inbox)
            if args.activate:
                ok = ok and "activate result=5" in log and re.search(r"after-activate .*slot0=state:(?:9|13),", log) is not None
            print(inbox.strip())
            print(("PASS" if ok else "FAIL") + f" band 11 {target}: installer loaded the Supervisor, encrypted CPC2 installed {module_id}")
            if not ok:
                sys.exit(1)
    finally:
        if not args.keep_device:
            ctl("delete", DEVICE, check=False)


if __name__ == "__main__":
    main()
