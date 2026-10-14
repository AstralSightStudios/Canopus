#!/usr/bin/env python3
"""Band 11 end-to-end smoke on Mirage (emulator-level evidence only).

Real AstroBox core (bridge) installs the production Canopus installer watchface;
the installer's native loader loads the Supervisor; a phone-delivered .rpk is
then imported by SCAN. Needs, besides Mirage and the firmware (exit 77 if
missing):
  * a qemu-miwear that includes commit 3e05ca7 (BES cache-controller words,
    8 MPU regions; the one Mirage pins),
  * Facetory's package_lua_watchface example built with
    scripts/emu/band11/facetory-lua-layer.patch (FACETORY_PACKAGER=<binary>),
  * the AstroBox bridge (MAB_SMOKE).
Usage: band11_smoke.py [155|177]
"""
import os, subprocess, sys, tempfile, time, zlib, struct, shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MIRAGECTL = Path(os.environ.get("MIRAGECTL", "/Volumes/EXT0/Mirage/Packages/MirageKit/.build/debug/miragectl"))
BRIDGE = Path(os.environ.get("MAB_SMOKE", "/Volumes/EXT0/Mirage/AstroBoxBridge/target/smoke"))
PACKAGER = Path(os.environ.get("FACETORY_PACKAGER",
                               "/Volumes/EXT0/GitHub/facetory/rust/target/debug/examples/package_lua_watchface"))
FIRMWARE = {"155": "miwear.watch.q66tc_v4.100.155_full_f1c824fe",
            "177": "miwear.watch.q66nfc_v4.100.177_full_6fc21448"}
DEVICE = "canopus-b11-smoke"
W, H = 212, 520
PAIR_CONFIRM = (106, 478)   # check mark on the pairing dialog
RUN_BUTTON = (106, 383)     # installer page


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
    if '"ok":false' in out:
        raise RuntimeError("bridge failed:\n" + out[-1500:])
    return out


def main():
    target = sys.argv[1] if len(sys.argv) > 1 else "155"
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
    subprocess.run(["git", "checkout", "watchfaces"], cwd=ROOT, check=False, capture_output=True)
    src = work / "face"
    src.mkdir()
    for f in [out_dir / "main.lua", *out_dir.glob("*.bin")]:
        shutil.copy(f, src / f.name)
    package_face(src, "000000000031", work / "installer.bin")
    # 2. the phone-delivered module and the SCAN driver face
    hello = ROOT / "modules/examples/hello-portable"
    canopus = ROOT / "target/debug/canopus"
    subprocess.run([str(canopus), "build"], cwd=hello, check=True, capture_output=True)
    subprocess.run([str(canopus), "package", "--rpk"], cwd=hello, check=True, capture_output=True)
    rpk = hello / "dist/hello_portable-1.0.0.rpk"
    e2e = work / "e2e"
    e2e.mkdir()
    shutil.copy(ROOT / "scripts/emu/band11/e2e_scan.lua", e2e / "main.lua")
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
    # 4. deliver the module like the phone does, then drive SCAN from a second face
    bridge(port, key, addr, state, 'install={"path":"%s","kind":"quickapp"}' % rpk,
           'install={"path":"%s","kind":"watchface"}' % (work / "e2e.bin"),
           'setWatchface={"id":"000000000032"}', "quickApps")
    time.sleep(25)
    ctl("shutdown", DEVICE)
    result = ctl("fs", "get", DEVICE, "nand_data", "/canopus/bootstrap-result.txt").replace("\r", "")
    log = ctl("fs", "get", DEVICE, "nand_data", "/canopus/e2e.log").replace("\r", "")
    inbox = ctl("fs", "ls", DEVICE, "nand_data", "/canopus/inbox")
    ok = ("result=0" in result and "scan result=5" in log and "after-scan err=0 count=1" in log
          and "hello_portable.cmi" in inbox)
    print(result.strip())
    print(log.strip())
    print(inbox.strip())
    print(("PASS" if ok else "FAIL") + f" band 11 {target}: installer loaded the Supervisor, SCAN imported the phone-delivered module")
    ctl("delete", DEVICE, check=False)
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
