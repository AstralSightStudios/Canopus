#!/usr/bin/env python3
"""Extract a per-function feature corpus from an IDB for the fw-match matcher.

The matcher needs, per function, position-independent structural features:
  - CFG shape (relative block layout + successor offsets) for the CFG layer
  - callees and string refs (content, not address) for the xref layer
  - constants (small immediates) for the constant layer
  - entry bytes (pattern layer)

Everything here is read-only. Run with the ida-pro-mcp plugin venv python:
  .venv/bin/python extract_corpus.py --idb <path.i64> --output corpus.json

Because idalib can only have one database open per process and an IDB can
only be opened by one process, run this only when the MCP worker is NOT
holding the same IDB.
"""

import argparse
import hashlib
import json
import sys
import time
from pathlib import Path

import idapro

# Core IDA modules (imported after idapro init)
import ida_bytes
import ida_funcs
import ida_nalt
import ida_ua
import idaapi
import idautils
import idc

ENTRY_BYTES = 32
MAX_CALLEES = 64
MAX_CALLERS = 64
MAX_STRINGS = 32
MAX_CONSTANTS = 128


def collect_function(fn) -> dict | None:
    start = fn.start_ea
    end = fn.end_ea
    size = end - start
    if size <= 0 or size > 0x10000:
        return None

    name = ida_funcs.get_func_name(start) or ""

    # instruction count
    insn_count = 0
    items = list(idautils.FuncItems(start))
    insn_count = len(items)

    # CFG: blocks with relative offsets + successors
    blocks = []
    succ_pairs = []
    block_id = {}
    for block in idaapi.FlowChart(fn):
        rel = block.start_ea - start
        # A block start below the function start (rare: alignment padding or
        # a jump-table stub) is clamped so offsets stay non-negative.
        rel = max(0, rel)
        block_id[block.start_ea] = len(blocks)
        blocks.append({"off": rel, "size": block.end_ea - block.start_ea})
    for block in idaapi.FlowChart(fn):
        sid = block_id[block.start_ea]
        for s in block.succs():
            tgt = block_id.get(s.start_ea)
            if tgt is not None:
                succ_pairs.append([sid, tgt])

    # callees: direct calls + indirect jump targets (veneer literal-pool refs).
    # Indirect veneers (LDR Rt,[PC,#imm]; BX Rt) reference their destination
    # through a data xref from a literal in the function, which CodeRefsFrom
    # misses. Without this, tiny veneers have no xref identity at all.
    callees = []
    seen = set()
    # 1. direct code calls
    for item_ea in items:
        for tgt in idautils.CodeRefsFrom(item_ea, 0):
            f = idaapi.get_func(tgt)
            if f is None:
                continue
            fs = f.start_ea
            if fs in seen:
                continue
            seen.add(fs)
            callees.append(fs)
            if len(callees) >= MAX_CALLEES:
                break
        if len(callees) >= MAX_CALLEES:
            break
    # 2. data xrefs from inside the function that land on a function start
    #    (indirect jump / veneer literal-pool targets)
    if len(callees) < MAX_CALLEES:
        for item_ea in items:
            for xref in idautils.XrefsFrom(item_ea, 0):
                if xref.iscode:
                    continue
                f = idaapi.get_func(xref.to)
                if f is None:
                    continue
                fs = f.start_ea
                if fs in seen:
                    continue
                seen.add(fs)
                callees.append(fs)
                if len(callees) >= MAX_CALLEES:
                    break
            if len(callees) >= MAX_CALLEES:
                break

    # callers
    callers = []
    seen_c = set()
    for site in idautils.CodeRefsTo(start, 0):
        f = idaapi.get_func(site)
        if f is None:
            continue
        fs = f.start_ea
        if fs in seen_c:
            continue
        seen_c.add(fs)
        callers.append(fs)
        if len(callers) >= MAX_CALLERS:
            break

    # string references (content only)
    strings = []
    seen_s = set()
    for item_ea in items:
        for xref in idautils.XrefsFrom(item_ea, 0):
            if xref.iscode:
                continue
            tgt = xref.to
            str_type = ida_nalt.get_str_type(tgt)
            if str_type != ida_nalt.STRTYPE_C:
                continue
            try:
                content = idc.get_strlit_contents(tgt)
            except Exception:
                content = None
            if content:
                try:
                    s = content.decode("utf-8", errors="replace")
                except Exception:
                    s = repr(content)
                if s in seen_s:
                    continue
                seen_s.add(s)
                strings.append(s)
                if len(strings) >= MAX_STRINGS:
                    break
        if len(strings) >= MAX_STRINGS:
            break

    # small immediates (drop huge values that are likely addresses)
    constants = []
    seen_k = set()
    for item_ea in items:
        insn = idaapi.insn_t()
        if idaapi.decode_insn(insn, item_ea) <= 0:
            continue
        for op in insn.ops:
            if op.type == idaapi.o_imm and 0 < op.value < 0x100000:
                if op.value in seen_k:
                    continue
                seen_k.add(op.value)
                constants.append(op.value)
                if len(constants) >= MAX_CONSTANTS:
                    break
        if len(constants) >= MAX_CONSTANTS:
            break

    # entry bytes (capped at function size so a veneer's pattern doesn't bleed
    # into the next function; the pattern layer masks relocation operands)
    n_entry = min(ENTRY_BYTES, size)
    raw = ida_bytes.get_bytes(start, n_entry)
    entry_hex = raw.hex() if raw else ""

    return {
        "addr": f"0x{start:x}",
        "name": name,
        "size": size,
        "insn": insn_count,
        "blocks": len(blocks),
        "block_offs": blocks,
        "succ": succ_pairs,
        "callees": [f"0x{a:x}" for a in callees],
        "callers": [f"0x{a:x}" for a in callers],
        "strings": strings,
        "constants": constants,
        "entry": entry_hex,
        "cyclic": None,
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--idb", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--target-id", required=True)
    parser.add_argument("--skip-entry", action="store_true",
                        help="skip entry-byte extraction (faster)")
    args = parser.parse_args()

    if not args.idb.exists():
        print(f"error: {args.idb} not found", file=sys.stderr)
        sys.exit(1)

    t0 = time.time()
    rc = idapro.open_database(str(args.idb), True)
    if rc != 0:
        print(f"error: open_database returned {rc} "
              f"(is another process holding the IDB?)", file=sys.stderr)
        sys.exit(2)

    try:
        fns = []
        n = 0
        for start_ea in idautils.Functions():
            fn = idaapi.get_func(start_ea)
            if fn is None:
                continue
            rec = collect_function(fn)
            if rec is None:
                continue
            if args.skip_entry:
                rec["entry"] = ""
            fns.append(rec)
            n += 1
            if n % 5000 == 0:
                print(f"  {n} ...", file=sys.stderr)
        print(f"collected {n} functions in {time.time()-t0:.1f}s", file=sys.stderr)

        corpus = {
            "schema": 1,
            "target_id": args.target_id,
            "image_base": "0x0",
            "functions": fns,
        }
        args.output.parent.mkdir(parents=True, exist_ok=True)
        tmp = args.output.with_suffix(".tmp")
        with open(tmp, "w") as f:
            json.dump(corpus, f, separators=(",", ":"))
        tmp.replace(args.output)
        print(f"wrote {args.output} ({n} functions)", file=sys.stderr)
    finally:
        idapro.close_database()


if __name__ == "__main__":
    main()
