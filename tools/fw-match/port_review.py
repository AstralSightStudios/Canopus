#!/usr/bin/env python3
"""Independent decompiler-level review of a firmware port.

The ensemble matcher compares Thumb instruction fingerprints. This tool gives a
second, differently-derived opinion: it decompiles the source function in the
source IDB and the predicted function in the target IDB, strips everything that
legitimately moves between builds (auto names, addresses, local numbering) and
reports whether the two pseudocode bodies are the same program.

  dump     (ida-pro-mcp venv python, needs idapro)
           --idb X.i64 --addrs-file addrs.json --output dump.json
  compare  (any python)
           --source dump-src.json --target dump-dst.json --pairs pairs.json --output review.json

An IDENTICAL verdict means the normalized pseudocode is token-for-token equal.
It is static evidence only: it does not establish a prototype or device behaviour.
"""
import argparse
import difflib
import json
import re
import sys

AUTO_NAME = re.compile(r"\b(?:j_)?(sub|loc|locret|dword|word|byte|off|unk|qword|flt|dbl|asc|stru|nullsub|jpt|def|algn|xmmword)_[0-9A-Fa-f]+\b")
HEX = re.compile(r"\b0[xX][0-9A-Fa-f]{4,}\b")
LOCAL = re.compile(r"\b(?:v|a|result)\d*\b")
COMMENT = re.compile(r"//[^\n]*")
TOKEN = re.compile(r"[A-Za-z_][A-Za-z_0-9]*|\d+|\S")


def normalize(code: str) -> list[str]:
    code = COMMENT.sub("", code)
    code = AUTO_NAME.sub(lambda m: m.group(1) + "_X", code)
    code = HEX.sub("ADDR", code)
    code = LOCAL.sub("V", code)
    return TOKEN.findall(code)


def dump(args) -> int:
    import idapro

    addrs = json.loads(open(args.addrs_file).read())
    if idapro.open_database(args.idb, False) != 0:
        print("cannot open " + args.idb, file=sys.stderr)
        return 2
    import ida_funcs
    import ida_hexrays
    import idaapi

    ida_hexrays.init_hexrays_plugin()
    out = {}
    for text in addrs:
        ea = int(text, 16) & ~1
        fn = idaapi.get_func(ea)
        if fn is None:
            out[text] = {"error": "not-a-function"}
            continue
        try:
            cfunc = ida_hexrays.decompile(fn.start_ea)
            code = str(cfunc) if cfunc else ""
        except Exception as error:  # decompiler failures are data, not crashes
            out[text] = {"error": f"decompile: {error}", "entry": hex(fn.start_ea)}
            continue
        out[text] = {
            "entry": hex(fn.start_ea),
            "name": ida_funcs.get_func_name(fn.start_ea) or "",
            "size": fn.end_ea - fn.start_ea,
            "code": code,
        }
    idapro.close_database(save=False)
    json.dump(out, open(args.output, "w"))
    print(f"wrote {args.output} ({len(out)} functions)", file=sys.stderr)
    return 0


def compare(args) -> int:
    source = json.load(open(args.source))
    target = json.load(open(args.target))
    pairs = json.load(open(args.pairs))
    report = {}
    for name, (src_addr, dst_addr) in pairs.items():
        a, b = source.get(src_addr, {}), target.get(dst_addr, {})
        if "code" not in a or "code" not in b:
            report[name] = {"verdict": "NO_PSEUDOCODE", "source": a.get("error"), "target": b.get("error")}
            continue
        ta, tb = normalize(a["code"]), normalize(b["code"])
        ratio = difflib.SequenceMatcher(None, ta, tb, autojunk=False).ratio()
        verdict = "IDENTICAL" if ta == tb else ("SIMILAR" if ratio >= 0.9 else "DIFFERENT")
        report[name] = {
            "verdict": verdict,
            "similarity": round(ratio, 4),
            "source_entry": a["entry"],
            "target_entry": b["entry"],
            "source_size": a["size"],
            "target_size": b["size"],
        }
    json.dump(report, open(args.output, "w"), indent=1)
    counts: dict[str, int] = {}
    for item in report.values():
        counts[item["verdict"]] = counts.get(item["verdict"], 0) + 1
    print(json.dumps(counts))
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    d = sub.add_parser("dump")
    d.add_argument("--idb", required=True)
    d.add_argument("--addrs-file", required=True)
    d.add_argument("--output", required=True)
    d.set_defaults(run=dump)
    c = sub.add_parser("compare")
    c.add_argument("--source", required=True)
    c.add_argument("--target", required=True)
    c.add_argument("--pairs", required=True, help='JSON {name: [source_addr, target_addr]}')
    c.add_argument("--output", required=True)
    c.set_defaults(run=compare)
    args = parser.parse_args()
    return args.run(args)


if __name__ == "__main__":
    raise SystemExit(main())
