#!/usr/bin/env python3
"""Extract function and referenced-data features from an IDB for fw-match.

Corpus v2 keeps the position-independent function features used by the original
matcher and adds bounded per-function data references plus separate non-code
DataObjectRecord entries for candidate-only global matching:
  - CFG shape (relative block layout + successor offsets) for the CFG layer
  - callees and string refs (content, not address) for the xref layer
  - constants (small immediates) for the constant layer
  - entry bytes (pattern layer)
  - data refs with access direction and instruction-relative position
  - referenced non-code objects with segment/shape and owner xrefs

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
import ida_segment
import ida_ua
import ida_xref
import idaapi
import idautils
import idc

ENTRY_BYTES = 32
MAX_CALLEES = 64
MAX_CALLERS = 64
MAX_STRINGS = 32
MAX_CONSTANTS = 128
MAX_DATA_REFS = 128
MAX_OBJECT_BYTES = 64


def data_access_name(xref_type: int) -> str:
    if xref_type == ida_xref.dr_R:
        return "read"
    if xref_type == ida_xref.dr_W:
        return "write"
    if xref_type in (ida_xref.dr_O, ida_xref.dr_T, ida_xref.dr_I):
        return "offset"
    return "unknown"


def canonical_segment(seg) -> str:
    name = (ida_segment.get_segm_name(seg) or "").lower()
    if seg.perm & ida_segment.SEGPERM_EXEC:
        return "xip-code"
    if seg.perm & ida_segment.SEGPERM_WRITE:
        return "ram"
    if "xip" in name or "text" in name or "ro" in name:
        return "xip-ro"
    return name or "data"


def referenced_data_object(tgt: int) -> dict | None:
    seg = ida_segment.getseg(tgt)
    if seg is None or seg.perm & ida_segment.SEGPERM_EXEC:
        return None
    if ida_nalt.get_str_type(tgt) == ida_nalt.STRTYPE_C:
        return None
    if idaapi.get_func(tgt) is not None:
        return None
    size = ida_bytes.get_item_size(tgt)
    if size <= 0 or size > 0x10000:
        size = 0
    raw = ida_bytes.get_bytes(tgt, min(size, MAX_OBJECT_BYTES)) if size else None
    alignment = 1
    while alignment < 16 and tgt % (alignment * 2) == 0:
        alignment *= 2
    return {
        "addr": f"0x{tgt:x}",
        "name": idc.get_name(tgt) or "",
        "segment": canonical_segment(seg),
        "writable": bool(seg.perm & ida_segment.SEGPERM_WRITE),
        "size": size,
        "alignment": alignment,
        "bytes": raw.hex() if raw else "",
    }


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

    # Non-code data references retain instruction-relative position and access
    # direction. They are the bridge from matched functions to writable globals.
    data_refs = []
    seen_d = set()
    for item_ea in items:
        for xref in idautils.XrefsFrom(item_ea, 0):
            if xref.iscode or referenced_data_object(xref.to) is None:
                continue
            access = data_access_name(xref.type)
            # Tail chunks can contain item addresses below the primary function
            # start. Keep the serialized offset non-negative like CFG offsets.
            relative_offset = max(0, item_ea - start)
            key = (relative_offset, xref.to, access)
            if key in seen_d:
                continue
            seen_d.add(key)
            data_refs.append({
                "off": relative_offset,
                "addr": f"0x{xref.to:x}",
                "access": access,
            })
            if len(data_refs) >= MAX_DATA_REFS:
                break
        if len(data_refs) >= MAX_DATA_REFS:
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
        "data_refs": data_refs,
        "cyclic": None,
    }


def collect_globals(functions: list[dict]) -> list[dict]:
    objects: dict[int, dict] = {}
    for fn in functions:
        function_address = fn["addr"]
        for ref in fn.get("data_refs", []):
            target = int(ref["addr"], 16)
            record = objects.get(target)
            if record is None:
                record = referenced_data_object(target)
                if record is None:
                    continue
                record["readers"] = []
                record["writers"] = []
                record["xrefs"] = []
                objects[target] = record
            access = ref["access"]
            if access == "write":
                if function_address not in record["writers"]:
                    record["writers"].append(function_address)
            elif function_address not in record["readers"]:
                record["readers"].append(function_address)
            record["xrefs"].append({
                "function": function_address,
                "off": ref["off"],
                "access": access,
            })
    return [objects[address] for address in sorted(objects)]


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

        globals_ = collect_globals(fns)
        print(f"collected {len(globals_)} referenced data objects", file=sys.stderr)
        corpus = {
            "schema": 2,
            "target_id": args.target_id,
            "image_base": "0x0",
            "functions": fns,
            "globals": globals_,
        }
        args.output.parent.mkdir(parents=True, exist_ok=True)
        tmp = args.output.with_suffix(".tmp")
        with open(tmp, "w") as f:
            json.dump(corpus, f, separators=(",", ":"))
        tmp.replace(args.output)
        print(
            f"wrote {args.output} ({n} functions, {len(globals_)} data objects)",
            file=sys.stderr,
        )
    finally:
        idapro.close_database()


if __name__ == "__main__":
    main()
