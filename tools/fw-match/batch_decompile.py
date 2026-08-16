#!/usr/bin/env python3
"""Batch-decompile a list of candidate addresses from an IDB, writing
decompiled C to a JSON file for offline semantic review.

Usage (with the ida-pro-mcp venv python):
  .venv/bin/python batch_decompile.py --idb <path.i64> --addrs-file addrs.json --output out.json
"""
import argparse
import json
import sys
import time

import idapro


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--idb", required=True)
    ap.add_argument("--addrs-file", required=True, help="JSON list of '0x...' addresses")
    ap.add_argument("--output", required=True)
    args = ap.parse_args()

    addrs = json.loads(open(args.addrs_file).read())
    rc = idapro.open_database(args.idb, True)
    if rc != 0:
        print(f"open failed rc={rc}", file=sys.stderr)
        sys.exit(2)

    import idaapi
    import ida_hexrays
    import ida_funcs
    import idautils

    ida_hexrays.init_hexrays_plugin()
    out = {}
    for a in addrs:
        ea = int(a, 16)
        fn = idaapi.get_func(ea)
        if fn is None:
            out[a] = {"error": "not-a-function"}
            continue
        name = ida_funcs.get_func_name(ea) or ""
        try:
            cf = ida_hexrays.decompile(ea)
            code = str(cf) if cf else ""
        except Exception as e:
            code = f"<decompile failed: {e}>"
        # strings referenced
        strings = []
        for item_ea in idautils.FuncItems(ea):
            for xref in idautils.XrefsFrom(item_ea, 0):
                if xref.iscode:
                    continue
                import ida_nalt, idc
                if ida_nalt.get_str_type(xref.to) == ida_nalt.STRTYPE_C:
                    try:
                        s = idc.get_strlit_contents(xref.to)
                        if s:
                            strings.append(s.decode("utf-8", errors="replace"))
                    except Exception:
                        pass
        out[a] = {"name": name, "size": fn.end_ea - fn.start_ea, "code": code, "strings": strings[:10]}
        print(f"done {a} ({name})", file=sys.stderr)

    idapro.close_database()
    with open(args.output, "w") as f:
        json.dump(out, f, indent=1)
    print(f"wrote {args.output}", file=sys.stderr)


if __name__ == "__main__":
    main()
