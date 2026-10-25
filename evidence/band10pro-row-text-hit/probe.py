#!/usr/bin/env python3
"""Row-text hit probe on stock Band 10 Pro firmware (Mirage).

Question: when the finger lands on a row's title TEXT, does the row react, or is
the text a dead zone that only the row surface around it responds to?

Drives the stock firmware's own settings list (its rows are the firmware's list
rows, the same widget family the manager's Band 10 Pro rows are built from), taps
one point ON the title glyphs and one point in the row's empty span, and compares
frames. Everything is screenshot evidence; missing tooling exits 77.
"""
import hashlib
import subprocess
import sys
import time
from pathlib import Path

MCTL = "/Users/lesetong/develop/Mirage/Packages/MirageKit/.build/debug/miragectl"
DEV = "rowhit-043"
OUT = Path("/tmp/rowhit/probe")
sys.path.insert(0, "/Users/lesetong/develop/rust/Canopus-Private/scripts/emu")
from ui_pages import png_rows  # noqa: E402

TITLE_YMAX = 130          # stock pages: title band above the first row


def ctl(*args):
    subprocess.run([MCTL, *map(str, args)], check=True, capture_output=True, timeout=60)


def shot(name):
    p = OUT / f"{name}.png"
    ctl("screenshot", DEV, p)
    print(f"  shot {name:24s} {p.stat().st_size:6d} {hashlib.sha256(p.read_bytes()).hexdigest()[:12]}")
    return p


def tap(x, y):
    ctl("tap", DEV, x, y)


def swipe(x0, y0, x1, y1, ms=300):
    ctl("swipe", DEV, x0, y0, x1, y1, ms)


def wake():
    tap(168, 470)
    tap(168, 470)
    time.sleep(0.7)


def lit(path):
    return path.stat().st_size > 4000


def text_profile(path):
    """Per-row bright-pixel counts and the x positions of bright pixels."""
    rows, bpp = png_rows(path)
    prof = []
    for y, line in enumerate(rows):
        xs = [x for x in range(336) if max(line[x * bpp:x * bpp + 3]) > 128]
        prof.append(xs)
    return prof


def row_bands(prof, ymin, ymax, min_px=25):
    """Horizontal bands of text pixels (a title glyph run counts as a band)."""
    bands, start = [], None
    for y in range(ymin, ymax):
        hot = len(prof[y]) >= min_px
        if hot and start is None:
            start = y
        elif not hot and start is not None:
            if y - start >= 8:
                bands.append((start, y - 1))
            start = None
    if start is not None:
        bands.append((start, ymax - 1))
    return bands


def pick_points(prof, band):
    """A tap point ON the densest glyphs of the band and one in its widest empty
    span (>= 24 px), for a text tap versus a same-row surface tap."""
    y0, y1 = band
    xs = sorted({x for y in range(y0, y1 + 1) for x in prof[y]})
    if not xs:
        return None
    best, run = None, None
    for i, x in enumerate(xs):        # densest 12px run = inside a glyph cluster
        density = sum(1 for xx in range(x - 6, x + 7) if xx in set(xs))
        if best is None or density > best[0]:
            best = (density, x)
    text = (best[1], (y0 + y1) // 2)
    gaps, start = [], xs[0]
    for x in xs:
        if x - start >= 24:
            gaps.append((start, x))
        start = x + 1
    if 335 - start >= 24:
        gaps.append((start, 335))
    empty = None
    if gaps:
        a, b = max(gaps, key=lambda g: g[1] - g[0])
        empty = ((a + b) // 2, (y0 + y1) // 2)
    return {"text": text, "empty": empty, "span": (xs[0], xs[-1]), "band": band}


def main():
    if not Path(MCTL).exists():
        print("miragectl missing", file=sys.stderr)
        return 77
    OUT.mkdir(parents=True, exist_ok=True)
    log = []

    print("== navigate: wake > launcher > scroll x3 > settings ==")
    wake()
    swipe(168, 450, 168, 60)
    time.sleep(0.8)
    for _ in range(3):
        swipe(168, 380, 168, 120)
        time.sleep(0.7)
    tap(280, 280)                       # the gear icon (measured on 3.101.043)
    time.sleep(0.9)
    page = shot("00-settings")
    if not lit(page):
        print("dark frame; probe aborted")
        return 1

    print("== locate rows ==")
    prof = text_profile(page)
    bands = row_bands(prof, TITLE_YMAX, 470)
    for band in bands:
        print(f"  band y={band}")
    if not bands:
        print("no text bands found")
        return 1

    # The first band below the title is the first row's text.
    plans = [pick_points(prof, b) for b in bands[:2]]
    for i, plan in enumerate(plans):
        print(f"  row{i}: text={plan['text']} empty={plan['empty']} span={plan['span']}")

    print("== probe: tap ON title text ==")
    plan = plans[0]
    tap(*plan["text"])
    time.sleep(0.8)
    after_text = shot("01-after-text-tap")

    print("== back, then tap the same row's empty span ==")
    swipe(10, 240, 170, 240)            # edge-back gesture
    time.sleep(0.9)
    back = shot("02-after-back")
    if plan["empty"] and lit(back):
        tap(*plan["empty"])
        time.sleep(0.8)
        after_empty = shot("03-after-empty-tap")
    else:
        after_empty = None

    print("== probe: second row text tap ==")
    if len(plans) > 1 and lit(shot("04-restab")):
        tap(*plans[1]["text"])
        time.sleep(0.8)
        shot("05-after-row2-text")

    base = hashlib.sha256(page.read_bytes()).hexdigest()
    print("== frames ==")
    for p in sorted(OUT.glob("*.png")):
        same = hashlib.sha256(p.read_bytes()).hexdigest() == base
        print(f"  {p.name:24s} {p.stat().st_size:6d} {'SAME' if same else 'DIFF'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
