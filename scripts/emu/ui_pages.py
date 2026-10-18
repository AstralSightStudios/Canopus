#!/usr/bin/env python3
"""Band 10 Pro .043 on Mirage: the Manager and a module page, drawn by the native
UI host (ADR-CAN-010). Emulator evidence only.

The production way in: the real AstroBox core installs the production installer
watchface, its Run button loads the Supervisor and registers the Manager in the
launcher (an INSTALL driven from an NSH task blocks in the UI-side registration of
a production build, so the harness never does that). The phone then installs the
`ui_demo` module over encrypted CPC2, and everything after that is touch: launcher
> Manager > modules > ui_demo > enable > activate now > 打开界面 > the module's page
(switch, three clicks, second level, back, back out). The module's own probe file,
its final state and screenshots are checked. Screenshots land in --shots (default:
a temp directory).

The emulated screen switches off a few seconds after the last touch (a double tap
wakes it, and does something else when it is already lit), a tap right after a page
push or a scroll is swallowed, and a message box needs a moment before its buttons
react; the waits below are those measured on Mirage. Installing a module posts a
notification that pops up over everything at the next wake, so the page phase starts
after a reboot and a second Run, lit and with nothing pending.

Needs Mirage, the 3.101.043 firmware, the Facetory packager (FACETORY_PACKAGER) and
the AstroBox bridge (MAB_SMOKE); exits 77 when any is missing.
"""
import argparse
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TARGET = "xiaomi-band-10-pro-3.101.043"
FIRMWARE = "miwear.watch.p67tc_v3.101.043_full_a4ce8564"
MIRAGECTL = Path(os.environ.get("MIRAGECTL", "/Volumes/EXT0/Mirage/Packages/MirageKit/.build/debug/miragectl"))
BRIDGE = Path(os.environ.get("MAB_SMOKE", "/Volumes/EXT0/Mirage/AstroBoxBridge/target/smoke"))
PACKAGER = Path(os.environ.get("FACETORY_PACKAGER",
                               "/Volumes/EXT0/GitHub/facetory/rust/target/debug/examples/package_lua_watchface"))
CANOPUS = ROOT / "target/debug/canopus"
DEVICE = os.environ.get("CANOPUS_UI_DEVICE", "canopus-ui-043")
W, H = 336, 480
PAIR_CONFIRM = (250, 435)
LIT_MIN_BYTES = 8000   # a lit UI screenshot; a dark screen is ~2 KB, a blank white page ~6 KB

# Touch points on the 336x480 panel.
CENTER = (168, 240)
RUN = (168, 203)
IDLE = (168, 440)                  # inert on the installer face
LAUNCHER_SKIP = (168, 432)
MANAGER_ICON = (282, 288)          # last page of the launcher, bottom right
MODULES_ROW = (168, 155)           # Manager home, scrolled to the end
FIRST_MODULE = (168, 100)
THIRD_ROW = (168, 340)             # detail page at the top: 启用 / 立即激活 / 打开界面
DIALOG_CONFIRM = (168, 410)
PAGE_SWITCH = (280, 240)
PAGE_CLICK = (168, 350)
PAGE_DETAILS = (168, 455)
TITLE_BACK = (30, 28)

EXPECTED_PROBE = ["open 0/0/0", "toggle 0/0/1", "add 0/1/1", "add 0/2/1", "add 0/3/1",
                  "detail 1/3/1", "pop 0/3/1", "close 0/3/1"]


def ctl(*args, check=True):
    r = subprocess.run([str(MIRAGECTL), *map(str, args)], capture_output=True, text=True, timeout=240)
    if check and r.returncode != 0:
        raise RuntimeError(f"miragectl {' '.join(map(str, args))}: {r.stderr or r.stdout}")
    return r.stdout


def tap(point, settle=0.6):
    ctl("tap", DEVICE, *point)
    time.sleep(settle)


def swipe(x0, y0, x1, y1, settle=2.5):
    ctl("swipe", DEVICE, x0, y0, x1, y1, 300)
    time.sleep(settle)


def wake():
    """The screen is off within seconds; a quick double tap lights it."""
    ctl("tap", DEVICE, *CENTER)
    ctl("tap", DEVICE, *CENTER)
    time.sleep(1)


class Shots:
    def __init__(self, directory):
        self.directory = Path(directory)
        self.directory.mkdir(parents=True, exist_ok=True)
        self.count = 0

    def take(self, name, lit=True):
        self.count += 1
        path = self.directory / f"{self.count:02d}-{name}.png"
        ctl("screenshot", DEVICE, path)
        print("screenshot", path)
        if lit and path.stat().st_size < LIT_MIN_BYTES:
            raise RuntimeError(f"{path.name}: the screen is dark or blank ({path.stat().st_size} bytes)")
        return path


def pixel(path, x, y):
    """RGB of one pixel of a Mirage screenshot (8-bit RGB or RGBA PNG)."""
    data = Path(path).read_bytes()
    pos, idat, width, height, depth, ctype = 8, b"", 0, 0, 0, 0
    while pos < len(data):
        (length,) = struct.unpack(">I", data[pos:pos + 4])
        kind, body = data[pos + 4:pos + 8], data[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            width, height, depth, ctype = struct.unpack(">IIBB", body[:10])
        elif kind == b"IDAT":
            idat += body
        pos += 12 + length
    if depth != 8 or ctype not in (2, 6):
        raise RuntimeError(f"{path}: unsupported PNG (depth {depth}, type {ctype})")
    bpp = 3 if ctype == 2 else 4
    raw, stride, rows, previous = zlib.decompress(idat), width * bpp, [], bytearray(width * bpp)
    for row in range(height):
        base = row * (stride + 1)
        mode, line = raw[base], bytearray(raw[base + 1:base + 1 + stride])
        for i in range(stride):
            left = line[i - bpp] if i >= bpp else 0
            up = previous[i]
            corner = previous[i - bpp] if i >= bpp else 0
            if mode == 1:
                line[i] = (line[i] + left) & 255
            elif mode == 2:
                line[i] = (line[i] + up) & 255
            elif mode == 3:
                line[i] = (line[i] + (left + up) // 2) & 255
            elif mode == 4:
                est = left + up - corner
                near = min((abs(est - left), left), (abs(est - up), up), (abs(est - corner), corner))[1]
                line[i] = (line[i] + near) & 255
        rows.append(line)
        previous = line
    return tuple(rows[y][x * bpp:x * bpp + 3])


def preview_png(path):
    rows = b"".join(b"\0" + bytes([40, 90, 200]) * W for _ in range(H))

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))


def package_installer(work):
    """The production installer watchface of this target, as a Xiaomi watchface .bin."""
    subprocess.run(["sh", "scripts/build_canopus_supervisor.sh"], cwd=ROOT, check=True,
                   env=dict(os.environ, CANOPUS_TARGET=TARGET), capture_output=True)
    production = ROOT / "watchfaces/canopus-installer-prod/xiaomi-band-10-pro"
    face = work / "face"
    face.mkdir()
    for name in ("main.lua", "manager_icon.bin", f"canopus_supervisor-{TARGET}.bin"):
        shutil.copy(production / name, face / name)
    preview_png(face / "preview.png")
    out = work / "installer.bin"
    subprocess.run([str(PACKAGER), str(face), "--name", "Canopus", "--package-id", "000000000031",
                    "--width", str(W), "--height", str(H), "-o", str(out)],
                   check=True, capture_output=True)
    return out


def phone(work, *methods):
    """Runs AstroBox core operations against the band, accepting the pairing dialog."""
    info = ctl("info", DEVICE)
    addr = [l.split()[-1] for l in info.splitlines() if l.startswith("bt addr")][0]
    key = [l.split()[-1] for l in info.splitlines() if l.startswith("bound")][0]
    port = [l for l in ctl("list").splitlines() if DEVICE in l][0].split("BT :")[1].rstrip(")")
    state = work / "abstate"
    proc = subprocess.Popen([str(BRIDGE), port, key, str(state), *methods], stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True, env=dict(os.environ, MAB_BAND=addr))
    if not (state / "link-keys").exists():
        time.sleep(8)
        wake()
        for _ in range(6):
            time.sleep(4)
            ctl("tap", DEVICE, *PAIR_CONFIRM, check=False)
    out, _ = proc.communicate(timeout=300)
    (work / f"bridge-{time.time_ns()}.log").write_text(out)
    if proc.returncode != 0 or '"ok":false' in out:
        raise RuntimeError("bridge failed:\n" + out[-3000:])
    return out


def modules(output):
    lines = [l for l in output.splitlines() if '"slot"' in l]
    if not lines:
        raise RuntimeError("no module list in the bridge output:\n" + output[-1500:])
    return json.loads(lines[-1][lines[-1].index("{"):])["result"]


def run_installer(shots):
    """Run loads the Supervisor and registers the Manager; its timers stall while the screen is off."""
    wake()
    shots.take("installer")
    tap(RUN, 1)
    for _ in range(16):
        time.sleep(2)
        tap(IDLE, 0.2)
    wake()
    done = shots.take("installer-done")
    red = pixel(done, 168, 22)   # the notification dot the loaded Supervisor posts
    if not (red[0] > 200 and red[1] < 90 and red[2] < 90):
        raise RuntimeError(f"Run did not complete (no notification dot, pixel {red})")


def skip_launcher_tutorial():
    """The first launcher visit of a device asks whether to teach the list editing."""
    wake()
    swipe(168, 450, 168, 60, 3)
    tap(LAUNCHER_SKIP, 1)


def open_manager(shots):
    """From the installer face right after Run: the screen is lit and stays lit by the touches."""
    swipe(168, 450, 168, 60, 2)
    for _ in range(12):
        swipe(168, 440, 168, 60, 0.4)
    last_page = shots.take("launcher-end")
    white = pixel(last_page, MANAGER_ICON[0], MANAGER_ICON[1] - 40)   # the Manager's icon has a white ground
    if min(white) < 200:
        raise RuntimeError(f"no Manager icon at the end of the launcher (pixel {white})")
    tap(MANAGER_ICON, 4)


def module_detail_at_top():
    for _ in range(2):
        swipe(168, 420, 168, 120)
    tap(MODULES_ROW, 3.5)
    tap(FIRST_MODULE, 3.5)
    for _ in range(2):
        swipe(168, 150, 168, 440)


def confirm_dialog(shots, name):
    tap(THIRD_ROW, 1.5)
    shots.take(name)
    tap(DIALOG_CONFIRM, 3)


def reboot():
    ctl("shutdown", DEVICE)
    time.sleep(3)
    ctl("boot", DEVICE, "--no-shell")
    time.sleep(60)


def prepare(work, installer, package, shots):
    """A fresh device with ui_demo installed, then rebooted and Run once more: the Manager is
    registered, the screen is lit on the installer face and no popup is pending."""
    ctl("boot", DEVICE, "--no-shell")
    time.sleep(60)
    phone(work, 'install={"path":"%s","kind":"watchface"}' % installer,
          'setWatchface={"id":"000000000031"}', "watchfaces")
    time.sleep(10)
    run_installer(shots)
    skip_launcher_tutorial()
    installed = modules(phone(work, 'install={"path":"%s","kind":"canopus"}' % package, "canopusModules"))
    if [m["state"] for m in installed] != [3]:
        raise RuntimeError(f"ui_demo should be installed and disabled: {installed}")
    reboot()
    run_installer(shots)


def use_pages(work, shots):
    open_manager(shots)
    shots.take("manager-home")
    module_detail_at_top()
    shots.take("detail-disabled")
    confirm_dialog(shots, "dialog-enable")
    for _ in range(2):
        swipe(168, 150, 168, 440)
    shots.take("detail-enabled")
    confirm_dialog(shots, "dialog-activate")
    for _ in range(2):
        swipe(168, 150, 168, 440)
    shots.take("detail-running")
    tap(THIRD_ROW, 3.5)
    shots.take("page-level1")
    tap(PAGE_SWITCH, 1.2)
    for _ in range(3):
        tap(PAGE_CLICK, 1.2)
    shots.take("page-clicked")
    tap(PAGE_DETAILS, 2.5)
    shots.take("page-level2")
    tap(TITLE_BACK, 2.5)
    shots.take("page-popped")
    tap(TITLE_BACK, 3.5)
    shots.take("manager-detail-again")

    time.sleep(8)   # let the screen go off before the stack is inspected
    final = modules(phone(work, "canopusModules"))
    ctl("shutdown", DEVICE)
    probe = ctl("fs", "get", DEVICE, "nand_data", "/canopus/m.ui_demo/ui_probe.txt").replace("\r", "")
    got = [line for line in probe.splitlines() if line.strip()]
    print("probe:", got)
    if got != EXPECTED_PROBE:
        raise RuntimeError(f"the module saw {got}, expected {EXPECTED_PROBE}")
    if [m["state"] for m in final] != [9]:
        raise RuntimeError(f"ui_demo should be active: {final}")
    print("PASS .043 ui pages: Manager > ui_demo > activate > page (switch, clicks, level 2, back, close)")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--keep-device", action="store_true")
    parser.add_argument("--shots", default=None)
    parser.add_argument("--work", default=None, help="keep the bridge state and packages here")
    parser.add_argument("--skip-prepare", action="store_true",
                        help="reuse the device and --work of an earlier --keep-device run, only drive the pages")
    args = parser.parse_args()
    sys.stdout.reconfigure(line_buffering=True)
    missing = [p for p in (MIRAGECTL, CANOPUS, BRIDGE, PACKAGER) if not p.is_file()]
    if missing:
        print("missing " + ", ".join(map(str, missing)) + "; skipping")
        return 77
    if FIRMWARE not in ctl("firmware", "list"):
        print("Required .043 firmware missing; skipping")
        return 77
    shots = Shots(args.shots or tempfile.mkdtemp(prefix="canopus-ui-shots-"))
    manifest = "modules/examples/ui-demo/Canopus.toml"
    for verb in ("build", "package"):
        subprocess.run([str(CANOPUS), verb, "--manifest", manifest], cwd=ROOT, check=True, capture_output=True)
    package = ROOT / "modules/examples/ui-demo/dist/ui_demo-1.0.0.canopus"
    if args.work:
        work = Path(args.work)
        work.mkdir(parents=True, exist_ok=True)
        holder = None
    else:
        holder = tempfile.TemporaryDirectory(prefix="canopus-ui-")
        work = Path(holder.name)
    try:
        if not args.skip_prepare:
            ctl("shutdown", DEVICE, check=False)
            time.sleep(3)
            ctl("delete", DEVICE, check=False)
            ctl("create", DEVICE, "--firmware", FIRMWARE)
            shutil.rmtree(work / "face", ignore_errors=True)
            prepare(work, package_installer(work), package, shots)
        use_pages(work, shots)
    finally:
        if not args.keep_device:
            ctl("shutdown", DEVICE, check=False)
            ctl("delete", DEVICE, check=False)
        if holder is not None:
            holder.cleanup()
    return 0


if __name__ == "__main__":
    sys.exit(main())
