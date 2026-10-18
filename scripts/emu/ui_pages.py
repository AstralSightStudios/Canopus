#!/usr/bin/env python3
"""Band 10 Pro .043 or Band 11 .155 on Mirage: the Manager and a module page, drawn by
the native UI host (ADR-CAN-010). Emulator evidence only.

The production way in: the real AstroBox core installs the production installer
watchface, its Run button loads the Supervisor and registers the Manager in the
launcher (a Band 10 Pro INSTALL driven from an NSH task blocks in the UI-side
registration of a production build, so the harness never does that). The phone then
installs the `ui_demo` module over encrypted CPC2, and everything after that is
touch: launcher > Manager > modules > ui_demo > enable > activate now > 打开界面 >
the module's page (switch, three clicks, second level, back, back out). The module's
own probe file, its final state and screenshots are checked. Screenshots land in
--shots (default: a temp directory).

Mirage behaviour the waits and the reset are built around: the screen switches off
a few seconds after the last touch; on Band 10 Pro a double tap wakes it (and does
something else when it is already lit) and the UI is back at the watchface after a
while, on Band 11 the home key switches the screen off and the next tap wakes it at
the watchface; a tap right after a page push or a scroll is swallowed; a Band 10 Pro
message box needs a moment before its buttons react. Installing a module on Band 10
Pro posts a notification that pops up over everything at the next wake, so its page
phase starts after a reboot and a second Run, lit and with nothing pending.

Needs Mirage, the target's firmware, the Facetory packager (FACETORY_PACKAGER) and
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
MIRAGECTL = Path(os.environ.get("MIRAGECTL", "/Volumes/EXT0/Mirage/Packages/MirageKit/.build/debug/miragectl"))
BRIDGE = Path(os.environ.get("MAB_SMOKE", "/Volumes/EXT0/Mirage/AstroBoxBridge/target/smoke"))
PACKAGER = Path(os.environ.get("FACETORY_PACKAGER",
                               "/Volumes/EXT0/GitHub/facetory/rust/target/debug/examples/package_lua_watchface"))
CANOPUS = ROOT / "target/debug/canopus"
LIT_MIN_BYTES = 8000   # a lit UI screenshot; a dark screen is ~2 KB, a blank white page ~6 KB


class Band10Pro:
    """336x480; touch points measured on Mirage 3.101.043."""
    name = "043"
    target = "xiaomi-band-10-pro-3.101.043"
    firmware = "miwear.watch.p67tc_v3.101.043_full_a4ce8564"
    device = os.environ.get("CANOPUS_UI_DEVICE", "canopus-ui-043")
    size = (336, 480)
    wake_taps = [(168, 240), (168, 240)]
    pair_wake = [(168, 240), (168, 240)]
    pair_confirm = (250, 435)
    run = (168, 203)
    idle = (168, 440)                 # inert on the installer face
    run_taps = (16, 2.2)
    dot = (168, 22)                   # the notification dot the loaded Supervisor posts
    tutorial_skip = (168, 432)        # the first launcher visit asks about list editing
    launcher_open = (168, 450, 168, 60)
    launcher_scroll = (12, (168, 440, 168, 60))
    manager_icon = (282, 288)
    icon_probe = (282, 248)           # white ground of the Manager's icon
    home_scroll = (2, (168, 420, 168, 120))
    modules_row = (168, 155)
    first_module = (168, 100)
    scroll_top = (2, (168, 150, 168, 440))
    third_row = (168, 340)            # detail page at the top: 启用 / 立即激活 / 打开界面
    dialog_confirm = (168, 410)       # message box
    activate_wait = 3
    page_switch = (280, 240)
    page_click = (168, 350)
    page_details = (168, 455)
    level2_back = (30, 28)            # the title's back reaches the module first
    close_page = ("tap", (30, 28))
    reboot_before_pages = True
    expected_probe = ["open 0/0/0", "toggle 0/0/1", "add 0/1/1", "add 0/2/1", "add 0/3/1",
                      "detail 1/3/1", "pop 0/3/1", "close 0/3/1"]


class Band11:
    """212x520; touch points measured on Mirage 4.100.155."""
    name = "155"
    target = "xiaomi-band-11-4.100.155"
    firmware = "miwear.watch.q66tc_v4.100.155_full_f1c824fe"
    device = os.environ.get("CANOPUS_UI_DEVICE", "canopus-ui-b11")
    size = (212, 520)
    wake_taps = [(106, 20)]
    pair_wake = [(106, 260)]
    pair_confirm = (106, 478)
    run = (106, 383)
    idle = (106, 300)
    run_taps = (15, 3.0)
    dot = None
    tutorial_skip = None
    launcher_open = (106, 500, 106, 100)
    launcher_scroll = (25, (106, 450, 106, 80))
    manager_icon = (106, 75)
    icon_probe = (106, 35)
    home_scroll = (3, (106, 450, 106, 100))
    modules_row = (106, 210)
    first_module = (106, 115)
    scroll_top = None
    third_row = (106, 310)
    dialog_confirm = (106, 460)       # message box (mode 4): cancel above, confirm below
    activate_wait = 5
    page_switch = (170, 215)
    page_click = (106, 310)
    page_details = (106, 415)
    level2_back = (106, 50)           # the title's back reaches the module first
    close_page = ("swipe", (4, 260, 190, 260))
    reboot_before_pages = False
    expected_probe = ["open 0/0/0", "toggle 0/0/1", "add 0/1/1", "add 0/2/1", "add 0/3/1",
                      "detail 1/3/1", "pop 0/3/1", "close 0/3/1"]


MODULES = {
    "ui-demo": {"id": "ui_demo", "manifest": "modules/examples/ui-demo/Canopus.toml",
                "package": "modules/examples/ui-demo/dist/ui_demo-1.0.0.canopus"},
    "ui-gallery": {"id": "ui_gallery", "manifest": "modules/examples/ui-gallery/Canopus.toml",
                   "package": "modules/examples/ui-gallery/dist/ui_gallery-1.0.0.canopus"},
}
M = MODULES["ui-demo"]
P = Band10Pro


def ctl(*args, check=True):
    r = subprocess.run([str(MIRAGECTL), *map(str, args)], capture_output=True, text=True, timeout=240)
    if check and r.returncode != 0:
        raise RuntimeError(f"miragectl {' '.join(map(str, args))}: {r.stderr or r.stdout}")
    return r.stdout


def tap(point, settle=0.6):
    ctl("tap", P.device, *point)
    time.sleep(settle)


def swipe(gesture, settle=2.5):
    ctl("swipe", P.device, *gesture, 300)
    time.sleep(settle)


def screen_lit():
    probe = Path(tempfile.gettempdir()) / f"canopus-ui-{P.device}-lit.png"
    ctl("screenshot", P.device, probe)
    return probe.stat().st_size > 4000


def wake():
    """Brings the screen up at the watchface. Band 10 Pro: a quick double tap lights it and
    acts on the UI when it is already lit, so call it with the screen off or on a page whose
    center is inert. Band 11: the home key puts a lit screen off first, which also resets
    the UI to the watchface."""
    if P is Band11 and screen_lit():
        ctl("button", P.device, "home")
        time.sleep(1.5)
    for point in P.wake_taps:
        ctl("tap", P.device, *point)
    time.sleep(1.5 if P is Band11 else 1)


class Shots:
    def __init__(self, directory):
        self.directory = Path(directory)
        self.directory.mkdir(parents=True, exist_ok=True)
        self.count = 0

    def take(self, name, lit=True):
        self.count += 1
        path = self.directory / f"{self.count:02d}-{name}.png"
        ctl("screenshot", P.device, path)
        print("screenshot", path)
        if lit and path.stat().st_size < LIT_MIN_BYTES:
            raise RuntimeError(f"{path.name}: the screen is dark or blank ({path.stat().st_size} bytes)")
        return path


def png_rows(path):
    """The decoded rows of a Mirage screenshot (8-bit RGB or RGBA PNG) and its bytes per pixel."""
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
    return rows, bpp


def pixel(path, x, y):
    """RGB of one pixel of a Mirage screenshot."""
    rows, bpp = png_rows(path)
    return tuple(rows[y][x * bpp:x * bpp + 3])


def preview_png(path):
    width, height = P.size
    rows = b"".join(b"\0" + bytes([40, 90, 200]) * width for _ in range(height))

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))


def package_installer(work):
    """The production installer watchface of this target, as a Xiaomi watchface .bin."""
    face = work / "face"
    face.mkdir()
    env = dict(os.environ, CANOPUS_TARGET=P.target)
    built = work / "installer"
    if P is Band11:
        built.mkdir()
        env["CANOPUS_BAND11_OUTPUT_DIR"] = str(built)
    subprocess.run(["sh", "scripts/build_canopus_supervisor.sh"], cwd=ROOT, check=True, env=env,
                   capture_output=True)
    if P is Band11:
        sources = [built / "main.lua", *built.glob("*.bin")]
    else:
        production = ROOT / "watchfaces/canopus-installer-prod/xiaomi-band-10-pro"
        sources = [production / name for name in
                   ("main.lua", "manager_icon.bin", f"canopus_supervisor-{P.target}.bin")]
    for source in sources:
        shutil.copy(source, face / source.name)
    preview_png(face / "preview.png")
    out = work / "installer.bin"
    subprocess.run([str(PACKAGER), str(face), "--name", "Canopus", "--package-id", "000000000031",
                    "--width", str(P.size[0]), "--height", str(P.size[1]), "-o", str(out)],
                   check=True, capture_output=True)
    return out


def phone(work, *methods):
    """Runs AstroBox core operations against the band, accepting the pairing dialog."""
    info = ctl("info", P.device)
    addr = [l.split()[-1] for l in info.splitlines() if l.startswith("bt addr")][0]
    key = [l.split()[-1] for l in info.splitlines() if l.startswith("bound")][0]
    port = [l for l in ctl("list").splitlines() if P.device in l][0].split("BT :")[1].rstrip(")")
    state = work / "abstate"
    proc = subprocess.Popen([str(BRIDGE), port, key, str(state), *methods], stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True, env=dict(os.environ, MAB_BAND=addr))
    if not (state / "link-keys").exists():
        time.sleep(8)
        for point in P.pair_wake:
            ctl("tap", P.device, *point)
        for _ in range(6):
            time.sleep(4)
            ctl("tap", P.device, *P.pair_confirm, check=False)
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
    tap(P.run, 1)
    count, interval = P.run_taps
    for _ in range(count):
        time.sleep(interval)
        tap(P.idle, 0.2)
    wake()
    done = shots.take("installer-done")
    if P.dot is not None:
        red = pixel(done, *P.dot)
        if not (red[0] > 200 and red[1] < 90 and red[2] < 90):
            raise RuntimeError(f"Run did not complete (no notification dot, pixel {red})")


def skip_launcher_tutorial():
    """The first launcher visit of a Band 10 Pro asks whether to teach the list editing."""
    wake()
    swipe(P.launcher_open, 3)
    tap(P.tutorial_skip, 1)


def reboot():
    ctl("shutdown", P.device)
    time.sleep(3)
    ctl("boot", P.device, "--no-shell")
    time.sleep(60)


def open_manager(shots):
    """From the watchface: up into the launcher, to its end, and into the Manager's icon."""
    swipe(P.launcher_open, 2.5)
    count, gesture = P.launcher_scroll
    for _ in range(count):
        swipe(gesture, 0.4)
    last_page = shots.take("launcher-end")
    white = pixel(last_page, *P.icon_probe)
    if min(white) < 200:
        raise RuntimeError(f"no Manager icon at the end of the launcher (pixel {white})")
    tap(P.manager_icon, 4)


def scroll_detail_to_top():
    if P.scroll_top is not None:
        count, gesture = P.scroll_top
        for _ in range(count):
            swipe(gesture)


def screen_body():
    """The screen below the status bar, whose clock would otherwise count as a change."""
    probe = Path(tempfile.gettempdir()) / f"canopus-ui-{P.device}-moved.png"
    ctl("screenshot", P.device, probe)
    rows, _ = png_rows(probe)
    return b"".join(bytes(row) for row in rows[70:])


def module_detail_at_top():
    count, gesture = P.home_scroll
    # Mirage sometimes leaves the freshly opened overview deaf to touch for a minute or two
    # (seen before the host changes too), so the first swipe repeats until the page moves.
    before = screen_body()
    for _ in range(40):
        swipe(gesture)
        if screen_body() != before:
            break
    else:
        raise RuntimeError("the Manager overview never reacted to touch")
    for _ in range(count - 1):
        swipe(gesture)
    tap(P.modules_row, 3.5)
    tap(P.first_module, 3.5)
    scroll_detail_to_top()


def confirm_dialog(shots, name, wait):
    tap(P.third_row, 1.5)
    shots.take(name)
    tap(P.dialog_confirm, wait)
    scroll_detail_to_top()


def prepare(work, installer, package, shots):
    """A fresh device with ui_demo installed and the installer's Run done. Band 10 Pro is then
    rebooted and Run once more, so the install notification is gone and the screen is lit."""
    ctl("boot", P.device, "--no-shell")
    time.sleep(60)
    phone(work, 'install={"path":"%s","kind":"watchface"}' % installer,
          'setWatchface={"id":"000000000031"}', "watchfaces")
    time.sleep(10)
    run_installer(shots)
    if P.tutorial_skip is not None:
        skip_launcher_tutorial()
    installed = modules(phone(work, 'install={"path":"%s","kind":"canopus"}' % package, "canopusModules"))
    if [m["state"] for m in installed] != [3]:
        raise RuntimeError(f"{M['id']} should be installed and disabled: {installed}")
    if P.reboot_before_pages:
        reboot()
        run_installer(shots)
    else:
        wake()


def open_module_page(shots):
    """Launcher > Manager > modules > the module > enable > activate now > 打开界面."""
    open_manager(shots)
    shots.take("manager-home")
    module_detail_at_top()
    shots.take("detail-disabled")
    confirm_dialog(shots, "dialog-enable", 3)
    shots.take("detail-enabled")
    confirm_dialog(shots, "dialog-activate", P.activate_wait)
    shots.take("detail-running")
    tap(P.third_row, 3.5)
    shots.take("page-level1")


def demo_scenario(shots):
    tap(P.page_switch, 1.2)
    for _ in range(3):
        tap(P.page_click, 1.2)
    shots.take("page-clicked")
    tap(P.page_details, 2.5)
    shots.take("page-level2")
    tap(P.level2_back, 2.5)
    shots.take("page-back")
    how, where = P.close_page
    if how == "swipe":
        swipe(where, 3.5)
    else:
        tap(where, 3.5)
    shots.take("manager-detail-again")
    return P.expected_probe


def blue(rgb):
    return rgb[2] > 200 and rgb[0] < 110


def gallery_scenario(shots):
    """Every widget kind of ctx->ui on its own sub-page of the gallery module."""
    return gallery_band10(shots) if P is Band10Pro else gallery_band11(shots)


def gallery_band10(shots):
    top = [(168, 120), (168, 240), (168, 360), (168, 455)]   # form, progress, text, image
    scrolled = [(168, 150), (168, 270), (168, 390)]          # dialog, style, close
    scroll = (168, 420, 168, 120)
    back = (30, 28)                                          # the title's back reaches on_back

    # Form controls: switch, two checkboxes, three exclusive radios, a button.
    tap(top[0], 3.5)
    tap((280, 240), 1.2)
    tap((280, 360), 1.2)
    tap((280, 455), 1.2)
    shots.take("form-toggled")
    swipe(scroll)
    swipe(scroll)
    tap((280, 150), 1.5)    # radio 2, the last one
    shot = shots.take("form-radio-2")
    if not blue(pixel(shot, 255, 150)):   # the switch's track, left of the knob
        raise RuntimeError("radio 2 should be the selected row")
    tap((168, 270), 1.2)    # the button
    tap(back, 3.5)

    # Progress bar.
    tap(top[1], 3.5)
    for _ in range(3):
        tap((168, 300), 0.8)
    shot = shots.take("progress-3")
    fill, empty = pixel(shot, 40, 190), pixel(shot, 300, 190)
    if not (blue(fill) and not blue(empty)):
        raise RuntimeError(f"the progress bar should be filled to about a third: {fill} / {empty}")
    tap(back, 3.5)

    # Text, divider, spacer.
    tap(top[2], 3.5)
    shots.take("text")
    tap(back, 3.5)

    # Image.
    tap(top[3], 3.5)
    shot = shots.take("image")
    if min(pixel(shot, 132, 112)) < 200:   # the icon's white ground
        raise RuntimeError("the image should show the Manager's icon")
    tap(back, 3.5)

    # A dialog: the stock message box. Cancel, then confirm.
    swipe(scroll)
    tap(scrolled[0], 3.5)
    tap((168, 120), 3)
    shots.take("dialog")
    tap((168, 308), 3)
    tap((168, 120), 3)
    tap((168, 410), 3)
    tap(back, 3.5)

    # Style and layout.
    swipe(scroll)
    tap(scrolled[1], 3.5)
    shots.take("style")
    tap((168, 120), 1.5)
    tap((168, 240), 1.5)
    tap(back, 3.5)

    # The module closes its own page.
    swipe(scroll)
    tap(scrolled[2], 3.5)
    shots.take("self-closed")
    return ["open 0/0",
            "route 1/1", "switch 1/1", "checkA 1/1", "checkB 1/1", "radio 1/2", "button 1/0", "back 1/0",
            "route 2/2", "progress 2/1", "progress 2/2", "progress 2/3", "back 2/0",
            "route 3/3", "back 3/0",
            "route 4/4", "back 4/0",
            "route 5/5", "dialogopen 5/0", "dialogcancel 5/0", "dialogopen 5/0", "dialogok 5/0", "back 5/0",
            "route 6/6", "styletap 6/0", "styletap 6/0", "back 6/0",
            "selfclose 0/0", "close 0/0"]


def gallery_band11(shots):
    scroll = (106, 450, 106, 100)
    row = [(106, 115), (106, 220), (106, 325), (106, 430)]

    def enter(point, scrolled=False):
        if scrolled:
            swipe(scroll)
        tap(point, 3.5)

    # Form controls: switch, two checkboxes, three exclusive radios, a button.
    enter(row[0])
    tap((170, 215), 1.2)
    tap((106, 310), 1.2)
    tap((106, 415), 1.2)
    shots.take("form-toggled")
    swipe(scroll)
    swipe(scroll)
    shots.take("form-scrolled")
    tap((106, 115), 1.5)
    tap((106, 215), 1.5)
    shot = shots.take("form-radio-2")
    if not (blue(pixel(shot, 160, 215)) and not blue(pixel(shot, 160, 115))):   # the switch's track
        raise RuntimeError("radio 2 should be the selected row, at the scrolled position")
    tap((106, 310), 1.2)
    tap((106, 415), 3.5)

    # Progress bar.
    enter(row[1])
    for _ in range(3):
        tap((106, 255), 0.8)
    shot = shots.take("progress-3")
    fill, empty = pixel(shot, 30, 182), pixel(shot, 190, 182)
    if not (blue(fill) and not blue(empty)):
        raise RuntimeError(f"the progress bar should be filled to about a third: {fill} / {empty}")
    tap((106, 460), 3.5)

    # Text, divider, spacer; the title's back returns to the list.
    enter(row[2])
    shots.take("text")
    tap((106, 50), 3.5)

    # Image.
    enter(row[3])
    shot = shots.take("image")
    if min(pixel(shot, 66, 100)) < 200:
        raise RuntimeError("the image should show the Manager's icon")
    tap((106, 330), 3.5)

    # A dialog: the stock message box. Cancel, then confirm; the title's back leaves the sub-page.
    enter(row[1], scrolled=True)
    shot = shots.take("dialog-entered", lit=False)   # opened from a scrolled page: its rows must be in view
    if max(pixel(shot, 106, 100)) < 20:
        raise RuntimeError("a page opened from a scrolled one drew its rows out of view")
    swipe((106, 300, 106, 150))   # a page that fits must not move when dragged
    shot = shots.take("dialog-dragged", lit=False)   # a sparse page is small, check its pixels instead
    if max(pixel(shot, 106, 100)) < 20:
        raise RuntimeError("a short page was dragged away from the top")
    tap((106, 115), 3.5)
    shots.take("dialog")
    tap((106, 368), 3.5)
    tap((106, 115), 3.5)
    tap((106, 460), 3.5)
    tap((106, 50), 3.5)

    # Style and layout.
    enter(row[2], scrolled=True)
    shots.take("style")
    tap((60, 115), 1.5)
    tap((106, 220), 1.5)
    tap((106, 325), 3.5)

    # The module closes its own page.
    enter(row[3], scrolled=True)
    shots.take("self-closed")
    return ["open 0/0",
            "route 1/1", "switch 1/1", "checkA 1/1", "checkB 1/1", "radio 1/1", "radio 1/2", "button 1/0",
            "route 0/0",
            "route 2/2", "progress 2/1", "progress 2/2", "progress 2/3", "route 0/0",
            "route 3/3", "back 3/0",
            "route 4/4", "route 0/0",
            "route 5/5", "dialogopen 5/0", "dialogcancel 5/0", "dialogopen 5/0", "dialogok 5/0", "back 5/0",
            "route 6/6", "styletap 6/0", "styletap 6/0", "route 0/0",
            "selfclose 0/0", "close 0/0"]


def finish(work, expected):
    time.sleep(8)   # let the screen go off before the stack is inspected
    final = modules(phone(work, "canopusModules"))
    ctl("shutdown", P.device)
    probe = ctl("fs", "get", P.device, "nand_data", f"/canopus/m.{M['id']}/ui_probe.txt").replace("\r", "")
    got = [line for line in probe.splitlines() if line.strip()]
    print("probe:", got)
    if got != expected:
        raise RuntimeError(f"the module saw {got}, expected {expected}")
    if [m["state"] for m in final] != [9]:
        raise RuntimeError(f"{M['id']} should be active: {final}")
    print(f"PASS {P.name} {M['id']}")


def main():
    global P, M
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("target", nargs="?", default="043", choices=["043", "155"])
    parser.add_argument("--module", default="ui-demo", choices=list(MODULES))
    parser.add_argument("--stop-at-page", action="store_true",
                        help="stop with the module's page open (use with --keep-device)")
    parser.add_argument("--keep-device", action="store_true")
    parser.add_argument("--steps", nargs="+", default=None, metavar="STEP",
                        help='explore instead of the scenario: "tap X Y", "swipe X1 Y1 X2 Y2", "wait S", "shot NAME"')
    parser.add_argument("--shots", default=None)
    parser.add_argument("--work", default=None, help="keep the bridge state and packages here")
    parser.add_argument("--skip-prepare", action="store_true",
                        help="reuse the device and --work of an earlier --keep-device run, only drive the pages")
    args = parser.parse_args()
    P = Band10Pro if args.target == "043" else Band11
    M = MODULES[args.module]
    sys.stdout.reconfigure(line_buffering=True)
    missing = [p for p in (MIRAGECTL, CANOPUS, BRIDGE, PACKAGER) if not p.is_file()]
    if missing:
        print("missing " + ", ".join(map(str, missing)) + "; skipping")
        return 77
    if P.firmware not in ctl("firmware", "list"):
        print(f"Required .{P.name} firmware missing; skipping")
        return 77
    shots = Shots(args.shots or tempfile.mkdtemp(prefix="canopus-ui-shots-"))
    for verb in ("build", "package"):
        subprocess.run([str(CANOPUS), verb, "--manifest", M["manifest"]], cwd=ROOT, check=True, capture_output=True)
    package = ROOT / M["package"]
    if args.work:
        work = Path(args.work)
        work.mkdir(parents=True, exist_ok=True)
        holder = None
    else:
        holder = tempfile.TemporaryDirectory(prefix="canopus-ui-")
        work = Path(holder.name)
    try:
        if not args.skip_prepare:
            ctl("shutdown", P.device, check=False)
            time.sleep(3)
            ctl("delete", P.device, check=False)
            ctl("create", P.device, "--firmware", P.firmware)
            shutil.rmtree(work / "face", ignore_errors=True)
            shutil.rmtree(work / "installer", ignore_errors=True)
            prepare(work, package_installer(work), package, shots)
        open_module_page(shots)
        if args.stop_at_page:
            return 0
        if args.steps:
            for step in args.steps:
                word = step.split()
                if word[0] == "tap":
                    tap((int(word[1]), int(word[2])), 0.3)
                elif word[0] == "swipe":
                    swipe(tuple(int(v) for v in word[1:5]), 0.3)
                elif word[0] == "wait":
                    time.sleep(float(word[1]))
                elif word[0] == "shot":
                    shots.take(word[1], lit=False)
            return 0
        expected = demo_scenario(shots) if args.module == "ui-demo" else gallery_scenario(shots)
        finish(work, expected)
    finally:
        if not args.keep_device:
            ctl("shutdown", P.device, check=False)
            ctl("delete", P.device, check=False)
        if holder is not None:
            holder.cleanup()
    return 0


if __name__ == "__main__":
    sys.exit(main())
