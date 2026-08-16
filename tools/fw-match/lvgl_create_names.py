#!/usr/bin/env python3
"""Identify LVGL v9 widget create functions by reading each class object's
name field. The create wrappers are small functions calling
lv_obj_class_create_obj(&lv_<w>_class, parent); each class object embeds a
name string pointer at +0x20 (LVGL v9 layout).

Usage (idalib venv):
  .venv/bin/python lvgl_create_names.py --idb <path.i64> --output out.json
"""
import argparse
import json
import sys

import idapro


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--idb", required=True)
    ap.add_argument("--output", required=True)
    args = ap.parse_args()

    rc = idapro.open_database(args.idb, True)
    if rc != 0:
        print(f"open failed rc={rc}", file=sys.stderr)
        sys.exit(2)

    import idaapi
    import ida_funcs
    import ida_bytes
    import idc
    import idautils

    # lv_obj_class_create_obj address (from earlier: 0xc381584)
    CREATE = 0xC381584
    out = {}
    n = 0
    for start_ea in idautils.Functions():
        fn = idaapi.get_func(start_ea)
        if fn is None:
            continue
        if fn.end_ea - fn.start_ea > 80:
            continue  # create wrappers are tiny
        # find a literal reference to CREATE or a data xref that is a class
        # Actually: the wrapper loads a class ptr into R0 then calls CREATE.
        # Find code refs from this fn to CREATE.
        calls_create = False
        for item_ea in idautils.FuncItems(start_ea):
            for tgt in idautils.CodeRefsFrom(item_ea, 0):
                if tgt == CREATE:
                    calls_create = True
                    break
            if calls_create:
                break
        if not calls_create:
            continue
        # find the class object address loaded in this fn: scan disasm for LDR R0, =off_...
        class_addr = None
        for item_ea in idautils.FuncItems(start_ea):
            insn = idaapi.insn_t()
            if idaapi.decode_insn(insn, item_ea) <= 0:
                continue
            for op in insn.ops:
                if op.type == idaapi.o_displ and op.addr and 0x2C000000 <= op.addr <= 0x2CFFFFFF:
                    class_addr = op.addr
                    break
            if class_addr:
                break
        # Also check data refs from each instruction
        if not class_addr:
            for item_ea in idautils.FuncItems(start_ea):
                for xr in idautils.XrefsFrom(item_ea, 0):
                    if not xr.iscode and 0x2C000000 <= xr.to <= 0x2CFFFFFF:
                        class_addr = xr.to
                        break
                if class_addr:
                    break
        if not class_addr:
            continue
        # read name pointer at +0x20
        name_ptr = ida_bytes.get_dword(class_addr + 0x20)
        name = ""
        if name_ptr:
            try:
                name = idc.get_strlit_contents(name_ptr).decode("utf-8", errors="replace")
            except Exception:
                name = ""
        out[hex(start_ea)] = {
            "size": fn.end_ea - fn.start_ea,
            "class": hex(class_addr),
            "name": name,
        }
        n += 1

    idapro.close_database()
    with open(args.output, "w") as f:
        json.dump(out, f, indent=1)
    print(f"wrote {n} create wrappers", file=sys.stderr)


if __name__ == "__main__":
    main()
