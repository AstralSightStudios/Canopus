#!/usr/bin/env python3
"""Decompile a batch of addresses from an IDB to JSON (compact)."""
import argparse, json, sys
import idapro

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--idb", required=True)
    ap.add_argument("--addrs-file", required=True)
    ap.add_argument("--output", required=True)
    args = ap.parse_args()
    addrs = json.loads(open(args.addrs_file).read())
    rc = idapro.open_database(args.idb, True)
    if rc != 0:
        print(f"open fail {rc}", file=sys.stderr); sys.exit(2)
    import idaapi, ida_hexrays, ida_funcs
    ida_hexrays.init_hexrays_plugin()
    out = {}
    for a in addrs:
        ea = int(a,16)
        fn = idaapi.get_func(ea)
        if not fn:
            out[a] = {"error": "not-fn"}; continue
        name = ida_funcs.get_func_name(ea) or ""
        try:
            cf = ida_hexrays.decompile(ea)
            code = str(cf) if cf else ""
        except Exception as e:
            code = ""
        out[a] = {"name": name, "size": fn.end_ea - fn.start_ea, "code": code[:1200]}
        print(f"done {a}", file=sys.stderr)
    idapro.close_database()
    json.dump(out, open(args.output, "w"))
    print("wrote", args.output, file=sys.stderr)

if __name__ == "__main__":
    main()
